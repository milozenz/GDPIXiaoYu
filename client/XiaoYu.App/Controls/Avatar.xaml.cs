using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using XiaoYu.App.Services;

namespace XiaoYu.App.Controls;

/// <summary>圆形头像：有头像图片就显示图片，没有就显示昵称的第一个字</summary>
public partial class Avatar : UserControl
{
    private static readonly Brush DefaultFill = new SolidColorBrush(Color.FromRgb(0xFF, 0xE9, 0x8A));

    public static readonly DependencyProperty ImageProperty = DependencyProperty.Register(
        nameof(Image), typeof(string), typeof(Avatar), new PropertyMetadata(null, (d, _) => ((Avatar)d).LoadImage()));

    public static readonly DependencyProperty NicknameProperty = DependencyProperty.Register(
        nameof(Nickname), typeof(string), typeof(Avatar), new PropertyMetadata(null, (d, _) => ((Avatar)d).UpdateInitial()));

    public static readonly DependencyProperty SizeProperty = DependencyProperty.Register(
        nameof(Size), typeof(double), typeof(Avatar), new PropertyMetadata(32.0, (d, _) => ((Avatar)d).UpdateSize()));

    /// <summary>头像图片名，空字符串表示没有头像</summary>
    public string? Image
    {
        get => (string?)GetValue(ImageProperty);
        set => SetValue(ImageProperty, value);
    }

    public string? Nickname
    {
        get => (string?)GetValue(NicknameProperty);
        set => SetValue(NicknameProperty, value);
    }

    /// <summary>直径（像素）</summary>
    public double Size
    {
        get => (double)GetValue(SizeProperty);
        set => SetValue(SizeProperty, value);
    }

    public Avatar()
    {
        InitializeComponent();
        UpdateSize();
    }

    private void UpdateSize()
    {
        Box.Width = Box.Height = Size;
        Initial.FontSize = Math.Max(8, Size * 0.45);
    }

    private void UpdateInitial()
    {
        var name = Nickname ?? "";
        Initial.Text = name.Length > 0 ? char.ConvertFromUtf32(char.ConvertToUtf32(name, 0)) : "";
    }

    private async void LoadImage()
    {
        var name = Image;
        Circle.Fill = DefaultFill;
        Initial.Visibility = Visibility.Visible;
        if (string.IsNullOrEmpty(name)) return;

        var bmp = await ImageCache.GetAsync(name);
        if (bmp == null || Image != name) return;
        Circle.Fill = new ImageBrush(bmp) { Stretch = Stretch.UniformToFill };
        Initial.Visibility = Visibility.Collapsed;
    }
}
