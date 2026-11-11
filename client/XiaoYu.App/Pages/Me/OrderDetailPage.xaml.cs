using System.Windows.Controls;

namespace XiaoYu.App.Pages.Me;

/// <summary>
/// 订单详情（负责人：组员B）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
///   orderId：订单 id
/// </summary>
public partial class OrderDetailPage : UserControl
{
    public OrderDetailPage(long orderId)
    {
        InitializeComponent();
        DataContext = new OrderDetailViewModel(orderId);
    }
}
