# 任务说明 · 组员B：客户端消息和个人中心（C# / WPF）

## 1. 你负责什么

| 界面 | 文件 | 从哪进来 |
|---|---|---|
| 消息列表 | `Pages/Chat/ChatListPage.xaml` + `ChatListViewModel.cs` | 顶部"消息" |
| 聊天 | `Pages/Chat/ChatPage.xaml` + `ChatViewModel.cs` | 消息列表点一行；商品详情点"我想要" |
| 我的 | `Pages/Me/MePage.xaml` + `MeViewModel.cs` | 顶部"我的" |
| 订单列表 | `Pages/Me/OrdersPage.xaml` + `OrdersViewModel.cs` | "我的"里点"我买到的/我卖出的" |
| 订单详情 | `Pages/Me/OrderDetailPage.xaml` + `OrderDetailViewModel.cs` | 订单列表点一行；买完东西自动跳过来 |

每个界面现在都是一个黄框占位，你要把它换成真正的界面。

**只改 `Pages/Chat/` 和 `Pages/Me/` 里的文件**（可以新建文件）。`.xaml.cs` 里构造函数的参数是和 `Navigator` 约定好的，**不要改**；`ChatPage` 的 `PeerId` 属性也不要删。

## 2. 先跑起来（第一天就要做）

1. VS 2022 双击打开 `client/XiaoYu.sln`。
2. 右边"解决方案资源管理器"里右键 **XiaoYu.App** →"设为启动项目"。
3. 按 **F5**。第一次会自动下载 NuGet 包，要等一会儿。
4. 用 **alice / 123456** 登录，点顶部"消息""我的"，看到黄色占位框就对了。顶部"消息"旁边应该有个红色的 3（3 条未读）。

现在用的是**假数据模式**：不需要服务器，关掉程序数据就恢复原样。alice 账号里已经准备好了数据：
- 3 条未读私信（鲍勃 1 条、小卡 2 条）
- 我买到的：1 个已完成、1 个待发货
- 我卖出的：1 个待发货（你可以点发货）、1 个待收货

为了方便你测**推送**，假数据会自动"演戏"：
- 你给任何人发私信，**1.5 秒后对方会自动回复**一条（触发 `MessageReceived`）
- 你买了别人的东西，**5 秒后卖家会自动发货**（触发 `OrderUpdated`）

## 3. 先读这些（按顺序）

1. 《协议文档》第 5 节（数据长什么样）、6.4–6.6 节（订单、收藏、聊天的规则）、第 7 节（推送）
2. `XiaoYu.Core/Api/IApiClient.cs`：所有能调的接口和推送事件
3. `XiaoYu.Core/Models/Models.cs`：数据模型和状态常量（`OrderStatus.Paid` 等）
4. `XiaoYu.App/ViewModels/ViewModelBase.cs`：顶部有完整的写法示例
5. **`XiaoYu.App/Shell/LoginWindow.xaml` + `LoginViewModel.cs`：已经写好的完整例子**
6. `XiaoYu.App/Shell/MainViewModel.cs`：看它怎么订阅推送、怎么用 `Ui.Run`
7. `XiaoYu.App/Styles/Theme.xaml`、`Services/`、`Controls/`

## 4. MVVM 五分钟入门

```csharp
public partial class OrdersViewModel : ViewModelBase
{
    [ObservableProperty] private string _status = "";                 // 自动生成 Status 属性
    public ObservableCollection<Order> Orders { get; } = new();        // 列表，Add 了界面自动多一行

    public override Task OnNavigatedToAsync() => LoadAsync();           // 页面打开时加载

    [RelayCommand]                                                       // 自动生成 LoadCommand
    private Task LoadAsync() => RunAsync(async () =>                    // 自动处理加载中和错误
    {
        var page = await Api.GetOrdersAsync(Role, Status == "" ? null : Status);
        Orders.Clear();
        foreach (var o in page.Items) Orders.Add(o);
    });

    [RelayCommand]
    private void Open(Order order) => Navigator.ToOrderDetail(order.OrderId);
}
```

列表每一行点击跳转（把这一行的数据当参数传给命令）：
```xml
<ItemsControl ItemsSource="{Binding Orders}">
    <ItemsControl.ItemTemplate>
        <DataTemplate>
            <Button Style="{StaticResource RowButton}"
                    Command="{Binding DataContext.OpenCommand, RelativeSource={RelativeSource AncestorType=UserControl}}"
                    CommandParameter="{Binding}">
                <!-- 这一行长什么样 -->
            </Button>
        </DataTemplate>
    </ItemsControl.ItemTemplate>
</ItemsControl>
```

常用写法速查：

| 想做的事 | 写法 |
|---|---|
| 显示价格 / 余额 | `Text="{Binding Price, Converter={StaticResource Money}}"` |
| 显示"5分钟前" / 完整时间 | `Converter={StaticResource TimeAgo}` / `Converter={StaticResource DateTime}` |
| 订单状态中文 | `Converter={StaticResource OrderStatusText}` |
| 服务器上的图片 | `<Image s:RemoteImage.Source="{Binding GoodsCover}" />` |
| 圆头像 | `<c:Avatar Image="{Binding Peer.Avatar}" Nickname="{Binding Peer.Nickname}" Size="44" />` |
| 有内容才显示（未读红点） | `Visibility="{Binding Unread, Converter={StaticResource NotEmptyToVis}}"` |
| 单选标签 | `<RadioButton Style="{StaticResource PillTab}" Content="待发货" IsChecked="{Binding Status, Converter={StaticResource ValueEquals}, ConverterParameter=paid}" />` |
| 我的 id / 我的信息 | `AppServices.MyId` / `AppServices.CurrentUser` |
| 跳转 | `Navigator.ToChat(id)`、`Navigator.ToOrderDetail(id)`、`Navigator.ToGoodsDetail(id)` |
| 确认框 / 提示 | `Ui.Confirm("确定取消订单吗？")`、`Ui.Toast("已发货")` |

## 5. 推送：本组最重要的知识点

服务器会主动推消息过来，触发 `Api.MessageReceived` 或 `Api.OrderUpdated`。有两个必须遵守的规矩：

**① 回调在后台线程，改界面数据前要用 `Ui.Run` 切回界面线程**，否则程序直接报错：
```csharp
private void OnMessage(ChatMessage msg) => Ui.Run(() =>
{
    if (msg.FromId != PeerId) return;   // 不是这个聊天窗口的，不管
    Messages.Add(msg);
});
```

**② 进页面订阅，离开页面取消订阅**，否则页面关了还在收消息，越积越多：
```csharp
public override async Task OnNavigatedToAsync()
{
    Api.MessageReceived += OnMessage;
    await LoadAsync();
}

public override void OnNavigatedFrom() => Api.MessageReceived -= OnMessage;
```

注意：从下一页返回时，`OnNavigatedToAsync` 会再调一次，所以要先 `-=` 再 `+=`，免得订阅两次。

## 6. 每个界面的要求

### 6.1 消息列表 `ChatListPage`
- `Api.GetConversationsAsync()`，每行：对方头像、昵称、最后一条消息（太长截断 `TextTrimming="CharacterEllipsis"`）、时间（`TimeAgo`）、未读数红点
- 点一行：`Navigator.ToChat(对方 id)`
- 订阅 `MessageReceived`：收到新消息就重新加载列表
- 没有会话时显示"还没有消息"

### 6.2 聊天 `ChatPage(long peerId, long goodsId)`
```
┌─────────────────────────────────────┐
│ 鲍勃                                 │
│ ┌──┐ 高等数学 同济第七版  ¥25   [查看] │ ← GoodsId > 0 时显示
│ └──┘                                 │
│        [查看更早的消息]               │
│ ┌──────────┐                         │
│ │在的，随时…│  ← 对方：靠左，白色       │
│ └──────────┘                         │
│                  ┌──────────────┐    │
│                  │好的，明天中午…│ ← 我：靠右，黄色
│                  └──────────────┘    │
│ [ 输入消息……                ] [发送] │
└─────────────────────────────────────┘
```
- 顶部：`Api.GetUserAsync(PeerId)` 拿对方昵称
- `GoodsId > 0`：`Api.GetGoodsDetailAsync` 显示商品小卡片，点击 `Navigator.ToGoodsDetail`
- 消息：`Api.GetChatHistoryAsync(PeerId)` 取最近 20 条；`msg.FromId == AppServices.MyId` 的靠右、黄色，别人的靠左、白色。做法：每条消息的 ViewModel 加一个 `IsMine` 属性，或者在 XAML 里用 `DataTrigger`
- "查看更早的消息"：`beforeId` = 当前第一条的 `MessageId`，拿到的插到列表**前面**；返回空就隐藏这个按钮
- 输入框 + 发送（回车发送）：`Api.SendMessageAsync(PeerId, 内容, GoodsId)`，成功后加到末尾、清空输入框、**滚到最底下**（`ScrollViewer.ScrollToEnd()`，可以在 `.xaml.cs` 里监听集合变化去滚）
- 打开页面时、收到对方新消息时：`Api.MarkReadAsync(PeerId)`，再 `AppServices.NotifyUnreadChanged()`，顶部红点会刷新
- 订阅 `MessageReceived`，只处理 `FromId == PeerId` 的

### 6.3 我的 `MePage`
- 顶部卡片：大头像、昵称、简介、余额。进页面先 `await AppServices.RefreshMeAsync()`，然后读 `AppServices.CurrentUser`
- **编辑资料**：可以在页面上展开一个小表单，改昵称、简介，换头像（`ImageTools.PickImages(false)` → `LoadForUpload` → `UploadImageAsync`）→ `Api.UpdateProfileAsync(...)` → `AppServices.SetCurrentUser(返回值)`
- **充值**：输入金额（元）→ `Fmt.ParseYuan` → `Api.RechargeAsync` → `AppServices.RefreshMeAsync()` → `Ui.Toast("充值成功")`
- 两个大按钮："我买到的" `Navigator.ToOrders(OrderRole.Buyer)`、"我卖出的" `Navigator.ToOrders(OrderRole.Seller)`
- **我发布的**：状态标签（全部/在售/已卖出/已下架）+ 列表 `Api.GetMyGoodsAsync(status)`；每行：图、标题、价格、状态，按钮"编辑"（`Navigator.ToEditGoods`，已卖出的不显示）、"下架"（在售时）、"重新上架"（已下架时）；操作完刷新列表；点图片或标题打开详情
- （选做）我的收藏：`Api.GetFavoritesAsync()`，可以用 `GoodsCard`

### 6.4 订单列表 `OrdersPage(string role)`
- 标题：`Role == OrderRole.Buyer` 显示"我买到的"，否则"我卖出的"
- 状态标签：全部 / 待发货 `paid` / 待收货 `shipped` / 已完成 `completed` / 已取消 `cancelled`
- 列表：`Api.GetOrdersAsync(Role, 状态或 null, page)`；每行：商品图、标题、价格、状态文字、对方昵称（买到的显示卖家，卖出的显示买家）、下单时间
- 点一行：`Navigator.ToOrderDetail(订单 id)`；"加载更多"：`HasMore` 时显示
- 订阅 `OrderUpdated`：有订单变化就重新加载

### 6.5 订单详情 `OrderDetailPage(long orderId)`
- `Api.GetOrderAsync(OrderId)`
- 顶部大字：状态（`OrderStatusText`）
- 进度：下单 → 发货 → 完成，每一步显示时间（0 表示还没到这一步，显示灰色）；已取消的显示取消时间
- 商品：图、标题、成交价，点击 `Navigator.ToGoodsDetail`；订单号；发货备注
- 对方：头像、昵称 +"联系 TA"按钮 `Navigator.ToChat(对方 id, 商品 id)`
- 操作按钮，**按我的身份和订单状态决定显示哪些**：

| 我是 | 状态 | 按钮 |
|---|---|---|
| 卖家 | 待发货 | "发货"（填备注的输入框 + 按钮）、"取消订单" |
| 买家 | 待发货 | "取消订单" |
| 买家 | 待收货 | "确认收货" |
| 其他 | | 没有按钮 |

- 每个操作先 `Ui.Confirm`；成功后用返回的 `Order` 刷新页面，再 `AppServices.RefreshMeAsync()`（余额会变）
- 订阅 `OrderUpdated`：推来的是这个订单就刷新页面（测试方法：买一件别人的东西，停在订单详情页，5 秒后自动变成"待收货"）

## 7. 规则和小提示

- 颜色、字号用 `Theme.xaml` 里的样式；需要新样式写在自己页面的 `<UserControl.Resources>` 里，**不要改 Theme.xaml**。
- 所有调接口的地方都放进 `RunAsync(...)`。
- 不要用 `.Result` / `.Wait()`，一律 `await`。
- 一个界面里显示的"按钮要不要出现"，建议在 ViewModel 里算好一个 `bool` 属性（比如 `CanShip`），XAML 里用 `BoolToVis` 绑定，比在 XAML 里写一堆触发器清楚。

## 8. 完成标准

- 5 个界面按要求都能用，在假数据模式下随便点不崩溃
- 收到推送时界面能自动更新（聊天自动出现新消息、订单自动变状态）
- 出错有提示，加载有"加载中"
- 编译没有错误
- 交付方式见《协作说明》第 7 节

## 9. 答辩可以讲的

- **实时推送**：服务器主动推送，不用客户端一直刷新；后台线程和界面线程的区别，为什么要 `Ui.Run`
- **事件订阅和取消订阅**：不取消会内存泄漏、重复处理
- **订单状态机在界面上的体现**：不同身份、不同状态显示不同按钮
- **MVVM + 数据绑定**
