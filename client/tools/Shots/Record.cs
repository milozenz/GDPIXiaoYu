// 录屏（做演示视频用）：真窗口在屏幕外渲染，按固定帧率存成 PNG 序列，文件名是 Unix 毫秒时间戳。
// 不碰真屏幕，所以录的时候电脑照常能用。两个进程（两个账号）同时录，之后按时间戳对齐拼成视频。
// 参数走环境变量（计划任务里 set 好）：
//   REC_USER / REC_PWD          登录账号（密码默认 Ab123456）
//   REC_PEER_ID / REC_GOODS_ID  聊天对象和商品编号
//   REC_T0                      开始录的时刻（Unix 毫秒），两个进程用同一个值
//   REC_SECS / REC_FPS          录多久、每秒几帧
//   REC_SAY / REC_SAY_AT        要打的字、第几秒开始打（不填就只录不发）
//   REC_SIZE / REC_DPI          窗口大小（逻辑像素）和渲染 DPI
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using XiaoYu.App.Controls;
using XiaoYu.App.Pages.Chat;
using XiaoYu.App.Pages.Me;
using XiaoYu.App.Pages.Trade;
using XiaoYu.App.Services;
using XiaoYu.App.Shell;
using XiaoYu.Core.Models;

namespace XiaoYu.Shots;

internal static partial class Program
{
    private static string Env(string name, string def = "") => Environment.GetEnvironmentVariable(name) is { Length: > 0 } v ? v : def;
    private static long NowMs() => DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

    private static void AddRecord(Dictionary<string, Func<Task>> s)
    {
        s["rec-chat"] = RecChat;
        s["rec-trade"] = RecTrade;
    }

    /// <summary>
    /// 担保交易（两个进程各录一边）：REC_ROLE=buyer 搜索 → 商品详情 → 下单 → 等发货 → 确认收货；
    /// REC_ROLE=seller 停在自己的商品页 → 订单来了打开订单 → 写备注发货 → 等收货 → 去"我的"看余额。
    /// 确认框自动点"确定"（不弹窗）。每个动作打印相对开始的毫秒数，剪辑时对时间用。
    /// </summary>
    private static async Task RecTrade()
    {
        Ui.DialogHook = (kind, _) => kind == DialogKind.Question;
        string role = Env("REC_ROLE");
        long goods = long.Parse(Env("REC_GOODS_ID"));
        long t0 = long.Parse(Env("REC_T0"));
        double secs = double.Parse(Env("REC_SECS", "20"), CultureInfo.InvariantCulture);
        int fps = int.Parse(Env("REC_FPS", "20"));
        var size = Env("REC_SIZE", "960x700").Split('x');
        double dpi = double.Parse(Env("REC_DPI", "144"), CultureInfo.InvariantCulture);
        void Mark(string what) => Console.WriteLine($"  [{NowMs() - t0,6} ms] {what}");
        async Task At(double s) { while (NowMs() < t0 + (long)(s * 1000)) await Task.Delay(10); }

        var api = AppServices.Api;
        await api.ConnectAsync();
        AppServices.SetCurrentUser(await api.LoginAsync(Env("REC_USER"), Env("REC_PWD", "Ab123456")));
        _main = Offscreen(new MainWindow(), double.Parse(size[0]), double.Parse(size[1]));
        Settle(_main, 800);
        if (role == "buyer")
        {
            Navigator.ToHome();
            var h = Vm<HomeViewModel>();
            await WaitUntil(() => !h.ShowSkeleton, 30000);
            Settle(_main, 6000);   // 等首页图片都下载完，免得后面的请求排在它们后面
        }
        else
        {
            Navigator.ToGoodsDetail(goods);
            await WaitUntil(() => Vm<GoodsDetailViewModel>().Loaded, 30000);
            Settle(_main, 4000);
        }
        Console.WriteLine($"  已就位，离开始还有 {(t0 - NowMs()) / 1000.0:F1} 秒");
        while (NowMs() < t0) await Task.Delay(5);
        var rec = new Recorder(_main, _out, fps, dpi);
        rec.Start();

        if (role == "buyer")
        {
            var home = Vm<HomeViewModel>();
            await At(0.8);
            foreach (var ch in new[] { "理", "光" }) { home.Keyword += ch; await Task.Delay(160); }
            await At(1.6); Mark("搜索"); await home.SearchCommand.ExecuteAsync(null);
            await At(3.2); Mark("打开商品"); Navigator.ToGoodsDetail(goods);
            var d = Vm<GoodsDetailViewModel>();
            await WaitUntil(() => d.Loaded);
            await At(5.6); Mark("点立即购买"); await d.BuyCommand.ExecuteAsync(null);
            await WaitUntil(() => Navigator.Current is OrderDetailPage);
            var o = Vm<OrderDetailViewModel>();
            await WaitUntil(() => o.Loaded);
            Mark($"订单页：{o.Order?.Status}，余额 {AppServices.CurrentUser?.Balance}");
            await WaitUntil(() => o.Order?.Status == OrderStatus.Shipped, 20000);
            Mark("收到推送：已发货");
            await Task.Delay(3500); Mark("点确认收货"); await o.ConfirmCommand.ExecuteAsync(null);
            Mark($"订单：{o.Order?.Status}");
        }
        else
        {
            await At(5.6);
            long orderId = 0;
            while (orderId == 0 && NowMs() < t0 + (long)(secs * 1000) - 8000)
            {
                var list = await api.GetOrdersAsync(OrderRole.Seller, OrderStatus.Paid);
                orderId = list.Items.FirstOrDefault(x => x.GoodsId == goods)?.OrderId ?? 0;
                if (orderId == 0) await Task.Delay(200);
            }
            Mark($"查到订单 {orderId}");
            if (orderId == 0) { rec.Stop(); throw new Exception("没等到订单"); }
            long seen = NowMs() - t0;
            await At(seen / 1000.0 + 2.2); Mark("打开订单"); Navigator.ToOrderDetail(orderId);
            var o = Vm<OrderDetailViewModel>();
            await WaitUntil(() => o.Loaded);
            await At(seen / 1000.0 + 3.6);
            var e = StringInfo.GetTextElementEnumerator("已当面交给买家");
            while (e.MoveNext()) { o.ShipNote += e.GetTextElement(); await Task.Delay(110); }
            await At(seen / 1000.0 + 5.2); Mark("点发货"); await o.ShipCommand.ExecuteAsync(null);
            Mark($"订单：{o.Order?.Status}");
            await WaitUntil(() => o.Order?.Status == OrderStatus.Completed, 20000);
            Mark("收到推送：已完成");
            await Task.Delay(2200); Mark("去我的"); Navigator.ToMe();
        }
        await At(secs);
        rec.Stop();
        Console.WriteLine($"  录了 {rec.Frames} 帧，丢了 {rec.Dropped} 帧；余额 {AppServices.CurrentUser?.Balance}");
    }

    private static async Task RecChat()
    {
        long peer = long.Parse(Env("REC_PEER_ID")), goods = long.Parse(Env("REC_GOODS_ID", "0"));
        long t0 = long.Parse(Env("REC_T0"));
        double secs = double.Parse(Env("REC_SECS", "14"), CultureInfo.InvariantCulture);
        int fps = int.Parse(Env("REC_FPS", "20"));
        string say = Env("REC_SAY");
        double sayAt = double.Parse(Env("REC_SAY_AT", "2"), CultureInfo.InvariantCulture);
        var size = Env("REC_SIZE", "960x700").Split('x');
        double dpi = double.Parse(Env("REC_DPI", "144"), CultureInfo.InvariantCulture);

        var api = AppServices.Api;
        await api.ConnectAsync();
        AppServices.SetCurrentUser(await api.LoginAsync(Env("REC_USER"), Env("REC_PWD", "Ab123456")));
        _main = Offscreen(new MainWindow(), double.Parse(size[0]), double.Parse(size[1]));
        Settle(_main, 800);
        Navigator.ToChat(peer, goods);
        var vm = Vm<ChatViewModel>();
        await WaitUntil(() => vm.Peer != null && vm.Items.Count > 0);
        Settle(_main, 1500);
        Console.WriteLine($"  已就位，离开始还有 {(t0 - NowMs()) / 1000.0:F1} 秒");
        while (NowMs() < t0) await Task.Delay(5);

        var rec = new Recorder(_main, _out, fps, dpi);
        rec.Start();
        if (say.Length > 0)
        {
            while (NowMs() < t0 + (long)(sayAt * 1000)) await Task.Delay(10);
            var e = StringInfo.GetTextElementEnumerator(say);
            while (e.MoveNext()) { vm.Draft += e.GetTextElement(); await Task.Delay(75); }
            await Task.Delay(450);
            Console.WriteLine($"  发送 @ {NowMs()}");
            await vm.SendCommand.ExecuteAsync(null);
        }
        while (NowMs() < t0 + (long)(secs * 1000)) await Task.Delay(20);
        rec.Stop();
        Console.WriteLine($"  录了 {rec.Frames} 帧，丢了 {rec.Dropped} 帧；最后一条：{vm.Items.LastOrDefault()?.Content}；禁言：{vm.IsMuted}");
    }

    /// <summary>定时把窗口渲染成位图，后台线程编码成 PNG。界面线程只做渲染和拷像素。</summary>
    private sealed class Recorder
    {
        private readonly BlockingCollection<(long ms, byte[] px)> _queue = new(boundedCapacity: 120);
        private readonly List<Thread> _workers = new();
        private readonly FrameworkElement _root;
        private readonly DispatcherTimer _timer;
        private readonly string _dir;
        private readonly int _pw, _ph;
        private readonly double _dpi;
        public int Frames, Dropped;

        public Recorder(Window w, string dir, int fps, double dpi)
        {
            _root = (FrameworkElement)VisualTreeHelper.GetChild(w, 0);
            _dir = dir;
            _dpi = dpi;
            _pw = (int)Math.Round(_root.ActualWidth * dpi / 96);
            _ph = (int)Math.Round(_root.ActualHeight * dpi / 96);
            for (int i = 0; i < 4; i++)
            {
                var t = new Thread(Work) { IsBackground = true };
                _workers.Add(t);
                t.Start();
            }
            _timer = new DispatcherTimer(DispatcherPriority.Normal) { Interval = TimeSpan.FromMilliseconds(1000.0 / fps) };
            _timer.Tick += (_, _) => Grab();
        }

        public void Start() { Grab(); _timer.Start(); }

        public void Stop()
        {
            _timer.Stop();
            _queue.CompleteAdding();
            foreach (var t in _workers) t.Join();
        }

        private void Grab()
        {
            long ms = NowMs();
            var bmp = new RenderTargetBitmap(_pw, _ph, _dpi, _dpi, PixelFormats.Pbgra32);
            bmp.Render(_root);
            var px = new byte[_pw * _ph * 4];
            bmp.CopyPixels(px, _pw * 4, 0);
            if (_queue.TryAdd((ms, px))) Frames++; else Dropped++;
        }

        private void Work()
        {
            foreach (var (ms, px) in _queue.GetConsumingEnumerable())
            {
                var src = BitmapSource.Create(_pw, _ph, _dpi, _dpi, PixelFormats.Pbgra32, null, px, _pw * 4);
                var enc = new PngBitmapEncoder();
                enc.Frames.Add(BitmapFrame.Create(src));
                using var f = File.Create(Path.Combine(_dir, ms + ".png"));
                enc.Save(f);
            }
        }
    }
}
