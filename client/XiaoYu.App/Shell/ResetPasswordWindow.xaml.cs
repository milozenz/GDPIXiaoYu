using System.Windows;

namespace XiaoYu.App.Shell;

/// <summary>忘记密码窗口。成功后 ResetEmail 是邮箱，DialogResult = true。</summary>
public partial class ResetPasswordWindow : Window
{
    private readonly ResetPasswordViewModel _vm = new();

    public string? ResetEmail { get; private set; }

    public ResetPasswordWindow()
    {
        InitializeComponent();
        _vm.Done += email =>
        {
            ResetEmail = email;
            DialogResult = true;
        };
        DataContext = _vm;
        Loaded += (_, _) => EmailBox.Focus();
    }

    private void OnPasswordChanged(object sender, RoutedEventArgs e) => _vm.Password = PasswordBox.Password;

    private void OnConfirmChanged(object sender, RoutedEventArgs e) => _vm.Confirm = ConfirmBox.Password;
}
