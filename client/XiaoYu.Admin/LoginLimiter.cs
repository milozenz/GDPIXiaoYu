namespace XiaoYu.Admin;

/// <summary>
/// 防止有人在后台登录页反复猜密码：
///   同一个 IP 15 分钟内输错 5 次，这个 IP 暂停登录；
///   所有 IP 加起来 15 分钟内输错 30 次，后台暂停所有登录（防止换着 IP 猜）。
/// 记录只存在内存里，程序重启就清空。
/// </summary>
public sealed class LoginLimiter
{
    private static readonly TimeSpan Window = TimeSpan.FromMinutes(15);
    private const int PerIp = 5;
    private const int Total = 30;
    private const int MaxTrackedIps = 10_000;

    private readonly object _lock = new();
    private readonly Dictionary<string, List<DateTime>> _byIp = new();
    private readonly List<DateTime> _all = new();

    /// <summary>现在能不能尝试登录；不能时 waitMinutes = 还要等几分钟</summary>
    public bool Allowed(string ip, out int waitMinutes)
    {
        lock (_lock)
        {
            var now = DateTime.UtcNow;
            Prune(now);
            DateTime? until = null;
            if (_byIp.TryGetValue(ip, out var list) && list.Count >= PerIp) until = list[0] + Window;
            if (_all.Count >= Total)
            {
                var t = _all[0] + Window;
                if (until == null || t > until) until = t;
            }
            waitMinutes = until == null ? 0 : Math.Max(1, (int)Math.Ceiling((until.Value - now).TotalMinutes));
            return until == null;
        }
    }

    public void Failed(string ip)
    {
        lock (_lock)
        {
            var now = DateTime.UtcNow;
            Prune(now);
            if (!_byIp.TryGetValue(ip, out var list))
            {
                if (_byIp.Count >= MaxTrackedIps) return;  // 只靠总数限制兜底
                _byIp[ip] = list = new List<DateTime>();
            }
            list.Add(now);
            _all.Add(now);
        }
    }

    public void Succeeded(string ip)
    {
        lock (_lock) _byIp.Remove(ip);
    }

    // 删掉 15 分钟以前的记录
    private void Prune(DateTime now)
    {
        _all.RemoveAll(t => now - t > Window);
        foreach (var ip in _byIp.Keys.ToList())
        {
            var list = _byIp[ip];
            list.RemoveAll(t => now - t > Window);
            if (list.Count == 0) _byIp.Remove(ip);
        }
    }
}
