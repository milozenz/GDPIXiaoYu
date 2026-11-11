using System.Windows.Controls;

namespace XiaoYu.App.Pages.Chat;

/// <summary>
/// 消息列表（负责人：组员B）。
/// 构造函数的参数是和 Navigator 约定好的，不要改。
/// </summary>
public partial class ChatListPage : UserControl
{
    public ChatListPage()
    {
        InitializeComponent();
        DataContext = new ChatListViewModel();
    }
}
