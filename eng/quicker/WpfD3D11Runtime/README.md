# WpfD3D11Runtime — P1 本地预览

这是 QuickerOrg 维护的 WPF D3D11 门面 fork 的运行时包，非微软官方分发或支持。
**仅供离线验证，不可公开发布。微软原生二进制再分发许可待确认，未确认前不得公开推送。**

本轮固定版本 `10.0.14-d3d11.0.local.1`，复用 S5q 已验收 R2R 文件，无源码重编。
版本标签不是整包官方服务基线的证明；自建 FileVersion 仍为开发值 42.42.42.42424。
正式 10.1.E.R 版本、真实源码基线、签名、许可证和首个公开 d3d11.1 预览由 P2 完成。

仅支持明确 `win-x64` / `win-arm64` RID 的 .NET 10 WPF，FDD 或 SC，非裁剪、非单文件、非 AOT，消费者不得二次 R2R。
编译与 XAML 编译继续使用官方 targeting pack；包无 ref、顶层 lib 或编译任务。
x64 含 compiler；ARM64 依赖 System32 的 D3DCompiler_47.dll，不随包分发 OS 文件。
FDD 需要 .NET 10 WindowsDesktop 共享框架；使用私有 WPF 后，共享补丁不会自动更新这些文件。

在应用项目中显式启用：

```xml
<PropertyGroup><RuntimeIdentifier>win-x64</RuntimeIdentifier></PropertyGroup>
<ItemGroup><PackageReference Include="WpfD3D11Runtime" Version="[10.0.14-d3d11.0.local.1]" PrivateAssets="all" /></ItemGroup>
```

验证器要求 PowerShell 7（默认 pwsh，可设置 WpfD3D11RuntimePowerShell 为可执行文件路径）。
SDK 10.0.403 的 Build/Publish 冲突项由包精确恢复；SDK 升级需重新验收。
最终 deps、PE 版本、架构与哈希不符时构建失败。licenses/WpfD3D11Runtime 带离线复核脚本和 manifest。

后端是编译时 D3D11 门面，不能用环境变量切回官方 WPF；回退需切换完整官方兼容构建并重启。
尚未验证本包下真实应用运行、MSI、升级回滚、uiAccess、完整 API、未来补丁组合、长期设备恢复及完整卫星资源。
S5s Qualcomm 特定采样差异沿用既定结论，未引入候选修复。

Pack-WpfD3D11Runtime.ps1 只接受参数指定的 overlay 目录及 manifest，核对全部 33 文件后按精确清单打包。
包内 manifest 锁每文件来源、PE Machine、AssemblyVersion/FileVersion、SHA256、静态 imports 与继承 R2R 证据。
输入 app 的其它文件不会收入包；已收集 runtimes 集合必须与 manifest 完全相等。
安全维护负责人及正式发布流程在 P5 默认开启前落实；本地包不提供公共支持承诺。
