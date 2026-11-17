#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$X64InputDirectory,
    [Parameter(Mandatory)][string]$Arm64InputDirectory,
    [Parameter(Mandatory)][string]$X64Compiler,
    [Parameter(Mandatory)][string]$LockedPackageRoot,
    [Parameter(Mandatory)][string]$QuickerRepository,
    [Parameter(Mandatory)][string]$OutputZip
)
$ErrorActionPreference='Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText("$PSScriptRoot/tools/Payload.Common.ps1")))
. "$PSScriptRoot/tools/SigningBundle.Common.ps1"
$OutputZip=[IO.Path]::GetFullPath($OutputZip)
Assert-Wpf (-not (Test-Path -LiteralPath $OutputZip)) 'Bundle already exists; do not overwrite a reviewed input'
$stage=$OutputZip+'.contents'
Assert-Wpf (-not (Test-Path -LiteralPath $stage)) 'Bundle staging directory already exists'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
function Copy-BundleFile([string]$Source,[string]$Relative) {
    $target=Get-WpfBundlePath $stage $Relative
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target
}
foreach($name in @('Sign-WpfD3D11Runtime.ps1','Pack-WpfD3D11Runtime.ps1','Invoke-WpfD3D11RuntimeRelease.ps1',
    'tools/Payload.Common.ps1','tools/Test-WpfD3D11Runtime.ps1','tools/SigningBundle.Common.ps1',
    'README.md','LICENSE.txt','THIRD-PARTY-NOTICES.txt')) {
    Copy-BundleFile "$PSScriptRoot/$name" $name
}
foreach($dir in @('licenses','buildTransitive')) {
    foreach($file in Get-ChildItem -LiteralPath "$PSScriptRoot/$dir" -File -Recurse) {
        Copy-BundleFile $file.FullName ((Get-WpfRelativePath $PSScriptRoot $file.FullName).Replace('\','/'))
    }
}
foreach($rid in @('win-x64','win-arm64')) {
    # Preserve the already accepted package manifests and generated asset lists.
    $locked=Test-WpfInput $LockedPackageRoot $rid
    Assert-Wpf ($locked.signingMode -ceq 'dryrun') 'Locked package must be the unsigned P2 candidate'
    Copy-BundleFile "$LockedPackageRoot/manifests/$rid.json" "manifests/$rid.json"
    Copy-BundleFile "$LockedPackageRoot/buildTransitive/assets/$rid.props" "buildTransitive/assets/$rid.props"
}
# Copy the canonical Quicker mutex implementation verbatim (including encoding).
Copy-BundleFile "$QuickerRepository/Tools/Build/RunWithMutex.ps1" 'tools/RunWithMutex.ps1'
$build=$null
foreach($input in @(@{rid='win-x64';root=$X64InputDirectory},@{rid='win-arm64';root=$Arm64InputDirectory})) {
    $overlay=Read-WpfJson "$($input.root)/overlay-manifest.json"
    Assert-Set @($overlay.files.name) @(Get-WpfNames) "$($input.rid) overlay"
    Assert-Wpf ($overlay.signing.mode -ceq 'dryrun') 'Export requires the unsigned P2 R2R receipt'
    if($build) {
        Assert-Wpf ($build.forkCommit -ceq $overlay.build.forkCommit -and $build.packageVersion -ceq $overlay.build.packageVersion) 'RID provenance mismatch'
    } else { $build=$overlay.build }
    Copy-BundleFile "$($input.root)/overlay-manifest.json" "inputs/$($input.rid)/overlay-manifest.json"
    $locked=Read-WpfManifest $LockedPackageRoot $input.rid
    Assert-Wpf ($locked.forkCommit -ceq $overlay.build.forkCommit -and $locked.packageVersion -ceq $overlay.build.packageVersion) 'Locked package/R2R provenance mismatch'
    foreach($entry in $overlay.files) {
        $source=Join-Path $input.root $entry.name
        Assert-Wpf ((Get-FileHash -LiteralPath $source).Hash -ceq $entry.sha256) "P2 input hash mismatch: $source"
        Assert-WpfReleaseFile (Get-PeFacts $source) $entry.name
        $accepted=@($locked.files | Where-Object name -ceq $entry.name)[0]
        Assert-Wpf ($accepted.sha256 -ceq $entry.sha256) 'P2 R2R input differs from the locked package'
        Copy-BundleFile $source "inputs/$($input.rid)/$($entry.name)"
    }
}
Assert-Wpf ((Get-FileHash -LiteralPath $X64Compiler).Hash -ceq 'A05F99734F7C4822FEFC12B367AF21FD0976ED6608752FB1E1E80B6ECE7ECBBB') 'Unexpected compiler source'
Copy-BundleFile $X64Compiler 'inputs/win-x64/D3DCompiler_47_cor3.dll'
$slice=(& git -C $PSScriptRoot rev-parse HEAD).Trim()
Assert-Wpf ($LASTEXITCODE -eq 0) 'Cannot read source slice commit'
$tree=(& git -C $PSScriptRoot rev-parse HEAD:src/wpf).Trim()
Assert-Wpf ($LASTEXITCODE -eq 0) 'Cannot read source WPF tree'
$files=@(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative=(Get-WpfRelativePath $stage $_.FullName).Replace('\','/')
    $role=if($relative -like 'inputs/*/*.dll') {
        if(Test-WpfForkFile $_.Name){'sign-fork'}else{'preserve-upstream'}
    } else { 'support' }
    [ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;role=$role}
})
Write-WpfJson "$stage/bundle-manifest.json" ([ordered]@{
    schema=1;kind='unsigned-signing-bundle';packageId='WpfD3D11Runtime';packageVersion=$build.packageVersion
    forkCommit=$build.forkCommit;forkTree=$build.forkTree;sourceSliceCommit=$slice;sourceWpfTree=$tree
    buildSourceSliceCommit=$build.sourceSliceCommit;files=$files;nugetAuthorSigning='omitted; optional per PKG0'
})
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$zip=[IO.Compression.ZipFile]::Open($OutputZip,[IO.Compression.ZipArchiveMode]::Create)
try {
    # .NET Framework CreateFromDirectory uses backslashes; use portable ZIP names.
    foreach($file in Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName) {
        $name=(Get-WpfRelativePath $stage $file.FullName).Replace('\','/')
        $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$name,[IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $zip.Dispose() }
$hash=(Get-FileHash -LiteralPath $OutputZip -Algorithm SHA256).Hash
[IO.File]::WriteAllText($OutputZip+'.sha256',"$hash  $([IO.Path]::GetFileName($OutputZip))`n",[Text.UTF8Encoding]::new($false))
$null=Test-WpfSigningBundle $stage $OutputZip $hash
Write-Output "Unsigned bundle: $OutputZip SHA256=$hash"
