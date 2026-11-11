# 任务说明 · 组员D：服务器的订单模块和聊天模块（C 语言）

## 1. 你负责什么

| 文件 | 内容 |
|---|---|
| `server/src/modules/order/order.c` | 下单、取消、发货、确认收货、订单详情、订单列表（6 个函数）；收藏 4 个函数（**选做**） |
| `server/src/modules/chat/chat.c` | 发私信、聊天记录、会话列表、标记已读（4 个函数，`chat_unread_count` 已经写好当例子） |
| `server/tests/test_order.c`、`test_chat.c` | 测试用例（可以在最后加自己的，**不要删改已有的**） |

**只改这几个文件。** `order.h`、`chat.h` 和 `core/` 下的文件都不要动。

完成标准：`test_order` 和 `test_chat` 的必做用例全部显示 `[通过]`，编译没有警告。收藏的用例标着 `[选做]`，没通过不算失败。

## 2. 先跑起来（第一天就要做）

1. VS 2022 → 文件 → 打开 → 文件夹 → 选 `server` 文件夹。
2. 等底部"输出"窗口显示 CMake 生成完毕（第一次要一两分钟，因为要编译 SQLite）。
3. 顶部工具栏"选择启动项"的下拉框，选 **test_order.exe**。
4. 按 **Ctrl+F5**，会弹出黑窗口：
   ```
   ======== 订单模块测试 ========
   [失败] 下单成功：扣钱、商品变已卖出、推送给卖家
       test_order.c:52  条件不成立：o > 0
   ...
   ```
   全是失败是**正常的**，你的任务就是把它们一个个变成"通过"。
5. 换成 test_chat.exe 再试一次，应该有 1 个通过（未读总数那个例子）。

想调试的话按 F5，可以在代码里打断点。

## 3. 先读这些

1. 《协议文档》第 4、5、7 节，以及 **6.4（订单）、6.5（收藏）、6.6（聊天）**。测试就是按这个写的。
2. 《数据库设计》：orders、messages、favorites 三张表，**第 5 节"防超卖"一定要看懂**。
3. 头文件里的注释：`core/ctx.h`、`core/jsonx.h`、`core/shared.h`、`core/util.h`、`core/push.h`、`core/db.h`。
4. 已经写好的例子：`chat.c` 里的 `chat_unread_count`、`goods.c` 里的 `goods_categories`。
5. `order.c`、`chat.c` 里每个函数上面的步骤提示。

## 4. 每个处理函数长什么样

```c
// req：请求里的 data（已经解析好的 JSON）
// resp：一个空的 JSON 对象，往里面加要返回的字段
// 返回值：成功返回 OK，失败用 fail() 返回错误码
int chat_mark_read(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t peer;
    if (!jx_int(req, "peer_id", &peer)) return fail(ctx, ERR_BAD_REQUEST, "缺少 peer_id");
    // ……UPDATE messages ……
    jx_add_int(resp, "updated", sqlite3_changes(ctx->db));
    return OK;
}
```

当前登录用户是 `ctx->user_id`。需要登录的命令，服务器在调用你之前已经检查过了。

## 5. SQLite 五步走

```c
sqlite3_stmt *st;
// 1. 准备：SQL 里的值一律写成 ?
if (sqlite3_prepare_v2(ctx->db, "SELECT status, price FROM orders WHERE id = ?", -1, &st, NULL) != SQLITE_OK)
    return db_fail(ctx);
// 2. 绑定：第 1 个 ? 换成 order_id（从 1 开始数）
sqlite3_bind_int64(st, 1, order_id);
// 3. 执行：SELECT 每次 step 拿一行（SQLITE_ROW）；INSERT/UPDATE 成功返回 SQLITE_DONE
if (sqlite3_step(st) == SQLITE_ROW) {
    // 4. 取列：列号从 0 开始
    const char *status = (const char *)sqlite3_column_text(st, 0);
    int64_t price = sqlite3_column_int64(st, 1);
}
// 5. 释放：每个 prepare 都要 finalize，提前 return 之前也要！
sqlite3_finalize(st);
```

- `sqlite3_changes(ctx->db)`：刚才的 UPDATE 改了几行 —— **订单模块全靠它判断状态对不对**
- `sqlite3_last_insert_rowid(ctx->db)`：刚 INSERT 的那一行的 id
- `sqlite3_column_text` 返回的指针在下一次 `step` 或 `finalize` 之后就失效了，要留着用得先 `snprintf` 复制一份

> ⚠️ 值一律用 `?` 绑定，**不要**用 `sprintf` 拼进 SQL（SQL 注入）。

## 6. 订单模块：核心是事务

### 订单状态
```
下单 → paid（已付款待发货）──卖家发货──► shipped ──买家确认──► completed
             │                                                    钱这时才打给卖家
             └──买家或卖家取消──► cancelled（退钱，商品重新上架）
```

### `order_create` 是整个项目最关键的函数

两个人同时点"立即购买"，服务器会在两个线程里同时处理。如果先查"在售吗"、再改"已卖出"，两个人可能都查到"在售"，结果都买成功了。这就是**超卖**。

正确做法见 `order.c` 里的注释和《数据库设计》第 5 节：
1. `BEGIN IMMEDIATE`：一开始就拿写锁，第二个人只能排队
2. `UPDATE goods SET status = 'sold' ... WHERE id = ? AND status = 'on_sale'`，**改了 0 行就说明被别人抢了**
3. `UPDATE users SET balance = balance - ? WHERE id = ? AND balance >= ?`，改了 0 行就说明钱不够
4. 插入订单 → `COMMIT`
5. **任何一步出错都要先 `ROLLBACK` 再返回**（测试会检查：钱不够时商品不能被锁住）

建议写一个小函数：
```c
static int rollback(Ctx *ctx, int code) {
    db_exec(ctx->db, "ROLLBACK");
    return code;
}
// 用法：if (n == 0) return rollback(ctx, fail(ctx, ERR_CONFLICT, "商品已卖出或已下架"));
```

### 其他函数

| 函数 | 要点 |
|---|---|
| `order_cancel` | 买家卖家都能取消；只有 `paid` 能取消；退钱 + 商品改回 `on_sale`，也要事务；推送给**另一方** |
| `order_ship` | 只有卖家；`paid → shipped`；备注最多 100 字；推送给买家 |
| `order_confirm` | 只有买家；`shipped → completed`；钱加给卖家（事务）；推送给卖家 |
| `order_detail` | 只有买卖双方能看（403） |
| `order_list` | `role` 必填；最新的在前（`ORDER BY id DESC`） |

建议先写这几个小工具，后面到处都用：
- `static int write_order(Ctx *ctx, int64_t order_id, cJSON *out)`：按 id 查订单，填一个完整的 Order 对象（买家卖家用 `add_user_brief`，封面用 `get_goods_cover`）
- `static void make_order_no(char out[19])`：用 `time` + `localtime` + `strftime("%Y%m%d%H%M%S")`，再加 4 位随机数
- 推送：`push_to_user(对方 id, "order.updated", resp)`，`resp` 就是你刚填好的 Order

## 7. 聊天模块

| 函数 | 要点 |
|---|---|
| `chat_send` | 检查顺序：`to_user_id`（400）→ 内容 1–500 字（400）→ 不能发给自己（400）→ 对方存在吗（`user_exists`，404）→ `goods_id` 传了就要存在（404）；插入后把这条消息查出来写进 `resp`，再 `push_to_user(对方, "chat.new_message", resp)` |
| `chat_history` | 只要我和对方之间的消息（两个方向都要）；`ORDER BY id DESC LIMIT ?` 取最新的 N 条，再**倒过来**放进数组（`cJSON_InsertItemInArray(items, 0, item)` 插到最前面） |
| `chat_conversations` | 每个聊过的人一行；对方 id 可以写 `CASE WHEN from_id = ? THEN to_id ELSE from_id END`，再 `GROUP BY` 它取 `MAX(id)` 就是最后一条消息；未读数只算**对方发给我**的 |
| `chat_mark_read` | 只标对方发给我的；返回 `sqlite3_changes` |

`is_read` 在数据库里是 0/1，返回给客户端要用 `jx_add_bool` 变成 `true/false`。

## 8. 常见的坑

- 事务里 `return` 之前忘了 `ROLLBACK` → 数据库一直锁着，后面的测试全挂。
- `prepare` 之后提前 `return`，忘了 `finalize`。
- `jx_str` 字段不存在返回 `NULL`，先判断再用。
- 字数用 `utf8_len`，不是 `strlen`。
- 测试里 `t_push_count` 不对：检查推送是不是推给了对的人、是不是推了不止一次。

## 9. 答辩可以讲的

- **防超卖**：事务 + 条件更新 + 部分唯一索引，两道保险。现场开两个客户端同时点购买演示
- **担保交易**：钱先从买家扣掉，确认收货才给卖家，取消就原路退回
- **订单状态机**：每个状态只能往特定状态走，非法操作返回 409
- **实时推送**：服务器主动把新消息、订单变化推给在线用户；不在线的话，消息也存在数据库里，上线后能拉到
