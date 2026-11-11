using System.Windows;
using System.Windows.Controls;

namespace XiaoYu.App.Pages.Trade;

/// <summary>
/// 商品详情（负责人：组员A）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
///   goodsId：商品 id
/// </summary>
public partial class GoodsDetailPage : UserControl
{
    public GoodsDetailPage(long goodsId)
    {
        InitializeComponent();
        DataContext = new GoodsDetailViewModel(goodsId);
    }

    // 左栏宽度变了：大图跟着变成同样边长的正方形，圆角裁剪也跟着变
    private void OnMainImageSizeChanged(object sender, SizeChangedEventArgs e)
    {
        double w = MainImageBox.ActualWidth;
        if (w <= 0 || Math.Abs(MainImageBox.Height - w) < 0.5) return;
        MainImageBox.Height = w;
        MainImageClip.Rect = new System.Windows.Rect(0, 0, w, w);
    }
}
