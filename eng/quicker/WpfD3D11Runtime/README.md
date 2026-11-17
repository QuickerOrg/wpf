# WpfD3D11Runtime — P2 本地候选

这是 QuickerOrg 维护的 WPF D3D11 门面 fork 的运行时包，非微软官方分发或支持。
**仅供离线验证，不可公开发布。微软原生二进制再分发许可待确认，未确认前不得公开推送。**

候选版本 `10.0.14-d3d11.1`，基线来自 slice `eng/Versions.props`（10.0.14）和 VMR source-manifest。
自建 FileVersion `10.1.0.1` 是私有版本域，并不表示微软发布了 .NET 10.1；AssemblyVersion 保持 `10.0.0.0`。
InformationalVersion 为 `10.0.14+private.1.g75065d894793c167726e62dd545d2fe1e2e38e65`。
fork SHA 与 slice SHA 分开记录；本地树比对排除了新增 eng/quicker 打包目录。包 manifest 提供准确来源和每文件哈希。
28 个库由 crossgen2 预编译，DirectWriteForwarder 单列。默认签名模式为 unsigned/dryrun；本地候选不是已签名发行版。
正式发布必须在 R2R 后用明确的工具/证书签 fork 自建文件，并开启 Pack 的 `-RequireSigned`；上游文件保持原字节。

仅支持明确 `win-x64` / `win-arm64` RID 的 .NET 10 WPF，FDD 或 SC，非裁剪、非单文件、非 AOT，消费者不得二次 R2R。
编译与 XAML 编译继续使用官方 targeting pack；包无 ref、顶层 lib 或编译任务。
x64 含 compiler；ARM64 依赖 System32 的 D3DCompiler_47.dll，不随包分发 OS 文件。
FDD 需要 .NET 10 WindowsDesktop 共享框架；使用私有 WPF 后，共享补丁不会自动更新这些文件。

在应用项目中显式启用：

```xml
<PropertyGroup><RuntimeIdentifier>win-x64</RuntimeIdentifier></PropertyGroup>
<ItemGroup><PackageReference Include="WpfD3D11Runtime" Version="[10.0.14-d3d11.1]" PrivateAssets="all" /></ItemGroup>
```

消费端验证器兼容 Windows PowerShell 5.1（默认 powershell.exe，可设置 WpfD3D11RuntimePowerShell）。构建/打包入口要求 PowerShell 7。
SDK 10.0.403 的 Build/Publish 冲突项由包精确恢复；SDK 升级需重新验收。
最终 deps、PE 版本、架构与哈希不符时构建失败。licenses/WpfD3D11Runtime 仅携带许可/说明和 manifest；验证器默认不进入应用输出。诊断时可显式设置 WpfD3D11CopyVerifier=true；安装器不收此验证器。Test-PrivateWpfPayload 使用受信的外部 PackageRoot。

后端是编译时 D3D11 门面，不能用环境变量切回官方 WPF；回退需切换完整官方兼容构建并重启。
尚未验证本包下真实应用运行、MSI、升级回滚、uiAccess、完整 API、未来补丁组合、长期设备恢复及完整卫星资源。
S5s Qualcomm 特定采样差异沿用既定结论，未引入候选修复。

Pack-WpfD3D11Runtime.ps1 只接受参数指定的 overlay 目录及 manifest，核对全部 33 文件后按精确清单打包。
包内 manifest 锁每文件来源、PE Machine、AssemblyVersion/FileVersion、SHA256、静态 imports 与 R2R 输入/工具证据。
输入 app 的其它文件不会收入包；已收集 runtimes 集合必须与 manifest 完全相等。
安全维护负责人及正式发布流程在 P5 默认开启前落实；本地包不提供公共支持承诺。

构建入口 `Build-WpfD3D11Runtime.ps1` 需要全新绝对 OutputRoot。C++/CLI 辅助项目需要继承源码树 targets，因此 obj 位于独立 `artifacts-p2-<OutputRoot末段>/obj`，bin/log 保持 OutputRoot；不复用 S5q/冻结 B。
Arcade 的任务输出会覆盖全局 FileVersion；入口通过构建适配 target 在初始化后恢复私有版本，保留原生/托管生成器。逐文件实证优先于命令行属性。
许可结构与逐文件待确认事项见 `licenses/README.md`。未完成许可确认、真实签名/时间戳、uiAccess、MSI 事务和更广运行矩阵，均不能据此公开发布。
