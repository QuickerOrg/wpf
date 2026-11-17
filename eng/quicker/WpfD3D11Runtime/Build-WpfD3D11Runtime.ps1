#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('x64','ARM64')][string]$Architecture,
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$QuickerRepository,
    [Parameter(Mandatory)][string]$ReferenceDirectory,
    [string]$ForkRepository='F:/WpfP0/wpf-repo',
    [int]$PrivateReleaseSequence=1,
    [string]$MSBuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe',
    [string]$Crossgen2='F:/WpfP0/r2r-tools-10.0.14/package/tools/crossgen2.exe',
    [string]$SignTool,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl,
    [switch]$ExecuteSigning,
    [string]$EquivalenceReport,
    [switch]$ResumeFailedBuild,
    [switch]$BuildOnly
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/tools/Payload.Common.ps1"
$wpf=[IO.Path]::GetFullPath("$PSScriptRoot/../../..")
$slice=[IO.Path]::GetFullPath("$wpf/../..")
foreach($p in @($OutputRoot,$QuickerRepository,$ReferenceDirectory)) {
    Assert-Wpf ([IO.Path]::IsPathFullyQualified($p)) "Absolute path required: $p"
}
if($ResumeFailedBuild) {
    $prior=Read-WpfJson "$OutputRoot/build-receipt.json"
    Assert-Wpf ($prior.sourceSliceCommit -eq (& git -C $slice rev-parse HEAD).Trim() -and $prior.architecture -eq $Architecture) 'Resume source/architecture mismatch'
} else { Assert-Wpf (-not (Test-Path -LiteralPath $OutputRoot)) 'OutputRoot must be NEW; never reuse evidence/build directories.' }
Assert-Wpf ($PrivateReleaseSequence -gt 0) 'Release sequence must be positive.'
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
# C++/CLI generates a helper project that must inherit WPF's Directory.Build.*
# and FilterItem1ByItem2. A tree-local, NEW obj root is required (P2 exception).
$sourceObj="$wpf/artifacts-p2-$([IO.Path]::GetFileName($OutputRoot.TrimEnd('/','\')))/obj/"
Assert-Wpf ($ResumeFailedBuild -or -not (Test-Path -LiteralPath $sourceObj)) 'P2 source obj directory already exists; use a new OutputRoot.'
$env:NUGET_PACKAGES='F:/WpfP0/packages'
$env:APPDATA="$OutputRoot/appdata"
$env:CL='/utf-8'; $env:LC_ALL='C'; $env:LANG='C'
New-Item -ItemType Directory -Path "$OutputRoot/empty-feed","$OutputRoot/appdata" -Force | Out-Null
$head=(& git -C $slice rev-parse HEAD).Trim()
$tree=(& git -C $slice rev-parse HEAD:src/wpf).Trim()
$forkCommit=(& git -C $ForkRepository rev-parse HEAD).Trim()
$forkTree=(& git -C $ForkRepository rev-parse 'HEAD^{tree}').Trim()
$sliceFiles=@(& git -C $slice ls-tree -r HEAD:src/wpf | Where-Object { $_ -notmatch '\teng/quicker/' })
$forkFiles=@(& git -C $ForkRepository ls-tree -r HEAD | Where-Object { $_ -notmatch '\teng/quicker/' })
Assert-Wpf (($sliceFiles -join "`n") -ceq ($forkFiles -join "`n")) 'Fork/slice trees differ outside packaging; do not claim fork provenance.'
$dirty=@(& git -C $slice status --porcelain --untracked-files=no | Where-Object { $_.Substring(3) -notlike 'src/wpf/eng/quicker/WpfD3D11Runtime/*' })
Assert-Wpf ($dirty.Count -eq 0) 'Source dirty outside permitted packaging directory.'
[xml]$versions=Get-Content "$wpf/eng/Versions.props"
$baseline=($versions.Project.PropertyGroup | Where-Object MajorVersion | Select-Object -First 1)
$baseline="$($baseline.MajorVersion).$($baseline.MinorVersion).$($baseline.PatchVersion)"
$fileVersion="10.1.$([math]::Floor($PrivateReleaseSequence/65536)).$($PrivateReleaseSequence%65536)"
$info="$baseline+private.$PrivateReleaseSequence.g$forkCommit"
# Same S5q missing-slice-project adapter; source files remain untouched.
$adapter=@'
<Project>
  <ItemGroup Condition="'$(MSBuildProjectName)' == 'sw' and !Exists('$(WpfGraphicsPath)core/sw/bilinearspan/bilinearspan.vcxproj')">
    <ClCompile Include="$(WpfGraphicsPath)core/sw/swlib/bilinearspan.cpp" />
  </ItemGroup>
</Project>
'@
[IO.File]::WriteAllText("$OutputRoot/slice-native.targets",$adapter,[Text.UTF8Encoding]::new($false))
$versionAdapter=@'
<Project>
  <!-- Arcade task outputs overwrite global FileVersion. Restore it with a task
       output AFTER the initializer, before native/managed version generation. -->
  <Target Name="WpfD3D11RestorePrivateVersion" AfterTargets="_InitializeAssemblyVersion">
    <Error Condition="'$(WpfD3D11PrivateFileVersion)' == '' or '$(AssemblyVersion)' != '10.0.0.0'" Text="Explicit private versions required." />
    <CreateProperty Value="$(WpfD3D11PrivateFileVersion)">
      <Output TaskParameter="Value" PropertyName="FileVersion" />
    </CreateProperty>
  </Target>
</Project>
'@
[IO.File]::WriteAllText("$OutputRoot/private-version.targets",$versionAdapter,[Text.UTF8Encoding]::new($false))
$common=@('-m:1','-nodeReuse:false','-nologo','-v:m','-restore','-p:Configuration=Release',"-p:Platform=$Architecture",
    '-p:VCToolsVersion=14.44.35207','-p:VCToolsRedistVersion=14.44.35112',
    "-p:ForceImportAfterCppTargets=$OutputRoot/slice-native.targets","-p:ArtifactsDir=$OutputRoot/artifacts/",
    "-p:CustomAfterMicrosoftCommonTargets=$OutputRoot/private-version.targets",
    "-p:ArtifactsObjDir=$sourceObj",'-p:WpfD3D11Facade=true',
    '-p:AssemblyVersion=10.0.0.0','-p:AutoGenerateAssemblyVersion=false',"-p:FileVersion=$fileVersion","-p:WpfD3D11PrivateFileVersion=$fileVersion",
    "-p:InformationalVersion=$info","-p:Version=$info",'-p:IncludeSourceRevisionInInformationalVersion=false',
    "-p:RestoreSources=$OutputRoot/empty-feed",'-p:NuGetAudit=false','-p:UseSharedCompilation=false',
    '-p:RestoreIgnoreFailedSources=false','-p:DotNetBuildWithOnlineFeeds=false',
    '-p:RunNetFrameworkApiCompat=false','-p:RunRefApiCompat=false','-p:RunApiCompat=false')
$names=@(Get-WpfNames | Where-Object { $_ -notin @('wpfgfx_cor3.dll','PenImc_cor3.dll','PresentationNative_cor3.dll','vcruntime140_cor3.dll') })
$all=@(Get-ChildItem "$wpf/src/Microsoft.DotNet.Wpf/src" -Recurse -File | Where-Object Extension -in @('.csproj','.vcxproj'))
$projects=@("$wpf/src/Microsoft.DotNet.Wpf/src/WpfGfx/core/dll/wpfgfx.vcxproj",
    "$wpf/src/Microsoft.DotNet.Wpf/src/PenImc/dll/PenImc.vcxproj",
    "$wpf/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/DirectWriteForwarder.vcxproj",
    "$wpf/src/Microsoft.DotNet.Wpf/redist/PresentationNative/PresentationNative.vcxproj",
    "$wpf/src/Microsoft.DotNet.Wpf/redist/VCRuntime/VCRuntime.vcxproj")
foreach($name in $names | Where-Object { $_ -ne 'DirectWriteForwarder.dll' }) {
    $matches=@($all | Where-Object BaseName -eq ([IO.Path]::GetFileNameWithoutExtension($name)))
    Assert-Wpf ($matches.Count -eq 1) "Expected one runtime project: $name (found $($matches.Count))"
    $projects+=$matches[0].FullName
}
$receipt=[ordered]@{schema=2;architecture=$Architecture;packageVersion="$baseline-d3d11.$PrivateReleaseSequence";
    privateReleaseSequence=$PrivateReleaseSequence;managedFileVersion=$fileVersion;assemblyVersion='10.0.0.0';
    informationalVersion=$info;upstreamBaseline=$baseline;sourceSliceCommit=$head;sourceWpfTree=$tree;forkTree=$forkTree;
    forkCommit=$forkCommit;forkCommitStatus='Local mirror HEAD; exact tree match excluding eng/quicker packaging. No network attestation.';
    upstreamCommit=(@((Get-Content "$slice/src/source-manifest.json" -Raw | ConvertFrom-Json).repositories | Where-Object path -eq wpf)[0].commitSha);
    vmrCommit=(& git -C $slice log --reverse --format=%H | Select-Object -First 1);sourceIntermediateRoot=$sourceObj;commonArguments=$common;steps=@()}
$timer=[Diagnostics.Stopwatch]::StartNew()
# EnsurePbtPath invokes Clean;Build;Publish, but does not restore this helper graph.
& $MSBuild "$wpf/src/Microsoft.DotNet.Wpf/src/PresentationBuildTasks/PresentationBuildTasks.csproj" @common `
    '-p:Platform=AnyCPU' '-p:TargetFramework=net472' '-t:Restore' "-bl:$OutputRoot/pbt-restore.binlog" *> "$OutputRoot/pbt-restore.log"
if($LASTEXITCODE) { throw 'Offline PresentationBuildTasks restore failed' }
# One MSBuild session caches shared project results; separate invocations cause
# WPF code generation/cycle breakers to rebuild the same graph repeatedly.
$items=($projects | ForEach-Object { '    <RuntimeProject Include="'+[Security.SecurityElement]::Escape($_)+'" />' }) -join "`n"
$graph=@"
<Project DefaultTargets="Build">
  <ItemGroup>
$items
  </ItemGroup>
  <Target Name="Restore"><MSBuild Projects="@(RuntimeProject)" Targets="Restore" BuildInParallel="false" /></Target>
  <Target Name="Build"><MSBuild Projects="@(RuntimeProject)" Targets="Build" BuildInParallel="false" /></Target>
</Project>
"@
[IO.File]::WriteAllText("$OutputRoot/runtime-graph.proj",$graph,[Text.UTF8Encoding]::new($false))
& $MSBuild "$OutputRoot/runtime-graph.proj" @common "-bl:$OutputRoot/runtime-graph.binlog" *> "$OutputRoot/runtime-graph.log"
$receipt.steps+=@{project='runtime-graph.proj';projects=$projects;exitCode=$LASTEXITCODE;elapsedSeconds=$timer.Elapsed.TotalSeconds}
[IO.File]::WriteAllText("$OutputRoot/build-receipt.json",($receipt | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
if($LASTEXITCODE) { throw "Build failed: see $OutputRoot/runtime-graph.log" }
if($timer.Elapsed.TotalMinutes -gt 60) { throw 'Architecture build exceeded 60 minute budget; stop this direction.' }
Write-Output "Built complete $Architecture runtime graph"
# Projection is exact; never copy all native-redist/tool output into the overlay.
New-Item -ItemType Directory "$OutputRoot/il" -Force | Out-Null
foreach($name in Get-WpfNames) {
    $candidates=@(Get-ChildItem "$OutputRoot/artifacts/bin" -Recurse -File -Filter $name |
        Where-Object { $_.FullName -notmatch '[\\/](ref|refint|publish)[\\/]|-ref[\\/]|-cycle[\\/]' })
    $hashes=@($candidates | Get-FileHash | Select-Object -ExpandProperty Hash -Unique)
    Assert-Wpf ($candidates.Count -gt 0 -and $hashes.Count -eq 1) "Missing/ambiguous output: $name"
    Copy-Item -LiteralPath $candidates[0].FullName -Destination "$OutputRoot/il/$name"
    $facts=Get-PeFacts "$OutputRoot/il/$name"
    if(Test-WpfForkFile $name) {
        Assert-Wpf ($facts.fileVersion -ceq $fileVersion) "Actual FileVersion mismatch: $name"
        if($facts.assemblyVersion) {
            Assert-Wpf ($facts.assemblyVersion -ceq '10.0.0.0' -and $facts.informationalVersion -ceq $info) "Actual managed identity/informational version mismatch: $name"
        }
    }
}
Assert-Set @(Get-ChildItem "$OutputRoot/il" -Filter '*.dll' -File | ForEach-Object Name) @(Get-WpfNames) 'IL projection'
if($BuildOnly) { return }
Assert-Wpf ($EquivalenceReport -and (Test-Path -LiteralPath $EquivalenceReport)) 'Use -BuildOnly, review equivalence, then Complete-WpfD3D11Runtime.ps1 with a hash-locked report.'
& "$PSScriptRoot/Complete-WpfD3D11Runtime.ps1" -OutputRoot $OutputRoot -QuickerRepository $QuickerRepository `
    -ReferenceDirectory $ReferenceDirectory -EquivalenceReport $EquivalenceReport -Crossgen2 $Crossgen2 `
    -SignTool $SignTool -CertificateThumbprint $CertificateThumbprint -TimestampUrl $TimestampUrl -ExecuteSigning:$ExecuteSigning
