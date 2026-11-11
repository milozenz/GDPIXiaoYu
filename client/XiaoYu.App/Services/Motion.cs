using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace XiaoYu.App.Services;

/// <summary>
/// 动效小工具。XAML 里给元素加 m:Motion.Appear="True"，它出现时会淡入并轻轻上滑；
/// 列表里的元素会自动错开一点点时间，看起来像一个一个浮上来。
/// </summary>
public static class Motion
{
    private static readonly IEasingFunction EaseOut = new CubicEase { EasingMode = EasingMode.EaseOut };
    private static int _sequence;

    public static readonly DependencyProperty AppearProperty = DependencyProperty.RegisterAttached(
        "Appear", typeof(bool), typeof(Motion), new PropertyMetadata(false, OnAppearChanged));

    public static bool GetAppear(DependencyObject d) => (bool)d.GetValue(AppearProperty);
    public static void SetAppear(DependencyObject d, bool value) => d.SetValue(AppearProperty, value);

    private static void OnAppearChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not FrameworkElement fe || e.NewValue is not true) return;
        fe.Opacity = 0;                         // 先藏起来，Loaded 时再播放，避免闪一下
        fe.Loaded += OnLoaded;
    }

    private static void OnLoaded(object sender, RoutedEventArgs e)
    {
        var fe = (FrameworkElement)sender;
        fe.Loaded -= OnLoaded;
        Play(fe, 14, (_sequence++ % 10) * 30);
    }

    /// <summary>淡入 + 上滑 dy 像素。delayMs 是延迟。</summary>
    public static void Play(FrameworkElement fe, double dy = 14, int delayMs = 0, int durationMs = 280)
    {
        var shift = new TranslateTransform(0, dy);
        fe.RenderTransform = shift;
        var begin = TimeSpan.FromMilliseconds(delayMs);
        var duration = TimeSpan.FromMilliseconds(durationMs);
        fe.BeginAnimation(UIElement.OpacityProperty,
            new DoubleAnimation(0, 1, duration) { BeginTime = begin, EasingFunction = EaseOut, FillBehavior = FillBehavior.HoldEnd });
        shift.BeginAnimation(TranslateTransform.YProperty,
            new DoubleAnimation(dy, 0, duration) { BeginTime = begin, EasingFunction = EaseOut, FillBehavior = FillBehavior.Stop });
    }

    /// <summary>弹出：从 0.92 倍放大到 1，同时淡入（对话框、提示用）</summary>
    public static void Pop(FrameworkElement fe, int durationMs = 200)
    {
        var scale = new ScaleTransform(0.92, 0.92);
        fe.RenderTransformOrigin = new Point(0.5, 0.5);
        fe.RenderTransform = scale;
        var duration = TimeSpan.FromMilliseconds(durationMs);
        var back = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.4 };
        fe.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, duration));
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(0.92, 1, duration) { EasingFunction = back });
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, new DoubleAnimation(0.92, 1, duration) { EasingFunction = back });
    }
}
