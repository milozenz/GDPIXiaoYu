using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.Core.Fake;

// 假数据用的内部结构
internal class FakeUser
{
    public long Id;
    public string Username = "", Password = "", Nickname = "", Avatar = "", Bio = "", Email = "";
    public long Balance, CreatedAt, MutedUntil;
}

internal class FakeGoods
{
    public long Id, SellerId, Price, OriginalPrice, CategoryId, CreatedAt, UpdatedAt;
    public string Title = "", Description = "", Status = GoodsStatus.OnSale, RejectReason = "";
    public int Condition, ViewCount;
    public List<string> Images = new();
}

internal class FakeStore
{
    public List<FakeUser> Users = new();
    public List<Category> Categories = new();
    public List<FakeGoods> Goods = new();
    public List<Order> Orders = new();
    public List<ChatMessage> Messages = new();
    public Dictionary<(long User, long Goods), long> Favorites = new();
    public Dictionary<string, byte[]> Uploads = new();
    public long NextUserId = 1100, NextGoodsId = 2100, NextOrderId = 3100, NextMessageId = 5100;
}

/// <summary>
/// 假数据：4 个用户（密码都是 Ab123456）、20 多件商品、几个订单和聊天。
/// 推荐用 alice 登录，她的"我买到的""我卖出的""消息"里都有数据。
/// </summary>
internal static class FakeData
{
    public static FakeStore Create()
    {
        long now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        long Ago(int minutes) => now - minutes * 60L;
        var s = new FakeStore();

        s.Users.Add(new FakeUser { Id = 1001, Username = "alice", Email = "alice@xiaoyu.test", Password = "Ab123456", Nickname = "阿狸", Avatar = "avatar-alice.png", Bio = "工业机器人专业大一，出点闲置", Balance = 50000, CreatedAt = Ago(60 * 24 * 60) });
        s.Users.Add(new FakeUser { Id = 1002, Username = "bob", Email = "bob@xiaoyu.test", Password = "Ab123456", Nickname = "鲍勃", Avatar = "avatar-bob.png", Bio = "考完研出书", Balance = 20000, CreatedAt = Ago(60 * 24 * 90) });
        s.Users.Add(new FakeUser { Id = 1003, Username = "carol", Email = "carol@xiaoyu.test", Password = "Ab123456", Nickname = "小卡", Avatar = "avatar-carol.png", Bio = "宿舍好物分享", Balance = 30000, CreatedAt = Ago(60 * 24 * 30) });
        s.Users.Add(new FakeUser { Id = 1004, Username = "dave", Email = "dave@xiaoyu.test", Password = "Ab123456", Nickname = "大卫", Avatar = "avatar-dave.png", Bio = "数码爱好者", Balance = 80000, CreatedAt = Ago(60 * 24 * 120) });

        string[] categories = { "数码电子", "书籍教材", "生活用品", "服饰鞋包", "运动户外", "美妆个护", "其他" };
        for (int i = 0; i < categories.Length; i++) s.Categories.Add(new Category { CategoryId = i + 1, Name = categories[i] });

        void G(long id, long seller, string title, string desc, long price, long orig, long cat, int cond, string img, int minutesAgo, int views, string status = GoodsStatus.OnSale) =>
            s.Goods.Add(new FakeGoods
            {
                Id = id, SellerId = seller, Title = title, Description = desc, Price = price, OriginalPrice = orig,
                CategoryId = cat, Condition = cond, Images = new() { img }, CreatedAt = Ago(minutesAgo),
                UpdatedAt = Ago(minutesAgo), ViewCount = views, Status = status,
            });

        G(2001, 1001, "九成新山地自行车", "骑了一学期，变速正常，送车锁和打气筒。校内自提。", 32000, 80000, 5, 1, "bike.jpg", 30, 56);
        G(2002, 1002, "高等数学 同济第七版 上下册", "有少量笔记，不影响使用，期末复习必备。", 2500, 6800, 2, 2, "math.jpg", 90, 33);
        G(2003, 1003, "宿舍USB小风扇 静音", "三档风速，充一次电用一天，夏天神器。", 1500, 3900, 3, 1, "fan.jpg", 200, 12);
        G(2004, 1004, "AirPods 二代", "电池健康 90%，配件齐全，有发票。", 39900, 89900, 1, 2, "airpods.jpg", 360, 128);
        G(2005, 1001, "罗技无线鼠标 M330", "静音款，用了半年，送鼠标垫。", 6000, 10900, 1, 2, "mouse.jpg", 600, 20);
        G(2006, 1002, "考研英语真题 黄皮书", "2026 版，几乎没写。", 3000, 7600, 2, 1, "english.jpg", 800, 41);
        G(2007, 1003, "斯伯丁篮球 7号", "室外用过几次，气很足。", 8000, 16900, 5, 2, "basketball.jpg", 1000, 18);
        G(2008, 1004, "小米台灯 1S", "护眼台灯，支持手机调光。", 7000, 16900, 3, 1, "lamp.jpg", 1500, 25);
        G(2009, 1001, "优衣库羽绒服 L码", "黑色，穿过一个冬天，已干洗。", 15000, 49900, 4, 2, "coat.jpg", 2000, 37);
        G(2010, 1002, "机械键盘 青轴 87键", "打字很爽，室友嫌吵所以出。", 12000, 29900, 1, 2, "keyboard.jpg", 2600, 64);
        G(2011, 1003, "C语言程序设计 谭浩强", "C 语言课本，重点都划好了。", 1200, 3900, 2, 2, "cbook.jpg", 3000, 88);
        G(2012, 1004, "加厚瑜伽垫", "10mm 加厚，带收纳袋。", 3500, 8900, 5, 1, "yoga.jpg", 4000, 9);
        G(2013, 1001, "折叠收纳箱 两个", "宿舍收纳好帮手，毕业带不走。", 2000, 4500, 3, 2, "box.jpg", 5000, 14);
        G(2014, 1003, "防晒霜 全新未拆", "买多了，保质期到 2028 年。", 6500, 12000, 6, 0, "sunscreen.jpg", 6000, 22);
        G(2015, 1002, "帆布鞋 41码", "只穿过两次，尺码不合适。", 4500, 13900, 4, 1, "shoes.jpg", 7000, 17);
        G(2016, 1004, "iPad 2021 64G", "无磕碰，送保护壳和贴膜。", 168000, 249900, 1, 1, "ipad.jpg", 8000, 203);
        G(2017, 1001, "民谣吉他 41寸", "初学者入门款，送变调夹和琴包。", 26000, 59900, 7, 2, "guitar.jpg", 9000, 31);
        G(2018, 1003, "四级听力耳机", "考试专用，调频正常。", 2500, 6000, 1, 2, "earphone.jpg", 10000, 10);
        // 已经卖掉的，给订单用
        G(2019, 1002, "线性代数 辅导书", "考研用过。", 1800, 4500, 2, 2, "math.jpg", 20000, 40, GoodsStatus.Sold);
        G(2020, 1004, "蓝牙音箱", "低音很足。", 9900, 19900, 1, 1, "earphone.jpg", 15000, 51, GoodsStatus.Sold);
        G(2021, 1001, "台式风扇", "宿舍用，声音小。", 3000, 8900, 3, 2, "fan.jpg", 12000, 23, GoodsStatus.Sold);
        G(2022, 1001, "大一教材一套", "高数、线代、C 语言。", 5000, 15000, 2, 2, "cbook.jpg", 11000, 30, GoodsStatus.Sold);
        G(2023, 1001, "旧台灯", "能用，就是有点旧。", 1000, 0, 3, 3, "lamp.jpg", 30000, 5, GoodsStatus.OffShelf);

        Order O(long id, long goods, long buyer, long seller, string status, int minutesAgo, string note = "")
        {
            var g = s.Goods.First(x => x.Id == goods);
            var o = new Order
            {
                OrderId = id, OrderNo = Fmt.ToLocalTime(Ago(minutesAgo)).ToString("yyyyMMddHHmmss") + (id % 10000).ToString("0000"),
                GoodsId = goods, GoodsTitle = g.Title, GoodsCover = g.Images[0], Price = g.Price, Status = status,
                ShipNote = note, CreatedAt = Ago(minutesAgo),
            };
            o.Buyer.UserId = buyer;
            o.Seller.UserId = seller;
            if (status is OrderStatus.Shipped or OrderStatus.Completed) o.ShippedAt = Ago(minutesAgo - 60);
            if (status == OrderStatus.Completed) o.CompletedAt = Ago(minutesAgo - 120);
            return o;
        }
        s.Orders.Add(O(3001, 2019, 1001, 1002, OrderStatus.Completed, 19000, "图书馆门口当面交易"));
        s.Orders.Add(O(3002, 2020, 1001, 1004, OrderStatus.Paid, 14000));
        s.Orders.Add(O(3003, 2021, 1003, 1001, OrderStatus.Paid, 11500));
        s.Orders.Add(O(3004, 2022, 1002, 1001, OrderStatus.Shipped, 10500, "已放在快递柜 3 号"));

        void M(long id, long from, long to, long goods, string content, int minutesAgo, bool read) =>
            s.Messages.Add(new ChatMessage { MessageId = id, FromId = from, ToId = to, GoodsId = goods, Content = content, CreatedAt = Ago(minutesAgo), IsRead = read });
        M(5001, 1001, 1002, 2002, "高数书还在吗？", 85, true);
        M(5002, 1002, 1001, 2002, "在的，随时可以来拿", 80, true);
        M(5003, 1001, 1002, 2002, "好的，明天中午去找你", 75, true);
        M(5004, 1002, 1001, 2002, "OK，到了宿舍楼下给我发消息", 70, false);
        M(5005, 1004, 1001, 2020, "音箱明天给你发货哈", 600, true);
        M(5006, 1003, 1001, 2001, "自行车能便宜点吗？", 20, false);
        M(5007, 1003, 1001, 2001, "300 可以吗", 19, false);
        // 命中二级词的消息：接收方会看到提醒条
        s.Messages.Add(new ChatMessage
        {
            MessageId = 5008, FromId = 1003, ToId = 1001, GoodsId = 2001, Content = "平台抽成太高了，加我vx私下转账吧，再便宜 20",
            CreatedAt = Ago(18), Flag = MessageFlag.Warn, FlagKind = "scam",
        });

        return s;
    }
}
