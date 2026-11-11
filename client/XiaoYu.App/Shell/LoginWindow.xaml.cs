using System.Windows;
using XiaoYu.App.Pages.Trade;

namespace XiaoYu.App.Shell;

public partial class LoginWindow : Window
{
    private readonly LoginViewModel _vm = new();

    public LoginWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        _vm.LoggedIn += OnLoggedIn;
        Loaded += (_, _) => UsernameBox.Focus();
    }

    private void OnLoggedIn()
    {
        new MainWindow().Show();
        Close();
    }

    // 忘记密码：重置成功后把邮箱填进用户名框（邮箱也能登录）
    private void OnForgotClick(object sender, RoutedEventArgs e)
    {
        var window = new ResetPasswordWindow { Owner = this };
        if (window.ShowDialog() == true && !string.IsNullOrEmpty(window.ResetEmail))
        {
            _vm.Username = window.ResetEmail;
            _vm.ErrorMessage = "";
            PasswordBox.Clear();
            PasswordBox.Focus();
        }
    }

    // 打开注册窗口（组员A做）。注册成功后把用户名填回来，光标跳到密码框。
    private void OnRegisterClick(object sender, RoutedEventArgs e)
    {
        var window = new RegisterWindow { Owner = this };
        if (window.ShowDialog() == true && !string.IsNullOrEmpty(window.RegisteredUsername))
        {
            _vm.Username = window.RegisteredUsername;
            PasswordBox.Focus();
        }
    }
}
