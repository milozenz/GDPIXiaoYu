using System.Windows;
using System.Windows.Media;
using XiaoYu.App.Services;

namespace XiaoYu.App.Controls;

public enum DialogKind { Question, Info, Error }

public partial class ConfirmDialog : Window
{
    public ConfirmDialog(string message, string title, DialogKind kind)
    {
        InitializeComponent();
        TitleText.Text = title;
        MessageText.Text = message;
        (string glyph, string fill, string color) = kind switch
        {
            DialogKind.Error => ("", "#FFECEC", "#F5484A"),   // 感叹号
            DialogKind.Info => ("", "#E8F1FF", "#1677FF"),    // 信息
            _ => ("", "#FFFBE0", "#C99A00"),                  // 问号
        };
        IconText.Text = glyph;
        IconText.Foreground = (Brush)new BrushConverter().ConvertFromString(color)!;
        IconBox.Background = (Brush)new BrushConverter().ConvertFromString(fill)!;
        if (kind != DialogKind.Question) CancelButton.Visibility = Visibility.Collapsed;
        OkButton.Content = kind == DialogKind.Question ? "确定" : "知道了";
        Loaded += (_, _) => Motion.Pop(Card);
    }

    private void OnOk(object sender, RoutedEventArgs e) => DialogResult = true;

    private void OnCancel(object sender, RoutedEventArgs e) => DialogResult = false;
}
