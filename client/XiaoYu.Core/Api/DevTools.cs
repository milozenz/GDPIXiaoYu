using XiaoYu.Core.Models;

namespace XiaoYu.Core.Api;

/// <summary>
/// 给灌数据、联调、端到端测试这些工具用的小帮手（界面代码不用）。
/// </summary>
public static class DevTools
{
    /// <summary>
    /// 用"开发模式验证码"注册：服务器没配真实邮箱时，user.send_code 会把验证码直接返回。
    /// 服务器配了真实邮箱就拿不到验证码，会抛异常提示。
    /// </summary>
    public static async Task<long> RegisterDevAsync(this IApiClient api, string username, string password, string nickname,
        string? email = null)
    {
        email ??= username.ToLowerInvariant() + "@xiaoyu.test";
        var r = await api.SendEmailCodeAsync(email);
        if (string.IsNullOrEmpty(r.DevCode))
            throw new InvalidOperationException("服务器配置了真实邮箱，拿不到开发模式验证码：先在 server.conf 里注释掉 smtp_url 再运行");
        return await api.RegisterAsync(username, password, nickname, email, r.DevCode);
    }

    /// <summary>等 AI 审完自己刚发布的商品，返回最终状态（on_sale / rejected）。超时返回 reviewing。</summary>
    public static async Task<string> WaitReviewAsync(this IApiClient seller, long goodsId, int timeoutMs = 20000)
    {
        var end = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (true)
        {
            var mine = await seller.GetMyGoodsAsync(null, 1, 50);
            string status = mine.Items.FirstOrDefault(g => g.GoodsId == goodsId)?.Status ?? GoodsStatus.Reviewing;
            if (status != GoodsStatus.Reviewing || DateTime.UtcNow > end) return status;
            await Task.Delay(500);
        }
    }
}
