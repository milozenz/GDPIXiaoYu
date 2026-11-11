using System.Text.RegularExpressions;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Shell;

/// <summary>
/// 忘记密码：填注册时的邮箱 → 获取验证码 → 填验证码和新密码。
/// 成功后所有设备上的登录都会失效，回到登录窗口用新密码登录。
/// </summary>
public partial class ResetPasswordViewModel : ViewModelBase
{
    private static readonly Regex EmailPattern = new(@"^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$", RegexOptions.Compiled);
    private readonly DispatcherTimer _countdown = new() { Interval = TimeSpan.FromSeconds(1) };
    private int _secondsLeft;

    [ObservableProperty] private string _email = "";
    [ObservableProperty] private string _code = "";
    [ObservableProperty] private string _codeButtonText = "获取验证码";
    [ObservableProperty] private bool _canSendCode = true;
    [ObservableProperty] private string _codeHint = "";

    public string Password { get; set; } = "";
    public string Confirm { get; set; } = "";

    /// <summary>重置成功，参数是邮箱（登录窗口会把它填进用户名框）</summary>
    public event Action<string>? Done;

    public ResetPasswordViewModel()
    {
        _countdown.Tick += (_, _) =>
        {
            _secondsLeft--;
            if (_secondsLeft <= 0) { _countdown.Stop(); CanSendCode = true; CodeButtonText = "重新获取"; }
            else CodeButtonText = $"{_secondsLeft} 秒后重发";
        };
    }

    [RelayCommand]
    private async Task SendCodeAsync()
    {
        string email = Email.Trim();
        if (!EmailPattern.IsMatch(email)) { ErrorMessage = "请填写注册时用的邮箱"; return; }
        CanSendCode = false;
        bool ok = await RunAsync(async () =>
        {
            if (!Api.IsConnected) await Api.ConnectAsync();
            var r = await Api.SendEmailCodeAsync(email, "reset");
            if (!string.IsNullOrEmpty(r.DevCode))
            {
                CodeHint = $"演示模式（服务器没配邮箱）：验证码是 {r.DevCode}";
                Code = r.DevCode;
            }
            else CodeHint = $"验证码已发到 {email}，{r.ExpiresIn / 60} 分钟内有效（没收到就看看垃圾箱）";
        });
        if (!ok) { CanSendCode = true; return; }
        _secondsLeft = 60;
        CodeButtonText = "60 秒后重发";
        _countdown.Start();
    }

    [RelayCommand]
    private async Task ResetAsync()
    {
        string email = Email.Trim(), code = Code.Trim();
        if (!EmailPattern.IsMatch(email)) { ErrorMessage = "请填写注册时用的邮箱"; return; }
        if (!Regex.IsMatch(code, "^[0-9]{6}$")) { ErrorMessage = "请填写邮件里的 6 位验证码"; return; }
        if (PasswordRules.Check(Password) is { } passwordError) { ErrorMessage = passwordError; return; }
        if (Password != Confirm) { ErrorMessage = "两次输入的密码不一致"; return; }
        await RunAsync(async () =>
        {
            if (!Api.IsConnected) await Api.ConnectAsync();
            await Api.ResetPasswordAsync(email, code, Password);
            _countdown.Stop();
            Done?.Invoke(email);
        });
    }
}
