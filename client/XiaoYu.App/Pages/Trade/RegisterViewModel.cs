using System.Text.RegularExpressions;
using System.Windows.Media;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Trade;

/// <summary>
/// 注册窗口的 ViewModel。
/// 先在界面上检查（不对就显示 ErrorMessage，不发请求）：
///   用户名 3–20 位，只能字母数字下划线；昵称 1–20 字；邮箱格式；6 位验证码；密码规则见 PasswordRules；两次密码一致。
/// 邮箱验证：点"获取验证码"→ 服务器发邮件（60 秒内不能重复发）。服务器没配邮箱时是开发模式，验证码直接显示在界面上。
/// 密码输入时实时更新强度条和四个规则小标签。
/// </summary>
public partial class RegisterViewModel : ViewModelBase
{
    private static readonly Regex UsernamePattern = new("^[A-Za-z0-9_]{3,20}$", RegexOptions.Compiled);
    private static readonly Regex EmailPattern = new(@"^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$", RegexOptions.Compiled);
    private readonly DispatcherTimer _countdown = new() { Interval = TimeSpan.FromSeconds(1) };
    private int _secondsLeft;

    private static Brush Frozen(string hex)
    {
        var b = (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
        b.Freeze();
        return b;
    }

    private static readonly Brush Off = Frozen("#EDEDEB");
    private static readonly Brush[] Levels = { Frozen("#F5484A"), Frozen("#FF9A2E"), Frozen("#E8C300"), Frozen("#1FB26B") };

    [ObservableProperty] private string _username = "";
    [ObservableProperty] private string _nickname = "";
    [ObservableProperty] private string _email = "";
    [ObservableProperty] private string _code = "";
    [ObservableProperty] private string _codeButtonText = "获取验证码";
    [ObservableProperty] private bool _canSendCode = true;
    /// <summary>开发模式下显示验证码，或者"已发送到邮箱"的提示</summary>
    [ObservableProperty] private string _codeHint = "";

    public RegisterViewModel()
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
        if (!EmailPattern.IsMatch(email)) { ErrorMessage = "请填写正确的邮箱"; return; }
        CanSendCode = false;
        bool ok = await RunAsync(async () =>
        {
            if (!Api.IsConnected) await Api.ConnectAsync();
            var r = await Api.SendEmailCodeAsync(email);
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

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasLength), nameof(HasDigit), nameof(HasUpper), nameof(HasLower),
        nameof(Segment1), nameof(Segment2), nameof(Segment3), nameof(Segment4), nameof(PasswordsMatch))]
    private string _password = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(PasswordsMatch))]
    private string _confirm = "";

    public bool HasLength => PasswordRules.LengthOk(Password);
    public bool HasDigit => PasswordRules.HasDigit(Password);
    public bool HasUpper => PasswordRules.HasUpper(Password);
    public bool HasLower => PasswordRules.HasLower(Password);
    public bool PasswordsMatch => Confirm.Length > 0 && Confirm == Password;

    private int Score => PasswordRules.Score(Password);
    private Brush Seg(int index) => Score > index ? Levels[Math.Max(0, Score - 1)] : Off;
    public Brush Segment1 => Seg(0);
    public Brush Segment2 => Seg(1);
    public Brush Segment3 => Seg(2);
    public Brush Segment4 => Seg(3);

    /// <summary>注册成功后触发，参数是用户名。窗口收到后会关闭并把用户名填回登录框。</summary>
    public event Action<string>? Registered;

    [RelayCommand]
    private async Task RegisterAsync()
    {
        string username = Username.Trim(), nickname = Nickname.Trim();
        if (!UsernamePattern.IsMatch(username)) { ErrorMessage = "用户名要 3-20 位，只能用字母、数字、下划线"; return; }
        int nickLength = nickname.EnumerateRunes().Count();
        if (nickLength < 1 || nickLength > 20) { ErrorMessage = "昵称要 1-20 个字"; return; }
        string email = Email.Trim(), code = Code.Trim();
        if (!EmailPattern.IsMatch(email)) { ErrorMessage = "请填写正确的邮箱"; return; }
        if (!Regex.IsMatch(code, "^[0-9]{6}$")) { ErrorMessage = "请填写邮件里的 6 位验证码"; return; }
        if (PasswordRules.Check(Password) is { } passwordError) { ErrorMessage = passwordError; return; }
        if (Password != Confirm) { ErrorMessage = "两次输入的密码不一致"; return; }

        await RunAsync(async () =>
        {
            if (!Api.IsConnected) await Api.ConnectAsync();
            await Api.RegisterAsync(username, Password, nickname, email, code);
            _countdown.Stop();
            Registered?.Invoke(username);
        });
    }
}
