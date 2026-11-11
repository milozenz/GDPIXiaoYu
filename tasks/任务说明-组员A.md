# 任务说明 · 组员A：客户端交易相关页面（C# / WPF）

## 1. 你负责什么

文件夹 `client/XiaoYu.App/Pages/Trade/` 里的 5 个界面：

| 界面 | 文件 | 从哪进来 |
|---|---|---|
| 注册 | `RegisterWindow.xaml` + `RegisterViewModel.cs` | 登录窗口点"去注册" |
| 首页 | `HomePage.xaml` + `HomeViewModel.cs` | 顶部"首页" |
| 商品详情 | `GoodsDetailPage.xaml` + `GoodsDetailViewModel.cs` | 点首页的商品卡片 |
| 发布 / 编辑商品 | `PublishPage.xaml` + `PublishViewModel.cs` | 顶部"发布闲置"；详情页的"编辑" |
| 用户主页 | `UserProfilePage.xaml` + `UserProfileViewModel.cs` | 详情页点卖家 |

每个界面现在都是一个黄框占位，你要把它换成真正的界面。

**只改这个文件夹里的文件**（可以新建文件）。`.xaml.cs` 里构造函数的参数是和 `Navigator` 约定好的，**不要改**。

## 2. 先跑起来（第一天就要做）

1. VS 2022 双击打开 `client/XiaoYu.sln`。
2. 右边"解决方案资源管理器"里右键 **XiaoYu.App** →"设为启动项目"。
3. 按 **F5**。第一次会自动下载 NuGet 包，要等一会儿。
4. 弹出登录窗口，用 **alice / 123456** 登录，进入主界面，看到"首页"的黄色占位框就对了。

现在用的是**假数据模式**（`appsettings.json` 里 `"UseFakeApi": true`）：不需要服务器，数据都在内存里，规则和真服务器一样，关掉程序就恢复原样。**整个开发过程都用假数据，不要改这个配置。**

## 3. 先读这些（按顺序）

1. 《协议文档》第 5 节（数据长什么样）和 6.2、6.3 节（用户、商品的规则）
2. `XiaoYu.Core/Api/IApiClient.cs`：所有能调的接口，每个方法都有注释
3. `XiaoYu.Core/Models/Models.cs`：数据模型
4. `XiaoYu.App/ViewModels/ViewModelBase.cs`：顶部有一个完整的 ViewModel 写法示例
5. **`XiaoYu.App/Shell/LoginWindow.xaml` + `LoginViewModel.cs`：已经写好的完整例子，照着写**
6. `XiaoYu.App/Styles/Theme.xaml`：能用的样式、颜色、转换器
7. `XiaoYu.App/Services/`：`Navigator`（跳转）、`Ui`（弹窗/提示）、`ImageTools`（选图）、`RemoteImage`（显示图片）
8. `XiaoYu.App/Controls/`：`GoodsCard`（商品卡片）、`Avatar`（圆头像）

## 4. MVVM 五分钟入门

界面（`.xaml`）只管长什么样，数据和逻辑都在 ViewModel 里，两边靠**绑定**连起来。

```csharp
public partial class HomeViewModel : ViewModelBase
{
    // 1. 普通属性：加 [ObservableProperty]，写成 _小写开头 的字段，会自动生成 Keyword 属性
    [ObservableProperty] private string _keyword = "";

    // 2. 列表：用 ObservableCollection，往里 Add/Remove，界面会自动跟着变
    public ObservableCollection<GoodsBrief> Goods { get; } = new();

    // 3. 页面打开时加载数据
    public override Task OnNavigatedToAsync() => SearchAsync();

    // 4. 按钮命令：加 [RelayCommand]，会自动生成 SearchCommand
    [RelayCommand]
    private Task SearchAsync() => RunAsync(async () =>   // RunAsync 会自动处理"加载中"和错误提示
    {
        var page = await Api.SearchGoodsAsync(new GoodsQuery { Keyword = Keyword });
        Goods.Clear();
        foreach (var g in page.Items) Goods.Add(g);
    });
}
```

```xml
<TextBox Style="{StaticResource Input}" Tag="搜索闲置" Text="{Binding Keyword, UpdateSourceTrigger=PropertyChanged}" />
<Button Style="{StaticResource PrimaryButton}" Content="搜索" Command="{Binding SearchCommand}" />
<TextBlock Style="{StaticResource ErrorText}" Text="{Binding ErrorMessage}" />
<TextBlock Text="加载中…" Visibility="{Binding IsBusy, Converter={StaticResource BoolToVis}}" />
```

常用写法速查：

| 想做的事 | 写法 |
|---|---|
| 显示价格 | `Text="{Binding Price, Converter={StaticResource Money}}"` → ¥320 |
| 显示"5分钟前" | `Text="{Binding CreatedAt, Converter={StaticResource TimeAgo}}"` |
| 显示服务器上的图片 | `<Image s:RemoteImage.Source="{Binding Cover}" Stretch="UniformToFill" />` |
| 圆头像 | `<c:Avatar Image="{Binding Seller.Avatar}" Nickname="{Binding Seller.Nickname}" Size="32" />` |
| 商品卡片网格 | 见 `Controls/GoodsCard.xaml` 顶部注释 |
| 一组单选标签 | `<RadioButton Style="{StaticResource PillTab}" Content="价格从低到高" IsChecked="{Binding Sort, Converter={StaticResource ValueEquals}, ConverterParameter=price_asc}" />` |
| 有内容才显示 | `Visibility="{Binding Xxx, Converter={StaticResource NotEmptyToVis}}"` |
| 列表为空时显示"暂无" | `Visibility="{Binding Goods.Count, Converter={StaticResource EmptyToVis}}"` |
| 跳转页面 | `Navigator.ToGoodsDetail(id)`、`Navigator.ToChat(sellerId, goodsId)`、`Navigator.Back()` |
| 确认框 / 提示 | `if (!Ui.Confirm("确定购买吗？")) return;`、`Ui.Toast("发布成功")` |
| 我的 id | `AppServices.MyId` |

`xmlns:c` 和 `xmlns:s` 已经在每个页面的开头声明好了。

## 5. 每个界面的要求

### 5.1 注册 `RegisterWindow`
- 输入框：用户名、密码、确认密码、昵称。密码框的取值方法照 `LoginViewModel` 的写法
- 点"注册"前先在界面上检查：用户名 3–20 位只能字母数字下划线；密码 6–32 位；两次密码一致；昵称 1–20 字。不对就显示 `ErrorMessage`，**不要发请求**
- 检查通过：如果 `!Api.IsConnected` 先 `await Api.ConnectAsync()`，再 `Api.RegisterAsync(...)`
- 成功：`Registered?.Invoke(用户名)`，窗口会自动关闭，登录框会自动填好用户名
- 失败（比如"用户名已被注册"）：显示 `ErrorMessage`

### 5.2 首页 `HomePage`
```
┌──────────────────────────────────────────────────────────┐
│ [ 搜索闲置……                           ] [搜索]          │
│ (全部)(数码电子)(书籍教材)(生活用品)……                     │
│ 排序：(最新发布)(价格从低到高)(价格从高到低)(最多浏览)     │
│ ┌────┐ ┌────┐ ┌────┐ ┌────┐                              │
│ │卡片│ │卡片│ │卡片│ │卡片│   ← WrapPanel + GoodsCard    │
│ └────┘ └────┘ └────┘ └────┘                              │
│                    [加载更多]                             │
└──────────────────────────────────────────────────────────┘
```
- 搜索框 + 搜索按钮（回车也能搜：TextBox 的 `KeyDown` 或者把按钮设成 `IsDefault`）
- 分类标签："全部" + `Api.GetCategoriesAsync()` 返回的分类，点了就按分类筛选
- 排序标签：`Fmt.SortOptions` 里的 4 种
- 列表：`Api.SearchGoodsAsync(query)`；条件变了从第 1 页重新查
- "加载更多"：`HasMore` 为 true 才显示；点了 `Page + 1`，结果**追加**到后面
- 没结果时显示"没有找到相关闲置"
- 整个页面外面包一层 `ScrollViewer`，内容多了能滚动
- （选做）价格区间：两个输入框，用 `Fmt.ParseYuan` 把元转成分

### 5.3 商品详情 `GoodsDetailPage(long goodsId)`
- `Api.GetGoodsDetailAsync(GoodsId)`
- 左边：一张大图 + 下面一排小图，点小图换大图
- 右边：标题；价格（红色大字）+ 原价（灰色删除线 `TextDecorations="Strikethrough"`，0 就不显示）；成色（`ConditionText` 转换器）；分类；浏览量；发布时间；描述
- 卖家卡片：头像 + 昵称，点击 `Navigator.ToUserProfile(卖家 id)`
- 按钮：
  - **我想要**：`Navigator.ToChat(卖家 id, GoodsId)`
  - **立即购买**：`Ui.Confirm` → `Api.CreateOrderAsync` → `await AppServices.RefreshMeAsync()`（余额变了）→ `Navigator.ToOrderDetail(订单 id)`。余额不足会报 402，提示"余额不足，可以去「我的」充值"
  - **收藏**（选做）：`IsFavoriteAsync` 决定显示"收藏"还是"已收藏"
- 如果是**自己的商品**（`Seller.UserId == AppServices.MyId`）：不显示"我想要""立即购买"，改成"编辑"（`Navigator.ToEditGoods`）和"下架"/"重新上架"
- 已卖出、已下架：购买按钮不可点，显示状态

### 5.4 发布 / 编辑 `PublishPage(long? editGoodsId)`
- **图片**：点"添加图片" → `ImageTools.PickImages()` 选图 → 每张 `ImageTools.LoadForUpload(path)` 压缩 → `Api.UploadImageAsync(bytes, "jpg")` 上传，拿到图片名存进列表；显示缩略图，每张右上角有"×"可以删掉；最多 9 张；第一张是封面
- 标题（1–30 字）、描述（多行，0–500 字：`AcceptsReturn="True" TextWrapping="Wrap" Height="120"`）
- 价格、原价：输入的是"元"，用 `Fmt.ParseYuan` 转成分，返回 null 说明格式不对
- 分类：`ComboBox`，`ItemsSource` 绑分类列表，`DisplayMemberPath="Name"`
- 成色：4 个单选，`Fmt.ConditionNames`
- 提交前先在界面检查，规则和协议文档 6.3 一样
- **发布**：`Api.PublishGoodsAsync(new GoodsEdit { ... })` → `Ui.Toast("发布成功")` → `Navigator.ToGoodsDetail(新 id)`
- **编辑**（`EditGoodsId != null`）：先 `GetGoodsDetailAsync` 把原内容填进表单，标题改成"编辑商品"；保存用 `Api.UpdateGoodsAsync` → `Navigator.Back()`

### 5.5 用户主页 `UserProfilePage(long userId)`
- `Api.GetUserAsync(UserId)`：大头像、昵称、简介、加入时间、在售 X 件、已卖出 Y 件
- 他在卖的：`Api.SearchGoodsAsync(new GoodsQuery { SellerId = UserId })`，用 `GoodsCard` 显示
- "私信 TA"：`Navigator.ToChat(UserId)`；看的是自己时不显示

## 6. 规则和小提示

- 颜色、字号尽量用 `Theme.xaml` 里的样式，界面才统一。实在需要新样式，写在自己页面的 `<UserControl.Resources>` 里，**不要改 Theme.xaml**。
- 所有调接口的地方都放进 `RunAsync(...)`，出错会自动显示在 `ErrorMessage`。
- 不要用 `.Result` 或 `.Wait()` 等异步结果，界面会卡死。一律 `await`。
- 想测试出错的情况：发布时标题留空、价格填 0；买自己的东西；买东西时用余额少的账号（bob）。

## 7. 完成标准

- 5 个界面按上面的要求都能用，在假数据模式下随便点不崩溃
- 出错有提示，加载有"加载中"
- 编译没有错误
- 交付方式见《协作说明》第 7 节

## 8. 答辩可以讲的

- **MVVM**：界面和逻辑分离，ViewModel 不知道界面长什么样，所以换皮肤不用改逻辑
- **数据绑定**：改了属性界面自动刷新，不用手动 `textBox.Text = ...`
- **接口和假数据**：页面只认 `IApiClient` 接口，开发时用假数据，联调时换成真服务器，页面一行不改
- **图片上传**：上传前压缩到 1280 像素，几 MB 的照片变成一两百 KB
