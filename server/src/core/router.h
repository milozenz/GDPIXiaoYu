// 命令分发：根据 cmd（比如 "user.login"）找到对应的处理函数，并检查登录。
#ifndef XIAOYU_ROUTER_H
#define XIAOYU_ROUTER_H

#include <stdint.h>

#include "core/ctx.h"

// 查 token 对应的用户；token 不存在或过期返回 0
int64_t session_user(sqlite3 *db, const char *token);

// 处理一个请求：查 token → 找命令 → 检查登录 → 调用处理函数。
// ctx->db 和 ctx->token 要先设好；返回错误码，提示在 ctx->msg，结果在 resp。
int router_dispatch(Ctx *ctx, const char *cmd, const cJSON *req, cJSON *resp);

#endif
