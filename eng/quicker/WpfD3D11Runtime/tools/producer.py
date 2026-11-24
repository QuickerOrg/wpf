"""单仓库 WPF 生产者；不下载工具、不签名、不发布。所有可写数据位于 outputRoot。"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import zipfile

HERE = Path(__file__).resolve().parent.parent
RIDS = {'win-x64': 'x64', 'win-arm64': 'ARM64'}
NATIVE = {'wpfgfx_cor3.dll', 'PenImc_cor3.dll', 'PresentationNative_cor3.dll',
          'vcruntime140_cor3.dll', 'D3DCompiler_47_cor3.dll'}
UPSTREAM = {'PresentationNative_cor3.dll', 'vcruntime140_cor3.dll', 'D3DCompiler_47_cor3.dll'}
SUPPORT = {'README.md', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt',
           'licenses/README.md', 'licenses/DNCENG-LICENSE.txt', 'licenses/DNCENG-THIRD-PARTY-NOTICES.txt',
           'licenses/UPSTREAM-THIRD-PARTY-NOTICES.txt', 'licenses/WINDOWSDESKTOP-LICENSE.txt',
           'buildTransitive/WpfD3D11Runtime.props', 'buildTransitive/WpfD3D11Runtime.targets',
           'tools/Payload.Common.ps1', 'tools/Test-WpfD3D11Runtime.ps1'}
NAMES = ['DirectWriteForwarder', 'PresentationCore', 'PresentationFramework',
         'PresentationFramework-SystemCore', 'PresentationFramework-SystemData', 'PresentationFramework-SystemDrawing',
         'PresentationFramework-SystemXml', 'PresentationFramework-SystemXmlLinq', 'PresentationFramework.Aero',
         'PresentationFramework.Aero2', 'PresentationFramework.AeroLite', 'PresentationFramework.Classic',
         'PresentationFramework.Fluent', 'PresentationFramework.Luna', 'PresentationFramework.Royale',
         'PresentationUI', 'ReachFramework', 'System.Printing', 'System.Windows.Controls.Ribbon',
         'System.Windows.Input.Manipulations', 'System.Windows.Presentation', 'System.Windows.Primitives',
         'System.Xaml', 'UIAutomationClient', 'UIAutomationClientSideProviders', 'UIAutomationProvider',
         'UIAutomationTypes', 'WindowsBase', 'WindowsFormsIntegration', 'wpfgfx_cor3', 'PenImc_cor3',
         'PresentationNative_cor3', 'vcruntime140_cor3']


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def sha(path):
    return digest(Path(path).read_bytes())


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args],
                                   env={**os.environ, 'GIT_NO_LAZY_FETCH': '1'}).decode().rstrip('\r\n')


def absolute(value, label, exists=False):
    path = Path(value)
    require(path.is_absolute(), f'{label} 必须为绝对路径: {value}')
    if exists:
        require(path.exists(), f'{label} 缺失: {path}；请显式设置配置文件中的 {label}')
    return path.resolve()


def safe_path(root, relative):
    require((relative == '[Content_Types].xml' or re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', relative)) and
            not any(s in ('.', '..') for s in relative.split('/')), f'不安全相对路径: {relative}')
    path = root / relative
    require(path.resolve().is_relative_to(root.resolve()), f'路径越界: {relative}')
    return path


def inventory():
    return [f'runtimes/{rid}/{"native" if name+".dll" in NATIVE else "lib/net10.0"}/{name}.dll'
            for rid in RIDS for name in NAMES + (['D3DCompiler_47_cor3'] if rid == 'win-x64' else [])]


class Producer:
    def __init__(self, file, signed):
        user = read(file)
        root = Path('G:/WpfD3D11Runtime')
        self.c = dict(sourceRoot=str(HERE.parents[2]), outputRoot=str(root/'_build/runtime'), mode='full',
                      msbuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe',
                      vcToolsVersion='14.44.35207', vcToolsRedistVersion='14.44.35112',
                      windowsSdkBin='C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64',
                      nugetPackagesDirectory=str(root/'tools/packages'),
                      compilerToolsetVersion='10.0.112', restoreSource='',
                      nativeSourcePrefix='', nativeTrimPrefix='', nativePdbAltPaths={},
                      perlCommand=str(root/'tools/strawberry-perl/5.38.0.1/portableshell.bat'),
                      crossgen2=str(root/'tools/crossgen2/10.0.14/crossgen2.exe'),
                      referenceDirectories={rid: str(root/f'tools/references/{rid}') for rid in RIDS},
                      x64Compiler=str(root/'tools/D3DCompiler_47_cor3.dll'),
                      baselineProvenance=dict(tag='v10.0.114', vmrCommit='0427b9abbead3fa64c6655eb4290c537397131fa',
                                              wpfTree='664f60538e7fbbc978dcd8984f3e9a023906e5de'),
                      signerThumbprint='261B3E1A62721291FADE6D1414E78F952B3EEDFA',
                      timestampThumbprint='571468410CA85AF3424EF9164A513610F4D38D98')
        self.c.update(user)
        c = self.c
        require(c['mode'] in ('full', 'incremental'), 'mode 必须是 full 或 incremental')
        self.source = absolute(c['sourceRoot'], 'sourceRoot', True)
        require(self.source == Path(git(self.source, 'rev-parse', '--show-toplevel')).resolve(), 'sourceRoot 必须是 fork 根目录')
        self.out = absolute(c['outputRoot'], 'outputRoot')
        require(not any((ancestor/'.git').exists() for ancestor in (self.out, *self.out.parents)), 'outputRoot 必须在 Git 工作树外')
        # Reject output under any worktree of this clone, including the resident clone.
        for line in git(self.source, 'worktree', 'list', '--porcelain').splitlines():
            if line.startswith('worktree '):
                require(not self.out.is_relative_to(Path(line[9:]).resolve()), 'outputRoot 不得位于 Git 工作树内')
        self.signed = signed
        self.version = c.get('packageVersion', '')
        match = re.fullmatch(r'(10\.0\.\d+)-d3d11\.([1-9]\d*)', self.version)
        require(match, '必须指定 packageVersion=10.0.P-d3d11.N，N>0')
        self.baseline, sequence = match.group(1), int(match.group(2))
        versions = ET.parse(self.source/'eng/Versions.props').getroot()
        actual_baseline = '.'.join(versions.find('.//{*}'+k).text for k in ('MajorVersion', 'MinorVersion', 'PatchVersion'))
        require(actual_baseline == self.baseline, f'packageVersion 与源码基线不一致: {actual_baseline}')
        require(sequence < 65536**2, '私有版本序号溢出')
        self.sequence = sequence
        self.file_version = f'10.1.{sequence//65536}.{sequence%65536}'
        self.runtime = inventory()
        self.rebuild = c.get('rebuildFiles', []) if c['mode'] == 'incremental' else [p for p in self.runtime if Path(p).name not in UPSTREAM]
        require(self.rebuild and len(self.rebuild) == len(set(self.rebuild)) and set(self.rebuild) <= set(self.runtime), '重建清单必须为非空、无重复的精确包路径')
        require(all(Path(p).name not in UPSTREAM for p in self.rebuild), '上游原件不可标为 fork 重建')
        # Source and producer dirtiness have separate meanings; only producer edits are allowed for a local rehearsal.
        status = git(self.source, 'status', '--porcelain', '--untracked-files=all')
        require(all(line[3:].startswith('eng/quicker/WpfD3D11Runtime/') for line in status.splitlines()), '运行时源码工作区不干净')
        self.provenance = dict(forkCommit=git(self.source, 'rev-parse', 'HEAD'),
                               forkRootTree=git(self.source, 'rev-parse', 'HEAD^{tree}'),
                               runtimeSrcTree=git(self.source, 'rev-parse', 'HEAD:src'),
                               baselineProvenance=c['baselineProvenance'],
                               producerDirty=bool(status), producerFiles={p.relative_to(HERE).as_posix(): sha(p)
                               for p in HERE.rglob('*') if p.is_file() and '__pycache__' not in p.parts})
        self.info = f'{self.baseline}+private.{sequence}.g{self.provenance["forkCommit"]}'
        self.env = {**os.environ, 'DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE': 'true',
                    'DOTNET_CLI_TELEMETRY_OPTOUT': '1', 'DOTNET_SKIP_FIRST_TIME_EXPERIENCE': '1',
                    'DOTNET_NOLOGO': '1', 'NUGET_PACKAGES': c['nugetPackagesDirectory'],
                    'GIT_NO_LAZY_FETCH': '1', 'CL': '/utf-8', 'LC_ALL': 'C', 'LANG': 'C',
                    'NUGET_HTTP_CACHE_PATH': str(self.out/'http-cache'),
                    'TEMP': str(self.out/'temp'), 'TMP': str(self.out/'temp'), 'APPDATA': str(self.out/'appdata')}
        if c['nativeSourcePrefix']:
            self.env['CL'] += f' /experimental:deterministic /pathmap:"{self.source}={c["nativeSourcePrefix"]}"'
        if c['nativeTrimPrefix']:
            require(not c['nativeSourcePrefix'], 'nativeTrimPrefix 与 nativeSourcePrefix 互斥')
            require(str(self.source).startswith(c['nativeTrimPrefix']), 'nativeTrimPrefix 必须是实际 sourceRoot 的前缀')
            self.env['CL'] += f' /d1trimfile:"{c["nativeTrimPrefix"]}"'
        self.env['PATH'] = c['windowsSdkBin'] + os.pathsep + self.env['PATH']
        self.stage = self.out/'stage'
        self.package = self.out/f'publish/WpfD3D11Runtime.{self.version}.nupkg'

    def preflight(self):
        c = self.c
        for key in ('msbuild', 'nugetPackagesDirectory', 'perlCommand', 'windowsSdkBin'):
            absolute(c[key], key, True)
        require((Path(c['windowsSdkBin'])/'rc.exe').is_file(), 'windowsSdkBin 中缺少 rc.exe')
        absolute(str(Path(c['nugetPackagesDirectory'])/'microsoft.net.sdk.compilers.toolset'/c['compilerToolsetVersion']/'csc.exe'), 'compilerToolsetVersion', True)
        require(c['restoreSource'] in ('', 'https://api.nuget.org/v3/index.json'), 'restoreSource 只允许空（离线）或 nuget.org')
        require(shutil.which('dotnet') and shutil.which('powershell.exe'), '需要 dotnet 与 Windows PowerShell 5.1')
        tools = Path(c['msbuild']).parents[3]/'VC/Tools/MSVC'/c['vcToolsVersion']
        for arch in ('x64', 'arm64'):
            require((tools/f'bin/Hostx64/{arch}/cl.exe').is_file(), f'MSVC {arch} 编译器缺失: {tools}')
        if c['mode'] == 'incremental':
            baseline = absolute(c.get('basePackage', ''), 'basePackage', True)
            require(c.get('basePackageSha256') and sha(baseline) == c['basePackageSha256'].upper(), 'basePackage SHA256 不匹配或未显式锁定')
            with zipfile.ZipFile(baseline) as z:
                require(z.testzip() is None and len(z.namelist()) == len(set(z.namelist())), '基准 ZIP 损坏/重复条目')
                require(set(n for n in z.namelist() if n.startswith('runtimes/')) == set(self.runtime), '基准包运行时文件集不匹配')
                for rid in RIDS:
                    require(read_manifest(z, rid)['upstreamBaseline'] == self.baseline, '基准 servicing 版本不匹配')
        if c['mode'] == 'full' or any(Path(p).name not in NATIVE and Path(p).name != 'DirectWriteForwarder.dll' for p in self.rebuild):
            crossgen = absolute(c['crossgen2'], 'crossgen2', True)
            for dependency in ('jitinterface_x64.dll', 'clrjit_win_x64_x64.dll', 'clrjit_universal_arm64_x64.dll'):
                require((crossgen.parent/dependency).is_file(), f'crossgen2 运行依赖缺失: {dependency}')
            for rid in RIDS:
                ref = absolute(c['referenceDirectories'][rid], f'referenceDirectories.{rid}', True)
                require((ref/'System.Private.CoreLib.dll').is_file(), f'缺少基础运行时引用: {ref}')
        if c['mode'] == 'full':
            require(sha(absolute(c['x64Compiler'], 'x64Compiler', True)) == 'A05F99734F7C4822FEFC12B367AF21FD0976ED6608752FB1E1E80B6ECE7ECBBB', 'x64Compiler 必须保留许可核对的上游原件')
            absolute(c.get('packageReadme', ''), 'packageReadme', True)
            require(all(c.get('metadata', {}).get(k) for k in ('authors','description','projectUrl','tags','releaseNotes')), '全量模式需要显式 metadata 公开元数据')
        projects = {p: self.project(Path(p).name).relative_to(self.source).as_posix() for p in self.rebuild}
        return dict(passed=True, mode=c['mode'], packageVersion=self.version, provenance=self.provenance,
                    rebuildFiles=self.rebuild, projects=projects, configuration=c, requireSigned=self.signed)

    def project(self, name):
        src = self.source/'src/Microsoft.DotNet.Wpf'
        mapping = {'wpfgfx_cor3.dll': 'src/WpfGfx/core/dll/wpfgfx.vcxproj',
                   'PenImc_cor3.dll': 'src/PenImc/dll/PenImc.vcxproj',
                   'DirectWriteForwarder.dll': 'src/DirectWriteForwarder/DirectWriteForwarder.vcxproj',
                   'PresentationNative_cor3.dll': 'redist/PresentationNative/PresentationNative.vcxproj',
                   'vcruntime140_cor3.dll': 'redist/VCRuntime/VCRuntime.vcxproj'}
        if name in mapping:
            path = src/mapping[name]
            require(path.is_file(), f'项目缺失: {path}')
            return path
        found = [p for ext in ('.csproj', '.vcxproj') for p in (src/'src').rglob(Path(name).stem+ext)]
        require(len(found) == 1, f'找不到唯一项目: {name}: {found}')
        return found[0]

    def run(self, command, label, cwd=None):
        for child in ('temp', 'appdata', 'empty-feed', 'logs'):
            (self.out/child).mkdir(parents=True, exist_ok=True)
        log = self.out/f'logs/{label}.log'
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run([str(x) for x in command], cwd=cwd or self.source, env=self.env,
                                    stdout=stream, stderr=subprocess.STDOUT)
        lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
        record = dict(command=[str(x) for x in command], exitCode=result.returncode,
                      warnings=[s for s in lines if re.search(r'\bwarning\b.*\b[A-Z]+\d{4}\b|: warning ', s)],
                      errors=[s for s in lines if ': error ' in s])
        write(self.out/f'logs/{label}.json', record)
        print(f'{label}: exit={result.returncode}; warnings={len(record["warnings"])}; errors={len(record["errors"])}', flush=True)
        require(result.returncode == 0, f'{label} 失败，见 {log}\n'+'\n'.join(lines[-12:]))
        return record

    def inspect(self, paths=(), root=None, signed=False, label='inspect'):
        request = dict(paths=[str(p) for p in paths], root=str(root) if root else None, requireSigned=signed,
                       signerThumbprint=self.c['signerThumbprint'], timestampThumbprint=self.c['timestampThumbprint'])
        path = self.out/f'logs/{label}-request.json'
        write(path, request)
        receipt = self.out/f'logs/{label}-receipt.json'
        self.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File', HERE/'tools/Inspect-ProducerPayload.ps1',
                  '-Request', path, '-Receipt', receipt], label)
        return read(receipt)

    def build(self):
        require(not (self.out/'build').exists(), 'build 目录必须全新；请指定新的 outputRoot')
        write(self.out/'preflight.json', self.preflight())
        for rid, arch in RIDS.items():
            selected = [p for p in self.rebuild if p.startswith(f'runtimes/{rid}/')]
            if not selected:
                continue
            root = self.out/f'build/{arch.lower()}'
            root.mkdir(parents=True)
            # Generated C++/CLI helper projects inherit these wrappers, never a tree-local obj/junction.
            for ext in ('props', 'targets'):
                (root/f'Directory.Build.{ext}').write_text(f'<Project><Import Project="{escape(str(self.source))}/Directory.Build.{ext}" /></Project>', encoding='utf-8')
            (root/'native-adapter.targets').write_text('''<Project><ItemGroup Condition="'$(MSBuildProjectName)' == 'sw' and !Exists('$(WpfGraphicsPath)core/sw/bilinearspan/bilinearspan.vcxproj')"><ClCompile Include="$(WpfGraphicsPath)core/sw/swlib/bilinearspan.cpp" /></ItemGroup></Project>''', encoding='utf-8')
            if self.c['nativePdbAltPaths'].get(rid):
                path = root/'native-adapter.targets'
                path.write_text(path.read_text().replace('</Project>', '<ItemDefinitionGroup><Link><AdditionalOptions>/PDBALTPATH:&quot;'+escape(self.c['nativePdbAltPaths'][rid])+'&quot; %(AdditionalOptions)</AdditionalOptions></Link></ItemDefinitionGroup></Project>'), encoding='utf-8')
            (root/'private-version.targets').write_text('''<Project><Target Name="WpfD3D11RestorePrivateVersion" AfterTargets="_InitializeAssemblyVersion"><Error Condition="'$(WpfD3D11PrivateFileVersion)' == '' or '$(AssemblyVersion)' != '10.0.0.0'" Text="Explicit private versions required." /><CreateProperty Value="$(WpfD3D11PrivateFileVersion)"><Output TaskParameter="Value" PropertyName="FileVersion" /></CreateProperty></Target></Project>''', encoding='utf-8')
            common = ['-m:1', '-nodeReuse:false', '-nologo', '-v:m', '-restore', '-p:Configuration=Release', f'-p:Platform={arch}',
                      f'-p:VCToolsVersion={self.c["vcToolsVersion"]}', f'-p:VCToolsRedistVersion={self.c["vcToolsRedistVersion"]}',
                      f'-p:ForceImportAfterCppTargets={root}/native-adapter.targets',
                      f'-p:CustomAfterMicrosoftCommonTargets={root}/private-version.targets',
                      f'-p:ArtifactsDir={root}/artifacts/', f'-p:ArtifactsObjDir={root}/obj/', f'-p:RepoRoot={self.source}/',
                      f'-p:PerlCommand="{self.c["perlCommand"]}"', '-p:WpfD3D11Facade=true',
                      f'-p:RoslynTargetsPath={self.c["nugetPackagesDirectory"]}/microsoft.net.sdk.compilers.toolset/{self.c["compilerToolsetVersion"]}/',
                      '-p:AssemblyVersion=10.0.0.0', '-p:AutoGenerateAssemblyVersion=false',
                      f'-p:FileVersion={self.file_version}', f'-p:WpfD3D11PrivateFileVersion={self.file_version}',
                      f'-p:InformationalVersion={self.info}', f'-p:Version={self.info}',
                      '-p:IncludeSourceRevisionInInformationalVersion=false',
                      f'-p:RestoreSources={self.c["restoreSource"] or self.out/"empty-feed"}', '-p:NuGetAudit=false',
                      '-p:UseSharedCompilation=false', '-p:RestoreIgnoreFailedSources=false', '-p:DotNetBuildWithOnlineFeeds=false',
                      '-p:RunNetFrameworkApiCompat=false', '-p:RunRefApiCompat=false', '-p:RunApiCompat=false']
            names = [Path(p).name for p in selected]
            if self.c['mode'] == 'full':
                names += ['PresentationNative_cor3.dll', 'vcruntime140_cor3.dll']
            projects = list(dict.fromkeys(self.project(n) for n in names))
            if any(p.suffix == '.csproj' for p in projects):
                self.run([self.c['msbuild'], self.project('PresentationBuildTasks.dll'), *common,
                          '-p:Platform=AnyCPU', '-p:TargetFramework=net472', '-t:Restore'], f'pbt-{arch.lower()}')
            items = ''.join(f'<RuntimeProject Include="{escape(str(p))}" />' for p in projects)
            graph = root/'runtime-graph.proj'
            graph.write_text('<Project DefaultTargets="Build"><ItemGroup>'+items+'</ItemGroup>'
                             '<Target Name="Restore"><MSBuild Projects="@(RuntimeProject)" Targets="Restore" BuildInParallel="false" /></Target>'
                             '<Target Name="Build"><MSBuild Projects="@(RuntimeProject)" Targets="Build" BuildInParallel="false" /></Target></Project>', encoding='utf-8')
            self.run([self.c['msbuild'], graph, *common, f'-bl:{root}/build.binlog'], f'build-{arch.lower()}')
            for name in names:
                candidates = [p for p in (root/'artifacts/bin').rglob(name) if not any(x in ('ref', 'refint', 'publish') or x.endswith(('-ref', '-cycle')) for x in p.parts)]
                require(candidates and len({sha(p) for p in candidates}) == 1, f'输出缺失或歧义: {name}')
                relative = next(p for p in self.runtime if p.startswith(f'runtimes/{rid}/') and Path(p).name == name)
                target = safe_path(self.out/'rebuilt', relative)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(candidates[0], target)
                if candidates[0].with_suffix('.pdb').is_file():
                    symbols = self.out/f'symbols/{rid}/{Path(name).stem}.pdb'
                    symbols.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(candidates[0].with_suffix('.pdb'), symbols)
            managed = [p for p in selected if Path(p).name not in NATIVE and Path(p).name != 'DirectWriteForwarder.dll']
            if managed:
                references = root/'references'
                references.mkdir()
                for p in Path(self.c['referenceDirectories'][rid]).glob('*.dll'):
                    shutil.copy2(p, references/p.name)
                for p in (self.out/f'rebuilt/runtimes/{rid}/lib/net10.0').glob('*.dll'):
                    shutil.copy2(p, references/p.name)
                for relative in managed:
                    target = self.out/'rebuilt'/relative
                    il = target.with_suffix('.il')
                    target.rename(il)
                    self.run([self.c['crossgen2'], il, '-o', target, '-r', str(references/'*.dll'),
                              '--targetos', 'windows', '--targetarch', arch.lower(), '-O'], f'r2r-{arch.lower()}-{target.stem}')
                write(root/'reference-inputs.json', {p.name: sha(p) for p in references.glob('*.dll')})
        files = [p for p in (self.out/'rebuilt').rglob('*.dll')]
        facts = self.inspect(files, label='built-facts')
        toolchain = {key: dict(sha256=sha(self.c[key])) for key in ('msbuild', 'perlCommand')}
        toolchain['compilerToolset'] = dict(version=self.c['compilerToolsetVersion'], sha256=sha(Path(self.c['nugetPackagesDirectory'])/'microsoft.net.sdk.compilers.toolset'/self.c['compilerToolsetVersion']/'csc.exe'))
        write(self.out/'build-receipt.json', dict(provenance=self.provenance, packageVersion=self.version,
                                                 rebuildFiles=self.rebuild, files=facts['files'], toolchain=toolchain,
                                                 environmentOverrides={k: self.env[k] for k in ('CL','NUGET_PACKAGES','APPDATA','TEMP','DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE')}))

    def prepare(self, signed_root=None):
        require(not self.stage.exists(), 'stage 必须全新；签后打包请使用独立 outputRoot 和 inputRoot')
        input_root = absolute(self.c.get('inputRoot', str(self.out)), 'inputRoot', True)
        build = read(input_root/'build-receipt.json')
        require(build['packageVersion'] == self.version and build['rebuildFiles'] == self.rebuild, '构建回执版本/文件集不一致')
        for key in ('forkCommit', 'forkRootTree', 'runtimeSrcTree'):
            require(build['provenance'][key] == self.provenance[key], f'构建来源发生变化: {key}')
        if self.c['mode'] == 'full':
            comparison = read(absolute(self.c.get('equivalenceReport', ''), 'equivalenceReport', True))
            require(comparison.get('unexpectedDifferences') is False, '全量等价报告没有明确通过')
            require({f['pathInPackage'] for f in comparison['files']} == set(self.rebuild), '全量等价报告必须覆盖每个新构建文件')
            for entry in comparison['files']:
                require(sha(input_root/'rebuilt'/entry['pathInPackage']) == entry['newSha256'], '全量等价报告未锁定实际新构建文件')
        self.stage.mkdir(parents=True)
        if self.c['mode'] == 'incremental':
            with zipfile.ZipFile(self.c['basePackage']) as z:
                for name in z.namelist():
                    if name.startswith(('runtimes/', 'buildTransitive/', 'tools/', 'licenses/')) or name in ('README.md', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt'):
                        target = safe_path(self.stage, name)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(z.read(name))
                prior_manifests = {rid: read_manifest(z, rid) for rid in RIDS}
                prior_manifest_hashes = {rid: digest(z.read(f'manifests/{rid}.json')) for rid in RIDS}
                meta = nuspec_metadata(z)
            base = dict(package=Path(self.c['basePackage']).name, sha256=sha(self.c['basePackage']), version=meta['version'])
            meta['releaseNotes'] = (f'Rebuilt {", ".join(self.rebuild)} from fork {self.provenance["forkCommit"]}. '
                                    f'Other runtime bytes preserved from {base["package"]} (SHA256 {base["sha256"]}). '
                                    'Baseline release notes: '+meta['releaseNotes'])
            baseline_readme = (self.stage/'README.md').read_text(encoding='utf-8-sig')
            prefix = (f'# 本次生产来源 / Production provenance\n\nFork `{self.provenance["forkCommit"]}`; '
                      f'runtime src tree `{self.provenance["runtimeSrcTree"]}`.\n\n'
                      f'重建 / Rebuilt: {", ".join(self.rebuild)}. '
                      f'其余运行时字节沿用 / Other runtime bytes preserved from `{base["package"]}` '
                      f'(SHA256 `{base["sha256"]}`). 逐文件真实版本、签名与原来源见 manifests。\n\n'
                      '以下功能说明沿用基准包文档；其中旧提交标识仅描述基准包。 '
                      'The following feature documentation is inherited from the baseline; its older commit identifiers describe that baseline.\n\n')
            if self.c.get('packageReadme'):
                # 新版本发布应提供人工撰写的包 README；未提供时才自动前插来源说明。
                shutil.copy2(self.c['packageReadme'], self.stage/'README.md')
            else:
                (self.stage/'README.md').write_text(prefix+baseline_readme, encoding='utf-8')
        else:
            for name in ('LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'licenses', 'buildTransitive', 'tools/Payload.Common.ps1', 'tools/Test-WpfD3D11Runtime.ps1'):
                src, dst = HERE/name, self.stage/name
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(src, dst) if src.is_dir() else shutil.copy2(src, dst)
            require(self.c.get('packageReadme') and self.c.get('metadata'), '全量模式需要显式 packageReadme 和 metadata')
            shutil.copy2(self.c['packageReadme'], self.stage/'README.md')
            meta, base, prior_manifests = {}, None, {}
        meta.update(self.c.get('metadata', {}))
        required = ('authors', 'description', 'projectUrl', 'tags', 'releaseNotes')
        require(all(meta.get(k) for k in required), '公开包元数据缺失')
        all_built = [p for p in self.runtime if self.c['mode'] == 'full' or p in self.rebuild]
        for relative in all_built:
            src = (Path(signed_root)/relative if signed_root and relative in self.rebuild else input_root/'rebuilt'/relative)
            if Path(relative).name == 'D3DCompiler_47_cor3.dll':
                src = Path(self.c['x64Compiler'])
            require(src.is_file(), f'产物缺失: {src}')
            if relative in self.rebuild:
                built = next(f['facts'] for f in build['files'] if Path(f['path']).as_posix().endswith('/'+relative))
                if not signed_root:
                    require(sha(src) == built['sha256'], f'构建输出哈希不匹配: {relative}')
                else:
                    require(unsigned_pe(src) == unsigned_pe(input_root/'rebuilt'/relative), f'签后更改了 PE 内容: {relative}')
            elif self.c['mode'] == 'full' and Path(relative).name != 'D3DCompiler_47_cor3.dll':
                built = next(f['facts'] for f in build['files'] if Path(f['path']).as_posix().endswith('/'+relative))
                require(sha(src) == built['sha256'], f'全量上游原件与构建回执不一致: {relative}')
            dst = self.stage/relative
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        actual = self.inspect([self.stage/p for p in self.runtime], label='prepare-facts')['files']
        actual = {Path(f['path']).relative_to(self.stage).as_posix(): f['facts'] for f in actual}
        for rid in RIDS:
            previous = prior_manifests.get(rid)
            m = copy.deepcopy(previous) if previous else dict(schema=2, packageId='WpfD3D11Runtime', rid=rid,
                upstreamBaseline=self.baseline, managedFileVersion=self.file_version, informationalVersion=self.info,
                assemblyVersion='10.0.0.0', files=[])
            # Historical full-build evidence belongs inside preservedOriginal, not as this build's source.
            for key in ('sourceSliceCommit', 'sourceWpfTree', 'forkTreeMapping', 'sourceManifest', 'sourceManifestSha256',
                        'vmrCommit', 'upstreamCommit', 'equivalence', 'referenceInputsSha256', 'r2rScriptSha256', 'overlayCommonSha256'):
                m.pop(key, None)
            m.update(packageVersion=self.version, forkCommit=self.provenance['forkCommit'], forkTree=self.provenance['forkRootTree'],
                     forkRootTree=self.provenance['forkRootTree'], runtimeSrcTree=self.provenance['runtimeSrcTree'],
                     publicRepositoryCommit=self.provenance['forkCommit'], productionSource=self.provenance,
                     baselineProvenance=self.c['baselineProvenance'], payloadBasePackage=base,
                     privateReleaseSequence=self.sequence, requireSigned=self.signed,
                     localValidationOnly=not self.signed, signingMode='execute' if self.signed else 'pending-rebuilt',
                     nativeFileVersions={Path(p).name: self.file_version for p in self.rebuild if p.startswith(f'runtimes/{rid}/') and Path(p).name in NATIVE},
                     releaseGate=dict(passed=False, reason='Pending measured gates'))
            records = []
            for relative in (p for p in self.runtime if p.startswith(f'runtimes/{rid}/')):
                prior = next((f for f in previous['files'] if f['pathInPackage'] == relative), None) if previous else None
                facts = actual[relative]
                rebuilt = relative in self.rebuild
                f = copy.deepcopy(prior) if prior else dict(name=Path(relative).name, pathInPackage=relative,
                    category='native' if Path(relative).name in NATIVE else 'runtime', origin='upstream' if Path(relative).name in UPSTREAM else 'fork')
                if rebuilt:
                    require(facts['fileVersion'] == self.file_version and facts['informationalVersion'] == self.info, f'新版本不匹配: {relative}')
                    f.update(facts)
                    f.update(source=f'fork@{self.provenance["forkCommit"]}/{self.project(f["name"]).relative_to(self.source).as_posix()}',
                             sourceProvenance=f'fork {self.provenance["forkCommit"]}; root {self.provenance["forkRootTree"]}; src {self.provenance["runtimeSrcTree"]}; fresh Release build',
                             production=dict(action='rebuilt', forkCommit=self.provenance['forkCommit'], forkRootTree=self.provenance['forkRootTree'], runtimeSrcTree=self.provenance['runtimeSrcTree'], buildProducer=build['provenance']),
                             r2rEvidence=dict(action='crossgen2' if facts['readyToRun'] else 'native or mixed build; R2R not applicable',
                                 crossgen2Sha256=sha(self.c['crossgen2']) if facts['readyToRun'] else None))
                    f.pop('preservedFromPackage', None)
                elif prior:
                    require(sha(self.stage/relative) == prior['sha256'], f'沿用文件改变: {relative}')
                    f.update(source=f'{base["package"]}!/{relative}', preservedFromPackage=base['version'],
                             production=dict(action='preserved', basePackage=base, originalRecord=prior,
                                             originalManifestSha256=prior_manifest_hashes[rid]))
                    f['signing']['sha256'] = facts['sha256']
                else:
                    f.update(facts)
                    f.update(source='upstream WPF redist' if Path(relative).name != 'D3DCompiler_47_cor3.dll' else 'Microsoft.WindowsDesktop.App.Runtime.win-x64/10.0.11',
                             sourceProvenance='Unmodified upstream binary; see licenses/', production=dict(action='upstream-copy'))
                if rebuilt or not prior:
                    f['signing'] = dict(action='signed' if facts['signatureStatus'] == 'Valid' and rebuilt else ('unsigned/pending' if rebuilt else 'preserve'),
                        path=relative, sha256=facts['sha256'], authenticodeStatus=facts['signatureStatus'],
                        signerThumbprint=facts['signerThumbprint'], timestampThumbprint=facts['timestampThumbprint'])
                    f['signatureStatus'] = 'signed' if facts['signatureStatus'] == 'Valid' and rebuilt else facts['signatureStatus']
                records.append(f)
            m['files'] = records
            write(self.stage/f'manifests/{rid}.json', m)
            assets = self.stage/f'buildTransitive/assets/{rid}.props'
            assets.parent.mkdir(parents=True, exist_ok=True)
            if previous:
                assets.write_bytes(assets.read_bytes().replace(previous['packageVersion'].encode(), self.version.encode()))
            else:
                items = []
                for f in records:
                    kind = '_WpfD3D11Runtime' if f['category'] == 'runtime' else '_WpfD3D11Native'
                    items.append(f'<{kind} Include="$(WpfD3D11RuntimeRoot){f["pathInPackage"]}" AssetType="{f["category"]}" NuGetPackageId="WpfD3D11Runtime" NuGetPackageVersion="{self.version}" PathInPackage="{f["pathInPackage"]}" DestinationSubPath="{f["name"]}" TargetPath="{f["name"]}" CopyLocal="true" CopyToOutputDirectory="PreserveNewest" CopyToPublishDirectory="PreserveNewest" />')
                assets.write_text('<Project><ItemGroup>'+''.join(items)+'</ItemGroup></Project>', encoding='utf-8')
        self.inspect(root=self.stage, signed=self.signed, label='prepare-gates')
        for rid in RIDS:
            m = read(self.stage/f'manifests/{rid}.json')
            m['releaseGate'] = dict(passed=self.signed, reason='Measured RequireSigned and publisher/timestamp policy passed' if self.signed else 'Unsigned rehearsal only')
            write(self.stage/f'manifests/{rid}.json', m)
        write(self.out/'preparation.json', dict(metadata=meta, provenance=self.provenance,
                                              stageFiles=sorted(p.relative_to(self.stage).as_posix() for p in self.stage.rglob('*') if p.is_file())))

    def pack(self):
        self.inspect(root=self.stage, signed=self.signed, label='pack-gates')
        preparation = read(self.out/'preparation.json')
        meta = preparation['metadata']
        properties = dict(TargetFramework='net10.0', PackageId='WpfD3D11Runtime', Version=self.version,
            Authors=meta['authors'], Description=meta['description'], PackageProjectUrl=meta['projectUrl'],
            PackageTags=meta['tags'], PackageReleaseNotes=meta['releaseNotes'], IncludeBuildOutput='false',
            EnableDefaultItems='false', PackageReadmeFile='README.md', PackageLicenseFile='LICENSE.txt',
            NoWarn='NU5128;NU5111', NuGetAudit='false', PackageOutputPath=str(self.out/'publish'),
            RepositoryCommit=self.provenance['forkCommit'], RepositoryBranch=git(self.source, 'branch', '--show-current'),
            RepositoryType='git', RepositoryUrl='https://github.com/QuickerOrg/wpf')
        project = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'+''.join(f'<{k}>{escape(v)}</{k}>' for k, v in properties.items())+'</PropertyGroup><ItemGroup>'
        project += ''.join(f'<None Include="{p}" Pack="true" PackagePath="{p}" />' for p in preparation['stageFiles'])+'</ItemGroup></Project>'
        proj = self.stage/'WpfD3D11Runtime.csproj'
        proj.write_text(project, encoding='utf-8')
        config = self.out/'pack.nuget.config'
        config.write_text('<configuration><packageSources><clear/></packageSources><fallbackPackageFolders><clear/></fallbackPackageFolders></configuration>', encoding='utf-8')
        isolation = ['-p:ImportDirectoryBuildProps=false', '-p:ImportDirectoryBuildTargets=false', '-p:ImportDirectoryPackagesProps=false', '-p:NuGetAudit=false']
        self.run(['dotnet', 'restore', proj, '--configfile', config, '--artifacts-path', self.out/'pack-artifacts', *isolation], 'pack-restore')
        self.run(['dotnet', 'pack', proj, '--no-build', '--no-restore', '--artifacts-path', self.out/'pack-artifacts', *isolation], 'pack')
        self.check()

    def check(self):
        preparation = read(self.out/'preparation.json')
        expanded = self.out/(('check-signed-' if self.signed else 'check-unsigned-')+uuid.uuid4().hex[:12])
        require(not expanded.exists(), f'检查目录必须全新: {expanded}')
        runtime = []
        with zipfile.ZipFile(self.package) as z:
            names = z.namelist()
            require(z.testzip() is None and len(names) == len(set(names)), 'ZIP CRC/重复条目错误')
            require('.signature.p7s' not in names and self.package.stat().st_size < 250*1024**2, '包作者签名/体积不符合策略')
            expected = set(preparation['stageFiles'])
            fixed_inventory = set(self.runtime) | SUPPORT | {f'manifests/{rid}.json' for rid in RIDS} | {f'buildTransitive/assets/{rid}.props' for rid in RIDS}
            require(expected == fixed_inventory, 'Staging 文件集不等于锁定的运行时/许可/消费者清单')
            generated = set(names)-expected
            require(len(generated) == 4 and {'[Content_Types].xml', '_rels/.rels', 'WpfD3D11Runtime.nuspec'} <= generated and
                    sum(n.startswith('package/services/metadata/core-properties/') for n in generated) == 1, 'NuGet 文件集错误')
            require(expected <= set(names), '包内缺少 staging 文件')
            for name in expected:
                require(z.read(name) == (self.stage/name).read_bytes(), f'pack 更改字节: {name}')
            if self.c['mode'] == 'incremental':
                with zipfile.ZipFile(self.c['basePackage']) as old:
                    canonical = lambda ns: {'package/services/metadata/core-properties/_.psmdcp' if n.startswith('package/services/metadata/core-properties/') else n for n in ns}
                    require(canonical(names) == canonical(old.namelist()), '与基准包文件集不一致')
                    allowed = set(self.rebuild) | {'README.md'} | {f'manifests/{rid}.json' for rid in RIDS} | {f'buildTransitive/assets/{rid}.props' for rid in RIDS} | generated
                    for name in names:
                        if name not in allowed:
                            require(z.read(name) == old.read(name), f'非重建文件改变: {name}')
                    for path in self.runtime:
                        equal = z.read(path) == old.read(path)
                        require(equal or path in self.rebuild, f'沿用运行时不一致: {path}')
                        runtime.append(dict(path=path, matchesBase=equal, rebuilt=path in self.rebuild, sha256=digest(z.read(path))))
            fields = nuspec_metadata(z)
            require(fields['id'] == 'WpfD3D11Runtime' and fields['version'] == self.version, 'nuspec id/version 错误')
            repo = ET.fromstring(z.read('WpfD3D11Runtime.nuspec')).find('.//{*}repository').attrib
            require(repo['commit'] == self.provenance['forkCommit'] and repo['url'] == 'https://github.com/QuickerOrg/wpf' and
                    repo['branch'] == git(self.source, 'branch', '--show-current') and repo['type'] == 'git', 'nuspec 来源错误')
            for key in ('authors', 'description', 'projectUrl', 'tags', 'releaseNotes', 'readme', 'license'):
                require(fields.get(key), f'缺少元数据: {key}')
            for key in ('authors', 'description', 'projectUrl', 'tags', 'releaseNotes'):
                require(fields[key] == preparation['metadata'][key], f'nuspec 元数据不匹配: {key}')
            xml = ET.fromstring(z.read('WpfD3D11Runtime.nuspec'))
            require(xml.find('.//{*}license').attrib['type'] == 'file' and fields['license'] == 'LICENSE.txt' and fields['readme'] == 'README.md', '许可/README 元数据错误')
            require(not xml.findall('.//{*}dependency'), 'runtime-only 包不可新增包依赖')
            text = z.read('README.md').decode('utf-8-sig')
            require(len(text.encode()) < 1024**2 and text.count('```') % 2 == 0 and not re.search(r'<script|<iframe|!\[', text, re.I), 'README 格式不符合策略')
            require(not re.search(r'\b[A-Za-z]:[/\\]|/Users/|\\Users\\', text), 'README 含本机路径')
            for rid in RIDS:
                m = read_manifest(z, rid)
                require(m['productionSource'] == self.provenance and m['forkCommit'] == self.provenance['forkCommit'], 'manifest 来源错误')
                for f in m['files']:
                    relative = f['pathInPackage']
                    require(f['sha256'] == digest(z.read(relative)), 'manifest 文件哈希错误')
                    require(f['production']['action'] == ('rebuilt' if relative in self.rebuild else ('preserved' if self.c['mode'] == 'incremental' else 'upstream-copy')), '逐文件来源分类错误')
                    if relative in self.rebuild:
                        require(f['production']['forkCommit'] == self.provenance['forkCommit'] and f['production']['runtimeSrcTree'] == self.provenance['runtimeSrcTree'], '新构建文件来源错误')
                    elif self.c['mode'] == 'incremental':
                        with zipfile.ZipFile(self.c['basePackage']) as baseline:
                            prior = next(p for p in read_manifest(baseline, rid)['files'] if p['pathInPackage'] == relative)
                            require(f['production']['originalRecord'] == prior and
                                    f['production']['originalManifestSha256'] == digest(baseline.read(f'manifests/{rid}.json')),
                                    f'沿用原来源链错误: {relative}')
                require(not re.search(rb'\b[A-Za-z]:[/\\]|/Users/|\\Users\\', z.read(f'manifests/{rid}.json')), 'manifest 含本机路径')
            for name in names:
                target = safe_path(expanded, name)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(name))
        self.inspect(root=expanded, signed=self.signed, label='check-signatures' if self.signed else 'check-content')
        result = dict(contentGatesPassed=True, requireSignedPassed=self.signed, releaseReady=self.signed and not self.provenance['producerDirty'],
                      packageSha256=sha(self.package), bytes=self.package.stat().st_size, zipEntries=len(names),
                      runtimeFiles=len(self.runtime), rebuiltFiles=len(self.rebuild), runtime=runtime, provenance=self.provenance)
        write(self.out/('package-validation-signed.json' if self.signed else 'package-validation.json'), result)
        (self.out/'publish/SHA256SUMS.txt').write_text(f'{sha(self.package)}  {self.package.name}\n', encoding='ascii')

    def export_bundle(self):
        self.inspect(root=self.stage, signed=False, label='bundle-gates')
        declared_rebuilt = {f['pathInPackage'] for rid in RIDS for f in read(self.stage/f'manifests/{rid}.json')['files']
                            if f['production']['action'] == 'rebuilt'}
        require(declared_rebuilt == set(self.rebuild), 'bundle 重建清单与已锁定 manifest 不一致')
        output = self.out/'signing-bundle.zip'
        require(not output.exists(), '签名 bundle 已存在，不覆盖')
        inputs = {p: self.stage/p for p in read(self.out/'preparation.json')['stageFiles']}
        for p in ('Sign-WpfD3D11Runtime.ps1', 'tools/Payload.Common.ps1', 'tools/RunWithMutex.ps1', 'tools/SigningBundle.Common.ps1'):
            inputs['signing/'+p] = HERE/p
        files = {p: sha(source) for p, source in inputs.items()}
        manifest = dict(schema=2, kind='unsigned-signing-bundle', packageVersion=self.version,
                        provenance=self.provenance, files=files, signFiles=self.rebuild,
                        preserveFiles=[p for p in self.runtime if p not in self.rebuild])
        with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as z:
            for p, source in inputs.items():
                z.write(source, p)
            z.writestr('bundle-manifest.json', json.dumps(manifest, indent=2))
        write(self.out/'bundle-receipt.json', dict(sha256=sha(output), manifest=manifest))
        with zipfile.ZipFile(output) as z:
            require(z.testzip() is None and set(z.namelist()) == set(files) | {'bundle-manifest.json'}, 'bundle ZIP 文件集/CRC 错误')
            for relative, expected in files.items():
                require(digest(z.read(relative)) == expected, f'bundle 文件哈希错误: {relative}')


def read_manifest(z, rid):
    return json.loads(z.read(f'manifests/{rid}.json'))


def nuspec_metadata(z):
    m = ET.fromstring(z.read('WpfD3D11Runtime.nuspec')).find('{*}metadata')
    return {item.tag.split('}')[-1]: item.text for item in m}


def unsigned_pe(path):
    # Authenticode may change only checksum, Security Directory and appended certificate.
    import struct
    b = bytearray(Path(path).read_bytes())
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    opt = pe+24
    dirs = opt+(112 if struct.unpack_from('<H', b, opt)[0] == 0x20b else 96)
    cert, size = struct.unpack_from('<II', b, dirs+32)
    if cert:
        require(cert+size == len(b), '证书不在文件末尾')
        b = b[:cert]
    b[opt+64:opt+68] = b'\0'*4
    b[dirs+32:dirs+40] = b'\0'*8
    # Signing aligns certificates to 8 bytes; compare the PE image up to the last section only.
    count = struct.unpack_from('<H', b, pe+6)[0]
    sections = opt+struct.unpack_from('<H', b, pe+20)[0]
    end = max(sum(struct.unpack_from('<II', b, sections+40*i+16)) for i in range(count))
    require(not any(b[end:]), 'PE 有未知尾部数据')
    return bytes(b[:end])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True)
    p.add_argument('--phase', choices=['Validate', 'Build', 'Prepare', 'ExportSigningBundle', 'Pack', 'Check'], default='Validate')
    p.add_argument('--require-signed', choices=['true', 'false'], default='true')
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--signed-payload-root')
    a = p.parse_args()
    producer = Producer(a.config, a.require_signed == 'true')
    plan = producer.preflight()
    if a.dry_run or a.phase == 'Validate':
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return
    if a.phase == 'Build':
        producer.build()
    elif a.phase == 'Prepare':
        producer.prepare(a.signed_payload_root)
    elif a.phase == 'ExportSigningBundle':
        producer.export_bundle()
    elif a.phase == 'Pack':
        producer.pack()
    elif a.phase == 'Check':
        producer.check()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'生产者拒绝: {error}', file=sys.stderr)
        sys.exit(1)
