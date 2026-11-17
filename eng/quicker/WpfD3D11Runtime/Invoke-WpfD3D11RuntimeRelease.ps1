#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BundleArchive,
    [Parameter(Mandatory)][string]$ExpectedBundleSha256,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('DryRun','Sign')][string]$Mode='DryRun',
    [string]$SignTool,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl
)
$ErrorActionPreference='Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText("$PSScriptRoot/tools/Payload.Common.ps1")))
. "$PSScriptRoot/tools/SigningBundle.Common.ps1"
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
Assert-Wpf (-not (Test-Path -LiteralPath $OutputDirectory)) 'Use a new output directory for every signing attempt'
$bundlePrefix=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')+'\'
Assert-Wpf (-not $OutputDirectory.StartsWith($bundlePrefix,[StringComparison]::OrdinalIgnoreCase)) 'Output must be outside the immutable bundle'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$report=[ordered]@{schema=1;mode=$Mode;passed=$false;phase='bundle-validation';bundleSha256=$ExpectedBundleSha256;
    certificateThumbprint=$CertificateThumbprint;nugetAuthorSigning='not performed (optional per PKG0)';publishAllowed=$false;rids=@()}
try {
    $bundle=Test-WpfSigningBundle $PSScriptRoot $BundleArchive $ExpectedBundleSha256
    $report.packageVersion=$bundle.packageVersion; $report.forkCommit=$bundle.forkCommit
    $execute=$Mode -ceq 'Sign'
    if($execute) {
        Assert-Wpf ($CertificateThumbprint -match '^[a-fA-F0-9]{40}$' -and $TimestampUrl -match '^https?://' -and
            (Test-Path -LiteralPath $SignTool -PathType Leaf)) 'Explicit signing configuration is required'
    }
    $report.phase='sign-and-refresh-manifests'
    $upstream=@($bundle.files | Where-Object role -ceq 'preserve-upstream')
    foreach($rid in @('win-x64','win-arm64')) {
        $work=Join-Path $OutputDirectory "inputs/$rid"
        New-Item -ItemType Directory -Path $work -Force | Out-Null
        Get-ChildItem -LiteralPath "$PSScriptRoot/inputs/$rid" -File | Copy-Item -Destination $work
        & "$PSScriptRoot/Sign-WpfD3D11Runtime.ps1" -Directory $work -SignTool $SignTool `
            -CertificateThumbprint $CertificateThumbprint -TimestampUrl $TimestampUrl -Execute:$execute | Out-Host
        $signing=Read-WpfJson "$work/signing.json"
        Assert-Wpf (@($signing.files | Where-Object origin -ceq 'fork').Count -eq 31) 'Unexpected fork signing count'
        foreach($file in $upstream | Where-Object { $_.path -like "inputs/$rid/*" }) {
            Assert-Wpf ((Get-FileHash -LiteralPath (Join-Path $OutputDirectory $file.path)).Hash -ceq $file.sha256) 'Upstream bytes changed during signing'
        }
        $overlay=Read-WpfJson "$work/overlay-manifest.json"
        $overlay.signing=$signing
        foreach($entry in $overlay.files) { $entry.sha256=(Get-FileHash -LiteralPath "$work/$($entry.name)").Hash }
        Write-WpfJson "$work/overlay-manifest.json" $overlay
        $report.rids+=@{rid=$rid;files=$signing.files;mode=$signing.mode}
    }
    # All restores use an empty source list. This runtime-only pack has no dependencies.
    $config=Join-Path $OutputDirectory 'NuGet.Config'
    [IO.File]::WriteAllText($config,'<configuration><packageSources><clear/></packageSources><fallbackPackageFolders><clear/></fallbackPackageFolders></configuration>',[Text.UTF8Encoding]::new($false))
    $report.phase='pack'
    $pack=@{
        X64InputDirectory="$OutputDirectory/inputs/win-x64";X64Manifest="$OutputDirectory/inputs/win-x64/overlay-manifest.json"
        Arm64InputDirectory="$OutputDirectory/inputs/win-arm64";Arm64Manifest="$OutputDirectory/inputs/win-arm64/overlay-manifest.json"
        X64Compiler="$OutputDirectory/inputs/win-x64/D3DCompiler_47_cor3.dll";StagingDirectory="$OutputDirectory/stage"
        ArtifactsPath="$OutputDirectory/artifacts";FeedDirectory="$OutputDirectory/feed";NuGetConfig=$config
        SourceSliceCommit=$bundle.sourceSliceCommit;SourceWpfTree=$bundle.sourceWpfTree;RequireSigned=$execute
    }
    & "$PSScriptRoot/Pack-WpfD3D11Runtime.ps1" @pack | Out-Host
    $package=Join-Path $OutputDirectory "feed/WpfD3D11Runtime.$($bundle.packageVersion).nupkg"
    $report.phase='unpack-and-verify'
    $expanded=Join-Path $OutputDirectory 'package-verification'
    [IO.Compression.ZipFile]::ExtractToDirectory($package,$expanded)
    $staged=@(Get-ChildItem -LiteralPath "$OutputDirectory/stage" -Recurse -File | Where-Object Extension -ne '.csproj' | ForEach-Object {
        (Get-WpfRelativePath "$OutputDirectory/stage" $_.FullName).Replace('\','/')
    })
    $zip=[IO.Compression.ZipFile]::OpenRead($package)
    try {
        Assert-Wpf (@($zip.Entries.FullName | Sort-Object -Unique).Count -eq $zip.Entries.Count) 'Duplicate nupkg entries'
        $extra=@($zip.Entries.FullName | Where-Object { $_ -cnotin $staged })
        Assert-Wpf ($extra.Count -eq 4) 'Unexpected NuGet generated entry count'
        foreach($name in $extra) {
            Assert-Wpf ($name -ceq '_rels/.rels' -or $name -ceq '[Content_Types].xml' -or
                $name -ceq 'WpfD3D11Runtime.nuspec' -or $name -match '^package/services/metadata/core-properties/(?:[0-9a-f]+|nuget)\.psmdcp$') "Unexpected nupkg file: $name"
        }
    } finally { $zip.Dispose() }
    foreach($relative in $staged) {
        Assert-Wpf ((Get-FileHash -LiteralPath "$expanded/$relative").Hash -ceq
            (Get-FileHash -LiteralPath "$OutputDirectory/stage/$relative").Hash) "Pack altered file: $relative"
    }
    [xml]$nuspec=[IO.File]::ReadAllText("$expanded/WpfD3D11Runtime.nuspec")
    Assert-Wpf ($nuspec.package.metadata.id -ceq 'WpfD3D11Runtime' -and
        $nuspec.package.metadata.version -ceq $bundle.packageVersion -and
        $nuspec.package.metadata.repository.commit -ceq $bundle.forkCommit) 'Nuspec identity/version/provenance mismatch'
    foreach($rid in @('win-x64','win-arm64')) {
        $manifest=Test-WpfInput $expanded $rid $execute
        Assert-Wpf ($manifest.packageVersion -ceq $bundle.packageVersion -and $manifest.forkCommit -ceq $bundle.forkCommit) 'Packaged manifest provenance mismatch'
        foreach($file in $manifest.files) {
            $path=Join-Path $expanded $file.pathInPackage
            if($execute) {
                & $SignTool verify /pa $path | Out-Host
                Assert-Wpf ($LASTEXITCODE -eq 0) "Packaged signature verification failed: $path"
                $sig=Get-AuthenticodeSignature -LiteralPath $path
                Assert-Wpf ($sig.Status -eq 'Valid') "Packaged Authenticode status invalid: $path"
                if($file.origin -ceq 'fork') {
                    Assert-Wpf ($sig.SignerCertificate.Thumbprint -eq $CertificateThumbprint -and $sig.TimeStamperCertificate) 'Unexpected packaged signer/timestamp'
                }
            }
            if($file.origin -ceq 'upstream') {
                $source=@($upstream | Where-Object path -ceq "inputs/$rid/$($file.name)")[0]
                Assert-Wpf ($file.sha256 -ceq $source.sha256) 'Packaged upstream hash differs from unsigned bundle'
            }
        }
    }
    # Recheck immutable input; signed hashes belong only to the new output manifests.
    $null=Test-WpfSigningBundle $PSScriptRoot $BundleArchive $ExpectedBundleSha256
    $hash=(Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($package+'.sha256',"$hash  $([IO.Path]::GetFileName($package))`n",[Text.UTF8Encoding]::new($false))
    $report.package=$package; $report.packageSha256=$hash; $report.phase='complete'; $report.passed=$true
    $report.localValidationOnly=$true
    Write-Output "Mode=$Mode; package=$package SHA256=$hash; public release clearance still required"
} catch {
    $report.error=$_.Exception.Message
    throw
} finally {
    Write-WpfJson "$OutputDirectory/signing-report.json" $report
}
