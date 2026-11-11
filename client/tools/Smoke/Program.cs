// 用法：dotnet run -- [host] [port]      连真服务器
//       dotnet run -- fake               测假数据
using XiaoYu.Core.Api;
using XiaoYu.Core.Fake;
using XiaoYu.Core.Models;

int failed = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"{(ok ? "[通过]" : "[失败]")} {what}");
    if (!ok) failed++;
}

if (args.Length > 0 && args[0] == "fake")
{
    var f = new FakeApiClient { DelayMs = 0 };
    await f.ConnectAsync();
    var me = await f.LoginAsync("alice", "Ab123456");
    Check(me.Nickname == "阿狸", "假数据登录 alice");
    var home = await f.SearchGoodsAsync(new GoodsQuery());
    Check(home.Total >= 18 && home.Items[0].Cover != "", $"首页 {home.Total} 件商品");
    Check((await f.GetImageAsync(home.Items[0].Cover)).Length > 1000, "示例图片能读到");
    Check(await f.GetUnreadCountAsync() == 4, "alice 有 4 条未读");
    var got = new TaskCompletionSource<ChatMessage>();
    f.MessageReceived += m => got.TrySetResult(m);
    await f.SendMessageAsync(1002, "你好");
    Check((await got.Task.WaitAsync(TimeSpan.FromSeconds(5))).FromId == 1002, "对方自动回复（推送）");
    var shipped = new TaskCompletionSource<Order>();
    f.OrderUpdated += o => shipped.TrySetResult(o);
    var fakeOrder = await f.CreateOrderAsync(2004);
    Check(fakeOrder.Status == OrderStatus.Paid, "下单");
    Check((await shipped.Task.WaitAsync(TimeSpan.FromSeconds(8))).Status == OrderStatus.Shipped, "卖家自动发货（推送）");
    try { await f.CreateOrderAsync(2004); Check(false, "重复购买应该失败"); }
    catch (ApiException ex) { Check(ex.Code == 409, $"重复购买 → {ex.Code} {ex.Message}"); }
    Console.WriteLine(failed == 0 ? "全部通过" : $"{failed} 项失败");
    return failed;
}

string host = args.Length > 0 ? args[0] : "127.0.0.1";
int port = args.Length > 1 ? int.Parse(args[1]) : 9527;
string suffix = DateTime.Now.ToString("HHmmss");

using var seller = new TcpApiClient(host, port);
using var buyer = new TcpApiClient(host, port);
await seller.ConnectAsync();
await buyer.ConnectAsync();
Check(seller.IsConnected && buyer.IsConnected, $"连上 {host}:{port}");

await seller.RegisterDevAsync("seller" + suffix, "Ab123456", "卖家" + suffix);
await buyer.RegisterDevAsync("buyer" + suffix, "Ab123456", "买家" + suffix);
var s = await seller.LoginAsync("seller" + suffix, "Ab123456");
var b = await buyer.LoginAsync("buyer" + suffix, "Ab123456");
Check(s.UserId > 0 && b.Nickname == "买家" + suffix, "注册并登录两个账号");

var img = await seller.UploadImageAsync(new byte[] { 0x89, 0x50, 0x4e, 0x47, 1, 2, 3 }, "png");
Check((await buyer.GetImageAsync(img)).Length == 7, "上传、下载图片");

var cats = await seller.GetCategoriesAsync();
var published = await seller.PublishGoodsAsync(new GoodsEdit
{
    Title = "联调用的自行车" + suffix, Description = "测试", Price = 32000, OriginalPrice = 80000,
    CategoryId = cats[4].CategoryId, Condition = 1, Images = new() { img },
});
long goodsId = published.GoodsId;
if (published.Status == GoodsStatus.Reviewing)
    Check(await seller.WaitReviewAsync(goodsId) == GoodsStatus.OnSale, "AI 审核通过，商品上架");
await seller.UpdateGoodsAsync(goodsId, new GoodsEdit { Price = 30000 });
var detail = await buyer.GetGoodsDetailAsync(goodsId);
Check(detail.Price == 30000 && detail.Images.Count == 1 && detail.CategoryName == cats[4].Name && detail.Seller.UserId == s.UserId,
      "发布、修改、查看详情");
var found = await buyer.SearchGoodsAsync(new GoodsQuery { Keyword = "联调用的自行车" + suffix, Sort = GoodsSort.PriceAsc });
Check(found.Total == 1 && found.Items[0].GoodsId == goodsId && found.Items[0].Cover == img, "搜索");

try { await buyer.CreateOrderAsync(goodsId); Check(false, "余额不足应该失败"); }
catch (ApiException ex) { Check(ex.Code == ErrorCode.NoMoney, $"余额不足 → {ex.Code} {ex.Message}"); }
Check(await buyer.RechargeAsync(100000) == 100000, "充值");

var orderPush = new TaskCompletionSource<Order>();
seller.OrderUpdated += o => orderPush.TrySetResult(o);
var order = await buyer.CreateOrderAsync(goodsId);
Check(order.Status == OrderStatus.Paid && order.OrderNo.Length == 18, "下单");
Check((await orderPush.Task.WaitAsync(TimeSpan.FromSeconds(3))).OrderId == order.OrderId, "卖家收到 order.updated 推送");

var chatPush = new TaskCompletionSource<ChatMessage>();
seller.MessageReceived += m => chatPush.TrySetResult(m);
await buyer.SendMessageAsync(s.UserId, "什么时候能当面交易？", goodsId);
Check((await chatPush.Task.WaitAsync(TimeSpan.FromSeconds(3))).Content == "什么时候能当面交易？", "卖家收到 chat.new_message 推送");
Check(await seller.GetUnreadCountAsync() == 1, "未读数");
var convs = await seller.GetConversationsAsync();
Check(convs.Count == 1 && convs[0].Peer.UserId == b.UserId && convs[0].Unread == 1, "会话列表");
Check(await seller.MarkReadAsync(b.UserId) == 1, "标记已读");
await seller.SendMessageAsync(b.UserId, "明天中午食堂门口");
var history = await buyer.GetChatHistoryAsync(s.UserId);
Check(history.Count == 2 && history[1].Content == "明天中午食堂门口", "聊天记录从早到晚");

await seller.ShipOrderAsync(order.OrderId, "食堂门口");
var done = await buyer.ConfirmOrderAsync(order.OrderId);
Check(done.Status == OrderStatus.Completed && (await seller.GetMeAsync()).Balance == 30000, "发货、确认收货、钱到卖家");
var bought = await buyer.GetOrdersAsync(OrderRole.Buyer);
Check(bought.Total == 1 && bought.Items[0].Seller.UserId == s.UserId, "我买到的");

await buyer.AddFavoriteAsync(goodsId);
Check(await buyer.IsFavoriteAsync(goodsId) && (await buyer.GetFavoritesAsync()).Total == 1, "收藏");

var profile = await buyer.GetUserAsync(s.UserId);
Check(profile.SoldCount == 1, "卖家主页");
await buyer.LogoutAsync();
try { await buyer.GetMeAsync(); Check(false, "退出后应该要求登录"); }
catch (ApiException ex) { Check(ex.Code == ErrorCode.NotLogin, "退出登录后 → 401"); }

Console.WriteLine(failed == 0 ? "全部通过" : $"{failed} 项失败");
return failed;
