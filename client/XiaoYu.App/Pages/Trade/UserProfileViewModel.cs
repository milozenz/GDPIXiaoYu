using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Models;

namespace XiaoYu.App.Pages.Trade;

/// <summary>用户主页：头像昵称简介、在售/已卖出数量、他在卖的商品、私信按钮</summary>
public partial class UserProfileViewModel : ViewModelBase
{
    private readonly long _userId;

    [ObservableProperty] private UserProfile? _profile;
    [ObservableProperty] private bool _isMe;
    [ObservableProperty] private bool _loaded;
    [ObservableProperty] private bool _isEmpty;

    public ObservableCollection<GoodsBrief> Goods { get; } = new();

    public UserProfileViewModel(long userId) => _userId = userId;

    public override async Task OnNavigatedToAsync()
    {
        await RunAsync(async () =>
        {
            Profile = await Api.GetUserAsync(_userId);
            IsMe = _userId == AppServices.MyId;
            var page = await Api.SearchGoodsAsync(new GoodsQuery { SellerId = _userId, PageSize = 50 });
            Goods.Clear();
            foreach (var g in page.Items) Goods.Add(g);
            IsEmpty = Goods.Count == 0;
            Loaded = true;
        });
    }

    [RelayCommand]
    private void Chat() => Navigator.ToChat(_userId);
}
