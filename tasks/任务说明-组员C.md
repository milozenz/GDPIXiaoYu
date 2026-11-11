# 任务说明 · 组员C：服务器的用户模块和商品模块（C 语言）

## 1. 你负责什么

| 文件 | 内容 |
|---|---|
| `server/src/modules/user/user.c` | 注册、登录、退出、个人信息、修改资料、别人的主页、充值（7 个函数） |
| `server/src/modules/goods/goods.c` | 发布、修改、上下架、详情、搜索、我发布的（7 个函数，`goods_categories` 已经写好当例子） |
| `server/tests/test_user.c`、`test_goods.c` | 测试用例（可以在最后加自己的，**不要删改已有的**） |

**只改这几个文件。** `user.h`、`goods.h` 和 `core/` 下的文件都不要动。

完成标准：`test_user` 和 `test_goods` 全部显示 `[通过]`，编译没有警告。

## 2. 先跑起来（第一天就要做）

1. VS 2022 → 文件 → 打开 → 文件夹 → 选 `server` 文件夹。
2. 等底部"输出"窗口显示 CMake 生成完毕（第一次要一两分钟，因为要编译 SQLite）。
3. 顶部工具栏有个"选择启动项"的下拉框，选 **test_user.exe**。
4. 按 **Ctrl+F5**（开始执行，不调试），会弹出黑窗口，看到类似：
   ```
   ======== 用户模块测试 ========
   [失败] 注册成功
       test_user.c:16  期望错误码 0，实际 501（提示：user.register 还没实现）
   ...
   ======== 用户模块测试：通过 0 个，失败 14 个 ========
   ```
   全是失败是**正常的**，因为函数还没写。你的任务就是把它们一个个变成"通过"。
5. 换成 test_goods.exe 再试一次，应该有 1 个通过（分类列表那个例子）。

想调试的话按 F5，可以在代码里打断点。

## 3. 先读这些

1. 《协议文档》第 4、5 节，以及 **6.2（用户）、6.3（商品）**：每个命令收什么、返回什么、什么情况报什么错。**测试就是按这个写的。**
2. 《数据库设计》：users、sessions、goods、goods_images 四张表。
3. 头文件里的注释：`core/ctx.h`（`fail`、`db_fail`、错误码）、`core/jsonx.h`（读写 JSON）、`core/shared.h`（公共查询）、`core/util.h`（`utf8_len`、`random_hex`、`sha256_hex`）、`core/files.h`（`image_exists`）。
4. 已经写好的例子：`goods.c` 里的 `goods_categories`、`chat.c` 里的 `chat_unread_count`。
5. `user.c`、`goods.c` 里每个函数上面的步骤提示。

## 4. 每个处理函数长什么样

```c
// req：请求里的 data（已经解析好的 JSON）
// resp：一个空的 JSON 对象，往里面加要返回的字段
// 返回值：成功返回 OK，失败用 fail() 返回错误码
int user_recharge(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t amount;
    if (!jx_int(req, "amount", &amount) || amount < 1 || amount > 1000000)
        return fail(ctx, ERR_BAD_REQUEST, "单次充值金额要在 0.01 到 10000 元之间");

    // ……改数据库……

    jx_add_int(resp, "balance", 新余额);
    return OK;
}
```

- 当前登录用户是 `ctx->user_id`。需要登录的命令，服务器在调用你之前已经检查过了，不用你再查。
- `fail()` 的提示文字会直接显示给用户看，写得像人话。

## 5. SQLite 五步走

所有数据库操作都是这个套路：

```c
sqlite3_stmt *st;
// 1. 准备：SQL 里的值一律写成 ?
if (sqlite3_prepare_v2(ctx->db, "SELECT nickname, balance FROM users WHERE id = ?", -1, &st, NULL) != SQLITE_OK)
    return db_fail(ctx);
// 2. 绑定：第 1 个 ? 换成 user_id（从 1 开始数）
sqlite3_bind_int64(st, 1, ctx->user_id);
// 3. 执行：SELECT 每次 step 拿一行，返回 SQLITE_ROW；INSERT/UPDATE/DELETE 成功返回 SQLITE_DONE
if (sqlite3_step(st) == SQLITE_ROW) {
    // 4. 取列：列号从 0 开始，和 SELECT 后面的顺序一致
    jx_add_col_str(resp, "nickname", st, 0);
    jx_add_col_int(resp, "balance", st, 1);
}
// 5. 释放：每个 prepare 都必须有一个 finalize，提前 return 之前也要！
sqlite3_finalize(st);
```

其他常用的：
- `sqlite3_bind_text(st, 2, 字符串, -1, SQLITE_STATIC)`：绑定字符串
- `sqlite3_last_insert_rowid(ctx->db)`：刚 INSERT 的那一行的 id
- `sqlite3_changes(ctx->db)`：刚才的 UPDATE/DELETE 改了几行
- `db_exec(ctx->db, "BEGIN")` / `"COMMIT"` / `"ROLLBACK"`：事务

> ⚠️ **绝对不要**用 `sprintf` 把用户输入拼进 SQL，比如 `"... WHERE username = '%s'"`。用户名里只要有个单引号就能改掉你的 SQL，这叫 **SQL 注入**。值一律用 `?` 绑定。

## 6. 每个函数的要点

### 用户模块

| 函数 | 要点 |
|---|---|
| `user_register` | 用户名只能是字母、数字、下划线（用 `isalnum`）；字数用 `utf8_len`（一个汉字算 1 个字）；**密码哈希必须是 `sha256_hex(盐 + 密码)`，盐在前**，否则测试里 `t_user` 造的用户就登录不上 |
| `user_login` | 用户不存在和密码错误，**错误码和提示要完全一样**（401 "用户名或密码错误"）；token 用 `random_hex(token, 32)` 生成 64 位；有效期 7 天 |
| `user_logout` | 删掉 `ctx->token` 对应的 session |
| `user_me` | **不能**返回 `password_hash` 和 `salt` |
| `user_update_profile` | 先把传来的字段全部检查完，再一起改（不能改了昵称才发现简介太长）；头像要用 `image_exists` 检查；空字符串表示清除头像 |
| `user_get` | 不返回 `username` 和 `balance`（别人的余额不能看）；`on_sale_count`、`sold_count` 可以用子查询 `(SELECT COUNT(*) FROM goods WHERE ...)` |
| `user_recharge` | 金额必须是**整数**（`jx_int` 会帮你检查，1.5 不是整数） |

建议先写一个 `static int write_user_info(Ctx *ctx, int64_t user_id, cJSON *out)` 填 UserInfo，`login`、`me`、`update_profile` 都能用。

### 商品模块

| 函数 | 要点 |
|---|---|
| `goods_publish` | 字段检查最多，建议写一个 `publish` 和 `update` 共用的检查函数；`category_id` 要查 `categories` 表看存不存在；`condition` 没传默认 2；商品和图片放在一个事务里插入 |
| `goods_update` | 顺序：商品存在吗（404）→ 是自己的吗（403）→ 已卖出了吗（409）→ 检查字段（400）；**没传的字段不改**；传了 `images` 就先删光旧图再插新图 |
| `goods_off_shelf` / `on_shelf` | `UPDATE ... WHERE id = ? AND status = 'on_sale'`，再看 `sqlite3_changes` 是不是 0 |
| `goods_detail` | **先**把浏览量 +1，**再**查出来返回（第一次看到的是 1）；卖家信息用 `add_user_brief`；`category_name` 用 JOIN 查 |
| `goods_search` | 最难的一个，见下面 |
| `goods_mine` | 和 search 很像，可以共用代码 |

**`goods_search` 的思路**：条件都是可选的，所以 SQL 要根据传了哪些参数拼起来。

```c
char sql[1024] = "SELECT ... FROM goods g WHERE g.status = 'on_sale'";
if (有 keyword)     strcat(sql, " AND (instr(g.title, ?) > 0 OR instr(g.description, ?) > 0)");
if (有 category_id) strcat(sql, " AND g.category_id = ?");
// ……然后按同样的顺序 bind，一个都不能错位
```

- 拼进 SQL 的只能是你自己写死的字符串和 `?`，用户输入的值只能 bind。
- 关键词用 `instr()` 而不是 `LIKE`，这样用户搜 `%` 时只当普通字符（测试会检查）。
- 先 `SELECT COUNT(*)` 算 `total`，再 `ORDER BY ... LIMIT ? OFFSET ?` 取这一页，`OFFSET = (page - 1) * page_size`。
- 排序要加"新的在前"作为第二排序条件：`ORDER BY g.price ASC, g.created_at DESC, g.id DESC`。
- 分页参数用 `read_paging()`，外壳用 `add_page()`，封面用 `get_goods_cover()`。

## 7. 常见的坑

- `prepare` 之后提前 `return`，忘了 `finalize` → 内存泄漏。
- 事务里出错，忘了 `ROLLBACK` 就 `return` → 后面的请求全都卡住。
- `jx_str` 字段不存在时返回 `NULL`，直接 `strlen(NULL)` 程序就崩了。先判断！
- 汉字在 UTF-8 里占 3 个字节，**字数**要用 `utf8_len`，不能用 `strlen`。
- 测试里看到 `期望错误码 400，实际 0`：说明你漏了某个参数检查。去协议文档对一下规则。

## 8. 答辩可以讲的

- **密码安全**：为什么加盐，为什么登录失败的提示要一模一样
- **SQL 注入**：为什么所有值都用 `?` 绑定，搜索时为什么用 `instr` 不用 `LIKE`
- **分页**：`COUNT` + `LIMIT/OFFSET`；索引让查询变快（见《数据库设计》第 4 节）
- **测试驱动**：先有测试再写代码，一共多少个用例，全部通过
