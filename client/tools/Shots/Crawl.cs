// 界面"点点点"测试：每个页面上看得见的按钮、链接、商品卡片都点一遍，记录点完发生了什么。
// 用法：Shots.exe crawl（用假数据；不会真的下单、下架——所有确认框一律点"取消"）
//
// 每点一个之前都重新打开那个页面，保证每次点击都从同样的状态开始。
// 记录：跳到了哪个页面、弹了什么确认框/输入框/窗口、底部提示、报错（未处理的异常）、绑定失效（Binding 错误）。
// "没有任何反应"的也列出来，人工看是不是本来就该这样（比如切换分类、收藏这种只改页面内容的）。
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using XiaoYu.App.Controls;
using XiaoYu.App.Pages.Trade;
using XiaoYu.App.Services;
using XiaoYu.App.Shell;
using XiaoYu.Core.Fake;
using XiaoYu.Core.Models;

namespace XiaoYu.Shots;

internal static partial class Program
{
    private static readonly List<string> _events = new();      // 一次点击期间发生的事
    private static readonly List<string> _bindingErrors = new();
    private static int _crawlProblems;

    private sealed class BindingListener : TraceListener
    {
        public override void Write(string? message) { }
        public override void WriteLine(string? message)
        {
            if (message == null) return;
            lock (_bindingErrors) _bindingErrors.Add(message);
        }
    }

    private static void AddCrawl(Dictionary<string, Func<Task>> s)
    {
        s["crawl"] = Crawl;
    }

    private static async Task Crawl()
    {
        if (_real) { Out("crawl 只用假数据跑（会改数据）"); return; }

        // ---- 把所有弹窗换成"记下来然后取消"
        Ui.DialogHook = (kind, msg) => { _events.Add($"确认框[{kind}]「{Short(msg)}」→取消"); return false; };
        Ui.PromptHook = title => { _events.Add($"输入框「{title}」→取消"); return null; };
        Ui.ModalHook = w =>
        {
            string owner = w.Owner == null ? "无父窗口" : w.Owner == w ? "！父窗口是自己" : "父窗口=" + w.Owner.GetType().Name;
            _events.Add($"窗口 {w.GetType().Name}（{owner}）→关掉");
            w.Close();
            return false;
        };
        string sample = Path.Combine(Path.GetTempPath(), "xy-crawl-sample.png");
        SaveSampleImage(sample);
        ImageTools.PickHook = _ => { _events.Add("选择图片窗口→选了 1 张示例图"); return new[] { sample }; };
        Ui.ToastRequested += t => _events.Add($"提示「{Short(t)}」");
        PresentationTraceSources.Refresh();
        PresentationTraceSources.DataBindingSource.Listeners.Add(new BindingListener());
        PresentationTraceSources.DataBindingSource.Switch.Level = SourceLevels.Error;
        Application.Current.DispatcherUnhandledException += (_, e) => { _events.Add($"！！报错 {e.Exception.GetType().Name}: {Short(e.Exception.Message)}"); };

        // 看门狗：每 2 秒检查一次，有没走钩子、自己弹出来的窗口就记下来并关掉，免得测试卡住
        var watchdog = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        watchdog.Tick += (_, _) =>
        {
            foreach (var w in Application.Current.Windows.OfType<Window>().ToList())
                if (w.IsVisible && w is not MainWindow && w is not LoginWindow && w.Left > -10000)
                { _events.Add($"！意外弹出的窗口 {w.GetType().Name}「{w.Title}」"); w.Close(); }
        };
        watchdog.Start();

        // ---- 1. 登录窗口：按钮都点一遍，然后真的走一遍登录（检查登录后主窗口的交接）
        var login = Offscreen(new LoginWindow()); Settle(login, 400);
        Section("登录窗口");
        var loginItems = Clickables(login);
        foreach (var (el, label) in loginItems)
        {
            if (label.Contains("登录") && el is Button) continue;      // 登录按钮最后单独点
            Click(el); Settle(login, 300); Report(label, "登录窗口", "登录窗口");
        }
        ((TextBox)login.FindName("UsernameBox")).Text = "alice";
        ((PasswordBox)login.FindName("PasswordBox")).Password = "Ab123456";
        var loginBtn = loginItems.First(x => x.Label.Contains("登录") && x.El is Button).El;
        Click(loginBtn);
        MainWindow? main = null;
        for (int i = 0; i < 40 && main == null; i++)
        {
            Settle(login, 150);
            main = Application.Current.Windows.OfType<MainWindow>().FirstOrDefault();
        }
        if (main == null) { Problem("登录后没有出现主窗口"); return; }
        main.Left = -20000; main.Top = -20000; main.Width = 1200; main.Height = 800;
        Settle(main, 1500);
        Check(Application.Current.MainWindow == main, "登录后 Application.MainWindow 是主窗口", $"登录后 Application.MainWindow 是 {Application.Current.MainWindow?.GetType().Name ?? "null"}");
        Check(!login.IsVisible, "登录窗口已关闭", "登录窗口还开着");
        _main = main;
        var host = (ContentControl)main.FindName("PageHost");
        var api = AppServices.Api;

        // ---- 2. 顶部导航
        Section("顶部导航栏");
        var header = (FrameworkElement)((Grid)main.Content).Children[0];   // 顶部栏是主窗口 Grid 的第一个孩子
        foreach (var (el, label) in Clickables(header).Where(x => !IsInside(x.El, host)))
        {
            Navigator.ToHome(); Settle(main, 500);
            string before = PageName(host);
            Click(el); Settle(main, 700);
            Report(label, before, PageName(host));
        }

        // ---- 2.5 小窗口：注册、找回密码、修改密码（不点"取消"：不是以对话框方式打开时设 DialogResult 会报错，正常使用时没有这个问题）
        foreach (var make in new Func<Window>[] { () => new RegisterWindow(), () => new ResetPasswordWindow(), () => new XiaoYu.App.Pages.Me.ChangePasswordWindow() })
        {
            var w = Offscreen(make()); Settle(w, 500);
            Section(w.GetType().Name);
            foreach (var (el, label) in Clickables(w).Where(x => !x.Label.Contains("取消")))
            {
                Click(el); Settle(w, 600);
                var err = ErrorTexts(w);
                if (err.Length > 0) _events.Add($"页面提示「{Short(err)}」");
                Report(label, w.GetType().Name, w.GetType().Name);
            }
            w.Close();
        }

        // ---- 3. 每个页面
        async Task<long> Gid(string kw) => (await api.SearchGoodsAsync(new GoodsQuery { Keyword = kw })).Items.First().GoodsId;
        long bob = (await api.GetConversationsAsync()).First(c => c.Peer.Nickname == "鲍勃").Peer.UserId;
        long xiaoka = (await api.GetConversationsAsync()).First(c => c.Peer.Nickname == "小卡").Peer.UserId;
        var rejected = await api.PublishGoodsAsync(new GoodsEdit
        {
            Title = "招兼职 日结300", Description = "在宿舍动动手指就能赚钱，私聊，名额有限", Price = 100,
            CategoryId = (await api.GetCategoriesAsync()).Last().CategoryId,
        });
        await Task.Delay(1600);
        async Task<long> Oid(string role, string status) => (await api.GetOrdersAsync(role, status)).Items.First().OrderId;
        long paidBuyer = await Oid(OrderRole.Buyer, OrderStatus.Paid), paidSeller = await Oid(OrderRole.Seller, OrderStatus.Paid);
        long shippedSeller = await Oid(OrderRole.Seller, OrderStatus.Shipped), doneBuyer = await Oid(OrderRole.Buyer, OrderStatus.Completed);
        long airpods = await Gid("AirPods"), bike = await Gid("山地自行车"), coat = await Gid("羽绒服");
        // 准备一件"已下架"的商品（测"重新上架"按钮）：每次打开前都先把它下架
        long offShelf = await Gid("无线鼠标");

        var pages = new (string Name, Func<Task> Go, int Wait)[]
        {
            ("首页", () => { Navigator.ToHome(); return Task.CompletedTask; }, 1500),
            ("别人的商品详情", () => { Navigator.ToGoodsDetail(airpods); return Task.CompletedTask; }, 1000),
            ("自己的商品详情", () => { Navigator.ToGoodsDetail(bike); return Task.CompletedTask; }, 1000),
            ("已下架的商品详情", async () => { try { await api.OffShelfAsync(offShelf); } catch { } Navigator.ToGoodsDetail(offShelf); }, 1000),
            ("被驳回的商品详情", () => { Navigator.ToGoodsDetail(rejected.GoodsId); return Task.CompletedTask; }, 1000),
            ("发布页", () => { Navigator.ToPublish(); return Task.CompletedTask; }, 800),
            ("编辑商品页", () => { Navigator.ToEditGoods(coat); return Task.CompletedTask; }, 1000),
            ("卖家主页", () => { Navigator.ToUserProfile(bob); return Task.CompletedTask; }, 1000),
            ("消息列表", () => { Navigator.ToChatList(); return Task.CompletedTask; }, 1000),
            ("聊天（普通）", () => { Navigator.ToChat(bob, 0); return Task.CompletedTask; }, 1200),
            ("聊天（有风险提醒）", () => { Navigator.ToChat(xiaoka, 0); return Task.CompletedTask; }, 1200),
            ("我的", () => { Navigator.ToMe(); return Task.CompletedTask; }, 1800),
            ("我买到的", () => { Navigator.ToOrders(OrderRole.Buyer); return Task.CompletedTask; }, 1000),
            ("我卖出的", () => { Navigator.ToOrders(OrderRole.Seller); return Task.CompletedTask; }, 1000),
            ("订单详情（买家·待发货）", () => { Navigator.ToOrderDetail(paidBuyer); return Task.CompletedTask; }, 1000),
            ("订单详情（卖家·待发货）", () => { Navigator.ToOrderDetail(paidSeller); return Task.CompletedTask; }, 1000),
            ("订单详情（卖家·待收货）", () => { Navigator.ToOrderDetail(shippedSeller); return Task.CompletedTask; }, 1000),
            ("订单详情（买家·已完成）", () => { Navigator.ToOrderDetail(doneBuyer); return Task.CompletedTask; }, 1000),
        };
        foreach (var (name, go, wait) in pages)
        {
            Section(name);
            await go(); Settle(main, wait);
            int count = Clickables(host).Count;
            if (count == 0) { Problem($"{name}：一个能点的都没有"); continue; }
            int cards = 0;
            for (int i = 0; i < Math.Min(count, 40); i++)
            {
                await go(); Settle(main, wait);
                var items = Clickables(host);
                if (i >= items.Count) { Out($"  （前一个操作改变了数据，按钮从 {count} 个变成 {items.Count} 个，这一页剩下的跳过）"); break; }
                var (el, label) = items[i];
                // 首页、主页的商品卡片很多，点 2 张就够了
                if (label.StartsWith("商品卡片") && ++cards > 2) continue;
                string before = PageName(host);
                string errBefore = ErrorTexts(host);
                Click(el); Settle(main, 900);
                string errAfter = ErrorTexts(host);
                if (errAfter.Length > 0 && errAfter != errBefore) _events.Add($"页面提示「{Short(errAfter)}」");
                Report(label, before, PageName(host));
            }
        }

        // ---- 4. 退出登录（确认框选"确定"），检查回到登录窗口、主窗口交接
        Section("退出登录");
        Ui.DialogHook = (kind, msg) => { _events.Add($"确认框「{Short(msg)}」→确定"); return true; };
        Navigator.ToMe(); Settle(main, 1500);
        var logout = Clickables(host).First(x => x.Label.Contains("退出登录")).El;
        Click(logout); Settle(main, 1200);
        var login2 = Application.Current.Windows.OfType<LoginWindow>().FirstOrDefault();
        Report("退出登录", "我的", login2 != null ? "登录窗口" : "？");
        Check(login2 != null && Application.Current.MainWindow == login2, "退出后 Application.MainWindow 是登录窗口",
              $"退出后 Application.MainWindow 是 {Application.Current.MainWindow?.GetType().Name ?? "null"}");
        login2?.Close();
        _main = null;   // 主窗口已经关了

        // ---- 汇总
        var uniqueBinding = _bindingErrors.Select(BindingSummary).Distinct().ToList();
        Out($"\n绑定错误 {uniqueBinding.Count} 种：");
        foreach (var b in uniqueBinding.Take(30)) Out("  ！" + b);
        if (uniqueBinding.Count > 0) _crawlProblems++;
        Out($"\n点点点测试结束：发现 {_crawlProblems} 个问题（见上面带 ！ 的行）");
        if (_crawlProblems > 0) _fails++;
        _checks++;
    }

    // ---------------------------------------------------------------- 工具

    /// <summary>窗口里红色错误提示（ErrorText 样式）的文字</summary>
    private static string ErrorTexts(DependencyObject root)
    {
        var errStyle = Application.Current.TryFindResource("ErrorText") as Style;
        var list = new List<string>();
        void Walk(DependencyObject d)
        {
            if (d is TextBlock t && t.IsVisible && t.Style == errStyle && !string.IsNullOrWhiteSpace(t.Text)) list.Add(t.Text);
            for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++) Walk(VisualTreeHelper.GetChild(d, i));
        }
        Walk(root);
        return string.Join(" / ", list);
    }

    private static StreamWriter? _crawlLog;
    private static void Out(string line)
    {
        Console.WriteLine(line);
        _crawlLog ??= new StreamWriter(Path.Combine(_out, "crawl.txt"), false, new System.Text.UTF8Encoding(false)) { AutoFlush = true };
        _crawlLog.WriteLine(line);
    }
    private static void Section(string name) => Out($"\n== {name}");
    private static void Problem(string msg) { _crawlProblems++; Out("  ！" + msg); }
    private static void Check(bool ok, string good, string bad) { if (ok) Out("  ✓ " + good); else Problem(bad); }
    private static string Short(string s) { s = s.Replace("\n", " "); return s.Length > 40 ? s[..40] + "…" : s; }

    private static void Report(string label, string before, string after)
    {
        var parts = new List<string>();
        if (after != before) parts.Add("→ " + after);
        parts.AddRange(_events);
        bool error = _events.Any(e => e.StartsWith("！"));
        if (error) _crawlProblems++;
        string what = parts.Count == 0 ? "（页面上没有跳转、弹窗和提示）" : string.Join("；", parts);
        Out($"  {(error ? "！" : "·")} {label}：{what}");
        _events.Clear();
    }

    private static string PageName(ContentControl host) => host.Content switch
    {
        null => "空",
        FrameworkElement fe => fe.GetType().Name.Replace("Page", ""),
        var x => x.GetType().Name,
    };

    private static bool IsInside(DependencyObject el, DependencyObject container)
    {
        for (var p = el; p != null; p = VisualTreeHelper.GetParent(p)) if (p == container) return true;
        return false;
    }

    /// <summary>看得见、能点的东西：按钮（含开关、单选）、文字里的链接、商品卡片。按在屏幕上的位置排序。</summary>
    private static List<(FrameworkElement El, string Label)> Clickables(DependencyObject root)
    {
        var list = new List<(FrameworkElement El, string Label)>();
        void Walk(DependencyObject d)
        {
            if (d is UIElement u && !u.IsVisible) return;
            if (d is GoodsCard card) { list.Add((card, "商品卡片「" + Short(((card.DataContext as GoodsBrief)?.Title) ?? "") + "」")); return; }
            if (d is ButtonBase b && b.IsEnabled && !IsTemplatePartOfInput(b)) { list.Add((b, Label(b))); }
            if (d is TextBlock tb)
                foreach (var h in tb.Inlines.OfType<Hyperlink>()) list.Add((tb, "链接「" + new TextRange(h.ContentStart, h.ContentEnd).Text + "」"));
            for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++) Walk(VisualTreeHelper.GetChild(d, i));
        }
        Walk(root);
        // 同一个 TextBlock 里只有一个链接时，标签里带链接文字；按位置从上到下、从左到右排
        return list.OrderBy(x => Pos(x.El, root).Y).ThenBy(x => Pos(x.El, root).X).ToList();
    }

    // 密码框里的小眼睛算正常按钮；滚动条、文本框内部的按钮不算
    private static bool IsTemplatePartOfInput(ButtonBase b)
    {
        for (DependencyObject? p = VisualTreeHelper.GetParent(b); p != null; p = VisualTreeHelper.GetParent(p))
            if (p is ScrollBar || p is ComboBox) return true;
        return false;
    }

    private static Point Pos(FrameworkElement el, DependencyObject root)
    {
        try { return el.TransformToAncestor((Visual)root).Transform(new Point(0, 0)); } catch { return new Point(0, 0); }
    }

    private static string Label(ButtonBase b)
    {
        string kind = b switch { RadioButton => "单选", ToggleButton => "开关", _ => "按钮" };
        var texts = new List<string>();
        if (b.Content is string s0) texts.Add(s0);
        void Walk(DependencyObject d)
        {
            if (d is TextBlock t && !string.IsNullOrWhiteSpace(t.Text) && !(t.FontFamily?.Source ?? "").Contains("MDL2")) texts.Add(t.Text);
            for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++) Walk(VisualTreeHelper.GetChild(d, i));
        }
        Walk(b);
        string name = string.Join(" ", texts.Distinct()).Trim();
        if (name.Length == 0 && b.ToolTip is string tip) name = tip;
        if (name.Length == 0 && b.Tag is string tag) name = tag;
        if (name.Length == 0) name = "（无文字的图标）";
        return $"{kind}「{Short(name)}」";
    }

    private static void Click(FrameworkElement el)
    {
        if (el is ButtonBase b)
            typeof(ButtonBase).GetMethod("OnClick", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(b, null);
        else if (el is GoodsCard card)
        {
            var border = (UIElement)card.FindName("CardBorder");
            border.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, 0, MouseButton.Left) { RoutedEvent = UIElement.MouseLeftButtonUpEvent });
        }
        else if (el is TextBlock tb)
        {
            var h = tb.Inlines.OfType<Hyperlink>().First();
            typeof(Hyperlink).GetMethod("OnClick", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(h, null);
        }
    }

    private static string BindingSummary(string msg)
    {
        // "System.Windows.Data Error: 40 : BindingExpression path error: 'X' property not found on 'object' ''Y' ...' target element is 'Z' (Name=''); target property is 'W'"
        var m = System.Text.RegularExpressions.Regex.Match(msg, @"Error: (\d+) : (.{0,90}).*?target element is '([^']+)'.*?target property is '([^']+)'");
        if (m.Success) return $"[{m.Groups[1].Value}] {m.Groups[2].Value.Trim()} → {m.Groups[3].Value}.{m.Groups[4].Value}";
        msg = msg.Replace("\n", " ");
        return msg.Length > 260 ? msg[..260] + "…" : msg;
    }

    private static void SaveSampleImage(string path)
    {
        var bmp = new System.Windows.Media.Imaging.RenderTargetBitmap(64, 64, 96, 96, PixelFormats.Pbgra32);
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen()) dc.DrawRectangle(Brushes.Gold, null, new Rect(0, 0, 64, 64));
        bmp.Render(dv);
        var enc = new System.Windows.Media.Imaging.PngBitmapEncoder();
        enc.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bmp));
        using var f = File.Create(path); enc.Save(f);
    }
}
