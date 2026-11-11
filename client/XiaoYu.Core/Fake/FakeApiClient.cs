using System.Text.RegularExpressions;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.Core.Fake;

/// <summary>
/// 假数据实现：不用服务器，数据都在内存里，规则尽量和真服务器一样（参数检查、错误码、状态流转）。
/// 每次调用会故意等 0.15 秒，模拟网络延迟，好让你看到"加载中"的效果。
///
/// 为了方便测试推送：
///   - 给别人发私信，1.5 秒后对方会自动回复一条（触发 MessageReceived）
///   - 买了别人的东西，5 秒后卖家会自动发货（触发 OrderUpdated）
///
/// 账号：alice / bob / carol / dave，密码都是 Ab123456。
/// </summary>
public sealed class FakeApiClient : IApiClient
{
    private static readonly string[] AutoReplies =
    {
        "好的，还在的～", "可以的，什么时候方便？", "价格可以再商量", "今晚宿舍楼下见？", "收到！",
    };

    private readonly object _lock = new();
    private readonly FakeStore _db = FakeData.Create();
    private long _me;  // 当前登录用户 id，0 = 没登录
    private bool _connected;

    /// <summary>模拟的网络延迟（毫秒）</summary>
    public int DelayMs { get; set; } = 150;

    public bool IsConnected => _connected;
    public event Action<ChatMessage>? MessageReceived;
    public event Action<Order>? OrderUpdated;
    public event Action<bool>? ConnectionChanged;
    public event Action<MessageMasked>? MessageMasked;
    public event Action<long>? MuteChanged;
    public event Action<GoodsReviewed>? GoodsReviewed;
    public event Action<ReportResult>? ReportResultReceived;
#pragma warning disable CS0067 // 假数据不会重置昵称，事件留着满足接口
    public event Action<ProfileReset>? ProfileReset;
#pragma warning restore CS0067

    // 内容安全的简化版：服务器用字典树 + AI，这里只做最简单的包含判断，够演示界面
    private static readonly (string Word, string Label)[] ChatBlock = { ("傻逼", "人身攻击"), ("操你妈", "人身攻击"), ("约炮", "色情低俗") };
    private static readonly (string Word, string Kind)[] ChatWarn =
        { ("微信", "scam"), ("vx", "scam"), ("转账", "scam"), ("扫码", "scam"), ("弄死你", "threat"), ("知道你住哪", "threat") };
    private static readonly (string Word, string Label)[] GoodsBlock =
        { ("代写", "代课代写"), ("代课", "代课代写"), ("代考", "代课代写"), ("仿真枪", "违禁品"), ("身份证", "证件账号"), ("校园卡", "证件账号") };
    private static readonly string[] GoodsSuspicious = { "兼职", "日结", "刷单", "校园贷" };
    private readonly Dictionary<string, string> _codes = new();
    private readonly HashSet<(long, long)> _reports = new();
    private long _nextReportId = 1, _nextAppealId = 1;

    public async Task ConnectAsync()
    {
        await Task.Delay(DelayMs);
        _connected = true;
        ConnectionChanged?.Invoke(true);
    }

    public void Dispose() { }

    // ======================== 小工具 ========================

    private Task Wait() => Task.Delay(DelayMs);
    private static long Now => DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    private static ApiException Error(int code, string msg) => new(code, msg);
    private static ApiException Bad(string msg) => new(ErrorCode.BadRequest, msg);

    private void RequireLogin()
    {
        if (_me == 0) throw Error(ErrorCode.NotLogin, "请先登录");
    }

    private FakeUser FindUser(long id) =>
        _db.Users.FirstOrDefault(u => u.Id == id) ?? throw Error(ErrorCode.NotFound, "用户不存在");

    private FakeGoods FindGoods(long id) =>
        _db.Goods.FirstOrDefault(g => g.Id == id) ?? throw Error(ErrorCode.NotFound, "商品不存在");

    private Order FindOrder(long id) =>
        _db.Orders.FirstOrDefault(o => o.OrderId == id) ?? throw Error(ErrorCode.NotFound, "订单不存在");

    private static int Len(string? s) => s == null ? 0 : s.EnumerateRunes().Count();

    private bool ImageExists(string name) =>
        _db.Uploads.ContainsKey(name) || typeof(FakeApiClient).Assembly.GetManifestResourceInfo("fake/" + name) != null;

    private UserBrief Brief(long userId)
    {
        var u = _db.Users.FirstOrDefault(x => x.Id == userId);
        return new UserBrief { UserId = userId, Nickname = u?.Nickname ?? "已注销用户", Avatar = u?.Avatar ?? "" };
    }

    private UserInfo Info(FakeUser u) => new()
    {
        UserId = u.Id, Username = u.Username, Nickname = u.Nickname, Avatar = u.Avatar,
        Bio = u.Bio, Balance = u.Balance, CreatedAt = u.CreatedAt, Email = u.Email, MutedUntil = u.MutedUntil,
    };

    private GoodsBrief ToBrief(FakeGoods g) => new()
    {
        GoodsId = g.Id, Title = g.Title, Price = g.Price, Cover = g.Images.FirstOrDefault() ?? "",
        Status = g.Status, ViewCount = g.ViewCount, CreatedAt = g.CreatedAt, Seller = Brief(g.SellerId),
        RejectReason = g.RejectReason,
    };

    private GoodsDetail ToDetail(FakeGoods g) => new()
    {
        GoodsId = g.Id, Title = g.Title, Price = g.Price, Cover = g.Images.FirstOrDefault() ?? "",
        Status = g.Status, ViewCount = g.ViewCount, CreatedAt = g.CreatedAt, Seller = Brief(g.SellerId),
        Description = g.Description, OriginalPrice = g.OriginalPrice, CategoryId = g.CategoryId,
        CategoryName = _db.Categories.First(c => c.CategoryId == g.CategoryId).Name,
        Condition = g.Condition, Images = new List<string>(g.Images), UpdatedAt = g.UpdatedAt,
        RejectReason = g.RejectReason,
    };

    // 返回一份新的 Order（带上最新的昵称头像），不把内部对象直接交出去
    private Order Copy(Order o) => new()
    {
        OrderId = o.OrderId, OrderNo = o.OrderNo, GoodsId = o.GoodsId, GoodsTitle = o.GoodsTitle,
        GoodsCover = o.GoodsCover, Price = o.Price, Status = o.Status, ShipNote = o.ShipNote,
        Buyer = Brief(o.Buyer.UserId), Seller = Brief(o.Seller.UserId), CreatedAt = o.CreatedAt,
        ShippedAt = o.ShippedAt, CompletedAt = o.CompletedAt, CancelledAt = o.CancelledAt,
    };

    private static ChatMessage Copy(ChatMessage m) => new()
    {
        MessageId = m.MessageId, FromId = m.FromId, ToId = m.ToId, GoodsId = m.GoodsId,
        Content = m.Flag == MessageFlag.Masked ? "该消息因违规已被屏蔽" : m.Content, IsRead = m.IsRead,
        CreatedAt = m.CreatedAt, Flag = m.Flag, FlagKind = m.FlagKind,
    };

    private static PagedList<T> Page<T>(IEnumerable<T> all, int page, int pageSize)
    {
        if (page < 1 || pageSize < 1 || pageSize > 50) throw Bad("分页参数不对");
        var list = all.ToList();
        return new PagedList<T>
        {
            Total = list.Count, Page = page, PageSize = pageSize,
            Items = list.Skip((page - 1) * pageSize).Take(pageSize).ToList(),
        };
    }

    // ======================== 用户 ========================

    public async Task<SendCodeResult> SendEmailCodeAsync(string email, string purpose = "register")
    {
        await Wait();
        if (email == null || !Regex.IsMatch(email, @"^[^@\s]+@[^@\s]+\.[^@\s]+$")) throw Bad("请填写正确的邮箱");
        lock (_lock)
        {
            bool exists = _db.Users.Any(u => u.Email == email.ToLowerInvariant());
            if (purpose == "register" && exists) throw Error(ErrorCode.Conflict, "这个邮箱已经注册过了");
            if (purpose == "reset" && !exists) throw Error(ErrorCode.NotFound, "这个邮箱还没有注册");
            _codes[email.ToLowerInvariant()] = "123456";
        }
        return new SendCodeResult { DevCode = "123456", ExpiresIn = 600 };
    }

    public async Task ChangePasswordAsync(string oldPassword, string newPassword)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            var u = FindUser(_me);
            if (u.Password != oldPassword) throw Bad("原密码不对");
            if (PasswordRules.Check(newPassword) is { } error) throw Bad(error);
            if (oldPassword == newPassword) throw Bad("新密码不能和原密码一样");
            u.Password = newPassword;
        }
    }

    public async Task ResetPasswordAsync(string email, string code, string newPassword)
    {
        await Wait();
        lock (_lock)
        {
            string key = email.ToLowerInvariant();
            if (!_codes.TryGetValue(key, out var c) || c != code) throw Bad("验证码不对");
            if (PasswordRules.Check(newPassword) is { } error) throw Bad(error);
            var u = _db.Users.FirstOrDefault(x => x.Email == key) ?? throw Bad("验证码已失效，请重新获取");
            u.Password = newPassword;
            _codes.Remove(key);
        }
    }

    public async Task<long> RegisterAsync(string username, string password, string nickname, string? email = null, string? code = null)
    {
        await Wait();
        if (!string.IsNullOrEmpty(email))
        {
            lock (_lock)
                if (!_codes.TryGetValue(email.ToLowerInvariant(), out var c) || c != code) throw Bad("验证码不对");
        }
        if (username == null || !Regex.IsMatch(username, "^[A-Za-z0-9_]{3,20}$"))
            throw Bad("用户名要 3-20 位，只能用字母、数字、下划线");
        if (PasswordRules.Check(password) is { } passwordError) throw Bad(passwordError);
        if (Len(nickname) < 1 || Len(nickname) > 20) throw Bad("昵称要 1-20 个字");
        lock (_lock)
        {
            if (_db.Users.Any(u => u.Username == username)) throw Error(ErrorCode.Conflict, "用户名已被注册");
            var u = new FakeUser { Id = _db.NextUserId++, Username = username, Password = password, Nickname = nickname, CreatedAt = Now, Email = email?.ToLowerInvariant() ?? "" };
            _db.Users.Add(u);
            return u.Id;
        }
    }

    public async Task<UserInfo> LoginAsync(string username, string password)
    {
        await Wait();
        if (username == null || password == null) throw Bad("请输入用户名和密码");
        lock (_lock)
        {
            var u = _db.Users.FirstOrDefault(x => (x.Username == username || (x.Email != "" && x.Email == username.ToLowerInvariant())) && x.Password == password)
                    ?? throw Error(ErrorCode.NotLogin, "用户名或密码错误");
            _me = u.Id;
            return Info(u);
        }
    }

    public async Task LogoutAsync()
    {
        await Wait();
        _me = 0;
    }

    public async Task<UserInfo> GetMeAsync()
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            return Info(FindUser(_me));
        }
    }

    public async Task<UserInfo> UpdateProfileAsync(string? nickname = null, string? avatar = null, string? bio = null)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            if (nickname != null && (Len(nickname) < 1 || Len(nickname) > 20)) throw Bad("昵称要 1-20 个字");
            if (bio != null && Len(bio) > 100) throw Bad("简介最多 100 个字");
            if (!string.IsNullOrEmpty(avatar) && !ImageExists(avatar)) throw Bad("头像图片不存在，请先上传");
            var u = FindUser(_me);
            if (nickname != null) u.Nickname = nickname;
            if (bio != null) u.Bio = bio;
            if (avatar != null) u.Avatar = avatar;
            return Info(u);
        }
    }

    public async Task<UserProfile> GetUserAsync(long userId)
    {
        await Wait();
        lock (_lock)
        {
            var u = FindUser(userId);
            return new UserProfile
            {
                UserId = u.Id, Nickname = u.Nickname, Avatar = u.Avatar, Bio = u.Bio, CreatedAt = u.CreatedAt,
                OnSaleCount = _db.Goods.Count(g => g.SellerId == u.Id && g.Status == GoodsStatus.OnSale),
                SoldCount = _db.Goods.Count(g => g.SellerId == u.Id && g.Status == GoodsStatus.Sold),
            };
        }
    }

    public async Task<long> RechargeAsync(long amount)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            if (amount < 1 || amount > 1_000_000) throw Bad("单次充值金额要在 0.01 到 10000 元之间");
            var u = FindUser(_me);
            u.Balance += amount;
            return u.Balance;
        }
    }

    // ======================== 图片 ========================

    public async Task<string> UploadImageAsync(byte[] data, string ext)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            if (ext is not ("jpg" or "png" or "gif" or "webp")) throw Bad("图片格式只能是 jpg/png/gif/webp");
            if (data == null || data.Length == 0) throw Bad("图片内容不能为空");
            if (data.Length > 5 * 1024 * 1024) throw Error(ErrorCode.TooLarge, "图片不能超过 5 MB");
            string name = Guid.NewGuid().ToString("N") + "." + ext;
            _db.Uploads[name] = data;
            return name;
        }
    }

    public async Task<byte[]> GetImageAsync(string image)
    {
        await Wait();
        lock (_lock)
        {
            if (_db.Uploads.TryGetValue(image, out var bytes)) return bytes;
        }
        using var stream = typeof(FakeApiClient).Assembly.GetManifestResourceStream("fake/" + image)
                           ?? throw Error(ErrorCode.NotFound, "图片不存在");
        using var ms = new MemoryStream();
        await stream.CopyToAsync(ms);
        return ms.ToArray();
    }

    // ======================== 商品 ========================

    public async Task<List<Category>> GetCategoriesAsync()
    {
        await Wait();
        return _db.Categories.Select(c => new Category { CategoryId = c.CategoryId, Name = c.Name }).ToList();
    }

    // 检查发布/修改的字段，和服务器规则一致
    private void Validate(GoodsEdit e, bool isPublish)
    {
        if ((isPublish || e.Title != null) && (Len(e.Title) < 1 || Len(e.Title) > 30)) throw Bad("标题要 1-30 个字");
        if (e.Description != null && Len(e.Description) > 500) throw Bad("描述最多 500 个字");
        if ((isPublish || e.Price != null) && (e.Price is null or < 1 or > 99_999_900)) throw Bad("价格要在 0.01 到 999999 元之间");
        if (e.OriginalPrice is < 0 or > 99_999_900) throw Bad("原价要在 0 到 999999 元之间");
        if ((isPublish || e.CategoryId != null) && !_db.Categories.Any(c => c.CategoryId == e.CategoryId)) throw Bad("请选择正确的分类");
        if (e.Condition is < 0 or > 3) throw Bad("成色只能是 0-3");
        if (e.Images != null)
        {
            if (e.Images.Count > 9) throw Bad("最多 9 张图片");
            if (e.Images.Any(i => !ImageExists(i))) throw Bad("有图片不存在，请重新上传");
        }
    }

    private static void CheckGoodsWords(string? title, string? description)
    {
        foreach (var (word, label) in GoodsBlock)
            if ((title ?? "").Contains(word) || (description ?? "").Contains(word))
                throw Bad($"商品包含禁售内容（{label}：「{word}」），不能发布");
    }

    // 假装 AI 1.2 秒后审完：可疑的驳回（不推送），正常的上架并推送
    private void SimulateReview(long goodsId)
    {
        _ = Task.Run(async () =>
        {
            await Task.Delay(1200);
            GoodsReviewed? pushed = null;
            lock (_lock)
            {
                var g = _db.Goods.FirstOrDefault(x => x.Id == goodsId);
                if (g == null || g.Status != GoodsStatus.Reviewing) return;
                if (GoodsSuspicious.Any(w => g.Title.Contains(w) || g.Description.Contains(w)))
                {
                    g.Status = GoodsStatus.Rejected;
                    g.RejectReason = "AI 审核判定为诈骗类（刷单兼职、校园贷等）";
                }
                else
                {
                    g.Status = GoodsStatus.OnSale;
                    pushed = new GoodsReviewed { GoodsId = g.Id, Status = g.Status };
                }
            }
            if (pushed != null) GoodsReviewed?.Invoke(pushed);
        });
    }

    public async Task<GoodsSaved> PublishGoodsAsync(GoodsEdit goods)
    {
        await Wait();
        GoodsSaved saved;
        lock (_lock)
        {
            RequireLogin();
            Validate(goods, true);
            CheckGoodsWords(goods.Title, goods.Description);
            var g = new FakeGoods
            {
                Id = _db.NextGoodsId++, SellerId = _me, Title = goods.Title!, Description = goods.Description ?? "",
                Price = goods.Price!.Value, OriginalPrice = goods.OriginalPrice ?? 0, CategoryId = goods.CategoryId!.Value,
                Condition = goods.Condition ?? 2, Images = goods.Images?.ToList() ?? new(), CreatedAt = Now, UpdatedAt = Now,
                Status = GoodsStatus.Reviewing,
            };
            _db.Goods.Add(g);
            saved = new GoodsSaved { GoodsId = g.Id, Status = g.Status };
        }
        SimulateReview(saved.GoodsId);
        return saved;
    }

    public async Task<string> UpdateGoodsAsync(long goodsId, GoodsEdit changes)
    {
        await Wait();
        bool review = false;
        string status;
        lock (_lock)
        {
            RequireLogin();
            var g = FindGoods(goodsId);
            if (g.SellerId != _me) throw Error(ErrorCode.Forbidden, "只能修改自己的商品");
            if (g.Status == GoodsStatus.Sold) throw Error(ErrorCode.Conflict, "商品已卖出，不能修改");
            Validate(changes, false);
            if (changes.Title != null) g.Title = changes.Title;
            if (changes.Description != null) g.Description = changes.Description;
            if (changes.Price != null) g.Price = changes.Price.Value;
            if (changes.OriginalPrice != null) g.OriginalPrice = changes.OriginalPrice.Value;
            if (changes.CategoryId != null) g.CategoryId = changes.CategoryId.Value;
            if (changes.Condition != null) g.Condition = changes.Condition.Value;
            if (changes.Images != null) g.Images = changes.Images.ToList();
            g.UpdatedAt = Now;
            // 改了内容要重新审核
            if (changes.Title != null || changes.Description != null || changes.Images != null || changes.CategoryId != null)
            {
                if (g.Status is GoodsStatus.OnSale or GoodsStatus.Rejected or GoodsStatus.Reviewing)
                {
                    g.Status = GoodsStatus.Reviewing;
                    g.RejectReason = "";
                }
                review = true;
            }
            status = g.Status;
        }
        if (review) SimulateReview(goodsId);
        return status;
    }

    private async Task ChangeStatus(long goodsId, string from, string to, string conflictMsg)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            var g = FindGoods(goodsId);
            if (g.SellerId != _me) throw Error(ErrorCode.Forbidden, "只能操作自己的商品");
            if (g.Status != from) throw Error(ErrorCode.Conflict, conflictMsg);
            g.Status = to;
            g.UpdatedAt = Now;
        }
    }

    public Task OffShelfAsync(long goodsId) =>
        ChangeStatus(goodsId, GoodsStatus.OnSale, GoodsStatus.OffShelf, "只有在售的商品才能下架");

    public Task OnShelfAsync(long goodsId) =>
        ChangeStatus(goodsId, GoodsStatus.OffShelf, GoodsStatus.OnSale, "只有已下架的商品才能重新上架");

    public async Task<GoodsDetail> GetGoodsDetailAsync(long goodsId)
    {
        await Wait();
        lock (_lock)
        {
            var g = FindGoods(goodsId);
            g.ViewCount++;
            return ToDetail(g);
        }
    }

    public async Task<PagedList<GoodsBrief>> SearchGoodsAsync(GoodsQuery q)
    {
        await Wait();
        if (Len(q.Keyword) > 30) throw Bad("关键词最多 30 个字");
        lock (_lock)
        {
            IEnumerable<FakeGoods> list = _db.Goods.Where(g => g.Status == GoodsStatus.OnSale);
            if (!string.IsNullOrEmpty(q.Keyword))
                list = list.Where(g => g.Title.Contains(q.Keyword, StringComparison.Ordinal) ||
                                       g.Description.Contains(q.Keyword, StringComparison.Ordinal));
            if (q.CategoryId != null) list = list.Where(g => g.CategoryId == q.CategoryId);
            if (q.SellerId != null) list = list.Where(g => g.SellerId == q.SellerId);
            if (q.MinPrice != null) list = list.Where(g => g.Price >= q.MinPrice);
            if (q.MaxPrice != null) list = list.Where(g => g.Price <= q.MaxPrice);

            var newest = list.OrderByDescending(g => g.CreatedAt).ThenByDescending(g => g.Id);
            list = q.Sort switch
            {
                GoodsSort.Newest => newest,
                GoodsSort.PriceAsc => newest.OrderBy(g => g.Price),
                GoodsSort.PriceDesc => newest.OrderByDescending(g => g.Price),
                GoodsSort.Hot => newest.OrderByDescending(g => g.ViewCount),
                _ => throw Bad("排序方式不对"),
            };
            return Page(list.Select(ToBrief), q.Page, q.PageSize);
        }
    }

    public async Task<PagedList<GoodsBrief>> GetMyGoodsAsync(string? status = null, int page = 1, int pageSize = 20)
    {
        await Wait();
        if (status is not (null or GoodsStatus.OnSale or GoodsStatus.Sold or GoodsStatus.OffShelf)) throw Bad("状态不对");
        lock (_lock)
        {
            RequireLogin();
            var list = _db.Goods.Where(g => g.SellerId == _me && (status == null || g.Status == status))
                .OrderByDescending(g => g.CreatedAt).ThenByDescending(g => g.Id);
            return Page(list.Select(ToBrief), page, pageSize);
        }
    }

    // ======================== 订单 ========================

    public async Task<Order> CreateOrderAsync(long goodsId)
    {
        await Wait();
        Order created;
        lock (_lock)
        {
            RequireLogin();
            var g = FindGoods(goodsId);
            if (g.SellerId == _me) throw Error(ErrorCode.Forbidden, "不能买自己的商品");
            if (g.Status != GoodsStatus.OnSale) throw Error(ErrorCode.Conflict, "商品已卖出或已下架");
            var me = FindUser(_me);
            if (me.Balance < g.Price) throw Error(ErrorCode.NoMoney, "余额不足，请先充值");

            me.Balance -= g.Price;
            g.Status = GoodsStatus.Sold;
            long id = _db.NextOrderId++;
            created = new Order
            {
                OrderId = id, OrderNo = DateTime.Now.ToString("yyyyMMddHHmmss") + Random.Shared.Next(10000).ToString("0000"),
                GoodsId = g.Id, GoodsTitle = g.Title, GoodsCover = g.Images.FirstOrDefault() ?? "", Price = g.Price,
                Status = OrderStatus.Paid, CreatedAt = Now,
            };
            created.Buyer.UserId = _me;
            created.Seller.UserId = g.SellerId;
            _db.Orders.Add(created);
        }
        SimulateSellerShips(created.OrderId, _me);
        lock (_lock) return Copy(created);
    }

    // 假装卖家 5 秒后发货，用来测试 OrderUpdated 推送
    private void SimulateSellerShips(long orderId, long buyerId)
    {
        _ = Task.Run(async () =>
        {
            await Task.Delay(5000);
            Order? pushed = null;
            lock (_lock)
            {
                var o = _db.Orders.FirstOrDefault(x => x.OrderId == orderId);
                if (o != null && o.Status == OrderStatus.Paid && _me == buyerId)
                {
                    o.Status = OrderStatus.Shipped;
                    o.ShippedAt = Now;
                    o.ShipNote = "（自动发货）明天中午食堂门口见～";
                    pushed = Copy(o);
                }
            }
            if (pushed != null) OrderUpdated?.Invoke(pushed);
        });
    }

    private Order ChangeOrder(long orderId, Action<Order> check, Action<Order> apply)
    {
        lock (_lock)
        {
            RequireLogin();
            var o = FindOrder(orderId);
            check(o);
            apply(o);
            return Copy(o);
        }
    }

    public async Task<Order> CancelOrderAsync(long orderId)
    {
        await Wait();
        return ChangeOrder(orderId,
            o =>
            {
                if (_me != o.Buyer.UserId && _me != o.Seller.UserId) throw Error(ErrorCode.Forbidden, "这不是你的订单");
                if (o.Status != OrderStatus.Paid) throw Error(ErrorCode.Conflict, "只有待发货的订单才能取消");
            },
            o =>
            {
                o.Status = OrderStatus.Cancelled;
                o.CancelledAt = Now;
                FindUser(o.Buyer.UserId).Balance += o.Price;
                var g = _db.Goods.FirstOrDefault(x => x.Id == o.GoodsId);
                if (g != null && g.Status == GoodsStatus.Sold) g.Status = GoodsStatus.OnSale;
            });
    }

    public async Task<Order> ShipOrderAsync(long orderId, string shipNote = "")
    {
        await Wait();
        if (Len(shipNote) > 100) throw Bad("发货备注最多 100 个字");
        return ChangeOrder(orderId,
            o =>
            {
                if (_me != o.Seller.UserId) throw Error(ErrorCode.Forbidden, "只有卖家才能发货");
                if (o.Status != OrderStatus.Paid) throw Error(ErrorCode.Conflict, "只有待发货的订单才能发货");
            },
            o =>
            {
                o.Status = OrderStatus.Shipped;
                o.ShippedAt = Now;
                o.ShipNote = shipNote ?? "";
            });
    }

    public async Task<Order> ConfirmOrderAsync(long orderId)
    {
        await Wait();
        return ChangeOrder(orderId,
            o =>
            {
                if (_me != o.Buyer.UserId) throw Error(ErrorCode.Forbidden, "只有买家才能确认收货");
                if (o.Status != OrderStatus.Shipped) throw Error(ErrorCode.Conflict, "卖家发货后才能确认收货");
            },
            o =>
            {
                o.Status = OrderStatus.Completed;
                o.CompletedAt = Now;
                FindUser(o.Seller.UserId).Balance += o.Price;
            });
    }

    public async Task<Order> GetOrderAsync(long orderId)
    {
        await Wait();
        return ChangeOrder(orderId,
            o =>
            {
                if (_me != o.Buyer.UserId && _me != o.Seller.UserId) throw Error(ErrorCode.Forbidden, "这不是你的订单");
            },
            _ => { });
    }

    public async Task<PagedList<Order>> GetOrdersAsync(string role, string? status = null, int page = 1, int pageSize = 20)
    {
        await Wait();
        if (role is not (OrderRole.Buyer or OrderRole.Seller)) throw Bad("role 只能是 buyer 或 seller");
        if (status is not (null or OrderStatus.Paid or OrderStatus.Shipped or OrderStatus.Completed or OrderStatus.Cancelled))
            throw Bad("订单状态不对");
        lock (_lock)
        {
            RequireLogin();
            var list = _db.Orders
                .Where(o => (role == OrderRole.Buyer ? o.Buyer.UserId : o.Seller.UserId) == _me)
                .Where(o => status == null || o.Status == status)
                .OrderByDescending(o => o.OrderId);
            return Page(list.Select(Copy), page, pageSize);
        }
    }

    // ======================== 收藏 ========================

    public async Task AddFavoriteAsync(long goodsId)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            FindGoods(goodsId);
            _db.Favorites.TryAdd((_me, goodsId), Now);
        }
    }

    public async Task RemoveFavoriteAsync(long goodsId)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            _db.Favorites.Remove((_me, goodsId));
        }
    }

    public async Task<bool> IsFavoriteAsync(long goodsId)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            return _db.Favorites.ContainsKey((_me, goodsId));
        }
    }

    public async Task<PagedList<GoodsBrief>> GetFavoritesAsync(int page = 1, int pageSize = 20)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            var list = _db.Favorites.Where(f => f.Key.User == _me)
                .OrderByDescending(f => f.Value).ThenByDescending(f => f.Key.Goods)
                .Select(f => ToBrief(FindGoods(f.Key.Goods)));
            return Page(list, page, pageSize);
        }
    }

    // ======================== 聊天 ========================

    public async Task<ChatMessage> SendMessageAsync(long toUserId, string content, long goodsId = 0)
    {
        await Wait();
        ChatMessage sent;
        lock (_lock)
        {
            RequireLogin();
            if (Len(content) < 1 || Len(content) > 500) throw Bad("消息要 1-500 个字");
            if (toUserId == _me) throw Bad("不能给自己发消息");
            FindUser(toUserId);
            if (goodsId != 0) FindGoods(goodsId);
            var me = FindUser(_me);
            if (me.MutedUntil > Now) throw Error(ErrorCode.Forbidden, "你因违规被禁言，可以在消息页提交申诉");
            foreach (var (word, label) in ChatBlock)
                if (content.Contains(word)) throw Bad($"消息包含违规内容（{label}），发送失败");
            var warn = ChatWarn.FirstOrDefault(w => content.Contains(w.Word, StringComparison.OrdinalIgnoreCase));
            sent = new ChatMessage
            {
                MessageId = _db.NextMessageId++, FromId = _me, ToId = toUserId, GoodsId = goodsId,
                Content = content, CreatedAt = Now,
                Flag = warn.Word != null ? MessageFlag.Warn : MessageFlag.None, FlagKind = warn.Kind ?? "",
            };
            _db.Messages.Add(sent);
        }
        SimulateReply(toUserId, _me, goodsId);
        return Copy(sent);
    }

    // 假装对方 1.5 秒后回复，用来测试 MessageReceived 推送
    private void SimulateReply(long from, long to, long goodsId)
    {
        _ = Task.Run(async () =>
        {
            await Task.Delay(1500);
            ChatMessage reply;
            lock (_lock)
            {
                if (_me != to) return;  // 已经换账号了
                reply = new ChatMessage
                {
                    MessageId = _db.NextMessageId++, FromId = from, ToId = to, GoodsId = goodsId,
                    Content = AutoReplies[Random.Shared.Next(AutoReplies.Length)], CreatedAt = Now,
                };
                _db.Messages.Add(reply);
            }
            MessageReceived?.Invoke(Copy(reply));
        });
    }

    private bool Between(ChatMessage m, long a, long b) =>
        (m.FromId == a && m.ToId == b) || (m.FromId == b && m.ToId == a);

    public async Task<List<ChatMessage>> GetChatHistoryAsync(long peerId, long beforeId = 0, int limit = 20)
    {
        await Wait();
        if (limit < 1 || limit > 50) throw Bad("limit 要在 1-50 之间");
        lock (_lock)
        {
            RequireLogin();
            return _db.Messages
                .Where(m => Between(m, _me, peerId) && (beforeId <= 0 || m.MessageId < beforeId))
                .OrderByDescending(m => m.MessageId).Take(limit)
                .OrderBy(m => m.MessageId).Select(Copy).ToList();
        }
    }

    public async Task<List<Conversation>> GetConversationsAsync()
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            return _db.Messages
                .Where(m => m.FromId == _me || m.ToId == _me)
                .GroupBy(m => m.FromId == _me ? m.ToId : m.FromId)
                .Select(g => new Conversation
                {
                    Peer = Brief(g.Key),
                    LastMessage = Copy(g.OrderByDescending(m => m.MessageId).First()),
                    Unread = g.Count(m => m.ToId == _me && !m.IsRead),
                })
                .OrderByDescending(c => c.LastMessage.MessageId)
                .ToList();
        }
    }

    public async Task<int> MarkReadAsync(long peerId)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            int n = 0;
            foreach (var m in _db.Messages.Where(m => m.FromId == peerId && m.ToId == _me && !m.IsRead))
            {
                m.IsRead = true;
                n++;
            }
            return n;
        }
    }

    public async Task<int> GetUnreadCountAsync()
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            return _db.Messages.Count(m => m.ToId == _me && !m.IsRead);
        }
    }

    // ======================== 内容安全 ========================

    /// <summary>演示/截图用：把当前用户禁言 seconds 秒（传 0 解除），并触发 MuteChanged 推送</summary>
    public void SimulateMuted(long seconds)
    {
        long until;
        lock (_lock)
        {
            var u = FindUser(_me);
            u.MutedUntil = seconds > 0 ? Now + seconds : 0;
            until = u.MutedUntil;
        }
        MuteChanged?.Invoke(until);
    }

    public async Task<ReportResult> ReportMessageAsync(long messageId, string type, string reason)
    {
        await Wait();
        ChatMessage m;
        long id;
        lock (_lock)
        {
            RequireLogin();
            if (!ReportTypes.All.Any(t => t.Code == type)) throw Bad("请选择举报类型");
            if (Len(reason) < 1 || Len(reason) > 200) throw Bad("举报理由要 1-200 个字");
            m = _db.Messages.FirstOrDefault(x => x.MessageId == messageId) ?? throw Error(ErrorCode.NotFound, "消息不存在");
            if (m.ToId != _me) throw Error(ErrorCode.Forbidden, "只能举报别人发给你的消息");
            if (m.Flag == MessageFlag.Masked) throw Error(ErrorCode.Conflict, "这条消息已经被处理了");
            if (!_reports.Add((_me, messageId))) throw Error(ErrorCode.Conflict, "你已经举报过这条消息了");
            id = _nextReportId++;
        }
        // 假装 AI 1.5 秒后判完：命中二级词的算成立（屏蔽），其他转人工
        _ = Task.Run(async () =>
        {
            await Task.Delay(1500);
            bool upheld;
            lock (_lock)
            {
                upheld = m.Flag == MessageFlag.Warn;
                if (upheld) m.Flag = MessageFlag.Masked;
            }
            if (upheld) MessageMasked?.Invoke(new MessageMasked { MessageId = m.MessageId, FromId = m.FromId, ToId = m.ToId, Masked = true });
            ReportResultReceived?.Invoke(new ReportResult { ReportId = id, Status = upheld ? "upheld" : "manual" });
        });
        return new ReportResult { ReportId = id, Status = "pending" };
    }

    public async Task<long> AppealAsync(string kind, string content, long goodsId = 0)
    {
        await Wait();
        lock (_lock)
        {
            RequireLogin();
            if (Len(content) < 1 || Len(content) > 200) throw Bad("申诉内容要 1-200 个字");
            if (kind == "mute")
            {
                if (FindUser(_me).MutedUntil <= Now) throw Error(ErrorCode.Conflict, "你现在没有被禁言");
            }
            else if (kind == "goods")
            {
                var g = FindGoods(goodsId);
                if (g.SellerId != _me) throw Error(ErrorCode.Forbidden, "只能申诉自己的商品");
                if (g.Status != GoodsStatus.Rejected) throw Error(ErrorCode.Conflict, "只有被驳回的商品可以申诉");
            }
            else throw Bad("申诉类型不对");
            return _nextAppealId++;
        }
    }
}
