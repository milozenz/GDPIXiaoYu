// 用法：dotnet run --project tools/Seed -- [主机] [端口]
// 已经有数据（alice 能登录）就什么都不做，所以可以放心重复运行。
using XiaoYu.Core.Api;
using XiaoYu.Core.Fake;
using XiaoYu.Core.Models;

string host = args.Length > 0 ? args[0] : "127.0.0.1";
int port = args.Length > 1 ? int.Parse(args[1]) : 9527;
const string Pw = "Ab123456";

var images = new FakeApiClient();    // 示例图片就放在 Core 里，借它读出来

async Task<TcpApiClient> Open()
{
    var c = new TcpApiClient(host, port);
    await c.ConnectAsync();
    return c;
}

// 已经灌过就跳过
using (var probe = await Open())
{
    try { await probe.LoginAsync("alice", Pw); Console.WriteLine("服务器上已经有演示数据，跳过。"); return 0; }
    catch (ApiException) { }
}

var people = new (string User, string Nick, string Bio, string Avatar, long Money)[]
{
    ("alice", "阿狸", "工业机器人专业大一，出点闲置", "avatar-alice.png", 100000),
    ("bob", "鲍勃", "考完研出书", "avatar-bob.png", 60000),
    ("carol", "小卡", "宿舍好物分享", "avatar-carol.png", 40000),
    ("dave", "大卫", "数码爱好者", "avatar-dave.png", 90000),
};
var api = new Dictionary<string, TcpApiClient>();
var id = new Dictionary<string, long>();
foreach (var p in people)
{
    using (var reg = await Open()) await reg.RegisterDevAsync(p.User, Pw, p.Nick);
    var c = await Open();
    var me = await c.LoginAsync(p.User, Pw);
    api[p.User] = c;
    id[p.User] = me.UserId;
    string avatar = await c.UploadImageAsync(await images.GetImageAsync(p.Avatar), "png");
    await c.UpdateProfileAsync(null, avatar, p.Bio);
    for (long left = p.Money; left > 0; left -= 1_000_000) await c.RechargeAsync(Math.Min(left, 1_000_000));
    Console.WriteLine($"用户 {p.User}（{p.Nick}）已创建");
}

var categories = (await api["alice"].GetCategoriesAsync()).ToDictionary(x => x.Name, x => x.CategoryId);
var uploaded = new Dictionary<string, string>();
async Task<string> Img(string file, TcpApiClient who)
{
    if (!uploaded.TryGetValue(file, out var name))
        uploaded[file] = name = await who.UploadImageAsync(await images.GetImageAsync(file), file.EndsWith(".png") ? "png" : "jpg");
    return name;
}

var goods = new Dictionary<string, long>();
async Task G(string seller, string title, string desc, long price, long orig, string cat, int cond, string img)
{
    var c = api[seller];
    var saved = await c.PublishGoodsAsync(new GoodsEdit
    {
        Title = title, Description = desc, Price = price, OriginalPrice = orig,
        CategoryId = categories[cat], Condition = cond, Images = new() { await Img(img, c) },
    });
    goods[title] = saved.GoodsId;
    // 开了 AI 审核的话先等它审完（下单、收藏都要求商品在售）
    if (saved.Status == GoodsStatus.Reviewing)
    {
        string status = await c.WaitReviewAsync(saved.GoodsId);
        if (status != GoodsStatus.OnSale) Console.WriteLine($"  注意：《{title}》审核结果是 {status}");
    }
}

// 先发商品（顺序决定"最新发布"的排序：后发的在前）
await G("bob", "线性代数 辅导书", "考研用过。", 1800, 4500, "书籍教材", 2, "math.jpg");
await G("dave", "蓝牙音箱", "低音很足。", 9900, 19900, "数码电子", 1, "earphone.jpg");
await G("alice", "台式风扇", "宿舍用，声音小。", 3000, 8900, "生活用品", 2, "fan.jpg");
await G("alice", "大一教材一套", "高数、线代、C 语言。", 5000, 15000, "书籍教材", 2, "cbook.jpg");
await G("alice", "旧台灯", "能用，就是有点旧。", 1000, 0, "生活用品", 3, "lamp.jpg");
await G("alice", "民谣吉他 41寸", "初学者入门款，送变调夹和琴包。", 26000, 59900, "其他", 2, "guitar.jpg");
await G("carol", "四级听力耳机", "考试专用，调频正常。", 2500, 6000, "数码电子", 2, "earphone.jpg");
await G("dave", "iPad 2021 64G", "无磕碰，送保护壳和贴膜。", 168000, 249900, "数码电子", 1, "ipad.jpg");
await G("bob", "帆布鞋 41码", "只穿过两次，尺码不合适。", 4500, 13900, "服饰鞋包", 1, "shoes.jpg");
await G("carol", "防晒霜 全新未拆", "买多了，保质期到 2028 年。", 6500, 12000, "美妆个护", 0, "sunscreen.jpg");
await G("alice", "折叠收纳箱 两个", "宿舍收纳好帮手，毕业带不走。", 2000, 4500, "生活用品", 2, "box.jpg");
await G("dave", "加厚瑜伽垫", "10mm 加厚，带收纳袋。", 3500, 8900, "运动户外", 1, "yoga.jpg");
await G("carol", "C语言程序设计 谭浩强", "C 语言课本，重点都划好了。", 1200, 3900, "书籍教材", 2, "cbook.jpg");
await G("bob", "机械键盘 青轴 87键", "打字很爽，室友嫌吵所以出。", 12000, 29900, "数码电子", 2, "keyboard.jpg");
await G("alice", "优衣库羽绒服 L码", "黑色，穿过一个冬天，已干洗。", 15000, 49900, "服饰鞋包", 2, "coat.jpg");
await G("dave", "小米台灯 1S", "护眼台灯，支持手机调光。", 7000, 16900, "生活用品", 1, "lamp.jpg");
await G("carol", "斯伯丁篮球 7号", "室外用过几次，气很足。", 8000, 16900, "运动户外", 2, "basketball.jpg");
await G("bob", "考研英语真题 黄皮书", "2026 版，几乎没写。", 3000, 7600, "书籍教材", 1, "english.jpg");
await G("alice", "罗技无线鼠标 M330", "静音款，用了半年，送鼠标垫。", 6000, 10900, "数码电子", 2, "mouse.jpg");
await G("dave", "AirPods 二代", "电池健康 90%，配件齐全，有发票。", 39900, 89900, "数码电子", 2, "airpods.jpg");
await G("carol", "宿舍USB小风扇 静音", "三档风速，充一次电用一天，夏天神器。", 1500, 3900, "生活用品", 1, "fan.jpg");
await G("bob", "高等数学 同济第七版 上下册", "有少量笔记，不影响使用，期末复习必备。", 2500, 6800, "书籍教材", 2, "math.jpg");
await G("alice", "九成新山地自行车", "骑了一学期，变速正常，送车锁和打气筒。", 32000, 80000, "运动户外", 1, "bike.jpg");
await api["alice"].OffShelfAsync(goods["旧台灯"]);
Console.WriteLine($"商品 {goods.Count} 件已发布");

// 订单：四种状态各来一笔
var o1 = await api["alice"].CreateOrderAsync(goods["线性代数 辅导书"]);       // 已完成
await api["bob"].ShipOrderAsync(o1.OrderId, "图书馆门口当面交易");
await api["alice"].ConfirmOrderAsync(o1.OrderId);
await api["alice"].CreateOrderAsync(goods["蓝牙音箱"]);                        // 待发货（我买的）
await api["carol"].CreateOrderAsync(goods["台式风扇"]);                        // 待发货（我卖的）
var o4 = await api["bob"].CreateOrderAsync(goods["大一教材一套"]);             // 待收货（我卖的）
await api["alice"].ShipOrderAsync(o4.OrderId, "已放在快递柜 3 号");
Console.WriteLine("订单 4 笔已创建");

// 聊天
async Task M(string from, string to, string text, string? title = null) =>
    await api[from].SendMessageAsync(id[to], text, title == null ? 0 : goods[title]);
await M("alice", "bob", "高数书还在吗？", "高等数学 同济第七版 上下册");
await M("bob", "alice", "在的，随时可以来拿", "高等数学 同济第七版 上下册");
await M("alice", "bob", "好的，明天中午去找你", "高等数学 同济第七版 上下册");
await M("bob", "alice", "OK，到了宿舍楼下给我发消息", "高等数学 同济第七版 上下册");
await api["alice"].MarkReadAsync(id["bob"]);
await M("bob", "alice", "宿舍楼下见");                                          // 留一条 bob 的未读
await M("dave", "alice", "音箱明天给你发货哈", "蓝牙音箱");
await api["alice"].MarkReadAsync(id["dave"]);
await M("carol", "alice", "自行车能便宜点吗？", "九成新山地自行车");
await M("carol", "alice", "300 可以吗", "九成新山地自行车");
Console.WriteLine("聊天已创建");

// 收藏
await api["alice"].AddFavoriteAsync(goods["AirPods 二代"]);
await api["alice"].AddFavoriteAsync(goods["iPad 2021 64G"]);
await api["alice"].AddFavoriteAsync(goods["机械键盘 青轴 87键"]);

foreach (var c in api.Values) c.Dispose();
Console.WriteLine("完成。账号 alice / bob / carol / dave，密码都是 " + Pw);
return 0;
