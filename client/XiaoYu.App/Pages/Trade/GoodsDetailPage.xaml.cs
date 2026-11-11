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
}
