# WpfD3D11Runtime 生产者

本目录是构建、签名交接和打包的唯一生产者。日常开发使用 QuickerOrg/wpf 的 `quicker/d3d11-facade` 与任务工作树；不再从 VMR/slice 开发，不再调用 Quicker 仓库里的 PKG/PUB1、PUB3、P3 包装或 `sync-wpf-fork.ps1`。消费者包仍为 runtime-only，双 RID 固定为 `win-x64`、`win-arm64`，保留 34/33 个运行时文件、原有 buildTransitive 和消费校验器。

统一入口：[`Invoke-WpfD3D11RuntimeProducer.ps1`](Invoke-WpfD3D11RuntimeProducer.ps1)。`Build-*`、`Complete-*`、`Export-*`、`Pack-*`、`Invoke-*Release` 只是此入口的阶段别名，旧 VMR 参数已退役。生产逻辑在 [`tools/producer.py`](tools/producer.py)，不依赖 Quicker 工作区。签名工具 [`Sign-WpfD3D11Runtime.ps1`](Sign-WpfD3D11Runtime.ps1) 只供签名操作员显式调用，不构建、不打包、不发布；默认只列出计划。

## 来源与版本不变量

- 新构建主来源为 fork `HEAD`、`HEAD^{tree}`、`HEAD:src`，分别记录为 `forkCommit`、`forkRootTree`、`runtimeSrcTree`。项目路径是 `src/...`。
- `baselineProvenance` 是可省略或设为 `null` 的独立历史说明；默认是 VMR `v10.0.114`、`0427b9abbead3fa64c6655eb4290c537397131fa`、WPF 子树 `664f60538e7fbbc978dcd8984f3e9a023906e5de`。不会为填此字段访问 VMR，也不将其冒充 fork commit。
- 构建前拒绝 producer 目录以外的工作区变化；本地 producer 修改记录 `producerDirty` 和逐文件 SHA。只有干净提交来源才可能成为可发布候选，未提交演练的 `releaseReady` 始终为 false。构建与打包阶段分别保留 producer 哈希；修改打包逻辑不伪造已完成构建的 producer 身份。
- 包版本为 `10.0.P-d3d11.N`，须与 `eng/Versions.props` 基线相同；新文件 FileVersion 为 `10.1.floor(N/65536).(N%65536)`，AssemblyVersion 为 `10.0.0.0`，新 informational version 带真实 fork commit。沿用文件绝不重标版本或重签。
- 增量清单使用精确包路径。每项 `production.action` 为 `rebuilt` 或 `preserved`；沿用项包含基准包名、SHA、原 manifest 原始字节 SHA 和完整 `originalRecord`，旧来源链不会被新 fork 来源覆盖。上游原件在全量模式标为 `upstream-copy`。
- 输出不得放在源码树内。外置 `Directory.Build.props/targets` 仅 Import fork 原文件，并显式设置 `RepoRoot`、`ArtifactsDir`、`ArtifactsObjDir`；不创建 junction，不写树内 obj/bin。

## 工具与配置

需要 Python 3.11+（生产者只用标准库）、Windows PowerShell 5.1、已安装的 .NET 10 SDK、VS 18 MSBuild/C++/CLI、MSVC 14.44.35207、redist 14.44.35112 和 Windows SDK。所有工具路径、版本和缓存均可在 JSON 覆盖；缺失时 Validate 给出具体字段和路径，不安装/更新工具。

| 配置项 | 默认值 / 约束 |
| --- | --- |
| `sourceRoot` | 当前入口所在的 fork 工作树根 |
| `outputRoot` | `G:/WpfD3D11Runtime/_build/runtime`；每次任务使用新目录，必须在 Git 工作树外 |
| `mode` | `full` 或 `incremental`；默认 full |
| `packageVersion` | 必填，无默认 |
| `basePackage` / `basePackageSha256` | 增量模式必填，绝对路径和显式 SHA256 锁 |
| `rebuildFiles` | 增量模式必填，精确包内路径数组；全量模式自动覆盖全部 fork 文件 |
| `msbuild` | VS 18 Community 的 `MSBuild/Current/Bin/MSBuild.exe` |
| `vcToolsVersion` / `vcToolsRedistVersion` | `14.44.35207` / `14.44.35112` |
| `windowsSdkBin` | Windows Kits 10 `bin/10.0.26100.0/x64`；必须包含 rc.exe，仅加入子进程 PATH |
| `nugetPackagesDirectory` | `G:/WpfD3D11Runtime/tools/packages`，绝对缓存路径 |
| `compilerToolsetVersion` | `10.0.112`；缓存内需有 `microsoft.net.sdk.compilers.toolset/<版本>/csc.exe`，显式传 RoslynTargetsPath |
| `perlCommand` | `G:/WpfD3D11Runtime/tools/strawberry-perl/5.38.0.1/portableshell.bat`；不从 slice 推导 |
| `restoreSource` | 默认空，用空本地 feed + 已有缓存；仅允许显式设为 `https://api.nuget.org/v3/index.json` |
| `crossgen2` | `G:/WpfD3D11Runtime/tools/crossgen2/10.0.14/crossgen2.exe`，托管重建/全量模式必需，保留同目录运行依赖 |
| `referenceDirectories` | 每 RID 的 `G:/WpfD3D11Runtime/tools/references/<rid>`；托管重建/全量必需，含 System.Private.CoreLib.dll 及完整同架构引用图 |
| `x64Compiler` | `G:/WpfD3D11Runtime/tools/D3DCompiler_47_cor3.dll`，全量必需，SHA 锁为 PKG0 的官方 10.0.11 原件 |
| `metadata` / `packageReadme` | 全量必填；metadata 至少含 authors、description、projectUrl、tags、releaseNotes。增量继承基准元数据并更新生产来源说明；发布新版本时应在配置中提供人工撰写的 `metadata.releaseNotes` 与 `packageReadme`，它们会覆盖自动生成的来源说明 |
| `equivalenceReport` | 全量 Prepare 必需；`unexpectedDifferences=false`，files 精确覆盖新文件，每项含 pathInPackage、newSha256；报告必须锁定实际产物 |
| `inputRoot` | 默认 outputRoot；签后 Prepare 指向未签构建目录，读取其 build-receipt 与 rebuilt |
| `signerThumbprint` / `timestampThumbprint` | 默认沿用现有发布者及 RFC3161 时间戳证书指纹；换证时必须显式修改并重新审查 |
| `nativeSourcePrefix` | 默认空；可显式开启 MSVC `/experimental:deterministic /pathmap`，只控制诊断路径，不承诺所有输出可重现 |
| `nativeTrimPrefix` / `nativePdbAltPaths` | 默认空；受控等价实验可裁诊断前缀、按 RID 指定 PDBALTPATH。与 nativeSourcePrefix 互斥；不改源码与 manifest 主来源 |

缓存由操作员事先准备；恢复缺包时仅允许 nuget.org。入口禁用子进程 dotnet 工作负载更新通知、遥测、首次体验与 NuGet audit，不运行 Arcade bootstrap、不写系统或全局环境。工具缓存可持久复用；任务 obj、bin、temp、HTTP cache 均在 outputRoot。Perl 可以从受信任的已验证工具包复制到公共工具缓存；不依赖旧源码检出。

增量配置示例（SHA 替换为实际基准包 SHA）：

```json
{
  "sourceRoot": "G:/WpfD3D11Runtime/worktrees/my-task",
  "outputRoot": "G:/WpfD3D11Runtime/_build/my-task/unsigned",
  "mode": "incremental",
  "packageVersion": "10.0.14-d3d11.5",
  "basePackage": "G:/WpfD3D11Runtime/packages/WpfD3D11Runtime.10.0.14-d3d11.4.nupkg",
  "basePackageSha256": "<64位SHA256>",
  "rebuildFiles": [
    "runtimes/win-x64/native/wpfgfx_cor3.dll",
    "runtimes/win-arm64/native/wpfgfx_cor3.dll"
  ]
}
```

全量模式改为 `mode=full`，不需要基准包或重建清单；提供双 RID references、crossgen2、x64Compiler、公开 packageReadme 与 metadata。Build 构建完整生产依赖图并为 28 个纯托管库/RID 执行 R2R；DirectWriteForwarder 和原生文件单独处理。Prepare 前审阅并提供哈希锁定的等价报告。上游二进制继续保留原字节与许可，不将其当 fork 自建。

## 日常任务流程

1. 从常驻克隆建任务工作树；只在任务树改文件。常驻目录不是输出目录，不使用旧双树同步。

```powershell
git -C G:/WpfD3D11Runtime/wpf worktree add -b quicker/my-task G:/WpfD3D11Runtime/worktrees/my-task quicker/d3d11-facade
$entry = 'G:/WpfD3D11Runtime/worktrees/my-task/eng/quicker/WpfD3D11Runtime/Invoke-WpfD3D11RuntimeProducer.ps1'
$config = 'G:/WpfD3D11Runtime/_build/my-task/unsigned.json'
& $entry -ConfigurationFile $config -Phase Build -WhatIf
& $entry -ConfigurationFile $config -Phase Build
```

`-WhatIf` 只解析配置、核对来源/工具/项目/基准，不创建输出或调用 MSBuild；所有阶段均支持同样的前置演练。Build 不签名、不打包。版本序号在重建前明确决定。

2. 检查双 RID 日志、PE/版本/R2R 与等价证据。Prepare 未签候选时须逐次显式传 `-RequireSigned:$false`；默认 true 不会自动降级。

```powershell
& $entry -ConfigurationFile $config -Phase Prepare -RequireSigned:$false
& $entry -ConfigurationFile $config -Phase ExportSigningBundle -RequireSigned:$false
```

签名 ZIP 包含已锁定文件、逐项哈希、`signFiles`/`preserveFiles` 和 signing 目录工具。记录 ZIP SHA，传到签名机，解压后用 `signing/tools/SigningBundle.Common.ps1` 的 `Test-WpfSigningBundle` 验证 SHA、精确文件集、无 reparse point、签名角色和原消费门禁。只把 signFiles 复制到新的签名输出目录，保留包内相对路径；不要原地改已锁定 bundle，更不要重签 preserveFiles。

签名操作员按 RID、native/lib 目录向 bundle 内 `Sign-WpfD3D11Runtime.ps1` 传 `-Directory`、精确 `-FileNames`、显式 SignTool/CertificateThumbprint/TimestampUrl，再明确 `-Execute`。它使用随包 mutex、sha256/RFC3161 和签后验证；不选择隐式证书、不创建或导入证书。未执行 Execute 时只列计划。此步骤需要单独签名授权，生产者本身不隐式调用签名器。

3. 签后建立新的配置：`outputRoot` 指向新 signed 目录，`inputRoot` 指向未签构建目录；其余来源、基准包、版本、重建清单保持一致。

```powershell
$signedConfig = 'G:/WpfD3D11Runtime/_build/my-task/signed.json'
& $entry -ConfigurationFile $signedConfig -Phase Prepare -SignedPayloadRoot 'G:/WpfD3D11Runtime/_build/my-task/signed-payload'
& $entry -ConfigurationFile $signedConfig -Phase Pack
& $entry -ConfigurationFile $signedConfig -Phase Check
```

Pack 已自动 Check；独立 Check 每次自动创建新的校验目录，不覆盖既有包与 staging。Prepare 的签后输入只能改变 PE checksum、Security Directory 与文件尾证书；所有代码/资源必须等于锁定未签构建。签后从实际 PE/签名重新生成记录，不手改 passed。默认 RequireSigned 验证每个 fork 文件的实际签名、发布者/时间戳与记录，并检查全部沿用/上游文件 Valid 与时间戳；未签新 DLL 必须拒绝。

Pack 使用隔离 SDK 项目、空包源，顺序为 restore → pack --no-build --no-restore → 解包校验。Check 覆盖 CRC、无重名/无额外资产、精确 67 文件、SHA、实际 PE 架构/AssemblyVersion/FileVersion/imports/R2R、manifest 来源分类、版本资产声明、nuspec 公开字段、README、原许可/消费者逻辑与增量沿用字节。保留原 `Test-WpfInput`，不修改消费者校验器或关闭 RequireSigned。没有 nupkg 作者签名；这不替代负载 Authenticode。

4. 在全新缓存、本地目录源消费精确版本，双 RID 执行 FDD build/publish，保留 Input/Build/Publish 三道原门禁和 deps/哈希结果；按改动面执行运行验证。不能把静态包检查当 GPU/ARM 运行验证。签前干跑可另外显式 Pack -RequireSigned:$false，但该包 `requireSignedPassed=false`、`releaseReady=false`，只能用于本地演练。

5. 审查并另行授权提交/推送任务改动，整合到 quicker/d3d11-facade 后，从最终干净提交重建发布候选。发布前复核 nupkg SHA、许可、签名、消费验证与版本；release 资产使用这个预制包。main 负责 `.github/workflows/publish-nuget.yml`，不是普通上游镜像，也不在云端重新编译运行时；以 release_tag、package_version、package_sha256 输入，经资产校验和 Trusted Publishing 发布。创建 release、上传资产、workflow dispatch、NuGet push 都不是本入口的隐式副作用，需要单独授权。

## 上游升级：逐版本核对完整树

不得只凭 source-manifest 的 wpf commit 认定快照相同。当前 dotnet/wpf v10.0.14 根树 `6c0c3cfa7865602595a995fdb51ad6be52509cd9` 与 VMR 0427b9ab 的 WPF 子树不同，fork 的纯基线导入提交 `f57a2327335725d33799d5978c39b610214b3865` 根树才等于 `664f60538e7fbbc978dcd8984f3e9a023906e5de`；差异包含服务/安全修复。

每次升级先核实 WPF tag、SDK/VMR tag，GitHub 只读 fetch，并比较整个根树（不是只比 src）：

```powershell
# 标签均为占位，必须先核实；此处没有执行网络或升级。
git -C G:/WpfD3D11Runtime/wpf fetch --filter=blob:none --no-tags https://github.com/dotnet/wpf.git refs/tags/<WPF标签>
git -C G:/WpfD3D11Runtime/wpf rev-parse 'FETCH_HEAD^{tree}'
# 与对应 VMR 的 <VMR提交>:src/wpf 树比较，记录 tag/commit/tree/source-manifest。
```

相同则直接使用真实 dotnet/wpf commit；不同时在 `G:/WpfD3D11Runtime/vmr-import` 建只读、浅层稀疏导入，不在那里开发或构建：

```powershell
git clone --filter=blob:none --no-checkout --depth=1 --single-branch --branch <SDK标签> https://github.com/dotnet/dotnet.git G:/WpfD3D11Runtime/vmr-import
git -C G:/WpfD3D11Runtime/vmr-import sparse-checkout init --no-cone
git -C G:/WpfD3D11Runtime/vmr-import sparse-checkout set --no-cone /src/wpf/ /src/source-manifest.json
git -C G:/WpfD3D11Runtime/vmr-import checkout <SDK标签>
git -C G:/WpfD3D11Runtime/vmr-import rev-parse 'HEAD:src/wpf'
# 后续标签：
git -C G:/WpfD3D11Runtime/vmr-import fetch --filter=blob:none --depth=1 --no-tags origin <新SDK标签>
```

只查询树/清单时保持 no-checkout；需要导入快照才 materialize。维护纯上游快照线，保留来源证明；在新的 fork 任务树整合私有提交并处理冲突，保留 eng/quicker 和 fork 自有文档。更新 baselineProvenance、精确依赖/版本后，重新做全量构建、R2R、等价、签名、包门禁与消费验收。不能把当前树证明外推到今后版本。

## 已验证与边界

REPO2 本地演练：从 fork c8ab0680 的任务树，以已签 `.4` 为基准，仅重建双 RID wpfgfx，未签名。0 警告/0 错误；原始代码节等于 PUB3，65 个沿用运行时逐字节不变；全量模式仅 Validate/WhatIf 前置通过。此演练不是新正式版本，也没有发布授权。

等价干跑使用显式 nativeTrimPrefix 将诊断路径保留为等长后缀，并用 nativePdbAltPaths 控制历史 PDB 元数据，再按已定位字段归一化比较 PE 节；没有 junction、没有改二进制代码或伪造 manifest commit。这些参数默认关闭；生产调试路径规范仍需单独决定。`nativeSourcePrefix` 的实验曾引出 C5049，映射 obj/PDB 又引出 LNK4099；最终通过路径不使用该方案，不压制警告。

尚未在迁移入口实跑完整 WPF/R2R 图、托管增量、签名 Execute、签后打包、冷缓存恢复、新 SDK、消费 build/publish 或 GPU/ARM 运行验证；旧 `.3`/`.4` 的这些历史验证不冒充本次验证。全量签名角色、托管等价报告和独立签名机交接应在下一次获授权完整生产演练中验收。当前不据此删除历史 slice/VMR 或原件。
