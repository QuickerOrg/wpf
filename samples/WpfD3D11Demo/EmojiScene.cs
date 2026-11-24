using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;

/// <summary>固定白底黑字区域用于验证颜色；纯文字区域独立于动画、标题与诊断。</summary>
internal sealed class EmojiScene
{
    private const string Samples = "😀 👍🏽 👩‍❤️‍👨 🧑🏽‍💻 👨‍👩‍👧‍👦 🏳️‍🌈";
    private readonly bool color;
    private readonly Border emojiArea;
    private readonly Border textArea;
    private readonly ScaleTransform scale = new(1, 1);
    private double minimum = double.MaxValue, maximum;
    private long samples;
    internal UIElement View { get; }

    internal EmojiScene(bool color)
    {
        this.color = color;
        var rows = new StackPanel();
        rows.Children.Add(Text("Emoji / 单码点 · 肤色 · ZWJ · 旗帜（字体支持范围内）", 17, false));
        foreach (var size in new[] { 14, 28, 48 })
        {
            var row = new Grid { Height = size + 16 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(65) });
            row.ColumnDefinitions.Add(new ColumnDefinition());
            row.Children.Add(Text($"{size} DIP", 13, false));
            var glyphs = Text(Samples, size, true);
            Grid.SetColumn(glyphs, 1);
            row.Children.Add(glyphs);
            rows.Children.Add(row);
        }
        rows.Children.Add(Text("中英文混排 Mixed text: 你好 😀 / Hello 🧑🏽‍💻", 22, false));
        emojiArea = new Border { Background = Brushes.White, Padding = new Thickness(8), Child = rows };
        textArea = new Border
        {
            Background = Brushes.White, Height = 48, Padding = new Thickness(8),
            Child = Text("纯文字对照 Text only: 中文 ABC abc 0123456789", 20, false)
        };
        var animated = Text("😀  🧑🏽‍💻  👩‍❤️‍👨", 24, true);
        animated.RenderTransform = scale;
        animated.RenderTransformOrigin = new Point(0, .5);
        var animation = new DoubleAnimation(1, 2, TimeSpan.FromSeconds(1.5))
        { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever };
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, animation);
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, animation);
        var panel = new StackPanel { Margin = new Thickness(0, 12, 0, 0) };
        TextOptions.SetTextRenderingMode(panel, TextRenderingMode.Grayscale);
        var status = Text(color ? "彩色已开启 / Color ON (new process)" : "彩色已关闭 / Color OFF (new process)", 16, false);
        status.Foreground = Brushes.White;
        panel.Children.Add(status);
        panel.Children.Add(emojiArea);
        panel.Children.Add(textArea);
        panel.Children.Add(new Border { Height = 72, Background = Brushes.White, Padding = new Thickness(12), Child = animated });
        View = panel;
    }

    private static TextBlock Text(string text, double size, bool emoji) => new()
    {
        Text = text, FontSize = size, Foreground = Brushes.Black,
        FontFamily = new FontFamily(emoji ? "Segoe UI Emoji" : "Segoe UI"),
        VerticalAlignment = VerticalAlignment.Center
    };

    internal void ObserveScale()
    {
        minimum = Math.Min(minimum, scale.ScaleX);
        maximum = Math.Max(maximum, scale.ScaleX);
        samples++;
    }

    internal void Stop()
    {
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, null);
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
    }

    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", SetLastError = true)] private static extern bool GetWindowRect(nint hwnd, out Rect rect);

    private static (string Hash, int Colored, int Ink, int Width, int Height) Region(
        BitmapSource image, FrameworkElement element, Rect window, string path)
    {
        var origin = element.PointToScreen(new Point());
        var dpi = VisualTreeHelper.GetDpi(element);
        // 排除白底边缘的 2 个物理像素；分数坐标边缘会混入后方动态渐变，非文字差异。
        const int inset = 2;
        var rect = new Int32Rect((int)Math.Round(origin.X - window.Left) + inset, (int)Math.Round(origin.Y - window.Top) + inset,
            (int)Math.Round(element.ActualWidth * dpi.DpiScaleX) - 2 * inset, (int)Math.Round(element.ActualHeight * dpi.DpiScaleY) - 2 * inset);
        if (rect.X < 0 || rect.Y < 0 || rect.X + rect.Width > image.PixelWidth || rect.Y + rect.Height > image.PixelHeight)
            throw new InvalidOperationException("Complete Emoji/text region must be visible in the window screenshot.");
        var crop = new CroppedBitmap(image, rect);
        var bgra = new FormatConvertedBitmap(crop, PixelFormats.Bgra32, null, 0);
        var pixels = new byte[rect.Width * rect.Height * 4];
        bgra.CopyPixels(pixels, rect.Width * 4, 0);
        var colored = 0;
        var ink = 0;
        for (var i = 0; i < pixels.Length; i += 4)
        {
            int b = pixels[i], g = pixels[i + 1], r = pixels[i + 2];
            if (Math.Max(r, Math.Max(g, b)) - Math.Min(r, Math.Min(g, b)) > 32) colored++;
            if (Math.Min(r, Math.Min(g, b)) < 220) ink++;
        }
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(crop));
        using (var file = File.Create(path)) encoder.Save(file);
        return (Convert.ToHexString(SHA256.HashData(pixels)), colored, ink, rect.Width, rect.Height);
    }

    internal JsonObject Validate(nint hwnd, string screenshot, string? baseline)
    {
        if (!GetWindowRect(hwnd, out var rect)) throw new System.ComponentModel.Win32Exception();
        var image = BitmapFrame.Create(new Uri(screenshot), BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
        var prefix = Path.Combine(Path.GetDirectoryName(screenshot)!, Path.GetFileNameWithoutExtension(screenshot));
        var emoji = Region(image, emojiArea, rect, prefix + "-emoji.png");
        var text = Region(image, textArea, rect, prefix + "-text.png");
        var sameText = !color;
        if (color && baseline is not null)
        {
            var previous = JsonNode.Parse(File.ReadAllText(baseline))!.AsObject();
            var prior = previous["emoji"]!.AsObject();
            sameText = previous["passed"]!.GetValue<bool>() && !prior["colorEnabled"]!.GetValue<bool>() &&
                prior["textPixelSha256"]!.GetValue<string>() == text.Hash &&
                prior["textWidth"]!.GetValue<int>() == text.Width && prior["textHeight"]!.GetValue<int>() == text.Height &&
                previous["processArchitecture"]!.GetValue<string>() == System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString();
        }
        var animationPassed = samples > 10 && minimum <= 1.15 && maximum >= 1.85;
        var colorPassed = color ? emoji.Colored > 100 : emoji.Colored == 0;
        return new JsonObject
        {
            ["colorEnabled"] = color, ["coloredPixels"] = emoji.Colored, ["inkPixels"] = emoji.Ink,
            ["regionInsetPixels"] = 2,
            ["colorAssertion"] = colorPassed, ["textPixelSha256"] = text.Hash,
            ["textWidth"] = text.Width, ["textHeight"] = text.Height,
            ["textMatchesColorOff"] = sameText, ["baseline"] = baseline,
            ["scaleMinimum"] = minimum, ["scaleMaximum"] = maximum, ["scaleSamples"] = samples,
            ["scaleAnimationPassed"] = animationPassed,
            ["passed"] = colorPassed && sameText && animationPassed && emoji.Ink > 100 && text.Ink > 100
        };
    }
}
