using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;

namespace XiaoYu.App.Pages.Chat;

/// <summary>聊天页。构造函数的参数是和 Navigator 约定好的，不要改。</summary>
public partial class ChatPage : UserControl
{
    /// <summary>正在和谁聊（主窗口收到私信时，用它判断要不要弹提示）</summary>
    public long PeerId { get; }

    public ChatPage(long peerId, long goodsId = 0)
    {
        InitializeComponent();
        PeerId = peerId;
        var vm = new ChatViewModel(peerId, goodsId);
        vm.ScrollToEndRequested += () => Dispatcher.BeginInvoke(DispatcherPriority.Background, () => Scroller.ScrollToEnd());
        DataContext = vm;
        Loaded += (_, _) => InputBox.Focus();
    }

    // 回车发送，Shift+回车换行
    private void OnInputKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && (Keyboard.Modifiers & ModifierKeys.Shift) == 0)
        {
            if (DataContext is ChatViewModel vm) vm.SendCommand.Execute(null);
            e.Handled = true;
        }
    }
}
