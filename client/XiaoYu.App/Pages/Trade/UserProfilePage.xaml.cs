using System.Windows.Controls;

namespace XiaoYu.App.Pages.Trade;

/// <summary>
/// 用户主页（负责人：组员A）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
///   userId：用户 id
/// </summary>
public partial class UserProfilePage : UserControl
{
    public UserProfilePage(long userId)
    {
        InitializeComponent();
        DataContext = new UserProfileViewModel(userId);
    }
}
