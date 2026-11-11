// 界面截图工具。用法：
//   Shots.exe --out 输出目录 [--real 主机:端口] [场景名 ...]      不写场景名 = 全部
// 默认用假数据；--real 连真服务器（账号 alice / Ab123456，需要先跑 Seed 灌数据）。
// 窗口放在屏幕外（Left = -20000），渲染完直接存 PNG，所以不会打扰正在用电脑的人。
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using XiaoYu.App;
using XiaoYu.Core.Api;
using XiaoYu.Core.Fake;
using XiaoYu.App.Pages.Trade;
using XiaoYu.App.Services;
using XiaoYu.App.Shell;
using XiaoYu.Core.Models;

namespace XiaoYu.Shots;

internal static partial class Program
{
    private const double Dpi = 144;          // 1.5 倍，图清楚一点
    private static string _out = "shots";
    private static bool _real;
    private static MainWindow? _main;
    private static double _mainW = 1200, _mainH = 800;   // 主窗口大小，--size 1600x900 可以改，用来检查不同窗口宽度下的布局

    [STAThread]
    private static int Main(string[] args)
    {
        string? real = null;
        var wanted = new List<string>();
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "--out") _out = args[++i];
            else if (args[i] == "--real") real = args[++i];
            else if (args[i] == "--size") { var wh = args[++i].Split('x'); _mainW = double.Parse(wh[0]); _mainH = double.Parse(wh[1]); }
            else wanted.Add(args[i]);
        }
        Directory.CreateDirectory(_out);
        _real = real != null;

        {
            var parts = (real ?? "127.0.0.1:9527").Split(':');
            File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "appsettings.json"),
                $"{{\"UseFakeApi\": {(real == null ? "true" : "false")}, \"Host\": \"{parts[0]}\", \"Port\": {(parts.Length > 1 ? parts[1] : "9527")}}}");
        }

        // 不能 new App()：它的 OnStartup 会把真正的登录窗口显示到屏幕上。这里只加载主题。
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        app.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("pack://application:,,,/XiaoYu;component/Styles/Theme.xaml", UriKind.Absolute),
        });
        app.DispatcherUnhandledException += (_, e) => { Console.WriteLine("界面异常: " + e.Exception.Message); e.Handled = true; };
        AppServices.Init();

        int failed = 0;
        app.Dispatcher.InvokeAsync(async () =>
        {
            var scenes = BuildScenes();
            var run = wanted.Count == 0 ? scenes.Keys.ToList() : wanted;
            foreach (var name in run)
            {
                // detail=关键词：搜这个关键词，截第一件商品的详情页（看真服务器上某件商品用）
                if (name.StartsWith("detail=") && name.Length > 7)
                {
                    string keyword = name[7..];
                    scenes[name] = () => Page("detail-search", async () => Navigator.ToGoodsDetail(await GoodsIdByTitle(keyword)), 2500);
                }
                if (!scenes.TryGetValue(name, out var scene)) { Console.WriteLine($"没有这个场景: {name}"); failed++; continue; }
                try { await scene(); Console.WriteLine($"[截图] {name}"); }
                catch (Exception ex) { Console.WriteLine($"[失败] {name}: {ex.GetType().Name}: {ex.Message}"); failed++; }
            }
            _main?.Close();
            if (_checks > 0) Console.WriteLine($"\n流程测试：{_checks - _fails} 项通过，{_fails} 项失败");
            if (_fails > 0) failed++;
            app.Shutdown();
        });
        app.Run();
        return failed == 0 ? 0 : 1;
    }

    // ---------------------------------------------------------------- 场景

    private static Dictionary<string, Func<Task>> BuildScenes()
    {
        var s = new Dictionary<string, Func<Task>>();
        if (_real) AddFlows(s);                       // 流程测试只在连真服务器时有意义
        AddCrawl(s);                                  // 点点点测试（只用假数据）
        s["icons"] = () => { ShotIcons(); return Task.CompletedTask; };
        s["login"] = () => { var w = Offscreen(new LoginWindow()); Settle(w, 600); Capture(w, "login"); w.Close(); return Task.CompletedTask; };
        // 密码小眼睛：先填好密码截一张（圆点），再按下眼睛截一张（明文）
        s["password-eye"] = () =>
        {
            var w = Offscreen(new LoginWindow()); Settle(w, 400);
            var box = (System.Windows.Controls.PasswordBox)w.FindName("PasswordBox");
            box.Password = "Xiaoyu2026"; Settle(w, 300); Capture(w, "password-eye-hidden");
            var eye = (System.Windows.Controls.Primitives.ToggleButton)box.Template.FindName("PART_Eye", box);
            eye.IsChecked = true; Settle(w, 300); Capture(w, "password-eye-shown");
            Console.WriteLine($"  字体：窗口 {w.FontFamily}，密码框 {box.FontFamily}，明文框 {((Control)box.Template.FindName("PART_Plain", box)).FontFamily}");
            var plain = (System.Windows.Controls.TextBox)box.Template.FindName("PART_Plain", box);
            plain.Text = "Xiaoyu2027"; Settle(w, 100);
            Console.WriteLine(box.Password == "Xiaoyu2027" ? "  明文框改动已同步回密码框" : "  ！明文框改动没有同步：" + box.Password);
            eye.IsChecked = false; Settle(w, 100);
            Console.WriteLine(box.Password == "Xiaoyu2027" && plain.Text == "" ? "  隐藏后密码保留、明文已清空" : "  ！隐藏后状态不对");
            w.Close(); return Task.CompletedTask;
        };
        s["register"] = () => { var w = Offscreen(new RegisterWindow()); Settle(w, 600); Capture(w, "register"); w.Close(); return Task.CompletedTask; };
        s["reset-password"] = () => { var w = Offscreen(new ResetPasswordWindow()); Settle(w, 600); Capture(w, "reset-password"); w.Close(); return Task.CompletedTask; };
        s["home"] = () => Page("home", () => { Navigator.ToHome(); return Task.CompletedTask; }, 2200);
        s["detail"] = () => Page("detail", async () => Navigator.ToGoodsDetail(await GoodsIdByTitle("AirPods")), 1800);
        s["detail-mine"] = () => Page("detail-mine", async () => Navigator.ToGoodsDetail(await GoodsIdByTitle("山地自行车")), 1800);
        s["publish"] = () => Page("publish", () => { Navigator.ToPublish(); return Task.CompletedTask; }, 1500);
        s["publish-edit"] = () => Page("publish-edit", async () => Navigator.ToEditGoods(await GoodsIdByTitle("羽绒服")), 1800);
        s["profile"] = () => Page("profile", async () =>
        {
            var d = await Api.GetGoodsDetailAsync(await GoodsIdByTitle("高等数学"));
            Navigator.ToUserProfile(d.Seller.UserId);
        }, 2000);
        s["chatlist"] = () => Page("chatlist", () => { Navigator.ToChatList(); return Task.CompletedTask; }, 1800);
        s["chat"] = () => Page("chat", async () =>
        {
            var c = (await Api.GetConversationsAsync()).First(x => x.Peer.Nickname == "鲍勃");
            Navigator.ToChat(c.Peer.UserId, 0);
        }, 2600);
        // 内容安全（假数据）：二级词提醒条、禁言提示、AI 驳回的商品
        s["chat-warn"] = () => Page("chat-warn", async () =>
        {
            var c = (await Api.GetConversationsAsync()).First(x => x.Peer.Nickname == "小卡");
            Navigator.ToChat(c.Peer.UserId, 0);
        }, 2600);
        s["chat-muted"] = () => Page("chat-muted", async () =>
        {
            if (Api is FakeApiClient f) f.SimulateMuted(12 * 3600 - 5 * 60);
            await AppServices.RefreshMeAsync();
            var c = (await Api.GetConversationsAsync()).First(x => x.Peer.Nickname == "鲍勃");
            Navigator.ToChat(c.Peer.UserId, 0);
        }, 2600);
        s["goods-rejected"] = () => Page("goods-rejected", async () =>
        {
            var saved = await Api.PublishGoodsAsync(new GoodsEdit
            {
                Title = "招兼职 日结300", Description = "在宿舍动动手指就能赚钱，私聊，名额有限", Price = 100,
                CategoryId = (await Api.GetCategoriesAsync()).Last().CategoryId,
            });
            await Task.Delay(1600);
            Navigator.ToGoodsDetail(saved.GoodsId);
        }, 1800);
        s["me"] = () => Page("me", () => { Navigator.ToMe(); return Task.CompletedTask; }, 4500);
        s["orders-buyer"] = () => Page("orders-buyer", () => { Navigator.ToOrders(OrderRole.Buyer); return Task.CompletedTask; }, 1800);
        s["orders-seller"] = () => Page("orders-seller", () => { Navigator.ToOrders(OrderRole.Seller); return Task.CompletedTask; }, 1800);
        s["order-paid-buyer"] = () => Page("order-paid-buyer", async () => Navigator.ToOrderDetail(await OrderId(OrderRole.Buyer, OrderStatus.Paid)), 1800);
        s["order-paid-seller"] = () => Page("order-paid-seller", async () => Navigator.ToOrderDetail(await OrderId(OrderRole.Seller, OrderStatus.Paid)), 1800);
        s["order-shipped"] = () => Page("order-shipped", async () => Navigator.ToOrderDetail(await OrderId(OrderRole.Seller, OrderStatus.Shipped)), 1800);
        s["order-done"] = () => Page("order-done", async () => Navigator.ToOrderDetail(await OrderId(OrderRole.Buyer, OrderStatus.Completed)), 1800);
        return s;
    }

    /// <summary>登录（只做一次），打开主窗口（屏幕外），跳到指定页面，等数据和动画，截图。</summary>
    private static IApiClient Api => AppServices.Api;

    /// <summary>按标题关键词找一件在售商品的编号（假数据和真服务器的编号不一样，所以不写死）</summary>
    private static async Task<long> GoodsIdByTitle(string keyword) =>
        (await Api.SearchGoodsAsync(new GoodsQuery { Keyword = keyword })).Items.First().GoodsId;

    private static async Task<long> OrderId(string role, string status) =>
        (await Api.GetOrdersAsync(role, status)).Items.First().OrderId;

    internal static async Task Page(string name, Func<Task> navigate, int waitMs)
    {
        await EnsureMain();
        await navigate();
        Settle(_main!, waitMs);
        Capture(_main!, name);
    }

    private static async Task EnsureMain()
    {
        if (_main != null) return;
        var api = AppServices.Api;
        await api.ConnectAsync();
        AppServices.SetCurrentUser(await api.LoginAsync("alice", "Ab123456"));
        _main = Offscreen(new MainWindow(), _mainW, _mainH);
        Settle(_main, 800);
    }

    // ---------------------------------------------------------------- 工具

    private static T Offscreen<T>(T w, double width = 0, double height = 0) where T : Window
    {
        w.WindowStartupLocation = WindowStartupLocation.Manual;
        w.Left = -20000; w.Top = -20000;
        w.ShowInTaskbar = false;
        w.ShowActivated = false;
        if (width > 0) { w.Width = width; w.Height = height; }
        w.Show();
        return w;
    }

    /// <summary>让界面线程跑一会儿：数据加载、图片解码、动画都靠它推进</summary>
    private static void Settle(Window w, int ms)
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(ms) };
        timer.Tick += (_, _) => { timer.Stop(); frame.Continue = false; };
        timer.Start();
        Dispatcher.PushFrame(frame);
        w.UpdateLayout();
    }

    private static void Capture(Window w, string name)
    {
        // 窗口的 ActualWidth 包含外框，渲染窗口里面那一层才没有白边
        var root = (FrameworkElement)VisualTreeHelper.GetChild(w, 0);
        int pw = (int)Math.Round(root.ActualWidth * Dpi / 96), ph = (int)Math.Round(root.ActualHeight * Dpi / 96);
        var bmp = new RenderTargetBitmap(pw, ph, Dpi, Dpi, PixelFormats.Pbgra32);
        bmp.Render(root);
        var enc = new PngBitmapEncoder();
        enc.Frames.Add(BitmapFrame.Create(bmp));
        using var f = File.Create(Path.Combine(_out, name + ".png"));
        enc.Save(f);
    }

    /// <summary>把主题里所有 Icon* 字形画成一张表，核对图标选对了没有</summary>
    private static void ShotIcons()
    {
        var wrap = new WrapPanel { Margin = new Thickness(16) };
        var keys = Application.Current.Resources.MergedDictionaries.SelectMany(d => d.Keys.Cast<object>()).OfType<string>().Where(k => k.StartsWith("Icon") && k != "IconFont" && Application.Current.FindResource(k) is string).OrderBy(k => k);
        foreach (var k in keys)
        {
            var panel = new StackPanel { Width = 96, Margin = new Thickness(4, 8, 4, 8) };
            panel.Children.Add(new TextBlock { Style = (Style)Application.Current.Resources["Icon"], Text = (string)Application.Current.FindResource(k), FontSize = 30 });
            panel.Children.Add(new TextBlock { Text = k.Substring(4), FontSize = 11, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 6, 0, 0) });
            wrap.Children.Add(panel);
        }
        var w = Offscreen(new Window { Content = new ScrollViewer { Content = wrap }, Background = Brushes.White, Width = 720, Height = 520, WindowStyle = WindowStyle.None });
        Settle(w, 300);
        Capture(w, "icons");
        w.Close();
    }
}
