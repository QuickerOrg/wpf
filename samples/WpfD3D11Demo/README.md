# WpfD3D11Demo：D3D11 与实验性彩色 Emoji

最小 .NET 10 WPF 示例，通过一条 `PackageReference` 使用 `WpfD3D11Runtime [10.0.14-d3d11.4]`，无需手动复制 DLL、修改加载器或 deps。它是 QuickerOrg 维护的第三方运行时，不是微软官方产品。该版本已发布到 nuget.org，默认配置即可还原；下文的本地包源仅用于验证尚未发布的候选包。

A minimal .NET 10 WPF application using one package reference. This is a third-party runtime, not an official Microsoft product. The version is published on nuget.org and restores with the default configuration; the local feed below is only for validating unpublished candidates.

## 前提与构建 / Build

需要 Windows、.NET 10 SDK（已验证 10.0.403）、构建时可调用的 Windows PowerShell 5.1，以及运行时相应架构的 .NET 10 Windows Desktop Runtime（10.0.14）。默认 x64；ARM64 请明确指定 `win-arm64`，在原生 ARM64 Windows 上运行，系统需提供 `D3DCompiler_47.dll`。

实验仓库：进入本 README 旁的 `WpfD3D11Demo` 子目录。公开 fork：进入 `samples/WpfD3D11Demo`。示例不依赖 Quicker；空的 `Directory.Build.props/targets` 隔离父级仓库构建规则。

Use Windows, the .NET 10 SDK, Windows PowerShell 5.1 for build checks, and the matching .NET 10 Windows Desktop Runtime. Enter the project directory before running these commands. Validate ARM64 on native Windows ARM64.

```powershell
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
$env:NUGET_PACKAGES = "$PWD/artifacts/packages"
$env:NUGET_HTTP_CACHE_PATH = "$PWD/artifacts/http-cache"
dotnet publish -c Release -r win-x64 --self-contained false `
  --configfile ./nuget.config --artifacts-path ./artifacts/build-x64 `
  -o ./artifacts/package-x64
# ARM64: change RID to win-arm64 and use separate build/output directories.
```

默认页面保留渐变、旋转、阴影、半透明和实际模块诊断。Emoji 页面展示 14/28/48 DIP 的单码点、肤色、ZWJ（👩‍❤️‍👨、🧑🏽‍💻、家庭）、彩虹旗、中英文混排，以及连续的 1→2→1 缩放动画。窗口可调整大小，内容可滚动。窗口不请求激活；普通模式关闭窗口退出，selftest 约六秒自行关闭。

The default scene retains gradients, rotation, shadows, transparency and diagnostics. The Emoji scene covers the sequences above at 14/28/48 DIP, mixed Chinese/English text, and a repeating 1→2→1 scale animation. Resize and scroll as needed. Selftest windows close themselves after about six seconds.

## 彩色开/关对照 / Color comparison

```powershell
./artifacts/package-x64/WpfD3D11Demo.exe --emoji
./artifacts/package-x64/WpfD3D11Demo.exe --emoji --color=off
# --emoji --color=on is equivalent to --emoji.
```

`Main` 在进入禁止内联的 WPF 启动方法前调用 `Environment.SetEnvironmentVariable("QUICKER_WPF_COLOR_GLYPHS", "1")`；关闭方式设为 `"0"`。入口以原生 `GetEnvironmentVariableW` 复核，并确认设置前后 wpfgfx 均未加载。原生代码在首次 `DrawGlyphRun` 的 `Enabled()` 中读取并缓存进程环境，因此本设置先于读取。需要分别启动新进程；默认非 Emoji 页面设为关闭。

`Main` sets the process environment before entering the non-inlined WPF startup method, verifies the native environment, and verifies wpfgfx has not loaded. Native `Enabled()` reads and caches it on the first glyph draw. Start a new process for each comparison; the default scene disables color.

运行时仍默认关闭彩色；`--emoji` 显式开启它。以下可选开关也须在启动前确定：

| 开关 / Switch | 说明 / Meaning |
| --- | --- |
| `QUICKER_WPF_COLOR_GLYPHS_PREWARM=0` | 关闭彩色默认开启的后台预热 / Disable background prewarming |
| `QUICKER_WPF_COLOR_GLYPHS_DISK_CACHE` | 设为应用选择的已有目录才开启；运行时不建目录 / Optional disk cache requiring an existing application-selected directory |
| `QUICKER_WPF_RT_TIMING_LOG` | 可选诊断 JSONL 路径 / Optional diagnostic JSONL path |

已验证 Segoe UI Emoji COLRv1；COLRv0、SVG、位图字体未覆盖，多适配器和设备丢失未实测。WPF shaping、字体回退与系统字体决定组合序列的呈现；国旗可能显示区域字母，不能据演示页声称支持所有旗帜。Qualcomm 采样截断差异仍存在。

Validated with Segoe UI Emoji COLRv1. COLRv0, SVG and bitmap fonts are not covered; multi-adapter/device-loss behavior is untested. WPF shaping/fallback and the system font determine sequence rendering; Windows 自带的 Segoe UI Emoji 不含国旗，因此示例去掉国旗并保留彩虹旗。Windows Segoe UI Emoji does not include national flags, so this demo omits them and retains the rainbow flag. Qualcomm sampling truncation remains.

## 自动验证 / Selftest

自动窗口不激活、不拦截鼠标，先缩小再恢复，保存完整窗口截图和 JSON。Emoji 另保存白底 Emoji 区及纯文字区截图和像素哈希。先跑关闭，再跑开启：

Run off before on. Each selftest saves a complete screenshot and JSON; Emoji tests also save region images and raw pixel hashes.

```powershell
$exe = "$PWD/artifacts/package-x64/WpfD3D11Demo.exe"
Start-Process $exe -WindowStyle Hidden -Wait -ArgumentList '--selftest artifacts/original.json'
Start-Process $exe -WindowStyle Hidden -Wait -ArgumentList '--emoji --color=off --selftest artifacts/off.json'
Start-Process $exe -WindowStyle Hidden -Wait -ArgumentList '--emoji --color=on --selftest artifacts/on.json --baseline artifacts/off.json'
```

成功（退出码 0）要求：wpfgfx `10.1.0.4` 与 PresentationCore 位于应用目录、D3D11 已加载、无 D3D9、Tier 2、帧数大于 10、窗口尺寸恢复、Demo 未成为前台。Emoji 关闭时白底区彩色像素为 0，开启时大于 100；纯文字区域与关闭时逐字节一致，缩放采样达到 1 和 2 附近。开启模式的 selftest 必须传 `--baseline`。失败为 1，参数错误为 2，提前关闭为 3。

Exit 0 requires the private modules, D3D11 without D3D9, Tier 2, frames, restored size and no foreground activation. Emoji assertions check color presence/absence, exact text pixels against the off baseline, and scale extrema. An on selftest requires `--baseline`. Other exit codes: failure 1, invalid arguments 2, premature close 3.

`screenshotMethod` 区分 DesktopBitBlt、PrintWindow、WpfVisualFallback。Visual 回退不证明桌面出光；纯文字比较使用完整截图内的同一白底区域，内缩 2 个物理像素以排除分数坐标边缘与后方动态渐变的混合，全部文字仍在区域内。请检查完整截图及 JSON，不能只看裁剪区域。

The screenshot method is reported explicitly. A visual fallback is not proof of desktop scanout; inspect the full screenshot alongside JSON and the additional region images.

## 本地候选包源 / Local candidate feed

将候选 nupkg 放进项目目录下自建的 `feed` 文件夹，把下列内容保存为 `nuget.local.config`，publish 改用 `--configfile ./nuget.local.config`。默认配置仅访问 nuget.org；本地覆盖将本包精确映射到本地源，Microsoft 包仅映射到 nuget.org，不使用其它源。每次新候选换新的缓存、artifacts 和输出目录。

Place the candidate nupkg in an application-created `feed` directory and save this configuration as `nuget.local.config`. Restore with fresh caches and output directories. This package maps only to the local feed; Microsoft packages map only to nuget.org.

```xml
<configuration>
  <packageSources>
    <clear />
    <add key="wpf-runtime" value="./feed" />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
  <packageSourceMapping>
    <clear />
    <packageSource key="wpf-runtime"><package pattern="WpfD3D11Runtime" /></packageSource>
    <packageSource key="nuget.org"><package pattern="Microsoft.*" /></packageSource>
  </packageSourceMapping>
  <fallbackPackageFolders><clear /></fallbackPackageFolders>
</configuration>
```

## 官方对照与范围 / Official comparison and scope

用独立输出 publish，增加 `-p:DemoUsePackage=false` 即可运行同源码官方 FDD 对照；默认 selftest 检查共享框架路径。官方 WPF 不支持此私有彩色开关，不能用彩色开启断言验收官方版。

包支持 .NET 10 WPF、x64/ARM64、FDD/SC。新 wpfgfx 为 `10.1.0.4`，其它文件保留 .3 字节和版本（自建文件 `10.1.0.3`）。文件锁定 10.0.14，共享框架补丁不会自动更新。不支持 x86、裁剪、单文件、AOT、消费端 R2R。环境变量不能切回官方后端；回退需移除包引用，清理并重新构建/部署后重启。

For official FDD comparison, use separate output with `-p:DemoUsePackage=false`. The private color switch does not enable color in official WPF. Supported scope: .NET 10 WPF, x64/ARM64, FDD/SC. Shared-framework servicing does not update bundled files. x86, trimming, single-file, AOT and consumer R2R are unsupported. Remove the package, clean and redeploy to return to the official backend.
