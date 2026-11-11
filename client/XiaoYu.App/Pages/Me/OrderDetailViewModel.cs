using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Me;

/// <summary>
/// 订单详情：进度（已付款 → 已发货 → 已完成）、商品、对方、时间，以及按角色和状态出现的操作：
///   卖家 + 待发货：发货（可写备注）；买家 + 待收货：确认收货；买卖双方 + 待发货：取消。
/// 担保交易：确认收货时才把钱打给卖家；取消会退款给买家并让商品重新上架。
/// </summary>
public partial class OrderDetailViewModel : ViewModelBase
{
    private readonly long _orderId;

    [ObservableProperty] private Order? _order;
    [ObservableProperty] private UserBrief _other = new();
    [ObservableProperty] private bool _loaded;
    [ObservableProperty] private bool _isBuyer;
    [ObservableProperty] private bool _isSeller;
    [ObservableProperty] private bool _isPaid;
    [ObservableProperty] private bool _isShipped;
    [ObservableProperty] private bool _isCompleted;
    [ObservableProperty] private bool _isCancelled;
    [ObservableProperty] private bool _step1Done;
    [ObservableProperty] private bool _step2Done;
    [ObservableProperty] private bool _step3Done;
    [ObservableProperty] private string _shipNote = "";

    public bool CanShip => IsSeller && IsPaid;
    public bool CanCancel => IsPaid && (IsBuyer || IsSeller);
    public bool CanConfirm => IsBuyer && IsShipped;
    public bool WaitingSeller => IsBuyer && IsPaid;
    public bool WaitingBuyer => IsSeller && IsShipped;

    public OrderDetailViewModel(long orderId) => _orderId = orderId;

    public override async Task OnNavigatedToAsync()
    {
        Api.OrderUpdated -= OnOrderUpdated;
        Api.OrderUpdated += OnOrderUpdated;
        await RunAsync(async () => Apply(await Api.GetOrderAsync(_orderId)));
    }

    public override void OnNavigatedFrom() => Api.OrderUpdated -= OnOrderUpdated;

    private void OnOrderUpdated(Order o)
    {
        if (o.OrderId == _orderId) Ui.Run(() => Apply(o));
    }

    private void Apply(Order o)
    {
        Order = o;
        IsBuyer = o.Buyer.UserId == AppServices.MyId;
        IsSeller = o.Seller.UserId == AppServices.MyId;
        Other = IsBuyer ? o.Seller : o.Buyer;
        IsPaid = o.Status == OrderStatus.Paid;
        IsShipped = o.Status == OrderStatus.Shipped;
        IsCompleted = o.Status == OrderStatus.Completed;
        IsCancelled = o.Status == OrderStatus.Cancelled;
        Step1Done = !IsCancelled;
        Step2Done = IsShipped || IsCompleted;
        Step3Done = IsCompleted;
        OnPropertyChanged(nameof(CanShip));
        OnPropertyChanged(nameof(CanCancel));
        OnPropertyChanged(nameof(CanConfirm));
        OnPropertyChanged(nameof(WaitingSeller));
        OnPropertyChanged(nameof(WaitingBuyer));
        Loaded = true;
    }

    [RelayCommand]
    private async Task ShipAsync()
    {
        if (await RunAsync(async () => Apply(await Api.ShipOrderAsync(_orderId, ShipNote.Trim())))) Ui.Toast("已发货");
    }

    [RelayCommand]
    private async Task ConfirmAsync()
    {
        if (!Ui.Confirm("确认收货后，货款会打给卖家。确定吗？", "确认收货")) return;
        if (await RunAsync(async () => Apply(await Api.ConfirmOrderAsync(_orderId))))
        {
            await AppServices.RefreshMeAsync();
            Ui.Toast("交易完成");
        }
    }

    [RelayCommand]
    private async Task CancelAsync()
    {
        if (!Ui.Confirm("取消后货款会退给买家，商品重新上架。确定取消吗？", "取消订单")) return;
        if (await RunAsync(async () => Apply(await Api.CancelOrderAsync(_orderId))))
        {
            await AppServices.RefreshMeAsync();
            Ui.Toast("订单已取消");
        }
    }

    [RelayCommand]
    private void OpenGoods()
    {
        if (Order != null) Navigator.ToGoodsDetail(Order.GoodsId);
    }

    [RelayCommand]
    private void OpenOther() => Navigator.ToUserProfile(Other.UserId);

    [RelayCommand]
    private void ChatOther() => Navigator.ToChat(Other.UserId, Order?.GoodsId ?? 0);
}
