using System.Windows.Controls;

namespace XiaoYu.App.Pages.Trade;

/// <summary>首页。构造函数的参数是和 Navigator 约定好的，不要改。</summary>
public partial class HomePage : UserControl
{
    private readonly HomeViewModel _vm = new();

    public HomePage()
    {
        InitializeComponent();
        DataContext = _vm;
    }

    // 滚到快到底时自动加载下一页
    private void OnScroll(object sender, ScrollChangedEventArgs e)
    {
        if (e.ExtentHeightChange != 0 || e.VerticalChange <= 0) return;
        if (e.VerticalOffset + e.ViewportHeight >= e.ExtentHeight - 240 && _vm.HasMore && !_vm.IsBusy)
            _vm.LoadMoreCommand.Execute(null);
    }
}
