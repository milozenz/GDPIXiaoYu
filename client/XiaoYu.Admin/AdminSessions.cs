using System.Collections.Concurrent;
using System.Security.Cryptography;
using XiaoYu.Core.Api;

namespace XiaoYu.Admin;

/// <summary>一个已登录的管理员：一条到服务器的连接 + 他的基本信息</summary>
public sealed class AdminSession
{
    public required TcpApiClient Api { get; init; }
    public required long UserId { get; init; }
    public required string Username { get; init; }
    public required string Nickname { get; init; }
    public DateTime CreatedAt { get; } = DateTime.UtcNow;
    public DateTime LastSeen { get; set; } = DateTime.UtcNow;
}

/// <summary>
/// 登录状态管理。
/// 登录成功后生成一个随机编号放进浏览器 Cookie（HttpOnly，网页脚本读不到），
/// 编号 → AdminSession 存在内存里。后台程序重启后所有人要重新登录。
/// 超过 IdleMinutes 没操作、或登录满 12 小时，自动退出并断开连接。
/// </summary>
public sealed class AdminSessions(AdminOptions options, ILogger<AdminSessions> log) : BackgroundService
{
    public const string CookieName = "xy_admin";
    private static readonly TimeSpan MaxAge = TimeSpan.FromHours(12);
    private const int MaxSessions = 50;

    private readonly ConcurrentDictionary<string, AdminSession> _sessions = new();

    public void SignIn(HttpContext http, AdminSession session)
    {
        // 正常只有几个管理员；太多就先踢掉最久没动的
        while (_sessions.Count >= MaxSessions)
        {
            var oldest = _sessions.MinBy(kv => kv.Value.LastSeen);
            Remove(oldest.Key);
        }
        string id = Convert.ToHexString(RandomNumberGenerator.GetBytes(32));
        _sessions[id] = session;
        http.Response.Cookies.Append(CookieName, id, new CookieOptions
        {
            HttpOnly = true,
            Secure = options.SecureCookie,
            SameSite = SameSiteMode.Strict,
            Path = "/",
            MaxAge = MaxAge,
            IsEssential = true,
        });
        log.LogInformation("管理员 {User} 登录后台", session.Username);
    }

    /// <summary>找到这次请求对应的管理员；没登录或已过期返回 null</summary>
    public AdminSession? Find(HttpContext http)
    {
        if (!http.Request.Cookies.TryGetValue(CookieName, out var id) || string.IsNullOrEmpty(id)) return null;
        if (!_sessions.TryGetValue(id, out var session)) return null;
        if (Expired(session, DateTime.UtcNow))
        {
            Remove(id);
            return null;
        }
        session.LastSeen = DateTime.UtcNow;
        return session;
    }

    public void SignOut(HttpContext http)
    {
        if (http.Request.Cookies.TryGetValue(CookieName, out var id) && !string.IsNullOrEmpty(id)) Remove(id);
        http.Response.Cookies.Delete(CookieName, new CookieOptions { Path = "/", Secure = options.SecureCookie });
    }

    private bool Expired(AdminSession s, DateTime now) =>
        now - s.LastSeen > TimeSpan.FromMinutes(options.IdleMinutes) || now - s.CreatedAt > MaxAge;

    private void Remove(string id)
    {
        if (_sessions.TryRemove(id, out var session)) _ = CloseAsync(session);
    }

    // 告诉服务器退出登录（让 token 作废），再断开连接
    private static async Task CloseAsync(AdminSession session)
    {
        try { await session.Api.LogoutAsync(); }
        catch (ApiException) { }
        session.Api.Dispose();
    }

    // 每分钟清理一次过期的登录
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMinutes(1));
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
            {
                var now = DateTime.UtcNow;
                foreach (var (id, session) in _sessions)
                    if (Expired(session, now)) Remove(id);
            }
        }
        catch (OperationCanceledException) { }
    }
}
