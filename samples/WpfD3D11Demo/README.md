# WpfD3D11Demo

最小 .NET 10 WPF 示例：通过一条 `PackageReference` 使用已在 [nuget.org 发布的 WpfD3D11Runtime 10.0.14-d3d11.3](https://www.nuget.org/packages/WpfD3D11Runtime/10.0.14-d3d11.3)。不需要手动复制 DLL 或修改加载器。它是 QuickerOrg 维护的第三方运行时，不是微软官方产品。

A minimal .NET 10 WPF application using the published WpfD3D11Runtime package through one `PackageReference`. No manual DLL copies or loader changes are required. This is a third-party runtime maintained by QuickerOrg, not an official Microsoft product.

## 前提 / Prerequisites

- Windows x64 或 ARM64、.NET 10 SDK；示例已用 SDK 10.0.403 验证。框架依赖运行需要相应架构、满足 `10.0.14` 运行时要求的 .NET 10 Windows Desktop Runtime。
- 构建时需要 Windows PowerShell 5.1（`powershell.exe`），供包内校验器使用；仅运行已发布的应用不需要执行该校验器。
- 默认 RID 是 `win-x64`；ARM64 请明确使用 `-r win-arm64`，并在 Windows ARM64 上原生运行。ARM64 需要系统提供 `D3DCompiler_47.dll`。

Use Windows x64 or ARM64 and the .NET 10 SDK (tested with 10.0.403). Framework-dependent execution also needs the matching .NET 10 Windows Desktop Runtime satisfying the project's `10.0.14` runtime requirement. Builds use Windows PowerShell 5.1 for package validation. The default RID is `win-x64`; use `win-arm64` and native Windows ARM64 execution for ARM64, where the OS must provide `D3DCompiler_47.dll`.

## 运行与发布 / Run and publish

在 PowerShell 中执行；如果已经克隆仓库，从第二行进入示例目录即可：

Run in PowerShell; if the repository is already cloned, enter the sample directory:

```powershell
git clone --branch quicker/d3d11-facade https://github.com/QuickerOrg/wpf.git
cd wpf/samples/WpfD3D11Demo
dotnet run -r win-x64
```

构建和框架依赖发布也可直接在本目录执行。普通运行关闭窗口后退出：

Build or publish directly from this directory. Close the window to exit interactive mode:

```powershell
dotnet build -r win-x64
dotnet publish -c Release -r win-x64 --self-contained false -o ./artifacts/package-x64
./artifacts/package-x64/WpfD3D11Demo.exe
```

自包含发布将 `--self-contained false` 改为 `--self-contained true`。ARM64 命令如下；将完整发布目录带到 Windows ARM64 机器运行，不要只复制 exe：

For self-contained publishing, change `--self-contained false` to `--self-contained true`. For ARM64, publish as follows and run the complete output directory on Windows ARM64:

```powershell
dotnet run -r win-arm64
dotnet publish -c Release -r win-arm64 --self-contained false -o ./artifacts/package-arm64
```

本目录的空 [Directory.Build.props](Directory.Build.props) / [Directory.Build.targets](Directory.Build.targets) 阻断父仓库构建钩子；[global.json](global.json) 选择稳定 .NET 10 SDK，并允许向较新的 feature band 滚动。[nuget.config](nuget.config) 用 `<clear />` 清空继承的源、映射与 fallback，只从 nuget.org 获取本包和 `Microsoft.*`。请在本目录执行命令，以使用这里的 SDK 和 NuGet 配置。

The empty build files isolate this sample from the WPF repository's build hooks. Its own `global.json` selects a stable .NET 10 SDK with feature-band roll-forward. Its `nuget.config` clears inherited sources, mappings and fallback folders, and uses only nuget.org for this package and `Microsoft.*`. Run commands from this directory so these configurations apply.

## 窗口里看什么 / What to inspect

窗口显示渐变、旋转文字、阴影和半透明卡片，可调整大小。诊断区每半秒更新：

The resizable window displays gradients, rotating text, a shadow and a translucent card. Diagnostics update every half second:

- `PresentationCore.dll` 和 `wpfgfx_cor3.dll` 的实际加载路径与四段数字 FileVersion：包版应来自应用输出目录，版本为 `10.1.0.3`。
- `d3d11.dll: true`、`d3d9.dll: false`：报告进程是否加载这些模块。
- `Tier` 是 `RenderCapability.Tier >> 16` 的值，`Frames` 是 WPF 渲染回调计数。Tier 取决于运行环境；加载 D3D11 且未加载 D3D9 本身不证明硬件 Tier 2。

For the package build, `PresentationCore.dll` and `wpfgfx_cor3.dll` should load from the application output directory with FileVersion `10.1.0.3`; D3D11 should be loaded and D3D9 absent. `Tier` reports `RenderCapability.Tier >> 16`, and `Frames` counts WPF rendering callbacks. Tier depends on the environment; module presence alone does not prove hardware Tier 2.

## 自动自检 / Automated self-test

```powershell
$demoProcess = Start-Process ./artifacts/package-x64/WpfD3D11Demo.exe `
  -ArgumentList '--selftest ./artifacts/selftest-x64.json' `
  -WindowStyle Hidden -PassThru -Wait
$demoProcess.ExitCode
```

`--selftest <out.json>` 约六秒后写出 JSON 与同名 PNG 并退出。它显示自己的置顶窗口但不激活，不模拟鼠标输入，自动缩小再恢复窗口。JSON 记录模块路径、数字和原始版本文本、D3D11/D3D9 状态、Tier、帧数、缩放恢复和截图方法。`DesktopBitBlt` / `PrintWindow` / `WpfVisualFallback` 表示不同截图路径；WPF Visual 回退不等同于桌面出光证据。

`--selftest <out.json>` writes JSON and a matching PNG after about six seconds, then exits. It displays its own topmost window without activation or simulated mouse input, and resizes/restores it automatically. JSON includes module paths and versions, D3D module state, Tier, frame count, resize status and capture method. A `WpfVisualFallback` capture is not proof of desktop presentation.

| 退出码 / Exit code | 含义 / Meaning |
| --- | --- |
| `0` | 后端检查通过，渲染回调 >10，窗口尺寸已恢复，截图成功取得彩色内容 / Backend checks passed, >10 rendering callbacks, size restored, capture produced colored content |
| `1` | 自检失败或运行异常 / Failed check or runtime exception |
| `2` | 参数格式不是 `--selftest <out.json>` / Invalid argument shape |
| `3` | 自检完成前关闭了窗口 / Window closed before completion |

包版后端检查要求应用目录内的 `wpfgfx_cor3.dll` 为 `10.1.0.3`、`PresentationCore.dll` 也在同目录、D3D11 已加载且无 D3D9；**不要求 Tier 2**。无参数普通运行不执行这些通过/失败判定。输出路径必须可写。

The package backend check requires application-local `wpfgfx_cor3.dll` version `10.1.0.3`, application-local `PresentationCore.dll`, D3D11 loaded and no D3D9. It **does not require Tier 2**. Interactive mode without arguments does not apply these pass/fail checks. Use a writable output path.

## 官方运行时对照 / Official runtime comparison

关闭包引用，并使用独立中间物和输出目录，避免残留私有 DLL：

Disable the package reference and use separate intermediate and output directories to avoid stale private DLLs:

```powershell
dotnet build -c Release -r win-x64 -p:DemoUsePackage=false `
  --artifacts-path ./artifacts/official-build
dotnet publish -c Release -r win-x64 --self-contained false -p:DemoUsePackage=false `
  --artifacts-path ./artifacts/official-build -o ./artifacts/official-x64
./artifacts/official-x64/WpfD3D11Demo.exe
```

同一份 [Program.cs](Program.cs) 会显示官方模块路径。官方 FDD 自检以模块来自 `shared/Microsoft.WindowsDesktop.App`、wpfgfx 不是私有 `10.1.0.3` 为后端条件；仍报告 `privateBackendSatisfied=false`。它不要求 D3D9 一定加载，也没有官方自包含版的自检判据。

The same source displays the official module paths. The official FDD self-test checks that modules come from `shared/Microsoft.WindowsDesktop.App` and wpfgfx is not private version `10.1.0.3`; `privateBackendSatisfied` remains false. It does not require D3D9 to be loaded. Official self-contained self-test criteria are not implemented.

## 常见问题 / Troubleshooting

- **RID 错误**：示例已默认 `win-x64`，但在自己的应用中必须显式设置 RID。包的真实错误为 `WPFDR001`：“WpfD3D11Runtime 必须显式使用 win-x64 或 win-arm64 RID；无 RID、x86 和其它 RID 不受支持。”
- **目标或发布选项错误**：`WPFDR002` 要求 .NET 10 WPF；`WPFDR003` 拒绝 trim、single-file、AOT、消费端 R2R（含 composite）；`WPFDR004` 要求生成 deps.json。不要为了通过编译而关闭包校验器。
- **旧版本或来源不确定**：包引用精确锁定 `[10.0.14-d3d11.3]`。换一组全新的 `NUGET_PACKAGES`、`NUGET_HTTP_CACHE_PATH`、`--artifacts-path` 与发布目录后重试；缓存中的 `.nupkg.metadata` 可核对源。不要在同一输出目录里来回切换包版与官方版。
- **SDK 或还原意外进入 WPF 仓库构建**：确认当前目录是本示例，且四个隔离文件仍在。也可给 restore/build/publish 显式传 `--configfile ./nuget.config`。
- **运行时缺失或版本不对**：安装相应架构的 Windows Desktop Runtime，或使用自包含发布；查看 JSON 中的实际模块路径，不只看 NuGet 引用。共享框架补丁不会自动更新包内 WPF 文件。
- **无 D3D11 / Tier 0 / 自检失败**：查看 JSON 的模块与 `error` 字段，检查会话、GPU/驱动和截图条件。Tier 0 单独出现不会使此自检失败；自检通过也不替代真实应用兼容性测试。

For RID failures, pass `-r win-x64` or `-r win-arm64` (`WPFDR001`). `WPFDR002` requires .NET 10 WPF; `WPFDR003` rejects trimming, single-file, AOT and consumer R2R; `WPFDR004` requires deps.json. For stale files, retry with fresh package/HTTP caches, artifacts and publish directories, and inspect `.nupkg.metadata`. For SDK/source inheritance problems, run from this directory with its isolation files and explicit `--configfile ./nuget.config`. For runtime or rendering issues, inspect the actual module paths and JSON errors. Tier 0 alone does not fail this test, and a passing test does not establish full application compatibility.

支持范围、回退、已知 Qualcomm 采样差异及本地 feed 使用方法见 [运行时说明](../../eng/quicker/WpfD3D11Runtime/README.md)。后端在编译时确定，不能用环境变量切回官方后端。

See the [runtime documentation](../../eng/quicker/WpfD3D11Runtime/README.md) for supported configurations, rollback, known Qualcomm sampling differences and local-feed instructions. Environment variables cannot switch this build to the official backend.
