using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;

namespace XiaoYu.App.Services;

/// <summary>
/// 密码框的"小眼睛"：按下去显示明文，再按一次隐藏。
/// 密码始终以 PasswordBox 为准：显示明文时在旁边的输入框里打字，会同步写回 PasswordBox，
/// 所以原来读 PasswordBox.Password、监听 PasswordChanged 的代码都不用改。
/// 界面在 Theme.xaml 的 BarePassword 样式里（PART_Eye、PART_Plain），样式里已经打开了 Enabled。
/// </summary>
public static class PasswordReveal
{
    public static readonly DependencyProperty EnabledProperty = DependencyProperty.RegisterAttached(
        "Enabled", typeof(bool), typeof(PasswordReveal), new PropertyMetadata(false, OnEnabledChanged));

    public static bool GetEnabled(DependencyObject o) => (bool)o.GetValue(EnabledProperty);
    public static void SetEnabled(DependencyObject o, bool value) => o.SetValue(EnabledProperty, value);

    private static void OnEnabledChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not PasswordBox box || e.NewValue is not true) return;
        // 模板要等控件加载后才有；窗口重新加载也只挂一次
        box.Loaded += (_, _) => Hook(box);
        if (box.IsLoaded) Hook(box);
    }

    private static void Hook(PasswordBox box)
    {
        box.ApplyTemplate();
        if (box.Template?.FindName("PART_Eye", box) is not ToggleButton eye ||
            box.Template.FindName("PART_Plain", box) is not TextBox plain ||
            box.Template.FindName("PART_ContentHost", box) is not FrameworkElement host) return;
        if (eye.Tag is "hooked") return;
        eye.Tag = "hooked";
        plain.MaxLength = box.MaxLength;
        bool syncing = false;

        eye.Checked += (_, _) =>
        {
            syncing = true; plain.Text = box.Password; syncing = false;
            host.Visibility = Visibility.Hidden;
            plain.Visibility = Visibility.Visible;
            eye.ToolTip = "隐藏密码";
            plain.Focus();
            plain.CaretIndex = plain.Text.Length;
        };
        eye.Unchecked += (_, _) =>
        {
            plain.Visibility = Visibility.Collapsed;
            host.Visibility = Visibility.Visible;
            eye.ToolTip = "显示密码";
            syncing = true; plain.Text = ""; syncing = false;   // 不留明文
            box.Focus();
        };
        // 明文框里打字 → 写回密码框（会触发 PasswordChanged，界面逻辑照常工作）
        plain.TextChanged += (_, _) =>
        {
            if (syncing || eye.IsChecked != true || plain.Text == box.Password) return;
            syncing = true; box.Password = plain.Text; syncing = false;
        };
        // 代码清空或改了密码（比如重置成功后 Clear）→ 明文框跟着变
        box.PasswordChanged += (_, _) =>
        {
            if (syncing || eye.IsChecked != true || plain.Text == box.Password) return;
            syncing = true; plain.Text = box.Password; plain.CaretIndex = plain.Text.Length; syncing = false;
        };
    }
}
