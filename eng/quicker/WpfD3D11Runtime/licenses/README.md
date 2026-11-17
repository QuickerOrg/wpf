# 本地许可证据与待确认清单

本文件只整理本机可见来源，不确认任何二进制已获独立 NuGet 再分发资格；许可待确认前不得公开推送。

- 根 `LICENSE.txt`：WPF 原始 MIT 文本，按原字节保留。
- 根 `THIRD-PARTY-NOTICES.txt`：本包分发边界与公开阻塞。
- `UPSTREAM-THIRD-PARTY-NOTICES.txt`：WPF 上游 notices，按原字节保留。
- `DNCENG-LICENSE.txt` / `DNCENG-THIRD-PARTY-NOTICES.txt`：PresentationNative 的实际上游缓存包文本，按原字节保留。
- `WINDOWSDESKTOP-LICENSE.txt`：x64 compiler 取件的官方缓存包许可，不能据此替代逐二进制授权确认。
- 每 RID manifest：逐文件来源、版本、哈希、签名真实状态；许可文件不能替代这些来源实证。

| 二进制（x64/ARM64，除注明外） | 本机可见来源/许可文件 | 待确认 |
|---|---|---|
| PresentationNative_cor3.dll | `F:/WpfP0/packages/runtime.win-{x64,arm64}.microsoft.dotnet.wpf.dnceng/10.0.7-servicing.26452.2/{LICENSE.TXT,THIRD-PARTY-NOTICES.TXT}`；WPF `redist/PresentationNative/PresentationNative.vcxproj` | 包级文本对本批私有化名称、独立通用 NuGet 包和下游再分发的覆盖；真实原始签名状态 |
| vcruntime140_cor3.dll | VS `VC/Redist/MSVC/14.44.35112/{x64,arm64}/Microsoft.VC143.CRT/vcruntime140.dll`；WPF `redist/VCRuntime/VCRuntime.vcxproj` | 具体 VS/VC redist 许可及 redist 清单、本批改名/私有化与独立包分发条件；本机指定 VC/Redist 范围未见独立许可文本，不能视为授权或禁止结论 |
| D3DCompiler_47_cor3.dll（仅 x64） | `%USERPROFILE%/.nuget/packages/microsoft.windowsdesktop.app.runtime.win-x64/10.0.11/LICENSE`；`C:/Program Files (x86)/Windows Kits/10/Licenses/10.0.26100.0/sdk_license.rtf`；WPF `redist/D3DCompiler/D3DCompiler.vcxproj` | SDK Distributable Code/DIST 清单是否覆盖本批改名二进制、修改与独立 NuGet 分发；下游分发要求及不得暗示微软背书 |
| D3DCompiler_47.dll（ARM64 OS） | Windows 系统前置条件 | 不随包再分发、不从 System32 复制；仅记录前置条件 |

以下每个 fork 自建文件分别关联 WPF MIT 与上游 notices；仍需核对包含的第三方部分、版权通知、修改说明和分发形式。逐个列出，不能把包级 MIT 自动推断为全部 binary 权利闭环。

| fork 自建二进制 | 许可/来源文本 | 待确认 |
|---|---|---|
| DirectWriteForwarder.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationCore.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework-SystemCore.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework-SystemData.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework-SystemDrawing.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework-SystemXml.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework-SystemXmlLinq.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.Aero.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.Aero2.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.AeroLite.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.Classic.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.Fluent.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.Luna.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationFramework.Royale.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PresentationUI.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| ReachFramework.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| System.Printing.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| System.Windows.Controls.Ribbon.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| System.Windows.Input.Manipulations.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| System.Windows.Presentation.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| System.Windows.Primitives.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| System.Xaml.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| UIAutomationClient.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| UIAutomationClientSideProviders.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| UIAutomationProvider.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| UIAutomationTypes.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| WindowsBase.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| WindowsFormsIntegration.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| wpfgfx_cor3.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
| PenImc_cor3.dll | LICENSE.txt + UPSTREAM-THIRD-PARTY-NOTICES.txt；fork/VMR 来源见 manifest | 第三方组成及 notice/修改/再分发条件逐项复核 |
