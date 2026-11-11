// 端到端流程测试：让界面的 ViewModel 在真服务器上走一遍用户会做的事，每一步断言结果并截图。
// 用法：Shots.exe --out 目录 --real 127.0.0.1:9527 flow-all     （需要先用 Seed 灌好数据）
// 另一个"人"用 TcpApiClient 扮演（bob），用来验证对方那一侧的结果和实时推送。
using System.Windows;
using System.Windows.Controls;
using XiaoYu.App.Controls;
using XiaoYu.App.Pages.Chat;
using XiaoYu.App.Pages.Me;
using XiaoYu.App.Pages.Trade;
using XiaoYu.App.Services;
using XiaoYu.App.Shell;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;

namespace XiaoYu.Shots;

internal static partial class Program
{
    private static int _checks, _fails;
    private static string Suffix => DateTime.Now.ToString("HHmmss");

    private static void Check(bool ok, string name)
    {
        _checks++;
        if (!ok) _fails++;
        Console.WriteLine($"  [{(ok ? "通过" : "失败")}] {name}");
    }

    private static async Task WaitUntil(Func<bool> condition, int ms = 8000)
    {
        var end = DateTime.UtcNow.AddMilliseconds(ms);
        while (!condition() && DateTime.UtcNow < end) await Task.Delay(60);
    }

    private static T Vm<T>() where T : class => (T)((FrameworkElement)Navigator.Current!).DataContext;

    private static async Task<TcpApiClient> LoginClient(string user)
    {
        var c = new TcpApiClient(AppServices.Settings.Host, AppServices.Settings.Port, AppServices.Settings.UseTls);
        await c.ConnectAsync();
        await c.LoginAsync(user, "Ab123456");
        return c;
    }

    private static void AddFlows(Dictionary<string, Func<Task>> s)
    {
        Ui.DialogHook = (kind, _) => kind == DialogKind.Question;   // 确认框一律点"确定"，不弹窗
        s["flow-login"] = FlowLogin;
        s["flow-register"] = FlowRegister;
        s["flow-publish"] = FlowPublish;
        s["flow-buy"] = FlowBuy;
        s["flow-favorite"] = FlowFavorite;
        s["flow-chat"] = FlowChat;
        s["flow-ship"] = FlowShip;
        s["flow-safety"] = FlowSafety;
        s["flow-password"] = FlowPassword;
        s["flow-all"] = async () =>
        {
            foreach (var name in new[] { "flow-login", "flow-register", "flow-publish", "flow-buy", "flow-favorite", "flow-chat", "flow-ship", "flow-safety", "flow-password" })
                await s[name]();
        };
    }

    private static async Task FlowLogin()
    {
        Console.WriteLine("登录");
        var vm = new LoginViewModel { Username = "nobody" + Suffix };
        var box = new PasswordBox { Password = "wrong-password" };
        await vm.LoginCommand.ExecuteAsync(box);
        Check(vm.ErrorMessage.Contains("用户名或密码错误"), "密码错了：提示「用户名或密码错误」");
        vm.Username = "";
        await vm.LoginCommand.ExecuteAsync(new PasswordBox());
        Check(vm.ErrorMessage.Contains("请输入"), "什么都没填：界面直接提示，不发请求");
    }

    private static async Task FlowRegister()
    {
        Console.WriteLine("注册");
        var vm = new RegisterViewModel();
        string user = "flow" + Suffix;
        bool done = false;
        vm.Registered += u => done = u == user;
        vm.Username = user; vm.Nickname = "流程测试";
        vm.Email = "bad-email";
        await vm.SendCodeCommand.ExecuteAsync(null);
        Check(vm.ErrorMessage.Contains("邮箱"), "邮箱格式不对：界面直接拦住");
        vm.Email = user + "@flow.test";
        await vm.SendCodeCommand.ExecuteAsync(null);
        Check(vm.Code.Length == 6 && !vm.CanSendCode, $"获取验证码：开发模式直接填好（{vm.CodeHint}），按钮进入 60 秒倒计时");
        vm.Password = "weak"; vm.Confirm = "weak";
        await vm.RegisterCommand.ExecuteAsync(null);
        Check(!done && vm.ErrorMessage.Contains("8–32"), "弱密码被界面拦住，没有发请求");
        vm.Password = "Strong123"; vm.Confirm = "Different123";
        await vm.RegisterCommand.ExecuteAsync(null);
        Check(!done && vm.ErrorMessage.Contains("不一致"), "两次密码不一致被拦住");
        vm.Confirm = "Strong123";
        await vm.RegisterCommand.ExecuteAsync(null);
        Check(done, "合格的密码：注册成功");
        using var c = new TcpApiClient(AppServices.Settings.Host, AppServices.Settings.Port, AppServices.Settings.UseTls);
        await c.ConnectAsync();
        Check((await c.LoginAsync(user, "Strong123")).Username == user, "新账号能登录");
        Check((await c.LoginAsync(user + "@FLOW.test", "Strong123")).Username == user, "用邮箱也能登录（不分大小写）");
        await vm.RegisterCommand.ExecuteAsync(null);
        Check(vm.ErrorMessage.Contains("失效"), "验证码用过一次就作废");
        var vm2 = new RegisterViewModel { Username = user, Nickname = "重名", Email = user + "x@flow.test", Password = "Strong123", Confirm = "Strong123" };
        await vm2.SendCodeCommand.ExecuteAsync(null);
        await vm2.RegisterCommand.ExecuteAsync(null);
        Check(vm2.ErrorMessage.Contains("已被注册"), "同名再注册：提示「用户名已被注册」");
    }

    private static async Task FlowPublish()
    {
        Console.WriteLine("发布商品");
        await EnsureMain();
        Navigator.ToPublish();
        var vm = Vm<PublishViewModel>();
        await WaitUntil(() => vm.Categories.Count > 0);
        string title = "流程测试商品" + Suffix;
        vm.Title = title; vm.PriceText = "abc";
        await vm.SubmitCommand.ExecuteAsync(null);
        Check(vm.ErrorMessage.Contains("价格"), "价格填 abc：被界面拦住");
        vm.PriceText = "12.5";
        await vm.SubmitCommand.ExecuteAsync(null);
        Check(vm.ErrorMessage.Contains("分类"), "没选分类：被界面拦住");
        vm.Categories[2].IsSelected = true;
        vm.Description = "端到端流程测试发布的商品";
        await vm.SubmitCommand.ExecuteAsync(null);
        await WaitUntil(() => Navigator.Current is GoodsDetailPage);
        Check(Navigator.Current is GoodsDetailPage, "发布成功后跳到商品详情");
        var mine = (await Api.GetMyGoodsAsync(null, 1, 5)).Items.First(g => g.Title == title);
        Check(mine.Status is GoodsStatus.Reviewing or GoodsStatus.OnSale, $"发布后的状态：{mine.Status}（开了 AI 审核就是审核中）");
        Check(await Api.WaitReviewAsync(mine.GoodsId) == GoodsStatus.OnSale, "AI 审核通过，自动上架");
        var found = await Api.SearchGoodsAsync(new GoodsQuery { Keyword = title });
        Check(found.Items.Count == 1 && found.Items[0].Price == 1250, "搜得到刚发布的商品，价格 ¥12.5");
        Navigator.ToGoodsDetail(mine.GoodsId);
        await WaitUntil(() => Vm<GoodsDetailViewModel>().Loaded);
        Check(Vm<GoodsDetailViewModel>().IsMine, "自己的商品：显示编辑和下架，不显示购买");
        Settle(_main!, 700);
        Capture(_main!, "flow-publish");
    }

    private static async Task FlowBuy()
    {
        Console.WriteLine("下单、余额不足、取消退款");
        await EnsureMain();
        await AppServices.RefreshMeAsync();

        // 1) 买不起
        Navigator.ToGoodsDetail(await GoodsIdByTitle("iPad"));
        var detail = Vm<GoodsDetailViewModel>();
        await WaitUntil(() => detail.Loaded);
        await detail.BuyCommand.ExecuteAsync(null);
        Check(detail.ErrorMessage.Contains("余额不足"), "余额不够：提示去充值，没有下单");
        Settle(_main!, 500);
        Capture(_main!, "flow-buy-nomoney");

        // 2) 买得起
        long fanId = await GoodsIdByTitle("小风扇");
        long before = AppServices.CurrentUser!.Balance;
        Navigator.ToGoodsDetail(fanId);
        detail = Vm<GoodsDetailViewModel>();
        await WaitUntil(() => detail.Loaded);
        await detail.BuyCommand.ExecuteAsync(null);
        await WaitUntil(() => Navigator.Current is OrderDetailPage);
        Check(Navigator.Current is OrderDetailPage, "下单后跳到订单详情");
        var order = Vm<OrderDetailViewModel>();
        await WaitUntil(() => order.Loaded);
        Check(order.Order?.Status == OrderStatus.Paid && order.IsBuyer, "订单状态：待发货，我是买家");
        Check(AppServices.CurrentUser!.Balance == before - 1500, $"余额扣了 15 元（{before} → {AppServices.CurrentUser.Balance} 分）");
        Check((await Api.GetGoodsDetailAsync(fanId)).Status == GoodsStatus.Sold, "商品变成已卖出");
        Settle(_main!, 600);
        Capture(_main!, "flow-buy-paid");

        // 3) 取消：退款 + 商品重新上架
        await order.CancelCommand.ExecuteAsync(null);
        Check(order.Order?.Status == OrderStatus.Cancelled, "取消后订单变成已取消");
        Check(AppServices.CurrentUser!.Balance == before, "钱退回来了，余额恢复");
        Check((await Api.GetGoodsDetailAsync(fanId)).Status == GoodsStatus.OnSale, "商品重新上架");
        Settle(_main!, 600);
        Capture(_main!, "flow-buy-cancelled");
    }

    private static async Task FlowFavorite()
    {
        Console.WriteLine("收藏");
        await EnsureMain();
        long id = await GoodsIdByTitle("AirPods");
        Navigator.ToGoodsDetail(id);
        var vm = Vm<GoodsDetailViewModel>();
        await WaitUntil(() => vm.Loaded);
        bool start = vm.IsFavorite;
        await vm.ToggleFavoriteCommand.ExecuteAsync(null);
        Check(vm.IsFavorite == !start, $"点心形：状态立刻切换（开始 {start}，现在 {vm.IsFavorite}{(vm.ErrorMessage.Length > 0 ? "，出错：" + vm.ErrorMessage : "")}）");
        var list = await Api.GetFavoritesAsync();
        Check(list.Items.Any(g => g.GoodsId == id) == vm.IsFavorite, "服务器那边的收藏列表同步变了");
        await vm.ToggleFavoriteCommand.ExecuteAsync(null);
        Check(vm.IsFavorite == start, "再点一次：恢复");
    }

    private static async Task FlowChat()
    {
        Console.WriteLine("聊天与实时推送");
        await EnsureMain();
        using var bob = await LoginClient("bob");
        long me = AppServices.MyId;
        long bobId = (await Api.GetConversationsAsync()).First(c => c.Peer.Nickname == "鲍勃").Peer.UserId;

        Navigator.ToChat(bobId, 0);
        var vm = Vm<ChatViewModel>();
        await WaitUntil(() => vm.Items.Count > 0 && vm.Peer != null);
        Check(vm.Items.Count > 0, "进入聊天：读出历史消息");

        string text = "流程测试：你好 " + Suffix;
        vm.Draft = text;
        await vm.SendCommand.ExecuteAsync(null);
        Check(vm.Items.Last().Message.Content == text && vm.Items.Last().IsMine && vm.Draft == "", "发送：气泡出现在右边，输入框清空");
        var theirView = await bob.GetChatHistoryAsync(me, 0, 10);
        Check(theirView.Any(m => m.Content == text), "对方那边读得到这条消息");

        int count = vm.Items.Count;
        string push = "推送测试 " + Suffix;
        await bob.SendMessageAsync(me, push, 0);
        await WaitUntil(() => vm.Items.Count > count);
        Check(vm.Items.Count > count && vm.Items.Last().Message.Content == push && !vm.Items.Last().IsMine, "对方发来的消息实时出现在左边（推送）");
        Settle(_main!, 800);
        Capture(_main!, "flow-chat");

        long unreadFromBob = (await Api.GetConversationsAsync()).First(c => c.Peer.UserId == bobId).Unread;
        Check(unreadFromBob == 0, "正在看着的会话，新消息自动标成已读");
    }

    private static async Task FlowShip()
    {
        Console.WriteLine("卖家发货，买家确认收货（推送到卖家）");
        await EnsureMain();
        using var bob = await LoginClient("bob");
        // alice 先发布一件新商品（重复运行也不会因为"已卖掉"而失败），bob 来买
        long mouse = (await Api.PublishGoodsAsync(new GoodsEdit
        {
            Title = "流程测试鼠标" + Suffix, Description = "罗技无线鼠标，用了半年", Price = 6000,
            CategoryId = (await Api.GetCategoriesAsync())[0].CategoryId,
        })).GoodsId;
        await Api.WaitReviewAsync(mouse);
        var order = await bob.CreateOrderAsync(mouse);
        long before = (await Api.GetMeAsync()).Balance;

        Navigator.ToOrderDetail(order.OrderId);
        var vm = Vm<OrderDetailViewModel>();
        await WaitUntil(() => vm.Loaded);
        Check(vm.IsSeller && vm.CanShip && !vm.CanConfirm, "卖家看到的订单：能发货，不能确认收货");
        vm.ShipNote = "流程测试发货";
        await vm.ShipCommand.ExecuteAsync(null);
        Check(vm.Order?.Status == OrderStatus.Shipped && vm.WaitingBuyer, "发货后：待收货，等买家确认");
        Settle(_main!, 600);
        Capture(_main!, "flow-ship");

        await bob.ConfirmOrderAsync(order.OrderId);                    // 买家那边点确认收货
        await WaitUntil(() => vm.Order?.Status == OrderStatus.Completed);
        Check(vm.Order?.Status == OrderStatus.Completed, "买家确认收货后，卖家这边的详情页实时变成已完成（推送）");
        await AppServices.RefreshMeAsync();
        Check(AppServices.CurrentUser!.Balance == before + 6000, $"货款到账 60 元（{before} → {AppServices.CurrentUser.Balance} 分）");
        Settle(_main!, 600);
        Capture(_main!, "flow-ship-done");
    }

    /// <summary>
    /// 内容安全：一级词拦截、二级词提醒 + 举报 → AI 判定 → 屏蔽（实时推送）、禁售词、AI 驳回商品。
    /// 发违规消息的是一个临时注册的"小号"，不会把 bob 这些演示账号禁言。
    /// </summary>
    private static async Task FlowSafety()
    {
        Console.WriteLine("内容安全");
        await EnsureMain();
        long me = AppServices.MyId;
        string spamUser = "spam" + Suffix;
        using var spam = new TcpApiClient(AppServices.Settings.Host, AppServices.Settings.Port, AppServices.Settings.UseTls);
        await spam.ConnectAsync();
        await spam.RegisterDevAsync(spamUser, "Ab123456", "可疑小号");
        long spamId = (await spam.LoginAsync(spamUser, "Ab123456")).UserId;

        // 1) 我发一级词：界面提示，消息不发出去
        await spam.SendMessageAsync(me, "你好，你那个耳机还在吗");
        Navigator.ToChat(spamId, 0);
        var vm = Vm<ChatViewModel>();
        await WaitUntil(() => vm.Items.Count > 0 && vm.Peer != null);
        vm.Draft = "你个傻 逼";
        await vm.SendCommand.ExecuteAsync(null);
        Check(vm.ErrorMessage.Contains("违规") && vm.Draft == "你个傻 逼", "发一级词：被拦下，提示违规，内容留在输入框");
        vm.Draft = "";
        vm.ErrorMessage = "";

        // 2) 对方发二级词：照常收到，下面有提醒条
        int count = vm.Items.Count;
        await spam.SendMessageAsync(me, "在的 平台抽成太高 你加我vx abc123 直接转账给我 便宜20");
        await WaitUntil(() => vm.Items.Count > count);
        var warned = vm.Items.Last();
        Check(warned.ShowWarn && warned.CanReport, "对方发二级词（加vx/转账）：消息照常显示，下面出现黄色提醒条和「举报」");
        Settle(_main!, 700);
        Capture(_main!, "flow-safety-warn");

        // 3) 举报：AI 判定后屏蔽，界面实时变灰（二级词本身也会被 AI 审，谁先到都行）
        Ui.PromptHook = _ => (4, "让我加微信私下转账");
        await vm.ReportCommand.ExecuteAsync(warned);
        Ui.PromptHook = null;
        Check(warned.Reported || warned.IsMasked, "点举报 → 选类型、写理由 → 提交成功");
        await WaitUntil(() => warned.IsMasked, 20000);
        Check(warned.IsMasked && warned.Content.Contains("屏蔽"), "AI 判定违规：这条消息实时变成「该消息因违规已被屏蔽」");
        try { await spam.SendMessageAsync(me, "在吗"); Check(false, "对方应该已经被禁言"); }
        catch (ApiException ex) { Check(ex.Code == ErrorCode.Forbidden && ex.Message.Contains("禁言"), "发违规消息的人被禁言，再发消息被拒"); }
        Settle(_main!, 700);
        Capture(_main!, "flow-safety-masked");

        // 4) 商品：禁售词直接拦；可疑商品被 AI 驳回
        Navigator.ToPublish();
        var pub = Vm<PublishViewModel>();
        await WaitUntil(() => pub.Categories.Count > 0);
        pub.Title = "代写作业 包过"; pub.PriceText = "50"; pub.Categories[6].IsSelected = true;
        await pub.SubmitCommand.ExecuteAsync(null);
        Check(pub.ErrorMessage.Contains("禁售"), "发布含禁售词（代写）的商品：直接被拦下");
        pub.Title = "招兼职 日结300"; pub.Description = "在宿舍动动手指就能赚钱，私聊，名额有限";
        await pub.SubmitCommand.ExecuteAsync(null);
        await WaitUntil(() => Navigator.Current is GoodsDetailPage);
        var saved = (await Api.GetMyGoodsAsync(null, 1, 5)).Items.First(g => g.Title == "招兼职 日结300");
        string final = await Api.WaitReviewAsync(saved.GoodsId);
        Check(final == GoodsStatus.Rejected, $"可疑商品（刷单兼职）：AI 审核后被驳回下架（结果 {final}）");
        Navigator.ToGoodsDetail(saved.GoodsId);
        var detail = Vm<GoodsDetailViewModel>();
        await WaitUntil(() => detail.Loaded);
        Check(detail.IsRejected && detail.Detail!.RejectReason.Length > 0, $"卖家在详情页看到驳回原因：{detail.Detail?.RejectReason}");
        Settle(_main!, 700);
        Capture(_main!, "flow-safety-rejected");
    }

    /// <summary>忘记密码、修改密码、昵称过一级词库（用一个临时账号，不动演示账号）</summary>
    private static async Task FlowPassword()
    {
        Console.WriteLine("密码与资料");
        string user = "pw" + Suffix, email = user + "@flow.test";
        using var c = new TcpApiClient(AppServices.Settings.Host, AppServices.Settings.Port, AppServices.Settings.UseTls);
        await c.ConnectAsync();
        await c.RegisterDevAsync(user, "Ab123456", "改密码测试", email);

        var vm = new ResetPasswordViewModel { Email = "nobody" + Suffix + "@flow.test" };
        await vm.SendCodeCommand.ExecuteAsync(null);
        Check(vm.ErrorMessage.Contains("没有注册"), "忘记密码：没注册过的邮箱，提示「还没有注册」");
        string? done = null;
        vm.Done += e => done = e;
        vm.Email = email;
        await vm.SendCodeCommand.ExecuteAsync(null);
        Check(vm.Code.Length == 6, "忘记密码：给注册邮箱发验证码（开发模式直接填好）");
        vm.Password = "Reset1234"; vm.Confirm = "Reset1234";
        await vm.ResetCommand.ExecuteAsync(null);
        Check(done == email, "验证码 + 新密码：重置成功");
        try { await c.LoginAsync(user, "Ab123456"); Check(false, "旧密码应该失效"); }
        catch (ApiException) { Check(true, "旧密码登录失败"); }
        Check((await c.LoginAsync(email, "Reset1234")).Username == user, "新密码 + 邮箱登录成功");

        try { await c.ChangePasswordAsync("Wrong1234", "Change123"); Check(false, "原密码错了应该失败"); }
        catch (ApiException ex) { Check(ex.Message.Contains("原密码"), "修改密码：原密码不对被拒"); }
        await c.ChangePasswordAsync("Reset1234", "Change123");
        Check((await c.GetMeAsync()).Username == user, "修改密码后，当前登录不受影响");
        using var c2 = new TcpApiClient(AppServices.Settings.Host, AppServices.Settings.Port, AppServices.Settings.UseTls);
        await c2.ConnectAsync();
        Check((await c2.LoginAsync(user, "Change123")).Username == user, "新密码能登录");

        try { await c.UpdateProfileAsync("傻 逼", null, null); Check(false, "昵称一级词应该被拦"); }
        catch (ApiException ex) { Check(ex.Message.Contains("昵称") && ex.Message.Contains("违规"), "昵称含一级词：被拦下"); }
    }
}
