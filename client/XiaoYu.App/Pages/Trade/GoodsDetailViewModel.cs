using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Trade;

/// <summary>商品详情里的一张小图（点它换大图）</summary>
public partial class ImageThumb : ObservableObject
{
    public string Name { get; init; } = "";
    [ObservableProperty] private bool _isSelected;
}

/// <summary>
/// 商品详情的 ViewModel：大图 + 缩略图、价格和信息、卖家、收藏、聊一聊、立即购买。
/// 自己的商品：不显示购买，改成"编辑"和"下架/重新上架"。
/// </summary>
public partial class GoodsDetailViewModel : ViewModelBase
{
    private readonly long _goodsId;

    [ObservableProperty] private GoodsDetail? _detail;
    [ObservableProperty] private string _mainImage = "";
    [ObservableProperty] private bool _loaded;
    [ObservableProperty] private bool _isMine;
    [ObservableProperty] private bool _isFavorite;
    [ObservableProperty] private bool _isOnSale;
    [ObservableProperty] private bool _isOffShelf;
    [ObservableProperty] private bool _isSold;
    [ObservableProperty] private bool _isReviewing;
    [ObservableProperty] private bool _isRejected;

    public ObservableCollection<ImageThumb> Thumbs { get; } = new();

    public GoodsDetailViewModel(long goodsId) => _goodsId = goodsId;

    public override async Task OnNavigatedToAsync()
    {
        // AI 审核出结果时服务器会推送，正在看这件商品就自动刷新，不用手动点"刷新"
        Api.GoodsReviewed -= OnGoodsReviewed;
        Api.GoodsReviewed += OnGoodsReviewed;
        await RunAsync(async () =>
        {
            var d = await Api.GetGoodsDetailAsync(_goodsId);
            Detail = d;
            IsMine = d.Seller.UserId == AppServices.MyId;
            IsOnSale = d.Status == GoodsStatus.OnSale;
            IsOffShelf = d.Status == GoodsStatus.OffShelf;
            IsSold = d.Status == GoodsStatus.Sold;
            IsReviewing = d.Status == GoodsStatus.Reviewing;
            IsRejected = d.Status == GoodsStatus.Rejected;

            Thumbs.Clear();
            var images = d.Images.Count > 0 ? d.Images : (string.IsNullOrEmpty(d.Cover) ? new List<string>() : new List<string> { d.Cover });
            foreach (var name in images) Thumbs.Add(new ImageThumb { Name = name });
            if (Thumbs.Count > 0) { Thumbs[0].IsSelected = true; MainImage = Thumbs[0].Name; }

            // 先查完收藏状态再算"加载完"：否则用户很快点了心形，晚到的旧结果会把它覆盖掉
            if (!IsMine)
            {
                try { IsFavorite = await Api.IsFavoriteAsync(_goodsId); }
                catch (ApiException) { }
            }
            Loaded = true;
        });
    }

    public override void OnNavigatedFrom() => Api.GoodsReviewed -= OnGoodsReviewed;

    private void OnGoodsReviewed(GoodsReviewed e)
    {
        if (e.GoodsId == _goodsId) Ui.Run(() => _ = OnNavigatedToAsync());
    }

    [RelayCommand]
    private void SelectImage(ImageThumb? thumb)
    {
        if (thumb == null) return;
        foreach (var t in Thumbs) t.IsSelected = t == thumb;
        MainImage = thumb.Name;
    }

    [RelayCommand]
    private async Task ToggleFavoriteAsync()
    {
        bool target = !IsFavorite;
        IsFavorite = target;                       // 先变，界面立刻有反馈（心跳动画）
        try
        {
            if (target) await Api.AddFavoriteAsync(_goodsId); else await Api.RemoveFavoriteAsync(_goodsId);
        }
        catch (ApiException ex)
        {
            IsFavorite = !target;                  // 失败了变回去
            ErrorMessage = ex.Message;
        }
    }

    [RelayCommand]
    private void OpenSeller()
    {
        if (Detail != null) Navigator.ToUserProfile(Detail.Seller.UserId);
    }

    [RelayCommand]
    private void Chat()
    {
        if (Detail != null) Navigator.ToChat(Detail.Seller.UserId, Detail.GoodsId);
    }

    [RelayCommand]
    private void Edit() => Navigator.ToEditGoods(_goodsId);

    /// <summary>商品被驳回后申诉，进人工审核</summary>
    [RelayCommand]
    private async Task AppealAsync()
    {
        var input = Ui.Prompt("申诉驳回", $"驳回原因：{Detail?.RejectReason}\n说明一下为什么这件商品没有问题，管理员会人工复核。",
            "申诉理由（必填，最多 200 字）");
        if (input == null) return;
        if (await RunAsync(() => Api.AppealAsync("goods", input.Value.Text, _goodsId)))
            Ui.Toast("申诉已提交，请等待管理员处理");
    }

    /// <summary>审核中：过一会儿刷新一下看看结果</summary>
    [RelayCommand]
    private Task RefreshAsync() => OnNavigatedToAsync();

    [RelayCommand]
    private async Task BuyAsync()
    {
        if (Detail == null) return;
        if (!Ui.Confirm($"确认以 {Fmt.Money(Detail.Price)} 买下「{Detail.Title}」？\n款项会先由平台保管，确认收货后才打给卖家。", "立即购买")) return;
        await RunAsync(async () =>
        {
            try
            {
                var order = await Api.CreateOrderAsync(_goodsId);
                await AppServices.RefreshMeAsync();      // 余额变了
                Ui.Toast("下单成功");
                Navigator.ToOrderDetail(order.OrderId);
            }
            catch (ApiException ex) when (ex.Code == ErrorCode.NoMoney)
            {
                ErrorMessage = "余额不足，去「我的」充值吧";
            }
        });
    }

    [RelayCommand]
    private async Task OffShelfAsync()
    {
        if (!Ui.Confirm("下架后别人就看不到这件商品了，确定吗？", "下架")) return;
        if (await RunAsync(() => Api.OffShelfAsync(_goodsId))) { Ui.Toast("已下架"); await OnNavigatedToAsync(); }
    }

    [RelayCommand]
    private async Task ReShelfAsync()   // 不能叫 OnShelfAsync：工具包会去掉开头的 On，生成的命令就成了 ShelfCommand
    {
        if (await RunAsync(() => Api.OnShelfAsync(_goodsId))) { Ui.Toast("已重新上架"); await OnNavigatedToAsync(); }
    }
}
