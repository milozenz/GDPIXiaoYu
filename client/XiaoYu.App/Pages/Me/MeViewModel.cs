using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Me;

/// <summary>
/// "我的"页：资料（可编辑、可换头像）、余额与充值、买到的/卖出的入口（待处理会亮红点）、
/// 我发布的 / 我的收藏。
/// </summary>
public partial class MeViewModel : ViewModelBase
{
    [ObservableProperty] private UserInfo? _me;
    [ObservableProperty] private bool _isEditing;
    [ObservableProperty] private bool _isRecharging;
    [ObservableProperty] private string _editNickname = "";
    [ObservableProperty] private string _editBio = "";
    [ObservableProperty] private string _tab = "mine";
    [ObservableProperty] private bool _showMine = true;
    [ObservableProperty] private bool _showFavorites;
    [ObservableProperty] private bool _mineEmpty;
    [ObservableProperty] private bool _favoritesEmpty;
    [ObservableProperty] private int _buyerPending;     // 待收货的订单数
    [ObservableProperty] private int _sellerPending;    // 待发货的订单数

    public ObservableCollection<GoodsBrief> MyGoods { get; } = new();
    public ObservableCollection<GoodsBrief> Favorites { get; } = new();

    partial void OnTabChanged(string value)
    {
        ShowMine = value == "mine";
        ShowFavorites = value == "fav";
    }

    public override async Task OnNavigatedToAsync()
    {
        await AppServices.RefreshMeAsync();
        Me = AppServices.CurrentUser;
        await RunAsync(async () =>
        {
            var mine = await Api.GetMyGoodsAsync(null, 1, 50);
            MyGoods.Clear();
            foreach (var g in mine.Items) MyGoods.Add(g);
            MineEmpty = MyGoods.Count == 0;

            var fav = await Api.GetFavoritesAsync(1, 50);
            Favorites.Clear();
            foreach (var g in fav.Items) Favorites.Add(g);
            FavoritesEmpty = Favorites.Count == 0;

            BuyerPending = (await Api.GetOrdersAsync(OrderRole.Buyer, OrderStatus.Shipped, 1, 1)).Total;
            SellerPending = (await Api.GetOrdersAsync(OrderRole.Seller, OrderStatus.Paid, 1, 1)).Total;
        });
    }

    [RelayCommand]
    private void OpenBuying() => Navigator.ToOrders(OrderRole.Buyer);

    [RelayCommand]
    private void OpenSelling() => Navigator.ToOrders(OrderRole.Seller);

    [RelayCommand]
    private void Logout() => AppServices.RequestLogout();

    [RelayCommand]
    private void ChangePassword()
    {
        var window = new ChangePasswordWindow { Owner = System.Windows.Application.Current.MainWindow };
        if (window.ShowDialog() == true) Ui.Toast("密码已修改，其他设备上的登录已失效");
    }

    // ---------------- 资料

    [RelayCommand]
    private void StartEdit()
    {
        EditNickname = Me?.Nickname ?? "";
        EditBio = Me?.Bio ?? "";
        ErrorMessage = "";
        IsEditing = true;
    }

    [RelayCommand]
    private void CancelEdit() => IsEditing = false;

    [RelayCommand]
    private async Task SaveProfileAsync()
    {
        string nickname = EditNickname.Trim();
        int length = nickname.EnumerateRunes().Count();
        if (length < 1 || length > 20) { ErrorMessage = "昵称要 1-20 个字"; return; }
        if (EditBio.EnumerateRunes().Count() > 100) { ErrorMessage = "简介最多 100 个字"; return; }
        if (await RunAsync(async () =>
        {
            var user = await Api.UpdateProfileAsync(nickname, null, EditBio.Trim());
            AppServices.SetCurrentUser(user);
            Me = user;
        }))
        {
            IsEditing = false;
            Ui.Toast("资料已保存");
        }
    }

    [RelayCommand]
    private async Task ChangeAvatarAsync()
    {
        var paths = ImageTools.PickImages(false);
        if (paths.Length == 0) return;
        await RunAsync(async () =>
        {
            var bytes = await Task.Run(() => ImageTools.LoadForUpload(paths[0]));
            var name = await Api.UploadImageAsync(bytes, "jpg");
            ImageCache.Put(name, bytes);
            var user = await Api.UpdateProfileAsync(null, name, null);
            AppServices.SetCurrentUser(user);
            Me = user;
            Ui.Toast("头像已更新");
        });
    }

    // ---------------- 钱

    [RelayCommand]
    private void ToggleRecharge() => IsRecharging = !IsRecharging;

    [RelayCommand]
    private async Task RechargeAsync(string? cents)
    {
        if (!long.TryParse(cents, out long amount)) return;
        if (await RunAsync(async () =>
        {
            await Api.RechargeAsync(amount);
            await AppServices.RefreshMeAsync();
            Me = AppServices.CurrentUser;
        }))
        {
            Ui.Toast($"已充值 {Fmt.Money(amount)}");
            IsRecharging = false;
        }
    }
}
