namespace XiaoYu.Admin;

/// <summary>
/// appsettings.json 里 "XiaoYu" 一节的配置。
/// 上线时用环境变量覆盖，比如 XiaoYu__ServerPort=443、XiaoYu__SecureCookie=true。
/// </summary>
public sealed class AdminOptions
{
    /// <summary>小鱼服务器地址（后台和服务器放在同一台机器上，一般是 127.0.0.1）</summary>
    public string ServerHost { get; set; } = "127.0.0.1";

    public int ServerPort { get; set; } = 9527;

    /// <summary>连服务器时是否用 TLS（后台和服务器在同一台机器上，走本机明文端口，不需要）</summary>
    public bool ServerTls { get; set; }

    /// <summary>登录 Cookie 是否只走 HTTPS。网页通过 https 域名访问时必须打开</summary>
    public bool SecureCookie { get; set; }

    /// <summary>多少分钟没操作就自动退出登录</summary>
    public int IdleMinutes { get; set; } = 120;
}
