using System.Windows;
using XiaoYu.App.Services;
using XiaoYu.Core.Api;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Me;

/// <summary>修改密码对话框。密码框不能绑定，所以直接在这里调接口（逻辑很少）。</summary>
public partial class ChangePasswordWindow : Window
{
    public ChangePasswordWindow()
    {
        InitializeComponent();
        Loaded += (_, _) => { Motion.Pop(Card); OldBox.Focus(); };
    }

    private async void OnOk(object sender, RoutedEventArgs e)
    {
        ErrorLine.Text = "";
        if (OldBox.Password.Length == 0) { ErrorLine.Text = "请输入原密码"; return; }
        if (PasswordRules.Check(NewBox.Password) is { } error) { ErrorLine.Text = error; return; }
        if (NewBox.Password != ConfirmBox.Password) { ErrorLine.Text = "两次输入的新密码不一致"; return; }
        if (NewBox.Password == OldBox.Password) { ErrorLine.Text = "新密码不能和原密码一样"; return; }
        OkButton.IsEnabled = false;
        try
        {
            await AppServices.Api.ChangePasswordAsync(OldBox.Password, NewBox.Password);
            DialogResult = true;
        }
        catch (ApiException ex)
        {
            ErrorLine.Text = ex.Message;
            OkButton.IsEnabled = true;
        }
    }

    private void OnCancel(object sender, RoutedEventArgs e) => DialogResult = false;
}
