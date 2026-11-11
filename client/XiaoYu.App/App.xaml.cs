using System.Windows;
using System.Windows.Threading;
using XiaoYu.App.Services;
using XiaoYu.App.Shell;

namespace XiaoYu.App;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // 没处理的异常弹个框，不让程序直接闪退（开发阶段方便看错误）
        DispatcherUnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += (_, args) => args.SetObserved();

        AppServices.Init();
        new LoginWindow().Show();
    }

    private void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        Ui.Error($"程序出错了：{e.Exception.Message}\n\n{e.Exception.StackTrace}", "未处理的错误");
        e.Handled = true;
    }

    protected override void OnExit(ExitEventArgs e)
    {
        AppServices.Api?.Dispose();
        base.OnExit(e);
    }
}
