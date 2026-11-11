using System.Windows;
using XiaoYu.App.Controls;

namespace XiaoYu.App.Services;

/// <summary>界面小工具：弹窗、右下角提示、切回界面线程</summary>
public static class Ui
{
    /// <summary>右下角提示（几秒后自动消失）。顶部主窗口负责显示。</summary>
    public static event Action<string>? ToastRequested;

    public static void Toast(string message) => ToastRequested?.Invoke(message);

    /// <summary>
    /// 在界面线程执行。推送事件（MessageReceived / OrderUpdated）是在后台线程触发的，
    /// 在里面改界面上绑定的数据必须包一层：
    ///   Api.MessageReceived += msg => Ui.Run(() => Messages.Add(msg));
    /// </summary>
    public static void Run(Action action)
    {
        var dispatcher = Application.Current?.Dispatcher;
        if (dispatcher == null) return;
        if (dispatcher.CheckAccess()) action();
        else dispatcher.BeginInvoke(action);
    }

    public static bool Confirm(string message, string title = "请确认") => Show(message, title, DialogKind.Question);

    public static void Info(string message, string title = "提示") => Show(message, title, DialogKind.Info);

    public static void Error(string message, string title = "出错了") => Show(message, title, DialogKind.Error);

    /// <summary>
    /// 弹出输入框。choices 不为空时上面多一排选项（必须选一个）。
    /// 点提交返回 (选项下标, 文字)，点取消返回 null。
    /// </summary>
    public static (int Choice, string Text)? Prompt(string title, string message, string placeholder,
        string[]? choices = null, int maxChars = 200, string okText = "提交")
    {
        if (PromptHook != null) return PromptHook(title);
        var owner = Application.Current?.Windows.OfType<Window>().FirstOrDefault(w => w.IsActive)
                    ?? Application.Current?.MainWindow;
        var dialog = new InputDialog(title, message, placeholder, choices, maxChars, okText);
        if (owner != null && owner.IsVisible) dialog.Owner = owner;
        else dialog.WindowStartupLocation = WindowStartupLocation.CenterScreen;
        return dialog.ShowDialog() == true ? (dialog.ChoiceIndex, dialog.Text) : null;
    }

    /// <summary>测试用：设置后输入框不再弹出，直接返回它给的结果（参数是标题）。</summary>
    public static Func<string, (int Choice, string Text)?>? PromptHook { get; set; }

    /// <summary>测试用：设置后对话框不再弹出，直接由它决定"确定/取消"。正常运行时为 null。</summary>
    public static Func<DialogKind, string, bool>? DialogHook { get; set; }

    private static bool Show(string message, string title, DialogKind kind)
    {
        if (DialogHook != null) return DialogHook(kind, message);
        var owner = Application.Current?.Windows.OfType<Window>().FirstOrDefault(w => w.IsActive)
                    ?? Application.Current?.MainWindow;
        var dialog = new ConfirmDialog(message, title, kind);
        if (owner != null && owner.IsVisible) dialog.Owner = owner;
        else dialog.WindowStartupLocation = WindowStartupLocation.CenterScreen;
        return dialog.ShowDialog() == true;
    }
}
