using System.IO;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Trade;

/// <summary>发布页里的一张已上传的图片</summary>
public partial class PublishImage : ObservableObject
{
    public string Name { get; init; } = "";
    /// <summary>true = 这不是图片，是列表末尾那个"添加图片"的方块</summary>
    public bool IsAdd { get; init; }
    [ObservableProperty] private bool _isCover;
}

/// <summary>
/// 发布 / 编辑商品。editGoodsId 不为空就是编辑：先把原来的内容填进表单，保存时走 UpdateGoodsAsync。
/// 图片：选图 → 压缩到 1280 像素 → 上传 → 拿到图片名；最多 9 张，第一张是封面。
/// 提交前先在界面检查，规则和协议文档 6.3 一样。
/// </summary>
public partial class PublishViewModel : ViewModelBase
{
    private const int MaxImages = 9;
    private readonly long? _editId;

    [ObservableProperty] private string _title = "";
    [ObservableProperty] private string _description = "";
    [ObservableProperty] private string _priceText = "";
    [ObservableProperty] private string _originalPriceText = "";
    [ObservableProperty] private string _conditionKey = "2";
    [ObservableProperty] private bool _isUploading;
    [ObservableProperty] private int _realCount;

    public bool IsEdit => _editId != null;
    public string PageTitle => IsEdit ? "编辑商品" : "发布闲置";
    public string SubmitText => IsEdit ? "保存" : "发布";
    public string[] ConditionNames => Fmt.ConditionNames;

    public ObservableCollection<CategoryItem> Categories { get; } = new();
    public ObservableCollection<PublishImage> Images { get; } = new();

    public PublishViewModel(long? editGoodsId)
    {
        _editId = editGoodsId;
        RefreshImages();
    }

    public override async Task OnNavigatedToAsync()
    {
        await RunAsync(async () =>
        {
            if (Categories.Count == 0)
                foreach (var c in await Api.GetCategoriesAsync())
                    Categories.Add(new CategoryItem { Id = c.CategoryId, Name = c.Name });

            if (_editId is { } id)
            {
                var d = await Api.GetGoodsDetailAsync(id);
                Title = d.Title;
                Description = d.Description;
                PriceText = Fmt.Yuan(d.Price);
                OriginalPriceText = d.OriginalPrice > 0 ? Fmt.Yuan(d.OriginalPrice) : "";
                ConditionKey = d.Condition.ToString();
                foreach (var c in Categories) c.IsSelected = c.Id == d.CategoryId;
                Images.Clear();
                foreach (var name in d.Images) Images.Add(new PublishImage { Name = name });
                RefreshImages();
            }
        });
    }

    /// <summary>真正的图片（不含末尾的"添加"方块）</summary>
    private List<PublishImage> RealImages => Images.Where(i => !i.IsAdd).ToList();

    /// <summary>重新整理列表：第一张标封面；不满 9 张时末尾放一个"添加"方块</summary>
    private void RefreshImages()
    {
        for (int i = Images.Count - 1; i >= 0; i--)
            if (Images[i].IsAdd) Images.RemoveAt(i);
        for (int i = 0; i < Images.Count; i++) Images[i].IsCover = i == 0;
        RealCount = Images.Count;
        if (Images.Count < MaxImages) Images.Add(new PublishImage { IsAdd = true });
    }

    [RelayCommand]
    private void SelectCategory(CategoryItem? item)
    {
        if (item == null) return;
        foreach (var c in Categories) c.IsSelected = c == item;
    }

    [RelayCommand]
    private async Task AddImagesAsync()
    {
        var paths = ImageTools.PickImages();
        if (paths.Length == 0) return;
        IsUploading = true;
        ErrorMessage = "";
        try
        {
            foreach (var path in paths.Take(MaxImages - RealCount))
            {
                var bytes = await Task.Run(() => ImageTools.LoadForUpload(path));
                var name = await Api.UploadImageAsync(bytes, "jpg");
                ImageCache.Put(name, bytes);
                Images.Add(new PublishImage { Name = name });
                RefreshImages();
            }
        }
        catch (ApiException ex) { ErrorMessage = ex.Message; }
        catch (Exception ex) when (ex is IOException or NotSupportedException or InvalidOperationException)
        {
            ErrorMessage = "这张图片打不开，换一张试试";
        }
        finally { IsUploading = false; }
    }

    [RelayCommand]
    private void RemoveImage(PublishImage? image)
    {
        if (image == null) return;
        Images.Remove(image);
        RefreshImages();
    }

    [RelayCommand]
    private async Task SubmitAsync()
    {
        string title = Title.Trim();
        int titleLength = title.EnumerateRunes().Count();
        if (titleLength < 1 || titleLength > 30) { ErrorMessage = "标题要 1-30 个字"; return; }
        if (Description.EnumerateRunes().Count() > 500) { ErrorMessage = "描述最多 500 个字"; return; }
        long? price = Fmt.ParseYuan(PriceText);
        if (price == null || price < 1 || price > 99_999_900) { ErrorMessage = "价格要在 0.01–999999 元之间，最多两位小数"; return; }
        long original = 0;
        if (!string.IsNullOrWhiteSpace(OriginalPriceText))
        {
            long? o = Fmt.ParseYuan(OriginalPriceText);
            if (o == null || o > 99_999_900) { ErrorMessage = "原价格式不对"; return; }
            original = o.Value;
        }
        var category = Categories.FirstOrDefault(c => c.IsSelected);
        if (category == null) { ErrorMessage = "请选择一个分类"; return; }

        var goods = new GoodsEdit
        {
            Title = title,
            Description = Description.Trim(),
            Price = price,
            OriginalPrice = original,
            CategoryId = category.Id,
            Condition = int.Parse(ConditionKey),
            Images = RealImages.Select(i => i.Name).ToList(),
        };

        await RunAsync(async () =>
        {
            if (_editId is { } id)
            {
                string status = await Api.UpdateGoodsAsync(id, goods);
                Ui.Toast(status == GoodsStatus.Reviewing ? "已保存，内容有改动，正在重新审核" : "已保存");
                Navigator.Back();
            }
            else
            {
                var saved = await Api.PublishGoodsAsync(goods);
                Ui.Toast(saved.Status == GoodsStatus.Reviewing ? "已提交，AI 正在审核，通过后自动上架" : "发布成功");
                Navigator.ToGoodsDetail(saved.GoodsId);
            }
        });
    }
}
