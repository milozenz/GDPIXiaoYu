using CommunityToolkit.Mvvm.ComponentModel;
using XiaoYu.App.Services;
using XiaoYu.Core.Api;

namespace XiaoYu.App.ViewModels;

/// <summary>
/// 所有 ViewModel 的基类。提供：
///   Api          —— 调接口
///   IsBusy       —— 正在加载（界面上可以绑一个"加载中…"）
///   ErrorMessage —— 出错提示（界面上用 ErrorText 样式绑它）
///   RunAsync     —— 帮你处理"加载中"和错误提示
///
/// 写法示例（CommunityToolkit.Mvvm）：
///   public partial class HomeViewModel : ViewModelBase
///   {
///       [ObservableProperty] private string _keyword = "";      // 自动生成 Keyword 属性，改了界面会刷新
///       public ObservableCollection&lt;GoodsBrief&gt; Goods { get; } = new();
///
///       public override Task OnNavigatedToAsync() => SearchAsync();
///
///       [RelayCommand]                                           // 自动生成 SearchCommand，按钮绑它
///       private Task SearchAsync() => RunAsync(async () =>
///       {
///           var page = await Api.SearchGoodsAsync(new GoodsQuery { Keyword = Keyword });
///           Goods.Clear();
///           foreach (var g in page.Items) Goods.Add(g);
///       });
///   }
/// </summary>
public abstract partial class ViewModelBase : ObservableObject
{
    protected static IApiClient Api => AppServices.Api;

    [ObservableProperty] private bool _isBusy;
    [ObservableProperty] private string _errorMessage = "";

    /// <summary>页面显示时调用（第一次打开、或者从下一页返回时）。在这里加载数据。</summary>
    public virtual Task OnNavigatedToAsync() => Task.CompletedTask;

    /// <summary>离开页面时调用。订阅了推送事件的，在这里取消订阅。</summary>
    public virtual void OnNavigatedFrom() { }

    /// <summary>
    /// 执行一段要调接口的代码：自动设置 IsBusy，出错时把提示写进 ErrorMessage。
    /// 成功返回 true，失败返回 false。
    /// </summary>
    protected async Task<bool> RunAsync(Func<Task> work)
    {
        IsBusy = true;
        ErrorMessage = "";
        try
        {
            await work();
            return true;
        }
        catch (ApiException ex)
        {
            ErrorMessage = ex.Message;
            return false;
        }
        finally
        {
            IsBusy = false;
        }
    }

    /// <summary>Navigator 用：加载数据，并且保证异常不会让程序崩溃</summary>
    internal async Task LoadSafelyAsync()
    {
        try
        {
            await OnNavigatedToAsync();
        }
        catch (Exception ex)
        {
            ErrorMessage = "页面加载出错：" + ex.Message;
        }
    }
}
