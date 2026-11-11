using System.Windows.Controls;

namespace XiaoYu.App.Pages.Trade;

/// <summary>
/// 发布 / 编辑商品（负责人：组员A）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
///   editGoodsId：要编辑的商品 id；null 表示发布新商品
/// </summary>
public partial class PublishPage : UserControl
{
    public PublishPage(long? editGoodsId = null)
    {
        InitializeComponent();
        DataContext = new PublishViewModel(editGoodsId);
    }
}
