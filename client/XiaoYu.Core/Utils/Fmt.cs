using System.Globalization;
using XiaoYu.Core.Models;

namespace XiaoYu.Core.Utils;

/// <summary>把服务器给的原始数据转成给人看的文字</summary>
public static class Fmt
{
    /// <summary>分 → "¥320" / "¥15.50"</summary>
    public static string Money(long cents)
    {
        string sign = cents < 0 ? "-" : "";
        cents = Math.Abs(cents);
        return cents % 100 == 0
            ? $"{sign}¥{cents / 100}"
            : $"{sign}¥{cents / 100}.{cents % 100:00}";
    }

    /// <summary>分 → "320" / "15.50"，不带 ¥（价格大字配小号 ¥ 时用）</summary>
    public static string Yuan(long cents) =>
        cents % 100 == 0 ? $"{cents / 100}" : $"{cents / 100}.{Math.Abs(cents % 100):00}";

    /// <summary>用户输入的元 → 分。"12.5" → 1250。格式不对或超过两位小数返回 null。</summary>
    public static long? ParseYuan(string? text)
    {
        if (string.IsNullOrWhiteSpace(text)) return null;
        text = text.Trim().TrimStart('¥', '￥');
        if (!decimal.TryParse(text, NumberStyles.Number, CultureInfo.InvariantCulture, out var yuan)) return null;
        if (yuan < 0 || yuan > 100_000_000m) return null;
        decimal cents = yuan * 100;
        if (cents != Math.Floor(cents)) return null;
        return (long)cents;
    }

    public static DateTime ToLocalTime(long unixSeconds) =>
        DateTimeOffset.FromUnixTimeSeconds(unixSeconds).LocalDateTime;

    /// <summary>Unix 秒 → "刚刚" / "5分钟前" / "3小时前" / "2天前" / "2026-10-01"</summary>
    public static string TimeAgo(long unixSeconds)
    {
        if (unixSeconds <= 0) return "";
        var seconds = DateTimeOffset.UtcNow.ToUnixTimeSeconds() - unixSeconds;
        if (seconds < 60) return "刚刚";
        if (seconds < 3600) return $"{seconds / 60}分钟前";
        if (seconds < 86400) return $"{seconds / 3600}小时前";
        if (seconds < 86400 * 7) return $"{seconds / 86400}天前";
        return ToLocalTime(unixSeconds).ToString("yyyy-MM-dd");
    }

    /// <summary>Unix 秒 → "2026-11-07 15:30"，0 显示为空</summary>
    public static string DateTimeText(long unixSeconds) =>
        unixSeconds <= 0 ? "" : ToLocalTime(unixSeconds).ToString("yyyy-MM-dd HH:mm");

    public static string GoodsStatusText(string status) => status switch
    {
        GoodsStatus.OnSale => "在售",
        GoodsStatus.Sold => "已卖出",
        GoodsStatus.OffShelf => "已下架",
        GoodsStatus.Reviewing => "审核中",
        GoodsStatus.Rejected => "已驳回",
        _ => status,
    };

    /// <summary>禁言还剩多久，比如"11 小时 52 分钟"；没被禁言返回空字符串</summary>
    public static string MuteLeft(long mutedUntil)
    {
        long left = mutedUntil - DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        if (left <= 0) return "";
        long h = left / 3600, m = (left % 3600 + 59) / 60;
        if (m == 60) { h++; m = 0; }
        return h > 0 ? $"{h} 小时 {m} 分钟" : $"{m} 分钟";
    }

    public static string OrderStatusText(string status) => status switch
    {
        OrderStatus.Paid => "待发货",
        OrderStatus.Shipped => "待收货",
        OrderStatus.Completed => "已完成",
        OrderStatus.Cancelled => "已取消",
        _ => status,
    };

    public static readonly string[] ConditionNames = { "全新", "几乎全新", "轻微使用痕迹", "明显使用痕迹" };

    public static string ConditionText(int condition) =>
        condition >= 0 && condition < ConditionNames.Length ? ConditionNames[condition] : "";

    public static readonly (string Value, string Name)[] SortOptions =
    {
        (GoodsSort.Newest, "最新发布"),
        (GoodsSort.PriceAsc, "价格从低到高"),
        (GoodsSort.PriceDesc, "价格从高到低"),
        (GoodsSort.Hot, "最多浏览"),
    };
}
