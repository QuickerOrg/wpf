# WpfD3D11Runtime

QuickerOrg 维护的第三方 **.NET 10 WPF D3D11 运行时替换包**，不是微软官方产品，也未获微软背书。当前版本 **[10.0.14-d3d11.3 已发布到 nuget.org](https://www.nuget.org/packages/WpfD3D11Runtime/10.0.14-d3d11.3)**；源码位于 [quicker/d3d11-facade](https://github.com/QuickerOrg/wpf/tree/quicker/d3d11-facade)。保留 WPF 上层渲染算法、应用 API 和官方编译引用，替换应用输出中的运行时文件。

可直接运行的示例见 [WpfD3D11Runtime-Samples](https://github.com/QuickerOrg/WpfD3D11Runtime-Samples)。仅使用包不需要 Visual Studio C++ 工具链或 WPF 源码树。

## 使用与限制

在 .NET 10 WPF 应用中设置 RID 并添加一条引用：

```xml
<PropertyGroup>
  <RuntimeIdentifier>win-x64</RuntimeIdentifier> <!-- 或 win-arm64 -->
</PropertyGroup>
<ItemGroup>
  <PackageReference Include="WpfD3D11Runtime" Version="[10.0.14-d3d11.3]" />
</ItemGroup>
```

- 支持 Windows `win-x64` / `win-arm64`、框架依赖（FDD）和自包含（SC）构建/发布；FDD 仍需要匹配架构的 .NET 10 Windows Desktop Runtime。示例将运行时要求设为 `10.0.14`。
- 不支持 x86、无 RID、非 .NET 10 WPF、裁剪、单文件、AOT，以及消费端 `PublishReadyToRun` / `PublishReadyToRunComposite`。必须生成 deps.json。
- 编译和 XAML 编译继续使用官方 targeting pack；本包不提供 ref 程序集或 WPF 编译任务，也不替换 CoreCLR 或整个 WinForms 框架。
- 消费者构建调用 Windows PowerShell 5.1 校验器（默认 `powershell.exe`，可用 `WpfD3D11RuntimePowerShell` 指定兼容宿主）。包消费已用 SDK 10.0.403 验证；其他 SDK/Visual Studio 构建组合需重新验证。
- ARM64 使用系统 `D3DCompiler_47.dll`，不从 System32 复制或随包分发；x64 携带 `D3DCompiler_47_cor3.dll`。
- 包内 WPF 基线锁定 **10.0.14**。共享框架补丁不会自动更新这些文件；维护者需显式升级包并重新验证应用。
- 后端在编译时确定，不能通过环境变量切回官方 D3D9。回退时移除包引用，使用干净输出重新构建、完整部署官方运行时版本并重启进程。
- 已知部分 Qualcomm GPU 有纹理采样截断差异。不保证所有 GPU、效果、设备恢复或未来补丁组合均逐像素一致；根 README 的验证有其设备与样本范围，不代表完整 WPF API/GPU 认证。

## 版本规则

| 标识 | 规则与当前值 |
| --- | --- |
| NuGet 包版本 | 上游服务基线加门面序号：`10.0.14-d3d11.N`；当前 `N=3` |
| 自建 FileVersion | 当前序列使用 `10.1.0.N`，本版 `10.1.0.3`；这是本项目版本域，不代表微软发布了 .NET 10.1 |
| AssemblyVersion | 保持 `10.0.0.0` |
| InformationalVersion | 构建脚本记录基线、私有序号和 fork commit；以包内逐文件 manifest 和真实文件为准 |

脚本完整 FileVersion 算法为 `10.1.floor(N/65536).(N%65536)`；`N` 必须为正整数。上游原字节二进制保留各自版本与签名。公开包 `.3` 的 `publicRepositoryCommit` 和 nuspec repository 指向包含门面优化的源码快照 `6079ac76740cba2b589fbe7616c2c063a6d827f1`；manifest 的原始 `forkCommit` 及内嵌 InformationalVersion 仍保留构建时基线与优化补丁信息。两类来源字段含义不同，后续文档提交也不等于原二进制内嵌版本。

## 包结构与 buildTransitive

```text
runtimes/win-x64/lib/net10.0/      # 29 个运行时程序集
runtimes/win-x64/native/          # 5 个原生文件，含 x64 compiler
runtimes/win-arm64/lib/net10.0/    # 29 个运行时程序集
runtimes/win-arm64/native/        # 4 个原生文件，无 OS compiler
buildTransitive/                 # props、targets、每 RID 的精确资产声明
manifests/win-{x64,arm64}.json    # 来源、版本、架构、哈希、R2R 与签名记录
tools/                          # 消费者输入/输出校验器
README.md, LICENSE.txt, THIRD-PARTY-NOTICES.txt, licenses/
```

本包是 runtime-only 包，没有顶层 `lib/` 或 `ref/`，不会改变应用编译引用。`buildTransitive` 从包自身位置定位文件：

1. `ResolveReferences` 前验证 RID、目标框架、发布选项及精确输入集合。
2. 在 SDK 的 Build/Publish 冲突处理之后恢复 manifest 所属运行时资产，移除对应同名 RuntimePackAsset，并参与生成最终 deps.json；不关闭 SDK 的重复文件检查。
3. Build/Publish 完成后核对实际输出、deps 声明、PE 架构/版本和 SHA256；FDD 拒绝混入 coreclr/hostfxr/hostpolicy。失败会使构建失败。
4. 向输出复制 `licenses/WpfD3D11Runtime/`，包含 README、许可、notices 和当前 RID manifest。默认不复制验证脚本；诊断时可设 `WpfD3D11CopyVerifier=true`。

无需手动覆盖 DLL 或修改 deps.json。包版和官方版应使用各自全新的 artifacts/publish 目录；切换开关不会卸载旧目录中的文件。

## 许可与第三方 notices

WPF fork 源码采用 [MIT 许可](LICENSE.txt)。**已发布的 nupkg** 根目录保留 `LICENSE.txt`、`THIRD-PARTY-NOTICES.txt`，`licenses/` 包含上游完整声明、PresentationNative 的 `DNCENG-*` 文本和 `WINDOWSDESKTOP-LICENSE.txt`。这些通知也随应用输出携带；逐文件来源见 `manifests/`。MIT 不替代第三方组件各自适用的条款。

本源码目录也保留 [上游 notices](licenses/UPSTREAM-THIRD-PARTY-NOTICES.txt)、[PresentationNative 许可](licenses/DNCENG-LICENSE.txt) 和 [WindowsDesktop 许可](licenses/WINDOWSDESKTOP-LICENSE.txt)。本目录的部分历史声明及打包脚本生成的描述仍保留早期本地验证措辞，和已发布包的文本不同；核对公开 `.3` 分发内容时，以 nuget.org 下载包及其中许可/manifest 为准。本说明不把历史脚本输出自动视为新的公开发行版。

## 从源码构建

以下区分公开 fork 的开发构建入口与已用于产包的构建环境。源码流程依据既有构建记录和当前脚本整理；**公开 fork 全新机器上的独立构建及完整产包流程均未验证**。示例的 nuget.org 消费已单独验证。

### 前提与已知工具链

| 项目 | 记录和源码能确认的要求 |
| --- | --- |
| 操作系统 / VS | 已有运行时构建记录使用 Windows、VS/MSBuild **18 Community**；脚本默认指向该版 MSBuild。上游指南写 VS 2022，但用 VS 2022 完整产包未验证；VS 18 的最低小版本也未验证 |
| 工作负载 | 安装“.NET 桌面开发”和“使用 C++ 的桌面开发”，含 MSBuild、C++/CLI、ATL/MFC、.NET Framework 4.7.2 targeting pack 等；参考上游 [组件清单](../../../Documentation/wpf.vsconfig)，该旧清单不能代替下列实际 SDK/工具集版本 |
| C++ | `v143`；producer 固定 `VCToolsVersion=14.44.35207`、`VCToolsRedistVersion=14.44.35112`。需装所构建架构的编译器/库（ARM64 还需对应 C++/CLI、ATL/MFC 组件）；其他版本未验证 |
| Windows SDK | 当前 [Wpf.Cpp.props](../../WpfArcadeSdk/tools/Wpf.Cpp.props) 固定 `10.0.26100.0`，不是旧组件清单中的 19041 |
| .NET SDK | 已有包构建/消费记录用 **10.0.403**。本 fork 的 [global.json](../../../global.json) 请求 `10.0.112` 并允许 `latestFeature`；示例另有隔离配置。更换 SDK 必须复验 |
| PowerShell | 编译入口 `Build-*`、完成/R2R 入口 `Complete-*` 要求 **PowerShell 7**；签名、打包、bundle 导出/处理和消费者验证兼容 **Windows PowerShell 5.1**，已有真实 5.1 打包记录 |
| 产包额外输入 | 相同源码快照的双架构完整 IL、相应架构非 WPF 自包含引用集、10.0.14 crossgen2、逐文件等价性报告，以及辅助脚本所在的 Quicker 工作区；本 fork 不会自动下载/生成这些输入 |

仅安装 .NET SDK 不足以构建 WPF 原生和 C++/CLI 部分。上游 [开发指南](../../../Documentation/developer-guide.md) 可作组件背景参考，本文实际版本约束优先。

### 获取源码与公开 fork 的入口

```powershell
git clone --branch quicker/d3d11-facade https://github.com/QuickerOrg/wpf.git
cd wpf
```

本仓库是 **dotnet/wpf 布局**，根目录就是 WPF 项目；实际发布二进制的构建使用 **dotnet/dotnet VMR 的 `src/wpf` 切片**。VMR 基线记录为 `0427b9ab`，source-manifest 中上游 WPF 基线为 `4726a1a9`；VMR 服务补丁快照与直接检出的该上游 WPF 提交仍有差别。公开 fork 先对齐切片快照，再重放门面修改；不能只检出上游 `4726a1a9` 就认定输入一致。

外部读者若只是修改/研究 WPF，可从本 fork 的 [build.cmd](../../../build.cmd) / [eng/common/Build.ps1](../../common/Build.ps1) 开始。下列参数与当前入口相符，但**本门面分支的独立仓库全新构建尚未实测**，也不等于构造可打包的私有版本：

```powershell
# 在 Windows PowerShell / PowerShell 中设置，勿在 Git Bash 中设置 CL。
$env:CL = '/utf-8'
./build.cmd -configuration Release -platform x64 -msbuildEngine vs `
  /p:WpfD3D11Facade=true /p:VCToolsVersion=14.44.35207 /p:VCToolsRedistVersion=14.44.35112
```

`build.cmd` 自带 `-restore -build`。新的构建变体必须 restore，不能复用另一变体的 obj；直接调用 MSBuild 则需显式 `-restore`。`CL=/utf-8` 避免原生源码编码问题。此入口可能还原上游配置的依赖源，不是仅依赖 nuget.org 的离线产包命令。不要把普通开发输出的 `42.*` 版本交给运行时打包器。

### 使用现有 producer 构建运行时

[Build-WpfD3D11Runtime.ps1](Build-WpfD3D11Runtime.ps1) **目前依赖 VMR 布局，不能直接在独立 fork 中运行**：从脚本位置上溯到 WPF 根，再上溯两级认作 VMR 根，读取 `HEAD:src/wpf` 与 `src/source-manifest.json`。它还比较 VMR 的 WPF 树与 `-ForkRepository` 的树，排除不参与运行时构建的 `eng/quicker/`、`samples/` 和根 `README.md`；其余文件必须逐一相同，否则拒绝构建。

要复用这条有历史构建记录的路线，需另备 [dotnet/dotnet VMR](https://github.com/dotnet/dotnet) 工作区，将与 fork 一致的 WPF 源码快照放到 `src/wpf` 并提交、记录真实来源，保留 VMR 构建基础设施。只把文件放进名叫 `src/wpf` 的空目录不够。**从公开基线在外部机器完整重建环境未验证**；本仓库未提供一键导入 VMR、准备全部依赖或生成等价性报告的公共入口。

参数与限制：

- 必填 `-Architecture x64|ARM64`、`-OutputRoot`、`-QuickerRepository`、`-ReferenceDirectory`；后三者必须为绝对路径。`OutputRoot` 必须全新且不存在，每架构单独设置。
- 显式传 `-ForkRepository`、`-MSBuild`、`-Crossgen2`、`-NuGetPackagesDirectory`，不要依赖脚本内的构建机默认路径。`PrivateReleaseSequence` 默认 **1**，重建本版应明确传 **3**；包版本由 `eng/Versions.props` 的基线加序号生成。
- NuGet 缓存由 `-NuGetPackagesDirectory` 指定（绝对路径，必须已存在），脚本据此设置 `NUGET_PACKAGES`；调用前设置的环境变量 `NUGET_PACKAGES` 会被它覆盖。脚本把 `RestoreSources` 指向新建空 feed 并关闭在线 feeds，所以这是**离线还原**：该目录必须事先包含构建所需的全部包（可先用联网的普通构建预热同一目录）。默认值是维护者构建机路径，外部构建必须显式传参。
- C++/CLI 中间项目需继承源码树 targets：obj 在 WPF 根下独立的 `artifacts-p2-<OutputRoot末段>/obj/`，bin/log 在 `OutputRoot`。不同构建使用不同末段名称，不能仅更换父目录后重用旧 obj。
- 内部已有 `-restore`、`CL=/utf-8`、Release 门面开关、私有版本适配及 net472 PresentationBuildTasks 先行 restore，一次 MSBuild 会话构建完整运行时图。脚本关闭三类 API compat 检查，不能把构建成功说成已通过 API compat。
- `-BuildOnly` 在完整 33 文件 IL 投影及版本校验后停止；不加它必须提供有效 `-EquivalenceReport`。`-ResumeFailedBuild` 只用于同一源码提交/架构的失败恢复。

下面是**已备齐上述 VMR、缓存与辅助输入之后**的参数模板，不是克隆本 fork 后即可执行的命令。路径为示例，须换成自己的绝对路径：

```powershell
$vmr = 'C:/src/dotnet-vmr'
$fork = 'C:/src/wpf'
$quicker = 'C:/src/Quicker'  # 须包含下文所列辅助脚本
$producer = "$vmr/src/wpf/eng/quicker/WpfD3D11Runtime"
$msbuild = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$crossgen2 = 'C:/tools/crossgen2-10.0.14/crossgen2.exe'

pwsh -NoProfile -File "$producer/Build-WpfD3D11Runtime.ps1" `
  -Architecture x64 -OutputRoot C:/build/wpf-x64-new `
  -QuickerRepository $quicker -ReferenceDirectory C:/inputs/references-x64 `
  -ForkRepository $fork -MSBuild $msbuild -Crossgen2 $crossgen2 `
  -NuGetPackagesDirectory C:/build/nuget-packages `
  -PrivateReleaseSequence 3 -BuildOnly
```

ARM64 使用 `-Architecture ARM64`、新的 `OutputRoot` 和相应架构引用目录。当前 pack 需要两个架构，只有 x64 输出不足。

### R2R 与可选签名

[Complete-WpfD3D11Runtime.ps1](Complete-WpfD3D11Runtime.ps1) 读取 `build-receipt.json` 和等价性报告，检查报告含 `unexpectedDifferences=false`、精确 33 文件集合及每项 `newSha256` 与本次 IL 相符。报告应来自实际构建差异审核，不能用空布尔标志代替。

它调用 `QuickerRepository/Experiments/WpfRenderBackend/new-r2r-overlay.ps1`，还依赖同目录 `overlay-common.ps1`；这些辅助脚本不在本公开 fork 中。引用集先取同架构非 WPF 自包含 DLL，再以本次 WPF IL 覆盖，避免混用旧 WPF。已用 crossgen2 **10.0.14** 对每 RID **28 个程序集**独立 R2R；`DirectWriteForwarder.dll` 和 native 只复制。完成入口要求 `references/`、`r2r/` 尚不存在。

```powershell
pwsh -NoProfile -File "$producer/Complete-WpfD3D11Runtime.ps1" `
  -OutputRoot C:/build/wpf-x64-new -QuickerRepository $quicker `
  -ReferenceDirectory C:/inputs/references-x64 `
  -EquivalenceReport C:/inputs/equivalence-x64.json -Crossgen2 $crossgen2
```

上例默认只记录 `unsigned/dryrun`，适合本地验证。可选真实签名在 R2R 与头/身份检查之后执行：向 Build/Complete 显式提供 `-ExecuteSigning` 及签名工具、证书标识、时间戳服务参数。签名还需互斥辅助脚本（通过 `QuickerRepository/Tools/Build/RunWithMutex.ps1`，或给 [Sign-WpfD3D11Runtime.ps1](Sign-WpfD3D11Runtime.ps1) 明确 `-MutexScript`）。上游 PresentationNative、VC redist 和 compiler 保留原字节/签名，不重新签名。

`-RequireSigned` 是 **Pack 的校验开关，不执行签名**：拒绝 fork 自建文件的 unsigned/dryrun，要求实际有效签名、时间戳及记录/指纹/哈希一致。未签名本地验证可不传；公开 `.3` 已通过 RequireSigned 路线准备。顺序为：编译/版本 → 差异审核 → R2R → 签名 → 最终哈希/manifest → pack。

### 打包并在 demo 中验证本地 feed

[Pack-WpfD3D11Runtime.ps1](Pack-WpfD3D11Runtime.ps1) 不做源码构建，接受双架构最终 `r2r/` 和 `overlay-manifest.json`，按精确清单投影 33 文件，x64 另加 compiler。compiler 必须来自官方 WindowsDesktop x64 **10.0.11** 缓存的 `D3DCompiler_47_cor3.dll`，SHA256 必须为 `A05F99734F7C4822FEFC12B367AF21FD0976ED6608752FB1E1E80B6ECE7ECBBB`，不能随意替换同名文件。

准备绝对路径 `NuGetConfig`，例如以下无网络源配置；生成的 pack 项目无包依赖，本机仍需可用 .NET 10 SDK：

```xml
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
  <fallbackPackageFolders><clear /></fallbackPackageFolders>
</configuration>
```

使用全新 staging、独立 artifacts/feed。在前述 PowerShell 会话中执行（双架构已完成），显式传来源，避免在非 VMR 工作区默认读取 `HEAD:src/wpf`：

```powershell
$sliceCommit = (git -C $vmr rev-parse HEAD).Trim()
$wpfTree = (git -C $vmr rev-parse HEAD:src/wpf).Trim()
powershell.exe -NoProfile -File "$producer/Pack-WpfD3D11Runtime.ps1" `
  -X64InputDirectory C:/build/wpf-x64-new/r2r `
  -X64Manifest C:/build/wpf-x64-new/r2r/overlay-manifest.json `
  -Arm64InputDirectory C:/build/wpf-arm64-new/r2r `
  -Arm64Manifest C:/build/wpf-arm64-new/r2r/overlay-manifest.json `
  -X64Compiler C:/inputs/D3DCompiler_47_cor3.dll `
  -StagingDirectory C:/build/wpf-pack-staging-new `
  -ArtifactsPath C:/build/wpf-pack-artifacts-new -FeedDirectory C:/build/wpf-local-feed `
  -NuGetConfig C:/inputs/nuget-offline.config `
  -SourceSliceCommit $sliceCommit -SourceWpfTree $wpfTree
```

输入已真实签名可追加 `-RequireSigned`，未签名输入加此开关会失败。双架构 manifest 的版本和 fork commit 必须一致。脚本仍保留历史本地包描述/标记，生成的是供自己验证的本地 feed 包；不会发布到 nuget.org，也不是公开 `.3` 元数据的重现流程。

验证时复制示例 nuget.config 到自己的工作目录并改成如下内容：将 `WpfD3D11Runtime` 只映射到本地源，`Microsoft.*` 保留在 nuget.org，不让两个源同时提供同一包标识：

```xml
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="local-wpf" value="C:/build/wpf-local-feed" />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
  <packageSourceMapping>
    <clear />
    <packageSource key="local-wpf"><package pattern="WpfD3D11Runtime" /></packageSource>
    <packageSource key="nuget.org"><package pattern="Microsoft.*" /></packageSource>
  </packageSourceMapping>
  <fallbackPackageFolders><clear /></fallbackPackageFolders>
</configuration>
```

若序号不是 3，先把示例 csproj 的精确包版本和 Program.cs 中 `10.1.0.3` 自检预期一起改成自己的版本。以下假设序号仍为 3；同版本重打包必须换全新缓存和输出，不能复用 nuget.org `.3` 缓存：

```powershell
cd C:/src/WpfD3D11Runtime-Samples/WpfD3D11Demo
$env:NUGET_PACKAGES = 'C:/build/demo-local-new/packages'
$env:NUGET_HTTP_CACHE_PATH = 'C:/build/demo-local-new/http-cache'
dotnet publish -c Release -r win-x64 --self-contained false `
  --configfile C:/inputs/nuget-local.config `
  --artifacts-path C:/build/demo-local-new/artifacts -o C:/build/demo-local-new/publish
$demoProcess = Start-Process C:/build/demo-local-new/publish/WpfD3D11Demo.exe `
  -ArgumentList '--selftest C:/build/demo-local-new/selftest.json' `
  -WindowStyle Hidden -PassThru -Wait
$demoProcess.ExitCode
```

检查 `.nupkg.metadata` 指向本地 feed、包内 manifest、JSON 模块路径/版本、D3D11 与无 D3D9，再按示例说明做官方 FDD 对照。ARM64 应独立发布并在对应设备原生验证。以上源码、R2R、签名、打包和本地 feed 命令是依据既有环境整理的模板，未在全新外部环境验证。

## English notes

- **Published:** WpfD3D11Runtime `10.0.14-d3d11.3` is on nuget.org. See the [samples repository](https://github.com/QuickerOrg/WpfD3D11Runtime-Samples) for run/publish/self-test commands.
- **Scope:** .NET 10 WPF, win-x64/win-arm64, FDD/SC. No x86, trimming, single-file, AOT or consumer R2R. Compilation keeps official targeting packs. Shared-framework servicing does not update bundled WPF files.
- **Versions:** custom FileVersion `10.1.0.3`, AssemblyVersion `10.0.0.0`, package baseline `10.0.14-d3d11.N`. This is not a Microsoft .NET 10.1 release.
- **Source builds:** the runtime was built from a dotnet/dotnet VMR `src/wpf` slice; this repository uses standalone dotnet/wpf layout. The producer needs a matching VMR tree, prepared cache, Quicker R2R helpers, reference inputs and an equivalence report; it also hardcodes a cache location. Fresh external setup and standalone builds remain unverified.
- **Tools:** recorded producer uses VS/MSBuild 18, v143 tools 14.44.35207/redist 14.44.35112, Windows SDK 10.0.26100.0 and .NET SDK 10.0.403. Build/Complete need PowerShell 7; signing/pack/consumer verification support Windows PowerShell 5.1. Signing is optional locally; Pack's `RequireSigned` validates signatures, never signs.
- **Licenses:** retain the published package's `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt`, `licenses/` and manifests. Third-party terms remain applicable. Historical source-side metadata is not the published package's current description.
