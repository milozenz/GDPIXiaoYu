using System.Windows;
using System.Windows.Controls;

namespace XiaoYu.App.Controls;

public partial class EmptyState : UserControl
{
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(
        nameof(Glyph), typeof(string), typeof(EmptyState), new PropertyMetadata("", (d, e) => ((EmptyState)d).IconText.Text = (string)e.NewValue));

    public static readonly DependencyProperty TextProperty = DependencyProperty.Register(
        nameof(Text), typeof(string), typeof(EmptyState), new PropertyMetadata("", (d, e) => ((EmptyState)d).LabelText.Text = (string)e.NewValue));

    public string Glyph { get => (string)GetValue(GlyphProperty); set => SetValue(GlyphProperty, value); }
    public string Text { get => (string)GetValue(TextProperty); set => SetValue(TextProperty, value); }

    public EmptyState() => InitializeComponent();
}
