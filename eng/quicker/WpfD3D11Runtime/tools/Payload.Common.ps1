#requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-WpfNames {
    @('DirectWriteForwarder','PresentationCore','PresentationFramework',
      'PresentationFramework-SystemCore','PresentationFramework-SystemData','PresentationFramework-SystemDrawing',
      'PresentationFramework-SystemXml','PresentationFramework-SystemXmlLinq','PresentationFramework.Aero',
      'PresentationFramework.Aero2','PresentationFramework.AeroLite','PresentationFramework.Classic',
      'PresentationFramework.Fluent','PresentationFramework.Luna','PresentationFramework.Royale','PresentationUI',
      'ReachFramework','System.Printing','System.Windows.Controls.Ribbon','System.Windows.Input.Manipulations',
      'System.Windows.Presentation','System.Windows.Primitives','System.Xaml','UIAutomationClient',
      'UIAutomationClientSideProviders','UIAutomationProvider','UIAutomationTypes','WindowsBase','WindowsFormsIntegration',
      'wpfgfx_cor3','PenImc_cor3','PresentationNative_cor3','vcruntime140_cor3') | ForEach-Object { "$_.dll" }
}

function Assert-Wpf($Condition, [string]$Message) {
    if (-not $Condition) { throw "WpfD3D11Runtime: $Message" }
}

function Assert-Set($Actual, $Expected, [string]$Context) {
    $a = @($Actual | Sort-Object); $e = @($Expected | Sort-Object)
    Assert-Wpf (($a.Count -eq $e.Count) -and (($a -join '|') -ceq ($e -join '|'))) "$Context 集合不等于锁定清单。缺少=[$(@($e | Where-Object { $_ -cnotin $a }) -join ',')]，多出=[$(@($a | Where-Object { $_ -cnotin $e }) -join ',')]；数量 $($a.Count)/$($e.Count)"
}

function Get-PeFacts([string]$Path) {
    # 仅读取 PE/元数据，不装载或执行目标程序集；适用于跨架构验证。
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        Assert-Wpf ($reader.ReadUInt16() -eq 0x5a4d) "不是 PE: $Path"
        $stream.Position = 0x3c; $pe = $reader.ReadInt32(); $stream.Position = $pe
        Assert-Wpf ($reader.ReadUInt32() -eq 0x4550) "PE signature 错误: $Path"
        $machine = $reader.ReadUInt16(); $count = $reader.ReadUInt16()
        $stream.Position = $pe + 20; $optionalSize = $reader.ReadUInt16()
        $optional = $pe + 24; $stream.Position = $optional; $magic = $reader.ReadUInt16()
        $directories = $optional + $(if ($magic -eq 0x20b) {112} else {96})
        $stream.Position = $directories + 8; $importRva = $reader.ReadUInt32()
        $sections = @()
        $stream.Position = $optional + $optionalSize
        for ($i=0; $i -lt $count; $i++) {
            $start = $stream.Position; $stream.Position += 8
            $virtualSize=$reader.ReadUInt32(); $virtual=$reader.ReadUInt32()
            $rawSize=$reader.ReadUInt32(); $raw=$reader.ReadUInt32()
            $sections += @{ start=$virtual; size=[Math]::Max($virtualSize,$rawSize); raw=$raw }
            $stream.Position=$start+40
        }
        function Convert-Rva([uint32]$Rva) {
            foreach ($s in $sections) { if ($Rva -ge $s.start -and $Rva -lt ($s.start+$s.size)) { return [long]($s.raw+$Rva-$s.start) } }
            throw "PE RVA 越界: $Path / $Rva"
        }
        $imports = @()
        if ($importRva) {
            $table = Convert-Rva $importRva
            for ($i=0; $i -lt 4096; $i++) {
                $stream.Position=$table+20*$i
                $first=$reader.ReadUInt32(); $time=$reader.ReadUInt32(); $chain=$reader.ReadUInt32()
                $name=$reader.ReadUInt32(); $thunk=$reader.ReadUInt32()
                if (($first -bor $time -bor $chain -bor $name -bor $thunk) -eq 0) { break }
                $stream.Position=Convert-Rva $name
                $chars=[Collections.Generic.List[char]]::new()
                while (($b=$reader.ReadByte()) -ne 0) { $chars.Add([char]$b) }
                $imports += (-join $chars).ToLowerInvariant()
            }
        }
    } finally { $reader.Dispose(); $stream.Dispose() }
    $assembly = $null
    try { $assembly = [Reflection.AssemblyName]::GetAssemblyName($Path).Version.ToString() }
    catch [BadImageFormatException] { }
    $v=[Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    [ordered]@{
        machine=('{0:X4}' -f $machine); assemblyVersion=$assembly
        fileVersion=('{0}.{1}.{2}.{3}' -f $v.FileMajorPart,$v.FileMinorPart,$v.FileBuildPart,$v.FilePrivatePart)
        sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        imports=@($imports | Sort-Object -Unique)
    }
}

function Read-WpfManifest([string]$Root, [string]$Rid) {
    Assert-Wpf ($Rid -cin @('win-x64','win-arm64')) "必须显式指定 win-x64 或 win-arm64 RID；当前 '$Rid'，不支持 x86 或 RID fallback。"
    $m=Get-Content -LiteralPath "$Root/manifests/$Rid.json" -Raw | ConvertFrom-Json -AsHashtable
    Assert-Wpf ($m.rid -ceq $Rid) 'manifest RID 不一致'
    $expected=@(Get-WpfNames)
    if ($Rid -eq 'win-x64') { $expected += 'D3DCompiler_47_cor3.dll' }
    Assert-Set @($m.files.name) $expected 'manifest 文件'
    foreach ($f in $m.files) {
        $native=$f.name -in @('wpfgfx_cor3.dll','PenImc_cor3.dll','PresentationNative_cor3.dll','vcruntime140_cor3.dll','D3DCompiler_47_cor3.dll')
        $category=if($native){'native'}else{'runtime'}
        Assert-Wpf ($f.category -ceq $category) "分类错误 $($f.name)，应为 $category（DirectWriteForwarder 必须为 runtime）"
        $relative=if($native){"runtimes/$Rid/native/$($f.name)"}else{"runtimes/$Rid/lib/net10.0/$($f.name)"}
        Assert-Wpf ($f.pathInPackage -ceq $relative) "包路径错误 $($f.name)"
        Assert-Wpf ($f.machine -ceq $(if($Rid -eq 'win-x64'){'8664'}else{'AA64'})) "PE 架构错误 $($f.name)"
        if (-not $native) { Assert-Wpf ($f.assemblyVersion -ceq '10.0.0.0') "AssemblyVersion 错误 $($f.name)" }
    }
    return $m
}

function Test-WpfInput([string]$Root, [string]$Rid) {
    $m=Read-WpfManifest $Root $Rid
    $actual=@(Get-ChildItem -LiteralPath "$Root/runtimes/$Rid" -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/') })
    Assert-Set $actual @($m.files.pathInPackage) '包输入'
    [xml]$assets=Get-Content -LiteralPath "$Root/buildTransitive/assets/$Rid.props" -Raw
    $items=@($assets.Project.ItemGroup.ChildNodes | Where-Object { $_.NodeType -eq 'Element' })
    Assert-Set @($items | ForEach-Object { $_.GetAttribute('PathInPackage') }) @($m.files.pathInPackage) 'MSBuild 声明'
    foreach($f in $m.files) {
        $facts=Get-PeFacts "$Root/$($f.pathInPackage)"
        foreach($key in @('machine','assemblyVersion','fileVersion','sha256')) {
            Assert-Wpf ($facts[$key] -ceq $f[$key]) "输入 $($f.name) $key 不符"
        }
        Assert-Set $facts.imports $f.imports "输入 $($f.name) imports"
        $item=@($items | Where-Object { $_.GetAttribute('PathInPackage') -ceq $f.pathInPackage })[0]
        $kind=if($f.category -eq 'runtime'){'_WpfD3D11Runtime'}else{'_WpfD3D11Native'}
        Assert-Wpf ($item.Name -ceq $kind -and $item.GetAttribute('AssetType') -ceq $f.category -and
            $item.GetAttribute('NuGetPackageId') -ceq 'WpfD3D11Runtime' -and
            $item.GetAttribute('NuGetPackageVersion') -ceq $m.packageVersion -and
            $item.GetAttribute('Include') -ceq ('$(WpfD3D11RuntimeRoot)'+$f.pathInPackage)) "MSBuild 元数据错误 $($f.name)"
    }
    return $m
}

function Test-WpfOutput([string]$Root, [string]$Rid, [string]$OutputDirectory, [string]$DepsFile, [bool]$SelfContained) {
    $m=Read-WpfManifest $Root $Rid
    $deps=Get-Content -LiteralPath $DepsFile -Raw | ConvertFrom-Json -AsHashtable
    Assert-Wpf ($deps.runtimeTarget.name.EndsWith("/$Rid",[StringComparison]::Ordinal)) 'deps runtimeTarget RID 不符'
    $target=$deps.targets[$deps.runtimeTarget.name]
    $declared=@{}
    foreach($lib in $target.Keys) {
        foreach($kind in @('runtime','native','runtimeTargets')) {
            if (-not $target[$lib].ContainsKey($kind)) { continue }
            foreach($path in $target[$lib][$kind].Keys) {
                $meta=$target[$lib][$kind][$path]; $category=$kind
                $name=[IO.Path]::GetFileName($path)
                if($kind -eq 'runtimeTargets') {
                    if($meta.rid -ne $Rid) {
                        Assert-Wpf ($name -notin $m.files.name) "deps 含跨 RID 的 WPF 声明: $path / $($meta.rid)"
                        continue
                    }
                    $category=$meta.assetType
                }
                if($name -notin $m.files.name) { continue }
                if(-not $declared.ContainsKey($name)) { $declared[$name]=@() }
                $declared[$name]+=@{ library=$lib; path=$path; category=$category; metadata=$meta }
            }
        }
    }
    foreach($f in $m.files) {
        Assert-Wpf ($declared.ContainsKey($f.name) -and $declared[$f.name].Count -eq 1) "deps 必须恰一个声明: $($f.name)"
        $d=$declared[$f.name][0]
        Assert-Wpf ($d.library -ceq "WpfD3D11Runtime/$($m.packageVersion)" -and $d.path -ceq $f.pathInPackage -and $d.category -ceq $f.category) "deps 所有权/分类不符: $($f.name)"
        Assert-Wpf ($d.metadata.fileVersion -ceq $f.fileVersion) "deps FileVersion 不符 $($f.name)"
        if($f.category -eq 'runtime') { Assert-Wpf ($d.metadata.assemblyVersion -ceq $f.assemblyVersion) "deps AssemblyVersion 不符 $($f.name)" }
        $path=Join-Path $OutputDirectory $f.name
        Assert-Wpf (Test-Path -LiteralPath $path -PathType Leaf) "缺少输出 $path"
        $facts=Get-PeFacts $path
        foreach($key in @('sha256','machine','assemblyVersion','fileVersion')) { Assert-Wpf ($facts[$key] -ceq $f[$key]) "输出 $($f.name) $key 不符" }
    }
    $all=@(Get-ChildItem -LiteralPath $OutputDirectory -File -Recurse)
    foreach($f in $m.files) { Assert-Wpf (@($all | Where-Object Name -eq $f.name).Count -eq 1) "输出有同名副本 $($f.name)" }
    if($Rid -eq 'win-arm64') {
        Assert-Wpf (@($all | Where-Object Name -like 'D3DCompiler*').Count -eq 0) 'ARM64 禁止包内 compiler'
        foreach($lib in $target.Values) {
            foreach($kind in @('native','runtimeTargets')) {
                if($lib.ContainsKey($kind)) { Assert-Wpf (@($lib[$kind].Keys | Where-Object { [IO.Path]::GetFileName($_) -like 'D3DCompiler*' }).Count -eq 0) 'ARM64 deps 禁止 compiler' }
            }
        }
    }
    if(-not $SelfContained) { Assert-Wpf (@($all | Where-Object Name -in @('coreclr.dll','hostfxr.dll','hostpolicy.dll')).Count -eq 0) 'FDD 禁带 coreclr/hostfxr/hostpolicy' }
    [ordered]@{ mode='enabled'; rid=$Rid; selfContained=$SelfContained; output=$OutputDirectory; deps=$DepsFile; packageVersion=$m.packageVersion; runtime=@($m.files | Where-Object category -eq runtime).Count; native=@($m.files | Where-Object category -eq native).Count; passed=$true }
}
