using System.Windows.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;

namespace XiaoYu.App.Shell;

public partial class LoginViewModel : ViewModelBase
{
    // [ObservableProperty] 会自动生成 Username 属性，值变了界面自动刷新
    [ObservableProperty] private string _username = "";

    public string ModeHint => AppServices.Settings.UseFakeApi
        ? "演示模式 · alice / Ab123456"
        : $"{AppServices.Settings.Host}:{AppServices.Settings.Port}{(AppServices.Settings.UseTls ? " · 加密连接" : "")}";

    /// <summary>登录成功后触发，窗口收到后打开主界面</summary>
    public event Action? LoggedIn;

    // [RelayCommand] 会自动生成 LoginCommand，按钮的 Command 绑它。
    // 执行期间按钮会自动变灰，不会重复点击。
    // PasswordBox 的密码出于安全考虑不能直接绑定，所以把整个 PasswordBox 当参数传进来取密码。
    [RelayCommand]
    private async Task LoginAsync(PasswordBox? passwordBox)
    {
        string password = passwordBox?.Password ?? "";
        if (string.IsNullOrWhiteSpace(Username) || password.Length == 0)
        {
            ErrorMessage = "请输入用户名和密码";
            return;
        }

        await RunAsync(async () =>
        {
            if (!Api.IsConnected) await Api.ConnectAsync();
            var user = await Api.LoginAsync(Username.Trim(), password);
            AppServices.SetCurrentUser(user);
            LoggedIn?.Invoke();
        });
    }
}
