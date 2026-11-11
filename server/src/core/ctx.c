#include "core/ctx.h"

#include <stdarg.h>
#include <stdio.h>

#include "core/log.h"

int fail(Ctx *ctx, int code, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->msg, sizeof ctx->msg, fmt, ap);
    va_end(ap);
    return code;
}

int db_fail(Ctx *ctx) {
    log_error("数据库错误: %s", sqlite3_errmsg(ctx->db));
    snprintf(ctx->msg, sizeof ctx->msg, "服务器内部错误");
    return ERR_INTERNAL;
}
