// 极简测试工具。每个测试程序都用它：不用启动服务器，直接调用处理函数，检查返回结果。
//
// 写法示例：
//   static void test_login_ok(void) {
//       t_begin("登录成功");
//       t_user("alice", "123456", 0);
//       CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice','password':'123456'}"));
//       CHECK(t_len_str("token") == 64);
//       CHECK_STR("alice", t_str("user.username"));
//   }
//
// JSON 里可以用单引号代替双引号（会自动替换），这样不用写一堆 \"。
#ifndef XIAOYU_T_H
#define XIAOYU_T_H

#include <stdbool.h>
#include <stdint.h>

#include "core/ctx.h"

// ---- 流程 ----
void t_init(const char *suite_name);   // main() 开头调用一次
void t_begin(const char *case_name);   // 每个用例开头调用：会换一个全新的空数据库
void t_begin_optional(const char *case_name);  // 选做功能的用例：没通过不算失败
int  t_done(void);                     // main() 最后：return t_done();

// ---- 调用处理函数 ----
// as_user：以哪个用户的身份调用（0 = 没登录）。返回错误码。
int t_call(Handler h, int64_t as_user, const char *json);
// 同上，额外带 token（只有 user.logout 需要）
int t_call_token(Handler h, int64_t as_user, const char *token, const char *json);
const char *t_msg(void);  // 上一次调用的提示文字

// ---- 读上一次返回的 data ----
// path 用点分隔，数组用数字下标，例如 "user.user_id"、"items.0.title"
bool        t_exists(const char *path);
int64_t     t_int(const char *path);       // 不存在返回 -999999
const char *t_str(const char *path);       // 不存在返回 NULL
bool        t_bool(const char *path);
int         t_len(const char *path);       // 数组长度，不是数组返回 -1
int         t_len_str(const char *path);   // 字符串字节数，不是字符串返回 -1
// 上一次返回的 data 整个打印出来（调试用）
void t_dump(void);

// ---- 造数据 ----
// 当前用例的数据库连接（需要直接调 SQLite 函数时用）
sqlite3 *t_db(void);
// 直接往数据库插一个用户（密码按协议规定的方式加盐哈希），返回 user_id
int64_t t_user(const char *username, const char *password, int64_t balance);
// 直接插一个在售商品（分类 1，成色 2，没有图片），返回 goods_id
int64_t t_goods(int64_t seller_id, const char *title, int64_t price);
// 生成一张假图片（真的写到 test_data/images 里），返回图片名
const char *t_image(void);
// 执行任意 SQL（写法和 printf 一样）
void t_sql(const char *fmt, ...);
// 查一个整数，例如 t_sql_int("SELECT balance FROM users WHERE id = %lld", (long long)uid)
int64_t t_sql_int(const char *fmt, ...);
// 把 unit 重复 n 次，例如 t_rep("字", 31) 得到 31 个"字"（结果在临时缓冲区里，马上用掉）
const char *t_rep(const char *unit, int n);

// ---- 推送记录（每个用例开头清零） ----
extern int     t_push_count;       // 一共推送了几次
extern int64_t t_push_user;        // 最后一次推给谁
extern char    t_push_event[64];   // 最后一次的事件名
// 最后一次推送的 data 里的字段，路径写法同 t_int
int64_t     t_push_int(const char *path);
const char *t_push_str(const char *path);

// ---- 断言 ----
#define CHECK(cond)              t_check(!!(cond), #cond, __FILE__, __LINE__)
#define CHECK_CODE(expect, call) t_check_code((expect), (call), #call, __FILE__, __LINE__)
#define CHECK_INT(expect, got)   t_check_int((int64_t)(expect), (int64_t)(got), #got, __FILE__, __LINE__)
#define CHECK_STR(expect, got)   t_check_str((expect), (got), #got, __FILE__, __LINE__)

void t_check(bool ok, const char *expr, const char *file, int line);
void t_check_code(int expect, int got, const char *expr, const char *file, int line);
void t_check_int(int64_t expect, int64_t got, const char *expr, const char *file, int line);
void t_check_str(const char *expect, const char *got, const char *expr, const char *file, int line);

#endif
