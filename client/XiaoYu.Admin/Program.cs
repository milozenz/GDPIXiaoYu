// 广理职小鱼 · 审核后台（网页版）
//
//   浏览器 ──HTTP──▶ 本程序 ──TCP（和客户端同一套协议）──▶ 小鱼服务器（C 语言）
//
// 本程序不碰数据库。管理员登录后，为他开一条到服务器的连接（XiaoYu.Core 的 TcpApiClient），
// 网页上的每个操作都翻译成一条 admin.* 命令发给服务器；"是不是管理员"由服务器检查。
// 网页本身是 wwwroot 里的三个静态文件：index.html、app.css、app.js。
//
// 接口一览（都在 /api 下，返回 JSON；出错时返回 {msg: "给人看的提示"}）：
//   POST /login {username, password}      登录（只有管理员账号能登录）
//   POST /logout                          退出
//   GET  /me                              当前管理员
//   GET  /stats                           概览数字               → admin.stats
//   GET  /cases?area=&status=&page=       案件列表               → admin.cases
//   GET  /cases/{id}                      案件详情               → admin.case_detail
//   POST /cases/{id}/resolve {action, note?, hours?}  处理案件   → admin.resolve
//   GET  /users?keyword=&muted=&page=     用户列表               → admin.users
//   POST /users/{id}/mute {hours}         禁言加时               → admin.mute
//   POST /users/{id}/unmute               解除禁言               → admin.unmute
//   GET  /goods?status=&page=             商品列表               → admin.goods
//   GET  /blocked?kind=&page=             一级词拦截记录         → admin.blocked
//   GET  /image?name=                     商品图片               → file.get
using System.Net;
using System.Text.RegularExpressions;
using XiaoYu.Admin;
using XiaoYu.Core.Api;

var builder = WebApplication.CreateBuilder(args);
var options = builder.Configuration.GetSection("XiaoYu").Get<AdminOptions>() ?? new AdminOptions();
builder.Services.AddSingleton(options);
builder.Services.AddSingleton<AdminSessions>();
builder.Services.AddHostedService(sp => sp.GetRequiredService<AdminSessions>());
builder.Services.AddSingleton<LoginLimiter>();

var app = builder.Build();

// ---- 安全相关的响应头 + 防跨站请求伪造 ----
app.Use(async (http, next) =>
{
    var headers = http.Response.Headers;
    headers["X-Content-Type-Options"] = "nosniff";
    headers["X-Frame-Options"] = "DENY";
    headers["Referrer-Policy"] = "no-referrer";
    headers["Content-Security-Policy"] =
        "default-src 'self'; img-src 'self' blob: data:; style-src 'self'; script-src 'self'; " +
        "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";
    if (http.Request.Path.StartsWithSegments("/api"))
    {
        headers.CacheControl = "no-store";
        // 改数据的请求必须带这个请求头。别的网站的页面没法带上它，所以没法冒充管理员发请求
        if (!HttpMethods.IsGet(http.Request.Method) && http.Request.Headers["X-XiaoYu-Admin"] != "1")
        {
            http.Response.StatusCode = StatusCodes.Status403Forbidden;
            await http.Response.WriteAsJsonAsync(new { msg = "请求来源不正确" });
            return;
        }
    }
    await next();
});

app.UseDefaultFiles();
app.UseStaticFiles(new StaticFileOptions
{
    // 网页文件每次都向服务器确认有没有新版本，更新后刷新就能看到
    OnPrepareResponse = ctx => ctx.Context.Response.Headers.CacheControl = "no-cache",
});

var api = app.MapGroup("/api");

// ======================== 登录 ========================

api.MapPost("/login", async (LoginBody body, HttpContext http, AdminSessions sessions, LoginLimiter limiter) =>
{
    string ip = ClientIp(http);
    if (!limiter.Allowed(ip, out int wait))
        return Error(StatusCodes.Status429TooManyRequests, $"密码错误次数太多，请 {wait} 分钟后再试");
    string username = body.Username?.Trim() ?? "", password = body.Password ?? "";
    if (username.Length is 0 or > 64 || password.Length is 0 or > 64)
        return Error(StatusCodes.Status400BadRequest, "请输入用户名（或邮箱）和密码");

    var client = new TcpApiClient(options.ServerHost, options.ServerPort, options.ServerTls);
    try
    {
        await client.ConnectAsync();
        var me = await client.LoginAsync(username, password);
        if (!me.IsAdmin)
        {
            try { await client.LogoutAsync(); } catch (ApiException) { }
            client.Dispose();
            limiter.Failed(ip);
            return Error(StatusCodes.Status403Forbidden, "这个账号不是管理员，不能登录审核后台");
        }
        limiter.Succeeded(ip);
        sessions.SignIn(http, new AdminSession
        {
            Api = client,
            UserId = me.UserId,
            Username = me.Username,
            Nickname = me.Nickname,
        });
        return Results.Json(new { user_id = me.UserId, username = me.Username, nickname = me.Nickname });
    }
    catch (ApiException ex)
    {
        client.Dispose();
        if (ex.Code == ErrorCode.Network) return Error(StatusCodes.Status502BadGateway, "后台连不上小鱼服务器，请稍后再试");
        limiter.Failed(ip);
        return Error(StatusCodes.Status400BadRequest, ex.Message);
    }
});

api.MapPost("/logout", (HttpContext http, AdminSessions sessions) =>
{
    sessions.SignOut(http);
    return Results.Json(new { });
});

api.MapGet("/me", (HttpContext http, AdminSessions sessions) =>
    sessions.Find(http) is { } s
        ? Results.Json(new { user_id = s.UserId, username = s.Username, nickname = s.Nickname })
        : NeedLogin());

// ======================== 审核 ========================

api.MapGet("/stats", (HttpContext http, AdminSessions sessions) =>
    Forward(http, sessions, "admin.stats"));

api.MapGet("/cases", (HttpContext http, AdminSessions sessions, string? area, string? status, int? page) =>
    Forward(http, sessions, "admin.cases",
        new { Area = Blank(area), Status = Blank(status), Page = page ?? 1, PageSize = 20 }));

api.MapGet("/cases/{id:long}", (long id, HttpContext http, AdminSessions sessions) =>
    Forward(http, sessions, "admin.case_detail", new { CaseId = id }));

api.MapPost("/cases/{id:long}/resolve", (long id, ResolveBody body, HttpContext http, AdminSessions sessions) =>
    Forward(http, sessions, "admin.resolve",
        new { CaseId = id, body.Action, Note = Blank(body.Note), body.Hours }));

// ======================== 用户、商品、拦截记录 ========================

api.MapGet("/users", (HttpContext http, AdminSessions sessions, string? keyword, bool? muted, int? page) =>
    Forward(http, sessions, "admin.users",
        new { Keyword = Blank(keyword?.Trim()), MutedOnly = muted == true ? true : (bool?)null, Page = page ?? 1, PageSize = 20 }));

api.MapPost("/users/{id:long}/mute", (long id, MuteBody body, HttpContext http, AdminSessions sessions) =>
    Forward(http, sessions, "admin.mute", new { UserId = id, body.Hours }));

api.MapPost("/users/{id:long}/unmute", (long id, HttpContext http, AdminSessions sessions) =>
    Forward(http, sessions, "admin.unmute", new { UserId = id }));

api.MapGet("/goods", (HttpContext http, AdminSessions sessions, string? status, int? page) =>
    Forward(http, sessions, "admin.goods", new { Status = Blank(status), Page = page ?? 1, PageSize = 20 }));

api.MapGet("/blocked", (HttpContext http, AdminSessions sessions, string? kind, int? page) =>
    Forward(http, sessions, "admin.blocked", new { Kind = Blank(kind), Page = page ?? 1, PageSize = 20 }));

// 服务器上的图片名 = 32 位十六进制 + 扩展名（和服务器 files.c 的规则一致）
var imageName = new Regex(@"^[0-9a-f]{32}\.(jpg|png|gif|webp)$");
api.MapGet("/image", async (HttpContext http, AdminSessions sessions, string? name) =>
{
    if (sessions.Find(http) is not { } s) return NeedLogin();
    if (name == null || !imageName.IsMatch(name)) return Error(StatusCodes.Status400BadRequest, "图片名格式不对");
    try
    {
        byte[] bytes = await s.Api.GetImageAsync(name);
        http.Response.Headers.CacheControl = "private, max-age=3600";
        string type = Path.GetExtension(name) switch
        {
            ".png" => "image/png",
            ".gif" => "image/gif",
            ".webp" => "image/webp",
            _ => "image/jpeg",
        };
        return Results.Bytes(bytes, type);
    }
    catch (ApiException ex)
    {
        return ApiError(http, sessions, ex);
    }
});

app.Run();

// ======================== 小工具 ========================

// 把一条命令转给服务器，原样返回服务器回复里的 data
static async Task<IResult> Forward(HttpContext http, AdminSessions sessions, string cmd, object? data = null)
{
    if (sessions.Find(http) is not { } s) return NeedLogin();
    try
    {
        var result = await s.Api.CallRawAsync(cmd, data);
        return Results.Text(result.GetRawText(), "application/json; charset=utf-8");
    }
    catch (ApiException ex)
    {
        return ApiError(http, sessions, ex);
    }
}

// 服务器返回的错误码基本就是 HTTP 状态码（400/403/404/409……），直接沿用
static IResult ApiError(HttpContext http, AdminSessions sessions, ApiException ex)
{
    if (ex.Code == ErrorCode.NotLogin)
    {
        sessions.SignOut(http);
        return NeedLogin("登录已过期，请重新登录");
    }
    if (ex.Code == ErrorCode.Network) return Error(StatusCodes.Status502BadGateway, ex.Message);
    return Error(ex.Code is >= 400 and < 600 ? ex.Code : 500, ex.Message);
}

static IResult NeedLogin(string msg = "请先登录") => Error(StatusCodes.Status401Unauthorized, msg);

static IResult Error(int status, string msg) => Results.Json(new { msg }, statusCode: status);

static string? Blank(string? s) => string.IsNullOrEmpty(s) ? null : s;

// 通过 Cloudflare 隧道进来的请求，真实 IP 在 CF-Connecting-IP 里（只信本机转发过来的）
static string ClientIp(HttpContext http)
{
    var remote = http.Connection.RemoteIpAddress;
    if (remote != null && IPAddress.IsLoopback(remote) &&
        http.Request.Headers.TryGetValue("CF-Connecting-IP", out var cf) && !string.IsNullOrEmpty(cf))
        return cf.ToString();
    return remote?.ToString() ?? "unknown";
}

record LoginBody(string? Username, string? Password);

record ResolveBody(string? Action, string? Note, int? Hours);

record MuteBody(int? Hours);
