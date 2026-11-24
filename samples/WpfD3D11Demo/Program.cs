using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

internal static class Program
{
#if DEMO_PACKAGE
    private const bool ExpectPrivate = true;
#else
    private const bool ExpectPrivate = false;
#endif
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    [STAThread]
    private static int Main(string[] args)
    {
        string? output = null;
        var emoji = false;
        bool? color = null;
        string? baseline = null;
        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--emoji": emoji = true; break;
                case "--color=on": color = true; break;
                case "--color=off": color = false; break;
                case "--selftest" when i + 1 < args.Length: output = Path.GetFullPath(args[++i]); break;
                case "--baseline" when i + 1 < args.Length: baseline = Path.GetFullPath(args[++i]); break;
                default: return 2;
            }
        }
        if (!emoji && (color.HasValue || baseline is not null)) return 2;
        var enabled = emoji && (color ?? true);
        if (output is not null && enabled && baseline is null) return 2;
        if (output is not null) Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        try
        {
            // Main 不触及 WPF；NoInlining 保证 JIT 不提前展开创建 Application 的方法。
            var loadedBefore = GetModuleHandle("wpfgfx_cor3.dll") != 0;
            Environment.SetEnvironmentVariable("QUICKER_WPF_COLOR_GLYPHS", enabled ? "1" : "0");
            var nativeBuffer = new char[8];
            var nativeLength = GetEnvironmentVariable("QUICKER_WPF_COLOR_GLYPHS", nativeBuffer, nativeBuffer.Length);
            var nativeValue = nativeLength < nativeBuffer.Length ? new string(nativeBuffer, 0, (int)nativeLength) : "";
            var bootstrap = new JsonObject
            {
                ["wpfgfxLoadedBeforeSetting"] = loadedBefore,
                ["wpfgfxLoadedAfterSetting"] = GetModuleHandle("wpfgfx_cor3.dll") != 0,
                ["nativeEnvironmentValue"] = nativeValue,
                ["setBeforeWpf"] = !loadedBefore && GetModuleHandle("wpfgfx_cor3.dll") == 0
            };
            if (!bootstrap["setBeforeWpf"]!.GetValue<bool>() || nativeValue != (enabled ? "1" : "0"))
                throw new InvalidOperationException("Color switch must reach the process environment before WPF loads.");
            return Run(output, emoji, enabled, baseline, bootstrap);
        }
        catch (Exception error)
        {
            if (output is not null)
                File.WriteAllText(output, new JsonObject
                {
                    ["schema"] = 1, ["passed"] = false, ["error"] = error.ToString()
                }.ToJsonString(Json));
            return 1;
        }
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetModuleHandleW")]
    private static extern nint GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetEnvironmentVariableW")]
    private static extern uint GetEnvironmentVariable(string name, [Out] char[] value, int size);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run(string? output, bool emoji, bool color, string? baseline, JsonObject bootstrap)
    {
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        var diagnosticText = new TextBlock
        {
            FontFamily = new FontFamily("Consolas"), FontSize = 13,
            Foreground = Brushes.White, TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 16, 0, 0)
        };
        var gradient = new LinearGradientBrush(Colors.MidnightBlue, Colors.Teal, 25);
        gradient.GradientStops[1].BeginAnimation(GradientStop.ColorProperty,
            new ColorAnimation(Colors.Teal, Colors.MediumPurple, TimeSpan.FromSeconds(3))
            { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever });
        var rotation = new RotateTransform();
        rotation.BeginAnimation(RotateTransform.AngleProperty,
            new DoubleAnimation(0, 360, TimeSpan.FromSeconds(5)) { RepeatBehavior = RepeatBehavior.Forever });
        var card = new Border
        {
            Width = 230, Height = 100, CornerRadius = new CornerRadius(18),
            Background = new SolidColorBrush(Color.FromArgb(180, 90, 180, 255)),
            RenderTransformOrigin = new Point(.5, .5), RenderTransform = rotation,
            Effect = new DropShadowEffect { BlurRadius = 24, ShadowDepth = 12, Opacity = .7 },
            Child = new TextBlock
            {
                Text = "WPF / D3D11\n渐变 · 旋转 · 阴影 · 半透明", FontSize = 18,
                Foreground = Brushes.White, TextAlignment = TextAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            }
        };
        var stage = new Grid { Height = 215, ClipToBounds = true };
        stage.Children.Add(card);
        var content = new StackPanel { Margin = new Thickness(24) };
        content.Children.Add(new TextBlock
        {
            Text = ExpectPrivate ? "WpfD3D11Runtime — 一条引用替换运行时" : "官方 WPF — 同源码对照",
            FontSize = 25, Foreground = Brushes.White, TextWrapping = TextWrapping.Wrap
        });
        var emojiScene = emoji ? new EmojiScene(color) : null;
        content.Children.Add(emojiScene?.View ?? (UIElement)stage);
        content.Children.Add(diagnosticText);
        var window = new Window
        {
            Title = "WpfD3D11Demo", Width = 920, Height = 690, MinWidth = 600, MinHeight = 440,
            Left = 24, Top = 24, Background = gradient,
            Content = new ScrollViewer { Content = content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto },
            ShowActivated = false, ShowInTaskbar = false, Topmost = output is not null
        };
        nint hwnd = 0;
        window.SourceInitialized += (_, _) =>
        {
            hwnd = new WindowInteropHelper(window).Handle;
            if (output is not null)
            {
                // 自动验证只显示自己的置顶窗口，不激活、不拦截鼠标输入。
                Native.SetWindowLongPtr(hwnd, -20, Native.GetWindowLongPtr(hwnd, -20) | 0x08000020);
            }
        };
        var watch = Stopwatch.StartNew();
        long frames = 0;
        var everForeground = false;
        EventHandler rendering = (_, _) =>
        {
            frames++;
            emojiScene?.ObserveScale();
            if (hwnd != 0 && Native.GetForegroundWindow() == hwnd) everForeground = true;
        };
        CompositionTarget.Rendering += rendering;
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        var resized = false;
        var restored = false;
        var completed = false;
        var exitCode = output is null ? 0 : 3;
        timer.Tick += (_, _) =>
        {
            try
            {
                var diagnostics = Diagnostics();
                diagnosticText.Text = Display(diagnostics, frames);
                if (output is null) return;
                if (!resized && watch.Elapsed.TotalSeconds >= 2)
                {
                    window.Width = 800; window.Height = 580; resized = true;
                }
                if (!restored && watch.Elapsed.TotalSeconds >= 3)
                {
                    window.Width = 920; window.Height = 690; restored = true;
                }
                if (watch.Elapsed.TotalSeconds < 6) return;
                timer.Stop();
                var screenshot = Path.ChangeExtension(output, ".png");
                var dimensions = Native.Capture(window, hwnd, screenshot);
                // 截图完成后再取模块，避免把首次呈现前快照当作最终结果。
                diagnostics = Diagnostics();
                var privateBackend = diagnostics["privateBackendSatisfied"]!.GetValue<bool>();
                var officialBackend = diagnostics["officialBackendSatisfied"]!.GetValue<bool>();
                var emojiFacts = emojiScene?.Validate(hwnd, screenshot, baseline);
                var passed = (ExpectPrivate ? privateBackend : officialBackend) && frames > 10 && restored &&
                    !everForeground && (emojiFacts is null || emojiFacts["passed"]!.GetValue<bool>());
                diagnostics["everForeground"] = everForeground;
                diagnostics["bootstrap"] = bootstrap;
                diagnostics["emoji"] = emojiFacts;
                diagnostics["passed"] = passed;
                diagnostics["frames"] = frames;
                diagnostics["elapsedSeconds"] = watch.Elapsed.TotalSeconds;
                diagnostics["resizeRestored"] = restored;
                diagnostics["screenshotPath"] = screenshot;
                diagnostics["screenshotWidth"] = dimensions.Width;
                diagnostics["screenshotHeight"] = dimensions.Height;
                diagnostics["screenshotMethod"] = dimensions.Method;
                File.WriteAllText(output, diagnostics.ToJsonString(Json));
                completed = true;
                exitCode = passed ? 0 : 1;
                window.Close();
            }
            catch (Exception error)
            {
                timer.Stop();
                if (output is not null)
                    File.WriteAllText(output, new JsonObject
                    {
                        ["schema"] = 1, ["passed"] = false, ["error"] = error.ToString()
                    }.ToJsonString(Json));
                exitCode = 1;
                window.Close();
            }
        };
        window.Closed += (_, _) =>
        {
            timer.Stop();
            CompositionTarget.Rendering -= rendering;
            rotation.BeginAnimation(RotateTransform.AngleProperty, null);
            emojiScene?.Stop();
            gradient.GradientStops[1].BeginAnimation(GradientStop.ColorProperty, null);
            if (output is not null && !completed && !File.Exists(output))
                File.WriteAllText(output, new JsonObject
                {
                    ["schema"] = 1, ["passed"] = false, ["error"] = "Window closed before selftest completed."
                }.ToJsonString(Json));
            app.Shutdown(exitCode);
        };
        window.Show();
        timer.Start();
        return app.Run();
    }

    private static JsonObject Diagnostics()
    {
        using var process = Process.GetCurrentProcess();
        var modules = new JsonArray();
        string? gfx = null, core = null, gfxVersion = null;
        var d3d11 = false;
        var d3d9 = false;
        foreach (ProcessModule module in process.Modules)
        {
            var name = module.ModuleName;
            if (name.Equals("d3d11.dll", StringComparison.OrdinalIgnoreCase)) d3d11 = true;
            if (name.Equals("d3d9.dll", StringComparison.OrdinalIgnoreCase)) d3d9 = true;
            if (!new[] { "wpfgfx_cor3.dll", "PresentationCore.dll", "PresentationFramework.dll", "WindowsBase.dll",
                "d3d11.dll", "d3d9.dll" }.Contains(name, StringComparer.OrdinalIgnoreCase)) continue;
            var path = module.FileName;
            var versionInfo = FileVersionInfo.GetVersionInfo(path);
            var version = $"{versionInfo.FileMajorPart}.{versionInfo.FileMinorPart}.{versionInfo.FileBuildPart}.{versionInfo.FilePrivatePart}";
            modules.Add(new JsonObject { ["name"] = name, ["path"] = path, ["fileVersion"] = version,
                ["fileVersionText"] = versionInfo.FileVersion });
            if (name.Equals("wpfgfx_cor3.dll", StringComparison.OrdinalIgnoreCase)) { gfx = path; gfxVersion = version; }
            if (name.Equals("PresentationCore.dll", StringComparison.OrdinalIgnoreCase)) core = path;
        }
        bool InApplication(string? path) => path is not null &&
            string.Equals(Path.GetDirectoryName(path), AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
        bool InSharedFramework(string? path) => path?.Contains(
            Path.Combine("shared", "Microsoft.WindowsDesktop.App") + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) == true;
        return new JsonObject
        {
            ["schema"] = 1, ["utc"] = DateTimeOffset.UtcNow.ToString("O"),
            ["processArchitecture"] = RuntimeInformation.ProcessArchitecture.ToString(),
            ["expectedBackend"] = ExpectPrivate ? "package" : "official",
            ["applicationDirectory"] = AppContext.BaseDirectory,
            ["modules"] = modules, ["d3d11Loaded"] = d3d11, ["d3d9Loaded"] = d3d9,
            ["renderCapabilityTier"] = RenderCapability.Tier >> 16,
            ["privateBackendSatisfied"] = gfxVersion == "10.1.0.4" && InApplication(gfx) && InApplication(core) && d3d11 && !d3d9 && (RenderCapability.Tier >> 16) == 2,
            ["officialBackendSatisfied"] = InSharedFramework(gfx) && InSharedFramework(core) && gfxVersion != "10.1.0.4"
        };
    }

    private static string Display(JsonObject facts, long frames)
    {
        var lines = new List<string>
        {
            $"{facts["processArchitecture"]} | Tier {facts["renderCapabilityTier"]} | Frames {frames}",
            $"d3d11.dll: {facts["d3d11Loaded"]} | d3d9.dll: {facts["d3d9Loaded"]}"
        };
        foreach (var module in facts["modules"]!.AsArray())
            if (module!["name"]!.GetValue<string>() is "wpfgfx_cor3.dll" or "PresentationCore.dll")
                lines.Add($"{module["name"]} — {module["fileVersion"]}\n{module["path"]}");
        return string.Join("\n", lines);
    }

    private static class Native
    {
        [StructLayout(LayoutKind.Sequential)] internal struct Rect { public int Left, Top, Right, Bottom; }
        [DllImport("user32.dll")] internal static extern nint GetWindowLongPtr(nint hwnd, int index);
        [DllImport("user32.dll")] internal static extern nint GetForegroundWindow();
        [DllImport("user32.dll")] internal static extern nint SetWindowLongPtr(nint hwnd, int index, nint value);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetWindowRect(nint hwnd, out Rect rect);
        [DllImport("user32.dll")] private static extern nint GetDC(nint hwnd);
        [DllImport("user32.dll")] private static extern int ReleaseDC(nint hwnd, nint dc);
        [DllImport("gdi32.dll")] private static extern nint CreateCompatibleDC(nint dc);
        [DllImport("gdi32.dll")] private static extern nint CreateCompatibleBitmap(nint dc, int width, int height);
        [DllImport("gdi32.dll")] private static extern nint SelectObject(nint dc, nint value);
        [DllImport("gdi32.dll")] private static extern bool DeleteObject(nint value);
        [DllImport("gdi32.dll")] private static extern bool DeleteDC(nint dc);
        [DllImport("gdi32.dll", SetLastError = true)] private static extern bool BitBlt(nint dc, int x, int y, int width, int height, nint source, int sx, int sy, uint operation);
        [DllImport("dwmapi.dll")] private static extern int DwmFlush();
        [DllImport("user32.dll", SetLastError = true)] private static extern bool PrintWindow(nint hwnd, nint dc, uint flags);

        private static bool HasColor(BitmapSource image)
        {
            var converted = new FormatConvertedBitmap(image, PixelFormats.Bgra32, null, 0);
            var pixels = new byte[converted.PixelWidth * converted.PixelHeight * 4];
            converted.CopyPixels(pixels, converted.PixelWidth * 4, 0);
            var colored = 0;
            for (var i = 0; i < pixels.Length; i += 64)
                if (Math.Abs(pixels[i] - pixels[i + 1]) > 8 || Math.Abs(pixels[i + 1] - pixels[i + 2]) > 8)
                    if (++colored >= 32) return true;
            return false;
        }

        internal static (int Width, int Height, string Method) Capture(Window window, nint hwnd, string path)
        {
            DwmFlush();
            if (!GetWindowRect(hwnd, out var rect)) throw new Win32Exception();
            var width = rect.Right - rect.Left;
            var height = rect.Bottom - rect.Top;
            var screen = GetDC(0);
            if (screen == 0) throw new Win32Exception();
            var memory = CreateCompatibleDC(screen);
            var bitmap = CreateCompatibleBitmap(screen, width, height);
            nint previous = 0;
            try
            {
                if (memory == 0 || bitmap == 0) throw new Win32Exception();
                previous = SelectObject(memory, bitmap);
                if (!BitBlt(memory, 0, 0, width, height, screen, rect.Left, rect.Top, 0x40CC0020)) throw new Win32Exception();
                BitmapSource image = Imaging.CreateBitmapSourceFromHBitmap(bitmap, 0, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
                var method = "DesktopBitBlt";
                if (!HasColor(image))
                {
                    method = "PrintWindow";
                    if (PrintWindow(hwnd, memory, 2))
                        image = Imaging.CreateBitmapSourceFromHBitmap(bitmap, 0, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
                    if (!HasColor(image))
                    {
                        // 锁屏/不可读桌面时，保存完整 WPF Visual，并明确标注，不能冒充桌面截图。
                        window.UpdateLayout();
                        var dpi = VisualTreeHelper.GetDpi(window);
                        var visual = new RenderTargetBitmap((int)Math.Ceiling(window.ActualWidth * dpi.DpiScaleX),
                            (int)Math.Ceiling(window.ActualHeight * dpi.DpiScaleY), dpi.PixelsPerInchX, dpi.PixelsPerInchY, PixelFormats.Pbgra32);
                        visual.Render(window);
                        image = visual;
                        method = "WpfVisualFallback";
                    }
                }
                if (!HasColor(image)) throw new InvalidOperationException("Screenshot contains no demo color content.");
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(image));
                using var file = File.Create(path);
                encoder.Save(file);
                return (image.PixelWidth, image.PixelHeight, method);
            }
            finally
            {
                if (previous != 0) SelectObject(memory, previous);
                if (bitmap != 0) DeleteObject(bitmap);
                if (memory != 0) DeleteDC(memory);
                ReleaseDC(0, screen);
            }
        }
    }
}
