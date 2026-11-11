namespace XiaoYu.Core.Api;

/// <summary>
/// 服务器返回错误时抛出。Message 是给用户看的中文提示，直接显示就行。
/// 用法：
///   try { await Api.LoginAsync(u, p); }
///   catch (ApiException ex) { ErrorMessage = ex.Message; }
/// </summary>
public class ApiException : Exception
{
    public int Code { get; }

    public ApiException(int code, string message) : base(message)
    {
        Code = code;
    }
}

/// <summary>错误码，和《协议文档》第 4 节一致</summary>
public static class ErrorCode
{
    public const int Network = -1;       // 客户端自己用：连不上服务器或断开了
    public const int BadRequest = 400;
    public const int NotLogin = 401;
    public const int NoMoney = 402;
    public const int Forbidden = 403;
    public const int NotFound = 404;
    public const int Conflict = 409;
    public const int TooLarge = 413;
    public const int Internal = 500;
    public const int NotImplemented = 501;
}
