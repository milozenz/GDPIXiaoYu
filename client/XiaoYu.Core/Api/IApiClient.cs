using XiaoYu.Core.Models;

namespace XiaoYu.Core.Api;

/// <summary>
/// 客户端和服务器打交道的唯一入口。每个方法对应《协议文档》第 6 节的一个命令。
///
/// 有两个实现：
///   FakeApiClient —— 假数据，不用服务器就能跑，做界面时用它
///   TcpApiClient  —— 真的连服务器
/// 界面代码只认这个接口，所以换实现时界面一行都不用改。
///
/// 所有方法失败时都抛 ApiException，Message 是中文提示。
/// 推送事件（MessageReceived 等）在后台线程触发，改界面前要切回界面线程（见任务说明）。
/// </summary>
public interface IApiClient : IDisposable
{
    // ======================== 连接和推送 ========================

    /// <summary>连接服务器。程序启动时调用一次。</summary>
    Task ConnectAsync();

    bool IsConnected { get; }

    /// <summary>推送：有人给我发了私信</summary>
    event Action<ChatMessage>? MessageReceived;

    /// <summary>推送：我参与的订单状态变了</summary>
    event Action<Order>? OrderUpdated;

    /// <summary>连接状态变了（断开 / 重新连上），参数是现在是否连着</summary>
    event Action<bool>? ConnectionChanged;

    /// <summary>推送：某条消息被屏蔽 / 恢复了（双方都会收到）</summary>
    event Action<MessageMasked>? MessageMasked;

    /// <summary>推送：我的禁言状态变了，参数是新的 muted_until（0 = 解除）</summary>
    event Action<long>? MuteChanged;

    /// <summary>推送：我发布的商品审核通过了</summary>
    event Action<GoodsReviewed>? GoodsReviewed;

    /// <summary>推送：我的举报有结果了</summary>
    event Action<ReportResult>? ReportResultReceived;

    /// <summary>推送：我的昵称和简介因违规被重置了</summary>
    event Action<ProfileReset>? ProfileReset;

    // ======================== 用户 ========================

    /// <summary>给邮箱发验证码（60 秒内只能发一次）。purpose：register 注册 / reset 忘记密码</summary>
    Task<SendCodeResult> SendEmailCodeAsync(string email, string purpose = "register");

    /// <summary>修改密码（要原密码）。成功后其他设备上的登录会失效，当前这个不受影响</summary>
    Task ChangePasswordAsync(string oldPassword, string newPassword);

    /// <summary>忘记密码：用邮箱验证码重置。成功后所有设备都要重新登录</summary>
    Task ResetPasswordAsync(string email, string code, string newPassword);

    /// <summary>注册，返回新用户 id。注册完需要再调 LoginAsync 登录。服务器要求时必须带邮箱和验证码。</summary>
    Task<long> RegisterAsync(string username, string password, string nickname, string? email = null, string? code = null);

    /// <summary>登录（用户名或邮箱都行）。成功后会自动记住 token，之后的请求都会带上。</summary>
    Task<UserInfo> LoginAsync(string username, string password);

    Task LogoutAsync();

    Task<UserInfo> GetMeAsync();

    /// <summary>修改资料，不想改的字段传 null。avatar 传空字符串表示清除头像。</summary>
    Task<UserInfo> UpdateProfileAsync(string? nickname = null, string? avatar = null, string? bio = null);

    Task<UserProfile> GetUserAsync(long userId);

    /// <summary>充值（模拟），amount 单位是分，返回充值后的余额</summary>
    Task<long> RechargeAsync(long amount);

    // ======================== 图片 ========================

    /// <summary>上传图片，返回图片名。ext 是 "jpg" / "png" / "gif" / "webp"。</summary>
    Task<string> UploadImageAsync(byte[] data, string ext);

    /// <summary>按图片名下载图片</summary>
    Task<byte[]> GetImageAsync(string image);

    // ======================== 商品 ========================

    Task<List<Category>> GetCategoriesAsync();

    /// <summary>发布商品。开了 AI 审核时 Status 是 reviewing，审完会推送 GoodsReviewed</summary>
    Task<GoodsSaved> PublishGoodsAsync(GoodsEdit goods);

    /// <summary>修改商品，只提交 changes 里不是 null 的字段。返回修改后的状态（改了内容会重新审核）</summary>
    Task<string> UpdateGoodsAsync(long goodsId, GoodsEdit changes);

    Task OffShelfAsync(long goodsId);

    Task OnShelfAsync(long goodsId);

    Task<GoodsDetail> GetGoodsDetailAsync(long goodsId);

    /// <summary>搜索在售商品（首页列表也用它）</summary>
    Task<PagedList<GoodsBrief>> SearchGoodsAsync(GoodsQuery query);

    /// <summary>我发布的。status 为 null 表示全部。</summary>
    Task<PagedList<GoodsBrief>> GetMyGoodsAsync(string? status = null, int page = 1, int pageSize = 20);

    // ======================== 订单 ========================

    /// <summary>下单并立即付款（从余额扣）</summary>
    Task<Order> CreateOrderAsync(long goodsId);

    Task<Order> CancelOrderAsync(long orderId);

    Task<Order> ShipOrderAsync(long orderId, string shipNote = "");

    Task<Order> ConfirmOrderAsync(long orderId);

    Task<Order> GetOrderAsync(long orderId);

    /// <summary>role 用 OrderRole.Buyer（我买到的）或 OrderRole.Seller（我卖出的）</summary>
    Task<PagedList<Order>> GetOrdersAsync(string role, string? status = null, int page = 1, int pageSize = 20);

    // ======================== 收藏 ========================

    Task AddFavoriteAsync(long goodsId);

    Task RemoveFavoriteAsync(long goodsId);

    Task<bool> IsFavoriteAsync(long goodsId);

    Task<PagedList<GoodsBrief>> GetFavoritesAsync(int page = 1, int pageSize = 20);

    // ======================== 聊天 ========================

    /// <summary>发私信。goodsId 是聊的哪件商品，没有就传 0。</summary>
    Task<ChatMessage> SendMessageAsync(long toUserId, string content, long goodsId = 0);

    /// <summary>聊天记录，按时间从早到晚。beforeId 不为 0 时取比它更早的（往上翻）。</summary>
    Task<List<ChatMessage>> GetChatHistoryAsync(long peerId, long beforeId = 0, int limit = 20);

    Task<List<Conversation>> GetConversationsAsync();

    /// <summary>把对方发给我的消息标成已读，返回标了几条</summary>
    Task<int> MarkReadAsync(long peerId);

    Task<int> GetUnreadCountAsync();

    // ======================== 内容安全 ========================

    /// <summary>举报别人发给我的一条消息。type 见 ReportTypes</summary>
    Task<ReportResult> ReportMessageAsync(long messageId, string type, string reason);

    /// <summary>申诉：kind = "mute"（禁言）或 "goods"（商品被驳回，要带 goodsId）</summary>
    Task<long> AppealAsync(string kind, string content, long goodsId = 0);
}
