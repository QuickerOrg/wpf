#requires -Version 7.0
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
    [Parameter(Mandatory)][string]$NuGetConfig
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/tools/Payload.Common.ps1"
$version='10.0.14-d3d11.0.local.1'
$sliceCommit=(& git -C $PSScriptRoot rev-parse HEAD).Trim()
if($LASTEXITCODE) { throw '无法登记 slice HEAD' }
$wpfTree=(& git -C $PSScriptRoot rev-parse HEAD:src/wpf).Trim()
if($LASTEXITCODE) { throw '无法登记 slice WPF 树映射' }
foreach($path in @($StagingDirectory,$ArtifactsPath,$FeedDirectory,$NuGetConfig)) {
    Assert-Wpf ([IO.Path]::IsPathFullyQualified($path)) "必须使用绝对路径: $path"
}
Assert-Wpf (-not (Test-Path -LiteralPath $StagingDirectory)) 'staging 已存在；为保证无旧输入，请使用新的空目录'
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
foreach($input in @(
    @{rid='win-x64';directory=$X64InputDirectory;manifest=$X64Manifest},
    @{rid='win-arm64';directory=$Arm64InputDirectory;manifest=$Arm64Manifest})) {
    $rid=$input.rid
    $overlay=Get-Content -LiteralPath $input.manifest -Raw | ConvertFrom-Json -AsHashtable
    Assert-Set @($overlay.files.name) @(Get-WpfNames) "$rid overlay"
    $files=@(); $xml=[Collections.Generic.List[string]]::new()
    foreach($entry in @($overlay.files)+$(if($rid -eq 'win-x64'){@{name='D3DCompiler_47_cor3.dll'}}else{@()})) {
        $compiler=$entry.name -eq 'D3DCompiler_47_cor3.dll'
        $source=if($compiler){$X64Compiler}else{Join-Path $input.directory $entry.name}
        $facts=Get-PeFacts $source
        if($compiler) {
            Assert-Wpf ($facts.sha256 -ceq 'A05F99734F7C4822FEFC12B367AF21FD0976ED6608752FB1E1E80B6ECE7ECBBB') 'x64 compiler 必须与 PKG0 官方 10.0.11 缓存来源相同'
        } else { Assert-Wpf ($facts.sha256 -ceq $entry.sha256) "overlay 哈希不符 $source" }
        $category=if($facts.assemblyVersion){'runtime'}else{'native'}
        $relative=if($category -eq 'runtime'){"runtimes/$rid/lib/net10.0/$($entry.name)"}else{"runtimes/$rid/native/$($entry.name)"}
        Add-File $source $relative
        $record=[ordered]@{ name=$entry.name; category=$category; pathInPackage=$relative; source=[IO.Path]::GetFullPath($source) }
        foreach($key in $facts.Keys) { $record[$key]=$facts[$key] }
        $record.readyToRun=if($compiler){$false}else{$entry.readyToRun}
        $record.r2rEvidence=if($compiler){$null}else{@{inputSha256=$entry.ilSha256;crossgen2=$overlay.crossgen2;crossgen2Sha256=$overlay.crossgen2Sha256}}
        $record.signatureStatus='P1 inherited bytes; production signing verification pending P2'
        $files+=$record
        $kind=if($category -eq 'runtime'){'_WpfD3D11Runtime'}else{'_WpfD3D11Native'}
        $xml.Add(('    <{0} Include="$(WpfD3D11RuntimeRoot){1}" AssetType="{2}" NuGetPackageId="WpfD3D11Runtime" NuGetPackageVersion="{3}" PathInPackage="{1}" DestinationSubPath="{4}" TargetPath="{4}" CopyLocal="true" CopyToOutputDirectory="PreserveNewest" CopyToPublishDirectory="PreserveNewest" />' -f $kind,$relative,$category,$version,$entry.name))
    }
    $manifest=[ordered]@{ schema=1; packageId='WpfD3D11Runtime'; packageVersion=$version; localValidationOnly=$true
        rid=$rid; upstreamBaseline='10.0.14 label only; exact servicing provenance pending P2'
        sourceSliceCommit=$sliceCommit; sourceWpfTree=$wpfTree; forkCommit=$null
        forkTreeMapping='slice HEAD:src/wpf maps to QuickerOrg/wpf root; public fork commit attestation pending P2'
        sourceManifest=[IO.Path]::GetFullPath($input.manifest);sourceManifestSha256=(Get-FileHash -LiteralPath $input.manifest).Hash
        compilerSource=if($rid -eq 'win-x64'){'Microsoft.WindowsDesktop.App.Runtime.win-x64/10.0.11 local official NuGet cache, same hash as PKG0 section 11'}else{'OS System32 D3DCompiler_47.dll prerequisite; NOT redistributed'}
        files=$files }
    Write-PackageText "manifests/$rid.json" ($manifest | ConvertTo-Json -Depth 20)
    Write-PackageText "buildTransitive/assets/$rid.props" ("<Project>`n  <ItemGroup>`n"+($xml -join "`n")+"`n  </ItemGroup>`n</Project>`n")
    $null=Test-WpfInput $StagingDirectory $rid
}
$items=@($packageFiles | ForEach-Object { '    <None Include="{0}" Pack="true" PackagePath="{0}" />' -f $_ }) -join "`n"
$project=@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework><PackageId>WpfD3D11Runtime</PackageId><Version>$version</Version>
    <Authors>QuickerOrg</Authors><Description>P1 LOCAL VALIDATION ONLY; redistribution clearance pending; DO NOT PUBLISH</Description>
    <IncludeBuildOutput>false</IncludeBuildOutput><EnableDefaultItems>false</EnableDefaultItems>
    <PackageReadmeFile>README.md</PackageReadmeFile><PackageLicenseFile>LICENSE.txt</PackageLicenseFile>
    <!-- 无顶层 lib 是 runtime-only 设计；ps1 由 MSBuild 显式调用，不使用 NuGet install.ps1。 -->
    <NoWarn>NU5128;NU5111</NoWarn><NuGetAudit>false</NuGetAudit>
    <RepositoryType>git</RepositoryType><RepositoryUrl>https://github.com/QuickerOrg/wpf</RepositoryUrl>
    <PackageOutputPath>$([Security.SecurityElement]::Escape($FeedDirectory))</PackageOutputPath>
  </PropertyGroup>
  <ItemGroup>
$items
  </ItemGroup>
</Project>
"@
[IO.File]::WriteAllText("$StagingDirectory/WpfD3D11Runtime.csproj",$project,[Text.UTF8Encoding]::new($false))
& dotnet restore "$StagingDirectory/WpfD3D11Runtime.csproj" --configfile $NuGetConfig --artifacts-path $ArtifactsPath -p:NuGetAudit=false
if($LASTEXITCODE) { throw '离线 pack restore 失败' }
& dotnet pack "$StagingDirectory/WpfD3D11Runtime.csproj" --no-build --no-restore --artifacts-path $ArtifactsPath
if($LASTEXITCODE) { throw 'pack 失败' }
$nupkg=Join-Path $FeedDirectory "WpfD3D11Runtime.$version.nupkg"
Write-Output "LOCAL ONLY: $nupkg SHA256=$((Get-FileHash -LiteralPath $nupkg).Hash)"
