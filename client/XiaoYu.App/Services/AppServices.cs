using System.IO;
using System.Text.Json;
using XiaoYu.Core.Api;
using XiaoYu.Core.Fake;
using XiaoYu.Core.Models;

namespace XiaoYu.App.Services;

/// <summary>
/// appsettings.json 里的配置。exe 旁边没有这个文件时用下面的默认值。
/// 平时开发：默认用假数据。
/// 线上版（dotnet publish -p:Online=true，给老师下载的那个 exe）：默认连线上服务器，
/// 这样就算直接在压缩包里双击 exe、读不到配置文件，也能连上。
/// </summary>
public class AppSettings
{
#if ONLINE
    public bool UseFakeApi { get; set; } = false;
    public string Host { get; set; } = "server.aaalez.com";
    public int Port { get; set; } = 443;
    public bool UseTls { get; set; } = true;
#else
    /// <summary>true = 用假数据（不用服务器）；false = 连真服务器</summary>
    public bool UseFakeApi { get; set; } = true;
    public string Host { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 9527;
    /// <summary>是否用 TLS 加密连接（线上服务器要开；自己电脑上的服务器不用）</summary>
    public bool UseTls { get; set; } = false;
#endif
}

/// <summary>
/// 全局共享的东西：接口、当前登录用户、配置。任何地方都能用：
///   AppServices.Api.SearchGoodsAsync(...)
///   AppServices.MyId   // 我的 user_id
/// </summary>
public static class AppServices
{
    public static AppSettings Settings { get; private set; } = new();
    public static IApiClient Api { get; private set; } = null!;

    /// <summary>当前登录的用户。登录前是 null。</summary>
    public static UserInfo? CurrentUser { get; private set; }

    /// <summary>我的 user_id，没登录是 0</summary>
    public static long MyId => CurrentUser?.UserId ?? 0;

    /// <summary>当前用户信息变了（昵称、头像、余额）。顶部栏靠它刷新。</summary>
    public static event Action? CurrentUserChanged;

    /// <summary>未读消息数可能变了（比如聊天页标记了已读）。顶部栏靠它刷新红点。</summary>
    public static event Action? UnreadChanged;

    /// <summary>某个页面请求退出登录（我的页面的退出按钮）。主窗口负责确认和跳转。</summary>
    public static event Action? LogoutRequested;

    public static void RequestLogout() => LogoutRequested?.Invoke();

    public static void Init()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "appsettings.json");
        if (File.Exists(path))
        {
            Settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true, ReadCommentHandling = JsonCommentHandling.Skip })
                ?? new AppSettings();
        }
        Api = Settings.UseFakeApi ? new FakeApiClient() : new TcpApiClient(Settings.Host, Settings.Port, Settings.UseTls);
    }

    public static void SetCurrentUser(UserInfo? user)
    {
        CurrentUser = user;
        CurrentUserChanged?.Invoke();
    }

    /// <summary>重新拉一次自己的信息（比如买完东西余额变了）</summary>
    public static async Task RefreshMeAsync()
    {
        try
        {
            SetCurrentUser(await Api.GetMeAsync());
        }
        catch (ApiException)
        {
            // 刷新失败不影响使用，下次再刷
        }
    }

    /// <summary>通知顶部栏重新查未读数</summary>
    public static void NotifyUnreadChanged() => UnreadChanged?.Invoke();
}
