using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using XiaoYu.Core.Models;

namespace XiaoYu.Core.Api;

/// <summary>
/// 真正连服务器的实现（公共部分）。
/// 按《协议文档》第 2、3 节：TCP 长连接，每条消息 = 4 字节大端长度 + UTF-8 JSON。
/// 一条后台循环负责收消息：回复按 id 交给等待中的请求，推送触发事件。
/// 断线后每 3 秒自动重连，重连成功后用 user.me 重新绑定推送。
/// useTls = true 时在 TCP 外面再包一层 TLS 加密（线上服务器用），证书按系统的信任链校验。
/// </summary>
public sealed class TcpApiClient : IApiClient
{
    public static readonly JsonSerializerOptions Json = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,   // GoodsId ↔ goods_id
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull, // 没赋值的可选字段不发
    };

    private const int MaxFrame = 8 * 1024 * 1024;
    private static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan HeartbeatInterval = TimeSpan.FromSeconds(30);

    private readonly string _host;
    private readonly int _port;
    private readonly bool _useTls;
    private readonly object _stateLock = new();
    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private readonly ConcurrentDictionary<long, TaskCompletionSource<JsonElement>> _pending = new();

    private TcpClient? _tcp;
    private Stream? _stream;   // 明文时是 NetworkStream，加密时是包在它外面的 SslStream
    private CancellationTokenSource? _loopCts;
    private long _nextId;
    private volatile string? _token;
    private volatile bool _disposed;
    private int _reconnecting;

    public TcpApiClient(string host, int port, bool useTls = false)
    {
        _host = host;
        _port = port;
        _useTls = useTls;
    }

    public bool IsConnected => _stream != null;

    public event Action<ChatMessage>? MessageReceived;
    public event Action<Order>? OrderUpdated;
    public event Action<bool>? ConnectionChanged;
    public event Action<MessageMasked>? MessageMasked;
    public event Action<long>? MuteChanged;
    public event Action<GoodsReviewed>? GoodsReviewed;
    public event Action<ReportResult>? ReportResultReceived;
    public event Action<ProfileReset>? ProfileReset;

    // ======================== 连接 ========================

    public async Task ConnectAsync()
    {
        var tcp = new TcpClient { NoDelay = true };
        try
        {
            await tcp.ConnectAsync(_host, _port).WaitAsync(TimeSpan.FromSeconds(5));
        }
        catch (Exception)
        {
            tcp.Dispose();
            throw new ApiException(ErrorCode.Network, $"连不上服务器 {_host}:{_port}，请检查网络或服务器是否已启动");
        }

        Stream stream = tcp.GetStream();
        if (_useTls)
        {
            // TLS 握手：确认对方确实是这个域名的服务器（证书由系统信任的机构签发），之后收发的数据都是加密的
            var ssl = new SslStream(stream, leaveInnerStreamOpen: false);
            try
            {
                await ssl.AuthenticateAsClientAsync(new SslClientAuthenticationOptions { TargetHost = _host })
                         .WaitAsync(TimeSpan.FromSeconds(10));
            }
            catch (AuthenticationException)
            {
                ssl.Dispose();
                tcp.Dispose();
                throw new ApiException(ErrorCode.Network, $"服务器 {_host} 的证书校验不通过，为了安全没有连接");
            }
            catch (Exception)
            {
                ssl.Dispose();
                tcp.Dispose();
                throw new ApiException(ErrorCode.Network, $"和服务器 {_host}:{_port} 建立加密连接失败，请检查网络");
            }
            stream = ssl;
        }
        var cts = new CancellationTokenSource();
        lock (_stateLock)
        {
            _tcp = tcp;
            _stream = stream;
            _loopCts = cts;
        }
        _ = Task.Run(() => ReadLoopAsync(stream, cts.Token));
        _ = Task.Run(() => HeartbeatLoopAsync(cts.Token));
        RaiseSafely(() => ConnectionChanged?.Invoke(true));
    }

    private async Task ReadLoopAsync(Stream stream, CancellationToken ct)
    {
        var header = new byte[4];
        try
        {
            while (!ct.IsCancellationRequested)
            {
                await stream.ReadExactlyAsync(header, ct);
                int len = BinaryPrimitives.ReadInt32BigEndian(header);
                if (len <= 0 || len > MaxFrame) throw new IOException($"收到异常长度 {len}");
                var body = new byte[len];
                await stream.ReadExactlyAsync(body, ct);
                HandleFrame(body);
            }
        }
        catch (Exception)
        {
            // 断开、超时或数据异常，统一按断线处理
        }
        finally
        {
            OnDisconnected(stream);
        }
    }

    private void HandleFrame(byte[] body)
    {
        JsonElement root;
        using (var doc = JsonDocument.Parse(body)) root = doc.RootElement.Clone();

        if (root.TryGetProperty("push", out var push))
        {
            var data = root.GetProperty("data");
            switch (push.GetString())
            {
                case "chat.new_message":
                    var msg = data.Deserialize<ChatMessage>(Json)!;
                    RaiseSafely(() => MessageReceived?.Invoke(msg));
                    break;
                case "order.updated":
                    var order = data.Deserialize<Order>(Json)!;
                    RaiseSafely(() => OrderUpdated?.Invoke(order));
                    break;
                case "chat.message_masked":
                    var masked = data.Deserialize<MessageMasked>(Json)!;
                    RaiseSafely(() => MessageMasked?.Invoke(masked));
                    break;
                case "user.muted":
                    long until = data.GetProperty("muted_until").GetInt64();
                    RaiseSafely(() => MuteChanged?.Invoke(until));
                    break;
                case "goods.reviewed":
                    var reviewed = data.Deserialize<GoodsReviewed>(Json)!;
                    RaiseSafely(() => GoodsReviewed?.Invoke(reviewed));
                    break;
                case "report.result":
                    var result = data.Deserialize<ReportResult>(Json)!;
                    RaiseSafely(() => ReportResultReceived?.Invoke(result));
                    break;
                case "user.profile_reset":
                    var reset = data.Deserialize<ProfileReset>(Json)!;
                    RaiseSafely(() => ProfileReset?.Invoke(reset));
                    break;
            }
            return;
        }

        long id = root.GetProperty("id").GetInt64();
        if (_pending.TryRemove(id, out var tcs)) tcs.TrySetResult(root);
    }

    private void OnDisconnected(Stream stream)
    {
        lock (_stateLock)
        {
            if (_stream != stream) return;  // 已经处理过了
            _stream = null;
            _loopCts?.Cancel();
            stream.Dispose();
            _tcp?.Dispose();
            _tcp = null;
        }
        foreach (var id in _pending.Keys)
            if (_pending.TryRemove(id, out var tcs))
                tcs.TrySetException(new ApiException(ErrorCode.Network, "和服务器的连接断开了"));

        if (_disposed) return;
        RaiseSafely(() => ConnectionChanged?.Invoke(false));
        _ = ReconnectLoopAsync();
    }

    private async Task ReconnectLoopAsync()
    {
        if (Interlocked.Exchange(ref _reconnecting, 1) == 1) return;
        try
        {
            while (!_disposed && !IsConnected)
            {
                await Task.Delay(3000);
                try
                {
                    await ConnectAsync();
                    if (_token != null) await CallAsync("user.me");  // 告诉服务器"我是谁"，推送才能找到我
                }
                catch (ApiException)
                {
                    // 还连不上，下一轮再试
                }
            }
        }
        finally
        {
            Interlocked.Exchange(ref _reconnecting, 0);
        }
    }

    private async Task HeartbeatLoopAsync(CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested)
            {
                await Task.Delay(HeartbeatInterval, ct);
                try { await CallAsync("sys.ping"); } catch (ApiException) { }
            }
        }
        catch (OperationCanceledException) { }
    }

    private static void RaiseSafely(Action raise)
    {
        try { raise(); }
        catch (Exception) { /* 界面代码里的异常不能把收消息的循环搞挂 */ }
    }

    public void Dispose()
    {
        _disposed = true;
        lock (_stateLock)
        {
            _loopCts?.Cancel();
            _stream?.Dispose();
            _tcp?.Dispose();
        }
    }

    // ======================== 发请求 ========================

    private async Task<JsonElement> CallAsync(string cmd, object? data = null)
    {
        var stream = _stream ?? throw new ApiException(ErrorCode.Network, "还没连上服务器");
        long id = Interlocked.Increment(ref _nextId);

        var request = new JsonObject
        {
            ["id"] = id,
            ["cmd"] = cmd,
            ["data"] = data switch
            {
                null => new JsonObject(),
                JsonNode node => node,
                _ => JsonSerializer.SerializeToNode(data, data.GetType(), Json),
            },
        };
        if (_token != null) request["token"] = _token;
        byte[] body = JsonSerializer.SerializeToUtf8Bytes(request, Json);
        var header = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(header, body.Length);

        var tcs = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[id] = tcs;
        try
        {
            await _writeLock.WaitAsync();
            try
            {
                await stream.WriteAsync(header);
                await stream.WriteAsync(body);
            }
            finally
            {
                _writeLock.Release();
            }
        }
        catch (Exception)
        {
            _pending.TryRemove(id, out _);
            throw new ApiException(ErrorCode.Network, "发送失败，和服务器的连接断开了");
        }

        JsonElement resp;
        try
        {
            resp = await tcs.Task.WaitAsync(RequestTimeout);
        }
        catch (TimeoutException)
        {
            _pending.TryRemove(id, out _);
            throw new ApiException(ErrorCode.Network, "服务器没有响应，请稍后再试");
        }

        int code = resp.GetProperty("code").GetInt32();
        if (code != 0) throw new ApiException(code, resp.GetProperty("msg").GetString() ?? "操作失败");
        return resp.GetProperty("data");
    }

    /// <summary>
    /// 直接发一个命令，返回回复里的 data（失败抛 ApiException）。
    /// 给审核后台这类"界面以外"的程序用，比如 CallRawAsync("admin.cases", new { Area = "manual" })。
    /// </summary>
    public Task<JsonElement> CallRawAsync(string cmd, object? data = null) => CallAsync(cmd, data);

    /// <summary>当前登录的 token（审核后台用它判断登录状态）</summary>
    public string? Token => _token;

    private async Task<T> CallAsync<T>(string cmd, object? data = null) =>
        (await CallAsync(cmd, data)).Deserialize<T>(Json)!;

    private static T Get<T>(JsonElement data, string name) => data.GetProperty(name).Deserialize<T>(Json)!;

    // ======================== 用户 ========================

    public Task<SendCodeResult> SendEmailCodeAsync(string email, string purpose = "register") =>
        CallAsync<SendCodeResult>("user.send_code", new { Email = email, Purpose = purpose });

    public Task ChangePasswordAsync(string oldPassword, string newPassword) =>
        CallAsync("user.change_password", new { OldPassword = oldPassword, NewPassword = newPassword });

    public Task ResetPasswordAsync(string email, string code, string newPassword) =>
        CallAsync("user.reset_password", new { Email = email, Code = code, NewPassword = newPassword });

    public async Task<long> RegisterAsync(string username, string password, string nickname, string? email = null, string? code = null) =>
        Get<long>(await CallAsync("user.register",
            new { Username = username, Password = password, Nickname = nickname, Email = email, Code = code }), "user_id");

    public async Task<UserInfo> LoginAsync(string username, string password)
    {
        var data = await CallAsync("user.login", new { Username = username, Password = password });
        _token = Get<string>(data, "token");
        return Get<UserInfo>(data, "user");
    }

    public async Task LogoutAsync()
    {
        try { await CallAsync("user.logout"); }
        finally { _token = null; }
    }

    public Task<UserInfo> GetMeAsync() => CallAsync<UserInfo>("user.me");

    public Task<UserInfo> UpdateProfileAsync(string? nickname = null, string? avatar = null, string? bio = null) =>
        CallAsync<UserInfo>("user.update_profile", new { Nickname = nickname, Avatar = avatar, Bio = bio });

    public Task<UserProfile> GetUserAsync(long userId) => CallAsync<UserProfile>("user.get", new { UserId = userId });

    public async Task<long> RechargeAsync(long amount) =>
        Get<long>(await CallAsync("user.recharge", new { Amount = amount }), "balance");

    // ======================== 图片 ========================

    public async Task<string> UploadImageAsync(byte[] data, string ext) =>
        Get<string>(await CallAsync("file.upload", new { Ext = ext, Data = Convert.ToBase64String(data) }), "image");

    public async Task<byte[]> GetImageAsync(string image) =>
        Convert.FromBase64String(Get<string>(await CallAsync("file.get", new { Image = image }), "data"));

    // ======================== 商品 ========================

    public async Task<List<Category>> GetCategoriesAsync() =>
        Get<List<Category>>(await CallAsync("goods.categories"), "items");

    public Task<GoodsSaved> PublishGoodsAsync(GoodsEdit goods) => CallAsync<GoodsSaved>("goods.publish", goods);

    public async Task<string> UpdateGoodsAsync(long goodsId, GoodsEdit changes)
    {
        var data = JsonSerializer.SerializeToNode(changes, Json)!.AsObject();
        data["goods_id"] = goodsId;
        var resp = await CallAsync("goods.update", data);
        return resp.TryGetProperty("status", out var s) ? s.GetString() ?? "" : "";
    }

    public Task OffShelfAsync(long goodsId) => CallAsync("goods.off_shelf", new { GoodsId = goodsId });

    public Task OnShelfAsync(long goodsId) => CallAsync("goods.on_shelf", new { GoodsId = goodsId });

    public Task<GoodsDetail> GetGoodsDetailAsync(long goodsId) =>
        CallAsync<GoodsDetail>("goods.detail", new { GoodsId = goodsId });

    public Task<PagedList<GoodsBrief>> SearchGoodsAsync(GoodsQuery query) =>
        CallAsync<PagedList<GoodsBrief>>("goods.search", query);

    public Task<PagedList<GoodsBrief>> GetMyGoodsAsync(string? status = null, int page = 1, int pageSize = 20) =>
        CallAsync<PagedList<GoodsBrief>>("goods.mine", new { Status = status, Page = page, PageSize = pageSize });

    // ======================== 订单 ========================

    public Task<Order> CreateOrderAsync(long goodsId) => CallAsync<Order>("order.create", new { GoodsId = goodsId });

    public Task<Order> CancelOrderAsync(long orderId) => CallAsync<Order>("order.cancel", new { OrderId = orderId });

    public Task<Order> ShipOrderAsync(long orderId, string shipNote = "") =>
        CallAsync<Order>("order.ship", new { OrderId = orderId, ShipNote = shipNote });

    public Task<Order> ConfirmOrderAsync(long orderId) => CallAsync<Order>("order.confirm", new { OrderId = orderId });

    public Task<Order> GetOrderAsync(long orderId) => CallAsync<Order>("order.detail", new { OrderId = orderId });

    public Task<PagedList<Order>> GetOrdersAsync(string role, string? status = null, int page = 1, int pageSize = 20) =>
        CallAsync<PagedList<Order>>("order.list", new { Role = role, Status = status, Page = page, PageSize = pageSize });

    // ======================== 收藏 ========================

    public Task AddFavoriteAsync(long goodsId) => CallAsync("fav.add", new { GoodsId = goodsId });

    public Task RemoveFavoriteAsync(long goodsId) => CallAsync("fav.remove", new { GoodsId = goodsId });

    public async Task<bool> IsFavoriteAsync(long goodsId) =>
        Get<bool>(await CallAsync("fav.check", new { GoodsId = goodsId }), "favorite");

    public Task<PagedList<GoodsBrief>> GetFavoritesAsync(int page = 1, int pageSize = 20) =>
        CallAsync<PagedList<GoodsBrief>>("fav.list", new { Page = page, PageSize = pageSize });

    // ======================== 聊天 ========================

    public Task<ChatMessage> SendMessageAsync(long toUserId, string content, long goodsId = 0) =>
        CallAsync<ChatMessage>("chat.send", new { ToUserId = toUserId, Content = content, GoodsId = goodsId > 0 ? goodsId : (long?)null });

    public async Task<List<ChatMessage>> GetChatHistoryAsync(long peerId, long beforeId = 0, int limit = 20) =>
        Get<List<ChatMessage>>(await CallAsync("chat.history",
            new { PeerId = peerId, BeforeId = beforeId > 0 ? beforeId : (long?)null, Limit = limit }), "items");

    public async Task<List<Conversation>> GetConversationsAsync() =>
        Get<List<Conversation>>(await CallAsync("chat.conversations"), "items");

    public async Task<int> MarkReadAsync(long peerId) =>
        Get<int>(await CallAsync("chat.mark_read", new { PeerId = peerId }), "updated");

    public async Task<int> GetUnreadCountAsync() => Get<int>(await CallAsync("chat.unread_count"), "count");

    // ======================== 内容安全 ========================

    public Task<ReportResult> ReportMessageAsync(long messageId, string type, string reason) =>
        CallAsync<ReportResult>("report.create", new { MessageId = messageId, Type = type, Reason = reason });

    public async Task<long> AppealAsync(string kind, string content, long goodsId = 0) =>
        Get<long>(await CallAsync("appeal.create",
            new { Kind = kind, Content = content, GoodsId = goodsId > 0 ? goodsId : (long?)null }), "appeal_id");
}
