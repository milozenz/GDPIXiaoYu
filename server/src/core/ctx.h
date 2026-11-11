// 请求上下文：每个处理函数都会拿到一个 Ctx，里面有数据库连接和当前登录的用户。
#ifndef XIAOYU_CTX_H
#define XIAOYU_CTX_H

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "sqlite3.h"

// 错误码，和《协议文档》第 4 节一一对应
enum {
    OK              = 0,
    ERR_BAD_REQUEST = 400,  // 参数错误
    ERR_NOT_LOGIN   = 401,  // 未登录或登录已过期
    ERR_NO_MONEY    = 402,  // 余额不足
    ERR_FORBIDDEN   = 403,  // 没有权限
    ERR_NOT_FOUND   = 404,  // 不存在
    ERR_CONFLICT    = 409,  // 状态冲突
    ERR_TOO_LARGE   = 413,  // 数据太大
    ERR_INTERNAL    = 500,  // 服务器内部错误
    ERR_NOT_IMPL    = 501,  // 还没实现
};

typedef struct Ctx {
    sqlite3    *db;         // 数据库连接（每个客户端连接各有一个）
    int64_t     user_id;    // 当前登录用户的 id；0 表示没登录
    const char *token;      // 这次请求带的 token；没带是 NULL
    char        msg[256];   // 出错时返回给客户端的提示
} Ctx;

// 处理函数的统一格式：
//   req  —— 请求里的 data 对象（一定不是 NULL，没传参数时是空对象 {}）
//   resp —— 一个空对象，把要返回的字段加进去
//   返回值 —— 成功返回 OK，失败返回错误码（并用 fail() 写好提示）
typedef int (*Handler)(Ctx *ctx, const cJSON *req, cJSON *resp);

// 写好错误提示并返回错误码，写法和 printf 一样：
//   return fail(ctx, ERR_BAD_REQUEST, "标题最多 %d 个字", 30);
int fail(Ctx *ctx, int code, const char *fmt, ...);

// 数据库出错时用：把 SQLite 的报错记进日志，返回 ERR_INTERNAL。
//   if (sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return db_fail(ctx); }
// 注意：如果在事务里，先 ROLLBACK 再调用它。
int db_fail(Ctx *ctx);

#endif
