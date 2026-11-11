using System.Windows;
using System.Windows.Controls;
using XiaoYu.App.Pages.Chat;
using XiaoYu.App.Pages.Me;
using XiaoYu.App.Pages.Trade;
using XiaoYu.App.ViewModels;

namespace XiaoYu.App.Services;

/// <summary>
/// 页面跳转。所有页面之间的跳转都通过这里，比如：
///   Navigator.ToGoodsDetail(goods.GoodsId);   // 打开商品详情
///   Navigator.ToChat(sellerId, goodsId);      // 和卖家聊天
///   Navigator.Back();                         // 返回上一页
///
/// 每次显示页面时，会调用页面 ViewModel 的 OnNavigatedToAsync()（在里面加载数据）；
/// 离开页面时调用 OnNavigatedFrom()（在里面取消订阅推送事件）。
///
/// 注意：下面各个页面的构造函数参数是约定好的，组员不要改。
/// </summary>
public static class Navigator
{
    private static ContentControl? _host;
    private static readonly Stack<FrameworkElement> BackStack = new();

    /// <summary>页面切换后触发（顶部导航高亮、返回按钮靠它更新）</summary>
    public static event Action? Navigated;

    public static FrameworkElement? Current => _host?.Content as FrameworkElement;

    public static bool CanGoBack => BackStack.Count > 0;

    public static void Init(ContentControl host)
    {
        _host = host;
        BackStack.Clear();
    }

    // ======================== 各个页面 ========================

    public static void ToHome() => GoRoot(new HomePage());

    public static void ToPublish() => GoRoot(new PublishPage());

    public static void ToEditGoods(long goodsId) => Go(new PublishPage(goodsId));

    public static void ToGoodsDetail(long goodsId) => Go(new GoodsDetailPage(goodsId));

    public static void ToUserProfile(long userId) => Go(new UserProfilePage(userId));

    public static void ToChatList() => GoRoot(new ChatListPage());

    public static void ToChat(long peerId, long goodsId = 0) => Go(new ChatPage(peerId, goodsId));

    public static void ToMe() => GoRoot(new MePage());

    /// <summary>role：OrderRole.Buyer 我买到的 / OrderRole.Seller 我卖出的</summary>
    public static void ToOrders(string role) => Go(new OrdersPage(role));

    public static void ToOrderDetail(long orderId) => Go(new OrderDetailPage(orderId));

    // ======================== 跳转逻辑 ========================

    /// <summary>打开一个新页面，可以返回</summary>
    public static void Go(FrameworkElement page)
    {
        if (_host == null) return;
        if (Current is { } old)
        {
            Leave(old);
            BackStack.Push(old);
        }
        Show(page);
    }

    /// <summary>切到顶部导航的页面（首页/发布/消息/我的），清空返回记录</summary>
    public static void GoRoot(FrameworkElement page)
    {
        if (_host == null) return;
        if (Current is { } old) Leave(old);
        BackStack.Clear();
        Show(page);
    }

    public static void Back()
    {
        if (_host == null || BackStack.Count == 0) return;
        if (Current is { } old) Leave(old);
        Show(BackStack.Pop());
    }

    private static void Show(FrameworkElement page)
    {
        _host!.Content = page;
        Motion.Play(page, 18, 0, 260);   // 淡入 + 轻轻上滑
        if (page.DataContext is ViewModelBase vm) _ = vm.LoadSafelyAsync();
        Navigated?.Invoke();
    }

    private static void Leave(FrameworkElement page)
    {
        if (page.DataContext is ViewModelBase vm) vm.OnNavigatedFrom();
    }
}
