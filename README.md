# WPF on Direct3D 11 — feasibility study (QuickerOrg fork)

**本仓库的主要目的：验证 WPF 能否不经过 D3D9、直接运行在 D3D11 上。** 也就是在保留 WPF 全部上层渲染算法和应用 API 的前提下，只替换底层图形设备，看画质、兼容性、内存和 CPU 能否达到可用水平。

*Main purpose: to verify whether WPF can run directly on Direct3D 11 instead of Direct3D 9, keeping WPF's rendering algorithms and application APIs unchanged.*

> 这是 [dotnet/wpf](https://github.com/dotnet/wpf) 的第三方 fork，由 [Quicker](https://getquicker.net) 团队维护，**不是微软官方产品，也未获微软背书**。fork 改动在 [`quicker/d3d11-facade`](https://github.com/QuickerOrg/wpf/tree/quicker/d3d11-facade) 分支，编译好的运行时以 NuGet 包 [WpfD3D11Runtime](https://www.nuget.org/packages/WpfD3D11Runtime) 发布。下方分隔线之后是上游原始 README。
>
> English summary below.

## 背景：为什么要验证这件事

WPF 的硬件渲染层（milcore / `wpfgfx`）至今仍建立在 Direct3D 9 之上。在 Windows on Arm 设备（如骁龙 X / Adreno X1-85）上，D3D9 没有原生驱动，实际路径是 **WPF → D3D9 → D3D9On12 转换层 → D3D12 → 驱动**。Quicker 在 ARM64 机器上的诊断发现：

- 一个带动画、阴影的半透明弹出窗口（Quicker 的轮盘菜单）在 GPU 模式下进程内存可达约 460 MiB，切换到软件渲染只有约 150–200 MiB。多出的部分主要来自 D3D9On12 转换层和驱动内部的分配，应用层的缓存和资源释放都消除不了。
- 关闭 GPU 加速（软件渲染）虽然省内存，但会牺牲动画流畅度，而且要逐个窗口取舍。

## 要回答的问题

1. **能不能跑**：只替换设备层、不重写渲染器，WPF 能否在进程内完全不加载 D3D9 / D3D9On12 的情况下，覆盖真实应用的全部窗口（普通窗口、透明分层窗口、文字、效果、视频、设备丢失）？
2. **画质是否一致**：几何栅格化、文字（灰度/ClearType）、画刷、模糊和阴影效果、BitmapCache 等沿用 WPF 原算法后，与原 D3D9 路径的像素差能否控制在很小范围内？
3. **对应用是否透明**：应用代码一行不改，只换运行时，能否通过一条 NuGet 引用完成替换？
4. **值不值得**：内存和 CPU 与原 D3D9 路径、与微软官方 WPF 相比如何，x64 和 ARM64 各是什么情况？

结论（截至 2026-11）：前三个问题的答案都是**可行**，并已在 Quicker 的真实窗口中试用；第四个问题上内存收益明显，ARM64 上 CPU 仍略高于官方 WPF，详见下方“验证结果”。

## 怎么做的：设备门面（device facade）

不重写 WPF 渲染器。WPF 硬件渲染栈 `core/hw/` 的上层（渲染目标、栅格化、字形、效果、缓存、软件后备）原样保留，只把它底下的设备门面层（设备、渲染状态、纹理/表面包装、shader 缓存、交换链与呈现）改用 D3D11 实现：

- 着色器：原 shader 片段生成器改为输出 SM4 HLSL（`vs/ps_4_0_level_9_x`）；16 个文字 shader 由原内联汇编翻译而来。
- 呈现：普通窗口用 DXGI flip 交换链加脏区 Present；分层（透明）窗口用 UpdateLayeredWindow，保留 constant alpha 和色键。
- 视频：MediaElement 改用 Media Foundation Media Engine 加 D3D11 硬件解码。
- 健壮性：设备移除/TDR 时重建设备；同一适配器共享一个设备；所有窗口都隐藏 60 秒后统一回收一次 GPU 内存（Trim）。
- 所有改动都在编译宏 `QUICKER_WPF_D3D11_FACADE` 内；宏关闭时编译结果与原 D3D9 路径相同。

## 验证结果

| 项目 | 结果 |
| --- | --- |
| 画质 | Quicker 全窗口巡检 186 个场景，与原 D3D9 路径逐像素对比：绝大多数场景像素差 >2 的数量为 0；文字最大差 2，轮盘最大差 1。已知差异：高通 GPU 的纹理采样截断造成个别像素差 1–2。 |
| 无 D3D9 | x64 和 ARM64 进程模块快照都没有 `d3d9.dll`；硬件渲染 Tier 2。 |
| 内存（ARM64，轮盘） | 与同一份源码编译的原 D3D9 路径相比：私有提交约 **−74%**，私有工作集约 −76%，GPU 提交 **225 MiB → 13 MiB**（冷进程 3×3）。预编译（R2R）包复测时私有提交约 −73%。 |
| CPU（x64） | 渲染线程 CPU，相对同一份源码编译的原 D3D9 路径：主面板约 −41%，轮盘约 −56%（三轮同向）。 |
| CPU（ARM64） | 真实轮盘每次 Present 的渲染 CPU，相对**微软官方 WPF**仍高约 16.5%；剩余差距主要在高通驱动和内核层，应用侧可做的优化已基本做完。 |
| 健壮性 | 设备移除注入、一次真实 TDR 恢复、四显示器、媒体 5 个场景、15 分钟浸泡；ARM64 上 MSI 安装、升级、降级、取消回滚、修复等事务。 |
| NuGet 包 | `demo` 程序只加一条 PackageReference：x64 和 ARM64 上都加载 10.1.0.3 私有运行时、走 D3D11、没有 D3D9；不引用该包的对照版走官方 10.0.14 + D3D9。 |

数字来自 Quicker 内部的分阶段实验，样本量有限，会话间波动约 8–12%，跨轮比较只看量级。

## 使用

三步上手（[可运行 demo 与说明](https://github.com/QuickerOrg/WpfD3D11Runtime-Samples)，示例已移至独立仓库）：

1. 在 Windows x64 / ARM64 安装 .NET 10 SDK；框架依赖运行还需相应 Windows Desktop Runtime。
2. 克隆 [QuickerOrg/WpfD3D11Runtime-Samples](https://github.com/QuickerOrg/WpfD3D11Runtime-Samples)，进入 `WpfD3D11Demo`。
3. 运行 `dotnet run -r win-x64`（ARM64 用 `win-arm64`），查看窗口中的模块路径、版本和 D3D11/D3D9 状态。

```xml
<PropertyGroup>
  <RuntimeIdentifier>win-x64</RuntimeIdentifier> <!-- 或 win-arm64 -->
</PropertyGroup>
<ItemGroup>
  <PackageReference Include="WpfD3D11Runtime" Version="[10.0.14-d3d11.3]" />
</ItemGroup>
```

- 仅支持 .NET 10 WPF（运行时锁定 10.0.14），win-x64 / win-arm64，支持框架依赖（FDD）和自包含（SC）发布。
- 不支持 x86、裁剪（trim）、单文件、AOT，以及消费端的 R2R。
- .NET 共享框架的补丁更新不会自动更新包内文件；不能用环境变量切回官方 D3D9 后端。

---

## English summary

**Why.** WPF's hardware renderer (milcore / `wpfgfx`) is still built on Direct3D 9. On Windows on Arm (for example Snapdragon X with Adreno X1-85) there is no native D3D9 driver, so WPF runs through **D3D9 → D3D9On12 → D3D12**. In [Quicker](https://getquicker.net), an animated, shadowed, semi-transparent popup reached about 460 MiB in GPU mode versus about 150–200 MiB with software rendering. Most of the difference came from allocations inside the translation layer and the driver, which application-level caching and releasing cannot remove.

**Questions this study answers.**

1. **Does it run?** Can WPF cover every window of a real application (normal and layered windows, text, effects, video, device loss) by replacing only the device layer, with no D3D9 or D3D9On12 loaded in the process?
2. **Is image quality preserved?** Do the pixels stay very close to the D3D9 path when WPF's own rasterization, text, brushes and effects are kept?
3. **Is it transparent to applications?** Can it be a drop-in replacement, through one NuGet reference and no code changes?
4. **Is it worth it?** How do memory and CPU compare, on x64 and ARM64, with both the D3D9 path and the official Microsoft WPF?

As of 2026-11, the answer to the first three questions is **yes**, and it is used in trial builds of Quicker. On the fourth, memory savings are substantial, while render CPU on ARM64 is still somewhat higher than the official WPF.

**Approach.** A *device facade*. The whole `core/hw/` rendering stack is kept. Only the device layer beneath it is reimplemented on D3D11:

- device, render state, textures, shader cache and present;
- shader generator ported to SM4, and 16 text shaders translated from the original inline assembly;
- DXGI flip swap chains for normal windows, and UpdateLayeredWindow for layered windows;
- Media Foundation plus D3D11 for MediaElement;
- device-removed/TDR recovery.

All changes are behind `QUICKER_WPF_D3D11_FACADE`.

**Results** (Quicker's internal staged experiments; limited samples, compare magnitudes only):

| Area | Result |
| --- | --- |
| Pixels | 186-scenario window sweep against the D3D9 path: almost all scenarios have 0 pixels differing by more than 2; text differs by at most 2, the wheel popup by at most 1. Known Qualcomm sampling-truncation differences of 1–2. |
| No D3D9 | No `d3d9.dll` on x64 or ARM64; hardware Tier 2. |
| Memory (ARM64 popup) | Private commit about −74%, private working set about −76%, GPU commit 225 → 13 MiB, measured against the same-source D3D9 build. |
| CPU (x64) | Render-thread CPU: main window about −41%, popup about −56%, against the same-source D3D9 build. |
| CPU (ARM64) | Render CPU per present on the real popup is still about +16.5% versus the official Microsoft WPF; the remaining gap is in the driver and kernel layers. |
| Robustness | Device-removal injection, one real TDR recovery, four monitors, media scenarios, a 15-minute soak, and MSI install/upgrade/rollback on ARM64. |

**Use.** Set `RuntimeIdentifier` (`win-x64` or `win-arm64`) and add one `PackageReference` to [WpfD3D11Runtime](https://www.nuget.org/packages/WpfD3D11Runtime). Requirements and limits:

- .NET 10 WPF only, pinned to 10.0.14;
- framework-dependent and self-contained publishing are supported;
- not supported: x86, trimming, single-file, AOT, consumer-side R2R;
- shared-framework servicing does not update the bundled files.

Quick start ([runnable demo and instructions](https://github.com/QuickerOrg/WpfD3D11Runtime-Samples); samples now live in a separate repository):

1. Install the .NET 10 SDK on Windows x64 / ARM64; framework-dependent execution also needs the matching Windows Desktop Runtime.
2. Clone [QuickerOrg/WpfD3D11Runtime-Samples](https://github.com/QuickerOrg/WpfD3D11Runtime-Samples) and enter `WpfD3D11Demo`.
3. Run `dotnet run -r win-x64` (`win-arm64` on ARM64) and inspect the loaded module paths, versions and D3D11/D3D9 state.

---

# Windows Presentation Foundation (WPF)
[![.NET Foundation](https://img.shields.io/badge/.NET%20Foundation-blueviolet.svg)](https://www.dotnetfoundation.org/)
[![Build Status](https://dnceng.visualstudio.com/public/_apis/build/status/dotnet/wpf/dotnet-wpf%20CI)](https://dnceng.visualstudio.com/public/_build/latest?definitionId=270)
[![codecov](https://codecov.io/gh/dotnet/wpf/branch/main/graph/badge.svg?flag=production)](https://codecov.io/gh/dotnet/wpf)
[![MIT License](https://img.shields.io/badge/license-MIT-green.svg)](https://github.com/dotnet/wpf/blob/main/LICENSE.TXT)

Windows Presentation Foundation (WPF) is a UI framework for building Windows desktop applications. 

WPF supports a broad set of application development features, including an application model, resources, controls, graphics, layout, data binding and documents. WPF uses the Extensible Application Markup Language (XAML) to provide a declarative model for application programming.

WPF's rendering is vector-based, which enables applications to look great on high DPI monitors, as they can be infinitely scaled. WPF also includes a flexible hosting model, which makes it straightforward to host a video in a button, for example.

Visual Studio's designer, as well as Visual Studio Blend, make it easy to build WPF applications, with drag-and-drop and/or direct editing of XAML markup.

As of .NET 6.0, WPF supports ARM64. 

See the [WPF Roadmap](roadmap.md) to learn about project priorities, status and ship dates.

[WinForms](https://github.com/dotnet/winforms) is another UI framework for building Windows desktop applications that is supported on .NET (7.0.x/6.0.x). WPF and WinForms applications only run on Windows. They are part of the `Microsoft.NET.Sdk.WindowsDesktop` SDK. You are recommended to use the most recent version of [Visual Studio](https://visualstudio.microsoft.com/downloads/) to develop WPF and WinForms applications for .NET.  

To build the WPF repo and contribute features and fixes for .NET 8.0, [Visual Studio 2022 Preview](https://visualstudio.microsoft.com/vs/preview/) is required.

## Getting started

* [.NET 6.0 SDK](https://dotnet.microsoft.com/download/dotnet/6.0), [.NET 7.0 SDK](https://dotnet.microsoft.com/download/dotnet/7.0), [.NET 8.0 SDK](https://dotnet.microsoft.com/download/dotnet/8.0), [.NET 9.0 SDK](https://dotnet.microsoft.com/download/dotnet/9.0)
* [.NET Preview SDKs](https://github.com/dotnet/dotnet/blob/main/docs/builds-table.md)
* [Getting started instructions](Documentation/getting-started.md)
* [Contributing guide](Documentation/contributing.md)
* [Migrating .NET Framework WPF Apps to .NET Core](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/migration/)

## Status

- We are currently developing WPF for .NET 10. 

See the [WPF roadmap](roadmap.md) to learn about the schedule for specific WPF components.

Test published at [separate repo](https://github.com/dotnet/wpf-test) Tests and have limited coverage at this time. We will add more tests, however, it will be a progressive process.

The Visual Studio WPF designer is now available as part of Visual Studio 2019. 

## How to Engage, Contribute and Provide Feedback

Some of the best ways to contribute are to try things out, file bugs, join in design conversations, and fix issues.

* This repo defines [contributing guidelines](Documentation/contributing.md) and also follows the more general [.NET Core contributing guide](https://github.com/dotnet/runtime/blob/main/CONTRIBUTING.md).
* If you have a question or have found a bug, [file an issue](https://github.com/dotnet/wpf/issues/new).
* Use [daily builds](Documentation/getting-started.md#installation) if you want to contribute and stay up to date with the team.

### .NET Framework issues

Issues with .NET Framework, including WPF, should be filed on [VS developer community](https://developercommunity.visualstudio.com/spaces/61/index.html), 
or [Product Support](https://support.microsoft.com/en-us/contactus?ws=support).
They should not be filed on this repo.

## Relationship to .NET Framework

This code base is a fork of the WPF code in the .NET Framework. .NET Core 3.0 was released with a goal of WPF having parity with the .NET Framework version. Over time, the two implementations may diverge.

The [Update on .NET Core 3.0 and .NET Framework 4.8](https://devblogs.microsoft.com/dotnet/update-on-net-core-3-0-and-net-framework-4-8/) provides a good description of the forward-looking differences between .NET Core and .NET Framework.

This [update](https://devblogs.microsoft.com/dotnet/net-core-is-the-future-of-net/) states how going forward .NET Core is the future of .NET. and .NET Framework 4.8 will be the last major version of .NET Framework.


## Code of Conduct

This project uses the [.NET Foundation Code of Conduct](https://dotnetfoundation.org/code-of-conduct) to define expected conduct in our community. Instances of abusive, harassing, or otherwise unacceptable behavior may be reported by contacting a project maintainer at conduct@dotnetfoundation.org.

## Reporting security issues and security bugs

Security issues and bugs should be reported privately, via email, to the Microsoft Security Response Center (MSRC) <secure@microsoft.com>. You should receive a response within 24 hours. If for some reason you do not, please follow up via email to ensure we received your original message. Further information, including the MSRC PGP key, can be found in the [Security TechCenter](https://www.microsoft.com/msrc/faqs-report-an-issue).

Also see info about related [Microsoft .NET Core and ASP.NET Core Bug Bounty Program](https://www.microsoft.com/msrc/bounty-dot-net-core).

## License

.NET Core (including the WPF repo) is licensed under the [MIT license](LICENSE.TXT).

## .NET Foundation

.NET Core WPF is a [.NET Foundation](https://www.dotnetfoundation.org/projects) project.

See the [.NET home repo](https://github.com/Microsoft/dotnet) to find other .NET-related projects.

