using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Pages.Chat;
using XiaoYu.App.Pages.Me;
using XiaoYu.App.Pages.Trade;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Shell;

/// <summary>主窗口：顶部导航、未读红点、断线提示、右下角提示、收到推送时提醒</summary>
public partial class MainViewModel : ViewModelBase
{
    private readonly DispatcherTimer _toastTimer = new() { Interval = TimeSpan.FromSeconds(3) };

    [ObservableProperty] private string _nickname = "";
    [ObservableProperty] private string _avatar = "";
    [ObservableProperty] private long _balance;
    [ObservableProperty] private int _unreadCount;
    [ObservableProperty] private string _activeTab = "home";
    [ObservableProperty] private bool _canGoBack;
    [ObservableProperty] private bool _isConnected = true;
    [ObservableProperty] private string _toastText = "";

    public event Action? LoggedOut;

    public MainViewModel()
    {
        _toastTimer.Tick += (_, _) =>
        {
            ToastText = "";
            _toastTimer.Stop();
        };
        AppServices.CurrentUserChanged += OnUserChanged;
        AppServices.UnreadChanged += OnUnreadChanged;
        AppServices.LogoutRequested += OnLogoutRequested;
        Navigator.Navigated += OnNavigated;
        Ui.ToastRequested += ShowToast;
        Api.MessageReceived += OnMessageReceived;
        Api.OrderUpdated += OnOrderUpdated;
        Api.ConnectionChanged += OnConnectionChanged;
        Api.MuteChanged += OnMuteChanged;
        Api.GoodsReviewed += OnGoodsReviewed;
        Api.ReportResultReceived += OnReportResult;
        Api.ProfileReset += OnProfileReset;
        OnUserChanged();
        _ = RefreshUnreadAsync();
    }

    /// <summary>窗口关闭时取消所有订阅</summary>
    public void Detach()
    {
        AppServices.CurrentUserChanged -= OnUserChanged;
        AppServices.UnreadChanged -= OnUnreadChanged;
        AppServices.LogoutRequested -= OnLogoutRequested;
        Navigator.Navigated -= OnNavigated;
        Ui.ToastRequested -= ShowToast;
        Api.MessageReceived -= OnMessageReceived;
        Api.OrderUpdated -= OnOrderUpdated;
        Api.ConnectionChanged -= OnConnectionChanged;
        Api.MuteChanged -= OnMuteChanged;
        Api.GoodsReviewed -= OnGoodsReviewed;
        Api.ReportResultReceived -= OnReportResult;
        Api.ProfileReset -= OnProfileReset;
        _toastTimer.Stop();
    }

    private void OnUserChanged() => Ui.Run(() =>
    {
        var u = AppServices.CurrentUser;
        Nickname = u?.Nickname ?? "";
        Avatar = u?.Avatar ?? "";
        Balance = u?.Balance ?? 0;
    });

    private void OnUnreadChanged() => _ = RefreshUnreadAsync();

    private void OnLogoutRequested() => Ui.Run(() => _ = LogoutAsync());

    private async Task RefreshUnreadAsync()
    {
        try
        {
            int count = await Api.GetUnreadCountAsync();
            Ui.Run(() => UnreadCount = count);
        }
        catch (ApiException)
        {
        }
    }

    private void OnNavigated()
    {
        CanGoBack = Navigator.CanGoBack;
        ActiveTab = Navigator.Current switch
        {
            HomePage or GoodsDetailPage or UserProfilePage => "home",
            PublishPage => "publish",
            ChatListPage or ChatPage => "chat",
            MePage or OrdersPage or OrderDetailPage => "me",
            _ => ActiveTab,
        };
        _ = RefreshUnreadAsync();
    }

    private void ShowToast(string text) => Ui.Run(() =>
    {
        ToastText = text;
        _toastTimer.Stop();
        _toastTimer.Start();
    });

    private void OnMessageReceived(ChatMessage msg)
    {
        _ = RefreshUnreadAsync();
        Ui.Run(() =>
        {
            if (Navigator.Current is ChatPage page && page.PeerId == msg.FromId) return;  // 正在和他聊，不用提醒
            ShowToast("收到新私信：" + msg.Content);
        });
    }

    private void OnOrderUpdated(Order order)
    {
        ShowToast($"订单「{order.GoodsTitle}」{Fmt.OrderStatusText(order.Status)}");
        _ = AppServices.RefreshMeAsync();  // 余额可能变了
    }

    private void OnConnectionChanged(bool connected) => Ui.Run(() => IsConnected = connected);

    // ---------- 内容安全相关的推送 ----------

    private void OnMuteChanged(long mutedUntil)
    {
        ShowToast(Fmt.MuteLeft(mutedUntil) is { Length: > 0 } left ? $"你因违规被禁言 {left}" : "禁言已解除");
        _ = AppServices.RefreshMeAsync();   // 聊天页靠 CurrentUserChanged 更新禁言提示
    }

    private void OnGoodsReviewed(GoodsReviewed e)
    {
        if (e.Status == GoodsStatus.OnSale) ShowToast("你发布的商品审核通过，已经上架");
    }

    private void OnProfileReset(ProfileReset e)
    {
        _ = AppServices.RefreshMeAsync();
        Ui.Run(() => Ui.Info($"{e.Reason}，昵称已重置为「{e.Nickname}」，简介已清空。\n24 小时内不能修改昵称。", "资料已被重置"));
    }

    private void OnReportResult(ReportResult r) => ShowToast(r.Status switch
    {
        "upheld" => "举报成立：对方的消息已被屏蔽，并已被禁言",
        "rejected" => "举报未成立：AI 判定这条消息没有违规",
        _ => "举报已转交管理员人工处理",
    });

    [RelayCommand] private void GoHome() => Navigator.ToHome();
    [RelayCommand] private void GoPublish() => Navigator.ToPublish();
    [RelayCommand] private void GoChat() => Navigator.ToChatList();
    [RelayCommand] private void GoMe() => Navigator.ToMe();
    [RelayCommand] private void Back() => Navigator.Back();

    [RelayCommand]
    private async Task LogoutAsync()
    {
        if (!Ui.Confirm("确定要退出登录吗？")) return;
        try { await Api.LogoutAsync(); } catch (ApiException) { }
        AppServices.SetCurrentUser(null);
        Detach();
        LoggedOut?.Invoke();
    }
}
