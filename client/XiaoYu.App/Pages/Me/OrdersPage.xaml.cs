using System.Windows.Controls;

namespace XiaoYu.App.Pages.Me;

/// <summary>
/// 订单列表（负责人：组员B）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
///   role：OrderRole.Buyer 我买到的 / OrderRole.Seller 我卖出的
/// </summary>
public partial class OrdersPage : UserControl
{
    public OrdersPage(string role)
    {
        InitializeComponent();
        DataContext = new OrdersViewModel(role);
    }
}
