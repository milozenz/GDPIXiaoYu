// 数据模型：和《协议文档》5.1 节的公共对象一一对应。
// 属性名用 C# 习惯的大驼峰（GoodsId），收发 JSON 时会自动转成下划线（goods_id）。
// 金额都是"分"（long），时间都是 Unix 秒（long）。显示时用 Fmt 里的方法转换。
namespace XiaoYu.Core.Models;

/// <summary>用户简要信息（列表、卡片里显示卖家/买家用）</summary>
public class UserBrief
{
    public long UserId { get; set; }
    public string Nickname { get; set; } = "";
    public string Avatar { get; set; } = "";
}

/// <summary>自己的完整信息（user.login / user.me）</summary>
public class UserInfo
{
    public long UserId { get; set; }
    public string Username { get; set; } = "";
    public string Nickname { get; set; } = "";
    public string Avatar { get; set; } = "";
    public string Bio { get; set; } = "";
    public long Balance { get; set; }
    public long CreatedAt { get; set; }
    /// <summary>注册时验证过的邮箱（小写），老账号可能是空的</summary>
    public string Email { get; set; } = "";
    /// <summary>管理员（能登录审核后台）</summary>
    public bool IsAdmin { get; set; }
    /// <summary>禁言到什么时候（Unix 秒），0 = 没被禁言</summary>
    public long MutedUntil { get; set; }
    /// <summary>昵称因违规被重置后，到这个时间之前不能改（Unix 秒）</summary>
    public long NicknameLockedUntil { get; set; }

    public bool IsMuted => MutedUntil > DateTimeOffset.UtcNow.ToUnixTimeSeconds();
}

/// <summary>别人的主页（user.get）</summary>
public class UserProfile
{
    public long UserId { get; set; }
    public string Nickname { get; set; } = "";
    public string Avatar { get; set; } = "";
    public string Bio { get; set; } = "";
    public long CreatedAt { get; set; }
    public int OnSaleCount { get; set; }
    public int SoldCount { get; set; }
}

public class Category
{
    public long CategoryId { get; set; }
    public string Name { get; set; } = "";
}

/// <summary>商品卡片（列表里用）</summary>
public class GoodsBrief
{
    public long GoodsId { get; set; }
    public string Title { get; set; } = "";
    public long Price { get; set; }
    public string Cover { get; set; } = "";
    public string Status { get; set; } = GoodsStatus.OnSale;
    public int ViewCount { get; set; }
    public long CreatedAt { get; set; }
    public UserBrief Seller { get; set; } = new();
    /// <summary>被驳回的原因（只有 status = rejected 时有内容）</summary>
    public string RejectReason { get; set; } = "";
}

/// <summary>商品详情 = 卡片的全部字段 + 详细信息</summary>
public class GoodsDetail : GoodsBrief
{
    public string Description { get; set; } = "";
    public long OriginalPrice { get; set; }
    public long CategoryId { get; set; }
    public string CategoryName { get; set; } = "";
    public int Condition { get; set; }
    public List<string> Images { get; set; } = new();
    public long UpdatedAt { get; set; }
}

/// <summary>发布或修改商品时提交的字段。修改时不想改的字段保持 null 就行。</summary>
public class GoodsEdit
{
    public string? Title { get; set; }
    public string? Description { get; set; }
    public long? Price { get; set; }
    public long? OriginalPrice { get; set; }
    public long? CategoryId { get; set; }
    public int? Condition { get; set; }
    public List<string>? Images { get; set; }
}

/// <summary>搜索条件（goods.search）。不需要的条件保持 null。</summary>
public class GoodsQuery
{
    public string? Keyword { get; set; }
    public long? CategoryId { get; set; }
    /// <summary>只看某个卖家的（用户主页用）</summary>
    public long? SellerId { get; set; }
    public long? MinPrice { get; set; }
    public long? MaxPrice { get; set; }
    public string Sort { get; set; } = GoodsSort.Newest;
    public int Page { get; set; } = 1;
    public int PageSize { get; set; } = 20;
}

public class Order
{
    public long OrderId { get; set; }
    public string OrderNo { get; set; } = "";
    public long GoodsId { get; set; }
    public string GoodsTitle { get; set; } = "";
    public string GoodsCover { get; set; } = "";
    public long Price { get; set; }
    public string Status { get; set; } = OrderStatus.Paid;
    public string ShipNote { get; set; } = "";
    public UserBrief Buyer { get; set; } = new();
    public UserBrief Seller { get; set; } = new();
    public long CreatedAt { get; set; }
    public long ShippedAt { get; set; }
    public long CompletedAt { get; set; }
    public long CancelledAt { get; set; }
}

public class ChatMessage
{
    public long MessageId { get; set; }
    public long FromId { get; set; }
    public long ToId { get; set; }
    public long GoodsId { get; set; }
    public string Content { get; set; } = "";
    public bool IsRead { get; set; }
    public long CreatedAt { get; set; }
    /// <summary>none 正常 / warn 命中二级词（接收方显示提醒）/ masked 已屏蔽（Content 已换成提示语）</summary>
    public string Flag { get; set; } = MessageFlag.None;
    /// <summary>命中的类别：scam 诈骗引流 / threat 骚扰威胁</summary>
    public string FlagKind { get; set; } = "";
}

/// <summary>发验证码的结果。DevCode 只在服务器没配邮箱（开发模式）时有值。</summary>
public class SendCodeResult
{
    public string DevCode { get; set; } = "";
    public int ExpiresIn { get; set; }
}

/// <summary>发布商品的结果：Status 是 reviewing（AI 审核中）或 on_sale</summary>
public class GoodsSaved
{
    public long GoodsId { get; set; }
    public string Status { get; set; } = GoodsStatus.OnSale;
}

/// <summary>举报的结果（report.create 的回复和 report.result 推送）</summary>
public class ReportResult
{
    public long ReportId { get; set; }
    /// <summary>pending 等 AI / upheld 成立 / rejected 驳回 / manual 转人工</summary>
    public string Status { get; set; } = "";
}

/// <summary>推送：商品审核完了（目前只有通过时推送）</summary>
public class GoodsReviewed
{
    public long GoodsId { get; set; }
    public string Status { get; set; } = "";
}

/// <summary>推送：我的昵称和简介因违规被重置了</summary>
public class ProfileReset
{
    public string Nickname { get; set; } = "";
    public string Reason { get; set; } = "";
    public long NicknameLockedUntil { get; set; }
}

/// <summary>推送：某条消息被屏蔽（或恢复）了</summary>
public class MessageMasked
{
    public long MessageId { get; set; }
    public long FromId { get; set; }
    public long ToId { get; set; }
    public bool Masked { get; set; }
}

/// <summary>会话列表的一行</summary>
public class Conversation
{
    public UserBrief Peer { get; set; } = new();
    public ChatMessage LastMessage { get; set; } = new();
    public int Unread { get; set; }
}

/// <summary>分页列表</summary>
public class PagedList<T>
{
    public int Total { get; set; }
    public int Page { get; set; }
    public int PageSize { get; set; }
    public List<T> Items { get; set; } = new();

    /// <summary>后面还有没有下一页</summary>
    public bool HasMore => Page * PageSize < Total;
}

// ---------- 各种状态值，写代码时用这些常量，别手写字符串 ----------

public static class GoodsStatus
{
    public const string OnSale = "on_sale";
    public const string Sold = "sold";
    public const string OffShelf = "off_shelf";
    /// <summary>AI 审核中（发布后、修改后）</summary>
    public const string Reviewing = "reviewing";
    /// <summary>审核被驳回，原因见 RejectReason。卖家不能自己重新上架，可以修改或申诉</summary>
    public const string Rejected = "rejected";
}

public static class MessageFlag
{
    public const string None = "none";
    public const string Warn = "warn";
    public const string Masked = "masked";
}

/// <summary>举报类型（和服务器一致）</summary>
public static class ReportTypes
{
    public static readonly (string Code, string Label)[] All =
    {
        ("abuse", "人身攻击"), ("porn", "色情低俗"), ("political", "政治敏感 / 违法"),
        ("threat", "骚扰威胁"), ("scam", "诈骗引流"), ("other", "其他"),
    };
}

public static class GoodsSort
{
    public const string Newest = "newest";
    public const string PriceAsc = "price_asc";
    public const string PriceDesc = "price_desc";
    public const string Hot = "hot";
}

public static class OrderStatus
{
    public const string Paid = "paid";
    public const string Shipped = "shipped";
    public const string Completed = "completed";
    public const string Cancelled = "cancelled";
}

public static class OrderRole
{
    public const string Buyer = "buyer";
    public const string Seller = "seller";
}
