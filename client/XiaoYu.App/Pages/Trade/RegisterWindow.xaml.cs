using System.Windows;

namespace XiaoYu.App.Pages.Trade;

/// <summary>
/// 注册窗口。和登录窗口的约定：
///   注册成功后，把用户名写进 RegisteredUsername，再设置 DialogResult = true（窗口会自动关闭），
///   登录窗口会把用户名自动填好。
/// </summary>
public partial class RegisterWindow : Window
{
    private readonly RegisterViewModel _vm = new();

    /// <summary>注册成功的用户名</summary>
    public string? RegisteredUsername { get; private set; }

    public RegisterWindow()
    {
        InitializeComponent();
        _vm.Registered += username =>
        {
            RegisteredUsername = username;
            DialogResult = true;
        };
        DataContext = _vm;
        Loaded += (_, _) => UsernameBox.Focus();
    }

    // 密码框的内容不能直接绑定，输入时手动同步给 ViewModel
    private void OnPasswordChanged(object sender, RoutedEventArgs e) => _vm.Password = PasswordBox.Password;

    private void OnConfirmChanged(object sender, RoutedEventArgs e) => _vm.Confirm = ConfirmBox.Password;
}
