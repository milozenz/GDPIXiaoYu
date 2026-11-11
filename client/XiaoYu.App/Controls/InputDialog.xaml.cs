using System.Windows;
using System.Windows.Controls;
using XiaoYu.App.Services;

namespace XiaoYu.App.Controls;

/// <summary>输入对话框。用 Ui.Prompt(...) 打开，不要直接 new。</summary>
public partial class InputDialog : Window
{
    private readonly int _maxChars;
    private readonly List<RadioButton> _choices = new();

    /// <summary>选中的选项下标（没有选项时是 -1）</summary>
    public int ChoiceIndex { get; private set; } = -1;

    /// <summary>输入的文字（去掉首尾空白）</summary>
    public string Text { get; private set; } = "";

    public InputDialog(string title, string message, string placeholder, string[]? choices, int maxChars, string okText)
    {
        InitializeComponent();
        _maxChars = maxChars;
        TitleText.Text = title;
        MessageText.Text = message;
        MessageText.Visibility = string.IsNullOrEmpty(message) ? Visibility.Collapsed : Visibility.Visible;
        InputBox.Tag = placeholder;
        InputBox.MaxLength = maxChars * 2;  // 按 UTF-16 算，留够余量；真正的字数在提交时检查
        OkButton.Content = okText;
        if (choices is { Length: > 0 })
        {
            foreach (var c in choices)
            {
                var rb = new RadioButton
                {
                    Content = c, GroupName = "choices", Margin = new Thickness(0, 0, 8, 8),
                    Style = (Style)FindResource("PillTab"),
                };
                _choices.Add(rb);
                ChoicePanel.Children.Add(rb);
            }
        }
        else ChoicePanel.Visibility = Visibility.Collapsed;
        Loaded += (_, _) => { Motion.Pop(Card); InputBox.Focus(); };
    }

    private void OnOk(object sender, RoutedEventArgs e)
    {
        ChoiceIndex = _choices.FindIndex(c => c.IsChecked == true);
        Text = InputBox.Text.Trim();
        if (_choices.Count > 0 && ChoiceIndex < 0) { ErrorLine.Text = "请先选一个类型"; return; }
        int length = Text.EnumerateRunes().Count();
        if (length < 1) { ErrorLine.Text = "请填写内容"; return; }
        if (length > _maxChars) { ErrorLine.Text = $"最多 {_maxChars} 个字"; return; }
        DialogResult = true;
    }

    private void OnCancel(object sender, RoutedEventArgs e) => DialogResult = false;
}
