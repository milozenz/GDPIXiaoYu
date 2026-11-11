using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;

namespace XiaoYu.App.Pages.Trade;

/// <summary>分类标签（"全部"的 Id 是 0）</summary>
public partial class CategoryItem : ObservableObject
{
    public long Id { get; init; }
    public string Name { get; init; } = "";
    [ObservableProperty] private bool _isSelected;
}

/// <summary>排序标签</summary>
public partial class SortItem : ObservableObject
{
    public string Value { get; init; } = "";
    public string Name { get; init; } = "";
    [ObservableProperty] private bool _isSelected;
}

/// <summary>
/// 首页的 ViewModel：搜索、分类、排序、商品网格、加载更多（滚到底也会自动加载）。
/// 条件一变就从第 1 页重新查；"加载更多"把下一页追加到后面。
/// </summary>
public partial class HomeViewModel : ViewModelBase
{
    private const int PageSize = 20;
    private long? _categoryId;
    private string _sort = GoodsSort.Newest;
    private string _term = "";
    private int _page = 1;

    [ObservableProperty] private string _keyword = "";
    [ObservableProperty] private bool _hasMore;
    [ObservableProperty] private bool _showSkeleton = true;   // 第一次加载时显示灰色占位卡片
    [ObservableProperty] private bool _isEmpty;               // 查完了但一件都没有

    public ObservableCollection<CategoryItem> Categories { get; } = new();
    public ObservableCollection<SortItem> Sorts { get; } = new()
    {
        new SortItem { Value = GoodsSort.Newest, Name = "最新", IsSelected = true },
        new SortItem { Value = GoodsSort.PriceAsc, Name = "价格 ↑" },
        new SortItem { Value = GoodsSort.PriceDesc, Name = "价格 ↓" },
        new SortItem { Value = GoodsSort.Hot, Name = "热门" },
    };
    public ObservableCollection<GoodsBrief> Goods { get; } = new();
    public int[] SkeletonSlots { get; } = Enumerable.Range(0, 10).ToArray();

    public override async Task OnNavigatedToAsync()
    {
        if (Categories.Count == 0)
        {
            Categories.Add(new CategoryItem { Id = 0, Name = "全部", IsSelected = true });
            try
            {
                foreach (var c in await Api.GetCategoriesAsync())
                    Categories.Add(new CategoryItem { Id = c.CategoryId, Name = c.Name });
            }
            catch (ApiException ex) { ErrorMessage = ex.Message; }
        }
        await ReloadAsync();
    }

    private async Task ReloadAsync()
    {
        _page = 1;
        await RunAsync(async () =>
        {
            var page = await Api.SearchGoodsAsync(Query(1));
            Goods.Clear();
            foreach (var g in page.Items) Goods.Add(g);
            HasMore = page.HasMore;
        });
        ShowSkeleton = false;
        IsEmpty = Goods.Count == 0 && string.IsNullOrEmpty(ErrorMessage);
    }

    private GoodsQuery Query(int page) => new()
    {
        Keyword = string.IsNullOrWhiteSpace(_term) ? null : _term.Trim(),
        CategoryId = _categoryId,
        Sort = _sort,
        Page = page,
        PageSize = PageSize,
    };

    [RelayCommand]
    private Task SearchAsync()
    {
        _term = Keyword;
        return ReloadAsync();
    }

    [RelayCommand]
    private Task ClearSearchAsync()
    {
        Keyword = "";
        _term = "";
        return ReloadAsync();
    }

    [RelayCommand]
    private Task SelectCategoryAsync(CategoryItem? item)
    {
        if (item == null) return Task.CompletedTask;
        foreach (var c in Categories) c.IsSelected = c == item;
        _categoryId = item.Id == 0 ? null : item.Id;
        return ReloadAsync();
    }

    [RelayCommand]
    private Task SelectSortAsync(SortItem? item)
    {
        if (item == null) return Task.CompletedTask;
        foreach (var s in Sorts) s.IsSelected = s == item;
        _sort = item.Value;
        return ReloadAsync();
    }

    [RelayCommand]
    private async Task LoadMoreAsync()
    {
        if (!HasMore || IsBusy) return;
        await RunAsync(async () =>
        {
            var page = await Api.SearchGoodsAsync(Query(_page + 1));
            _page++;
            foreach (var g in page.Items) Goods.Add(g);
            HasMore = page.HasMore;
        });
    }
}
