using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Models;

namespace XiaoYu.App.Pages.Me;

/// <summary>订单状态筛选标签（Value 为空 = 全部）</summary>
public partial class StatusFilter : ObservableObject
{
    public string Value { get; init; } = "";
    public string Name { get; init; } = "";
    [ObservableProperty] private bool _isSelected;
}

/// <summary>列表里的一行：订单 + 对方（我是买家就显示卖家，反之亦然）</summary>
public class OrderRow
{
    public Order Order { get; init; } = new();
    public UserBrief Other { get; init; } = new();
}

/// <summary>订单列表：role 是 buyer（我买到的）或 seller（我卖出的），可按状态筛选，可加载更多。</summary>
public partial class OrdersViewModel : ViewModelBase
{
    private const int PageSize = 20;
    private readonly string _role;
    private string? _status;
    private int _page = 1;

    [ObservableProperty] private bool _hasMore;
    [ObservableProperty] private bool _isEmpty;

    public string Title => _role == OrderRole.Buyer ? "我买到的" : "我卖出的";
    public string Glyph => _role == OrderRole.Buyer ? "" : "";

    public ObservableCollection<StatusFilter> Filters { get; } = new()
    {
        new StatusFilter { Value = "", Name = "全部", IsSelected = true },
        new StatusFilter { Value = OrderStatus.Paid, Name = "待发货" },
        new StatusFilter { Value = OrderStatus.Shipped, Name = "待收货" },
        new StatusFilter { Value = OrderStatus.Completed, Name = "已完成" },
        new StatusFilter { Value = OrderStatus.Cancelled, Name = "已取消" },
    };
    public ObservableCollection<OrderRow> Rows { get; } = new();

    public OrdersViewModel(string role) => _role = role;

    public override async Task OnNavigatedToAsync()
    {
        Api.OrderUpdated -= OnOrderUpdated;
        Api.OrderUpdated += OnOrderUpdated;
        await ReloadAsync();
    }

    public override void OnNavigatedFrom() => Api.OrderUpdated -= OnOrderUpdated;

    private void OnOrderUpdated(Order order) => Ui.Run(() => _ = ReloadAsync());

    private OrderRow Wrap(Order o) => new() { Order = o, Other = _role == OrderRole.Buyer ? o.Seller : o.Buyer };

    private async Task ReloadAsync()
    {
        _page = 1;
        await RunAsync(async () =>
        {
            var page = await Api.GetOrdersAsync(_role, _status, 1, PageSize);
            Rows.Clear();
            foreach (var o in page.Items) Rows.Add(Wrap(o));
            HasMore = page.HasMore;
        });
        IsEmpty = Rows.Count == 0 && string.IsNullOrEmpty(ErrorMessage);
    }

    [RelayCommand]
    private Task SelectFilterAsync(StatusFilter? filter)
    {
        if (filter == null) return Task.CompletedTask;
        foreach (var f in Filters) f.IsSelected = f == filter;
        _status = string.IsNullOrEmpty(filter.Value) ? null : filter.Value;
        return ReloadAsync();
    }

    [RelayCommand]
    private async Task LoadMoreAsync()
    {
        if (!HasMore || IsBusy) return;
        await RunAsync(async () =>
        {
            var page = await Api.GetOrdersAsync(_role, _status, _page + 1, PageSize);
            _page++;
            foreach (var o in page.Items) Rows.Add(Wrap(o));
            HasMore = page.HasMore;
        });
    }

    [RelayCommand]
    private void Open(OrderRow? row)
    {
        if (row != null) Navigator.ToOrderDetail(row.Order.OrderId);
    }
}
