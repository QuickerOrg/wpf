#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$X64InputDirectory,
    [Parameter(Mandatory)][string]$X64Manifest,
    [Parameter(Mandatory)][string]$Arm64InputDirectory,
    [Parameter(Mandatory)][string]$Arm64Manifest,
    [Parameter(Mandatory)][string]$X64Compiler,
    [Parameter(Mandatory)][string]$StagingDirectory,
    [Parameter(Mandatory)][string]$ArtifactsPath,
    [Parameter(Mandatory)][string]$FeedDirectory,
    [Parameter(Mandatory)][string]$NuGetConfig,
    [string]$SourceSliceCommit,
    [string]$SourceWpfTree,
    [switch]$RequireSigned
)
$ErrorActionPreference='Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText("$PSScriptRoot/tools/Payload.Common.ps1")))
$build=(Read-WpfJson $X64Manifest).build
$armBuild=(Read-WpfJson $Arm64Manifest).build
Assert-Wpf ($build.packageVersion -ceq $armBuild.packageVersion -and $build.forkCommit -ceq $armBuild.forkCommit) 'Architecture provenance/version mismatch'
$version=$build.packageVersion
Assert-Wpf ($version -match '^\d+\.\d+\.\d+-d3d11\.\d+$') 'Expected servicing baseline-d3d11.N package version'

if(-not $SourceSliceCommit) {
    $SourceSliceCommit=(& git -C $PSScriptRoot rev-parse HEAD).Trim()
    if($LASTEXITCODE) { throw 'Cannot record slice HEAD' }
}
if(-not $SourceWpfTree) {
    $SourceWpfTree=(& git -C $PSScriptRoot rev-parse HEAD:src/wpf).Trim()
    if($LASTEXITCODE) { throw 'Cannot record slice WPF tree' }
}
Assert-Wpf ($SourceSliceCommit -match '^[0-9a-f]{40}$' -and $SourceWpfTree -match '^[0-9a-f]{40}$') 'Invalid source provenance'
foreach($path in @($StagingDirectory,$ArtifactsPath,$FeedDirectory,$NuGetConfig)) {
    Assert-Wpf ($path -match '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+[\\/])') "Absolute path required: $path"
}
Assert-Wpf (-not (Test-Path -LiteralPath $StagingDirectory)) 'Use a new staging directory to exclude stale inputs'
New-Item -ItemType Directory -Path $StagingDirectory -Force | Out-Null
$packageFiles=[Collections.Generic.List[string]]::new()
function Add-File([string]$Source,[string]$Relative) {
    $destination=Join-Path $StagingDirectory $Relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination
    $packageFiles.Add($Relative)
}
function Write-PackageText([string]$Relative,[string]$Text) {
    $destination=Join-Path $StagingDirectory $Relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    [IO.File]::WriteAllText($destination,$Text,[Text.UTF8Encoding]::new($false))
    $packageFiles.Add($Relative)
}
foreach($file in @('README.md','LICENSE.txt','THIRD-PARTY-NOTICES.txt','licenses/UPSTREAM-THIRD-PARTY-NOTICES.txt',
    'buildTransitive/WpfD3D11Runtime.props','buildTransitive/WpfD3D11Runtime.targets',
    'tools/Payload.Common.ps1','tools/Test-WpfD3D11Runtime.ps1')) { Add-File "$PSScriptRoot/$file" $file }
foreach($license in Get-ChildItem -LiteralPath "$PSScriptRoot/licenses" -File | Where-Object Name -ne 'UPSTREAM-THIRD-PARTY-NOTICES.txt') { Add-File $license.FullName "licenses/$($license.Name)" }
foreach($input in @(
    @{rid='win-x64';directory=$X64InputDirectory;manifest=$X64Manifest},
    @{rid='win-arm64';directory=$Arm64InputDirectory;manifest=$Arm64Manifest})) {
    $rid=$input.rid
    $overlay=Read-WpfJson $input.manifest
    Assert-Set @($overlay.files.name) @(Get-WpfNames) "$rid overlay"
    $files=@(); $xml=[Collections.Generic.List[string]]::new()
    foreach($entry in @($overlay.files)+$(if($rid -eq 'win-x64'){@{name='D3DCompiler_47_cor3.dll'}}else{@()})) {
        $compiler=$entry.name -eq 'D3DCompiler_47_cor3.dll'
        $source=if($compiler){$X64Compiler}else{Join-Path $input.directory $entry.name}
        $facts=Get-PeFacts $source
        if($compiler) {
            Assert-Wpf ($facts.sha256 -ceq 'A05F99734F7C4822FEFC12B367AF21FD0976ED6608752FB1E1E80B6ECE7ECBBB') 'x64 compiler must match the PKG0 official 10.0.11 cache hash'
        } else { Assert-Wpf ($facts.sha256 -ceq $entry.sha256) "Overlay hash mismatch: $source" }
        $origin=if(Test-WpfForkFile $entry.name){'fork'}else{'upstream'}
        $signing=if($compiler){
            $sig=Get-AuthenticodeSignature -LiteralPath $source
            @{action='preserve';authenticodeStatus=$sig.Status.ToString();signerThumbprint=if($sig.SignerCertificate){$sig.SignerCertificate.Thumbprint}else{$null}}
        }else{@($overlay.signing.files | Where-Object name -eq $entry.name)[0]}
        if($RequireSigned -and $origin -eq 'fork') {
            $actualSignature=Get-AuthenticodeSignature -LiteralPath $source
            $facts.signatureStatus=$actualSignature.Status.ToString()
            Assert-Wpf ($signing.action -eq 'signed') "RequireSigned rejects unsigned/dryrun: $($entry.name)"
            Assert-Wpf ($actualSignature.SignerCertificate -and $actualSignature.TimeStamperCertificate -and
                $actualSignature.SignerCertificate.Thumbprint -eq $signing.signerThumbprint -and
                $actualSignature.TimeStamperCertificate.Thumbprint -eq $signing.timestampThumbprint) "Signer/timestamp differs from signing record: $source"
        }
        Assert-WpfReleaseFile $facts $entry.name $RequireSigned.IsPresent
        if(-not $compiler) { Assert-Wpf ($facts.sha256 -ceq $signing.sha256) "Signing record hash mismatch: $source" }
        if($origin -eq 'fork') {
            Assert-Wpf ($facts.fileVersion -ceq $overlay.build.managedFileVersion) "Unexpected private release version: $source"
            if($facts.assemblyVersion) { Assert-Wpf ($facts.informationalVersion -ceq $overlay.build.informationalVersion) "InformationalVersion mismatch: $source" }
        }
        $category=if($facts.assemblyVersion){'runtime'}else{'native'}
        $relative=if($category -eq 'runtime'){"runtimes/$rid/lib/net10.0/$($entry.name)"}else{"runtimes/$rid/native/$($entry.name)"}
        Add-File $source $relative
        $record=[ordered]@{ name=$entry.name; category=$category; pathInPackage=$relative; source=[IO.Path]::GetFullPath($source); origin=$origin; signing=$signing }
        $record.sourceProvenance=switch($entry.name) {
            'PresentationNative_cor3.dll' { "runtime.$rid.Microsoft.DotNet.Wpf.DncEng/10.0.7-servicing.26452.2 local cache; WPF redist/PresentationNative copy; unchanged versus S5q" }
            'vcruntime140_cor3.dll' { "VS VC/Redist/MSVC/14.44.35112/$($rid.Substring(4))/Microsoft.VC143.CRT/vcruntime140.dll; upstream WPF redist/VCRuntime privatization; unchanged versus S5q" }
            'D3DCompiler_47_cor3.dll' { 'Microsoft.WindowsDesktop.App.Runtime.win-x64/10.0.11 local official NuGet cache; SHA locked to PKG0/P1' }
            default { "fork $($overlay.build.forkCommit); tree $($overlay.build.forkTree); slice $($overlay.build.sourceSliceCommit); fresh Release facade build; R2R/signing evidence below" }
        }
        foreach($key in $facts.Keys) { $record[$key]=$facts[$key] }
        $record.readyToRun=$facts.readyToRun
        $record.r2rEvidence=if($compiler){$null}else{@{action=if($entry.managed){'crossgen2'}else{'copy; DirectWriteForwarder/native excluded'};inputSha256=$entry.ilSha256;crossgen2=$overlay.crossgen2;crossgen2Sha256=$overlay.crossgen2Sha256}}
        $record.signatureStatus=if($origin -eq 'fork'){$signing.action}else{$signing.authenticodeStatus}
        $files+=$record
        $kind=if($category -eq 'runtime'){'_WpfD3D11Runtime'}else{'_WpfD3D11Native'}
        $xml.Add(('    <{0} Include="$(WpfD3D11RuntimeRoot){1}" AssetType="{2}" NuGetPackageId="WpfD3D11Runtime" NuGetPackageVersion="{3}" PathInPackage="{1}" DestinationSubPath="{4}" TargetPath="{4}" CopyLocal="true" CopyToOutputDirectory="PreserveNewest" CopyToPublishDirectory="PreserveNewest" />' -f $kind,$relative,$category,$version,$entry.name))
    }
    $manifest=[ordered]@{ schema=2; packageId='WpfD3D11Runtime'; packageVersion=$version; localValidationOnly=$true
        rid=$rid; upstreamBaseline=$overlay.build.upstreamBaseline
        privateReleaseSequence=$overlay.build.privateReleaseSequence;managedFileVersion=$overlay.build.managedFileVersion;assemblyVersion='10.0.0.0'
        informationalVersion=$overlay.build.informationalVersion;upstreamCommit=$overlay.build.upstreamCommit;vmrCommit=$overlay.build.vmrCommit
        signingMode=$overlay.signing.mode;requireSigned=$RequireSigned.IsPresent
        equivalence=$overlay.equivalence;referenceInputsSha256=$overlay.referenceInputsSha256
        r2rScriptSha256=$overlay.r2rScriptSha256;overlayCommonSha256=$overlay.overlayCommonSha256
        sourceSliceCommit=$SourceSliceCommit; sourceWpfTree=$SourceWpfTree; forkCommit=$overlay.build.forkCommit; forkTree=$overlay.build.forkTree
        forkTreeMapping=$overlay.build.forkCommitStatus
        sourceManifest=[IO.Path]::GetFullPath($input.manifest);sourceManifestSha256=(Get-FileHash -LiteralPath $input.manifest).Hash
        compilerSource=if($rid -eq 'win-x64'){'Microsoft.WindowsDesktop.App.Runtime.win-x64/10.0.11 local official NuGet cache, same hash as PKG0 section 11'}else{'OS System32 D3DCompiler_47.dll prerequisite; NOT redistributed'}
        files=$files }
    Write-PackageText "manifests/$rid.json" ($manifest | ConvertTo-Json -Depth 20)
    Write-PackageText "buildTransitive/assets/$rid.props" ("<Project>`n  <ItemGroup>`n"+($xml -join "`n")+"`n  </ItemGroup>`n</Project>`n")
    $null=Test-WpfInput $StagingDirectory $rid $RequireSigned.IsPresent
}
$items=@($packageFiles | ForEach-Object { '    <None Include="{0}" Pack="true" PackagePath="{0}" />' -f $_ }) -join "`n"
$project=@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework><PackageId>WpfD3D11Runtime</PackageId><Version>$version</Version>
    <Authors>QuickerOrg</Authors><Description>P2 LOCAL CANDIDATE; redistribution clearance pending; DO NOT PUBLISH</Description>
    <IncludeBuildOutput>false</IncludeBuildOutput><EnableDefaultItems>false</EnableDefaultItems>
    <PackageReadmeFile>README.md</PackageReadmeFile><PackageLicenseFile>LICENSE.txt</PackageLicenseFile>
    <!-- Runtime-only package; MSBuild explicitly invokes validation scripts. -->
    <NoWarn>NU5128;NU5111</NoWarn><NuGetAudit>false</NuGetAudit>
    <RepositoryCommit>$($build.forkCommit)</RepositoryCommit><RepositoryType>git</RepositoryType><RepositoryUrl>https://github.com/QuickerOrg/wpf</RepositoryUrl>
    <PackageOutputPath>$([Security.SecurityElement]::Escape($FeedDirectory))</PackageOutputPath>
  </PropertyGroup>
  <ItemGroup>
$items
  </ItemGroup>
</Project>
"@
[IO.File]::WriteAllText("$StagingDirectory/WpfD3D11Runtime.csproj",$project,[Text.UTF8Encoding]::new($false))
# The release workspace may be inside Quicker or another repository. Never inherit
# its build hooks, signing targets, central packages or restore dependencies.
$isolation=@('-p:ImportDirectoryBuildProps=false','-p:ImportDirectoryBuildTargets=false','-p:ImportDirectoryPackagesProps=false','-p:NuGetAudit=false')
& dotnet restore "$StagingDirectory/WpfD3D11Runtime.csproj" --configfile $NuGetConfig --artifacts-path $ArtifactsPath @isolation
if($LASTEXITCODE) { throw 'Offline pack restore failed' }
& dotnet pack "$StagingDirectory/WpfD3D11Runtime.csproj" --no-build --no-restore --artifacts-path $ArtifactsPath @isolation
if($LASTEXITCODE) { throw 'Pack failed' }
$nupkg=Join-Path $FeedDirectory "WpfD3D11Runtime.$version.nupkg"
Write-Output "LOCAL ONLY: $nupkg SHA256=$((Get-FileHash -LiteralPath $nupkg).Hash)"
