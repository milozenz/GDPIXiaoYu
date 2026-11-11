namespace XiaoYu.Core.Utils;

/// <summary>
/// 注册密码规则，和服务器 user.c 保持一致（协议文档 6.2）：
/// 8–32 个字符，且同时包含数字、大写字母和小写字母。
/// 登录时不检查强度（兼容旧账户）。
/// </summary>
public static class PasswordRules
{
    public const string Message = "密码须为 8–32 位，且同时包含数字、大写字母和小写字母";

    public static bool LengthOk(string? p)
    {
        if (p == null) return false;
        int n = p.EnumerateRunes().Count();
        return n >= 8 && n <= 32;
    }

    public static bool HasDigit(string? p) => p != null && p.Any(c => c >= '0' && c <= '9');
    public static bool HasUpper(string? p) => p != null && p.Any(c => c >= 'A' && c <= 'Z');
    public static bool HasLower(string? p) => p != null && p.Any(c => c >= 'a' && c <= 'z');

    /// <summary>满足了几条（0–4）：长度、数字、大写、小写。界面上的强度条用它。</summary>
    public static int Score(string? p) =>
        (LengthOk(p) ? 1 : 0) + (HasDigit(p) ? 1 : 0) + (HasUpper(p) ? 1 : 0) + (HasLower(p) ? 1 : 0);

    /// <summary>合格返回 null，不合格返回提示</summary>
    public static string? Check(string? p) => Score(p) == 4 ? null : Message;
}
