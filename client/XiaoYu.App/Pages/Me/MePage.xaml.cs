using System.Windows.Controls;

namespace XiaoYu.App.Pages.Me;

/// <summary>
/// 我的（负责人：组员B）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
/// </summary>
public partial class MePage : UserControl
{
    public MePage()
    {
        InitializeComponent();
        DataContext = new MeViewModel();
    }
}
