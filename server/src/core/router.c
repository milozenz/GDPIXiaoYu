#include "core/router.h"

#include <stdbool.h>
#include <string.h>

#include "core/files.h"
#include "core/jsonx.h"
#include "core/util.h"
#include "modules/chat/chat.h"
#include "modules/goods/goods.h"
#include "modules/order/order.h"
#include "modules/safety/safety.h"
#include "modules/user/user.h"

static int sys_ping(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)ctx;
    (void)req;
    jx_add_int(resp, "server_time", now_sec());
    return OK;
}

typedef struct {
    const char *cmd;
    Handler     fn;
    bool        need_login;
    bool        need_admin;  // 只有管理员能用（审核后台）
} Route;

// 所有命令都在这张表里，和《协议文档》第 6 节一一对应
static const Route ROUTES[] = {
    {"sys.ping",            sys_ping,            false, false},
    {"file.upload",         file_upload,         true, false},
    {"file.get",            file_get,            false, false},

    {"user.register",       user_register,       false, false},
    {"user.login",          user_login,          false, false},
    {"user.logout",         user_logout,         true, false},
    {"user.me",             user_me,             true, false},
    {"user.update_profile", user_update_profile, true, false},
    {"user.get",            user_get,            false, false},
    {"user.recharge",       user_recharge,       true, false},

    {"goods.categories",    goods_categories,    false, false},
    {"goods.publish",       goods_publish,       true, false},
    {"goods.update",        goods_update,        true, false},
    {"goods.off_shelf",     goods_off_shelf,     true, false},
    {"goods.on_shelf",      goods_on_shelf,      true, false},
    {"goods.detail",        goods_detail,        false, false},
    {"goods.search",        goods_search,        false, false},
    {"goods.mine",          goods_mine,          true, false},

    {"order.create",        order_create,        true, false},
    {"order.cancel",        order_cancel,        true, false},
    {"order.ship",          order_ship,          true, false},
    {"order.confirm",       order_confirm,       true, false},
    {"order.detail",        order_detail,        true, false},
    {"order.list",          order_list,          true, false},

    {"fav.add",             fav_add,             true, false},
    {"fav.remove",          fav_remove,          true, false},
    {"fav.check",           fav_check,           true, false},
    {"fav.list",            fav_list,            true, false},

    {"chat.send",           chat_send,           true, false},
    {"chat.history",        chat_history,        true, false},
    {"chat.conversations",  chat_conversations,  true, false},
    {"chat.mark_read",      chat_mark_read,      true, false},
    {"chat.unread_count",   chat_unread_count,   true, false},

    {"user.send_code",      user_send_code,      false, false},
    {"user.change_password", user_change_password, true, false},
    {"user.reset_password", user_reset_password, false, false},
    {"report.create",       report_create,       true, false},
    {"appeal.create",       appeal_create,       true, false},

    {"admin.stats",         admin_stats,         true, true},
    {"admin.cases",         admin_cases,         true, true},
    {"admin.case_detail",   admin_case_detail,   true, true},
    {"admin.resolve",       admin_resolve,       true, true},
    {"admin.mute",          admin_mute,          true, true},
    {"admin.unmute",        admin_unmute,        true, true},
    {"admin.users",         admin_users,         true, true},
    {"admin.goods",         admin_goods,         true, true},
    {"admin.blocked",       admin_blocked,       true, true},
};

static bool is_admin(sqlite3 *db, int64_t uid) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT is_admin FROM users WHERE id = ?", -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, uid);
    bool admin = sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) != 0;
    sqlite3_finalize(st);
    return admin;
}

int64_t session_user(sqlite3 *db, const char *token) {
    if (!token || !token[0]) return 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT user_id FROM sessions WHERE token = ? AND expires_at > ?",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, now_sec());
    int64_t uid = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return uid;
}

int router_dispatch(Ctx *ctx, const char *cmd, const cJSON *req, cJSON *resp) {
    ctx->user_id = session_user(ctx->db, ctx->token);

    const Route *route = NULL;
    for (size_t i = 0; i < sizeof ROUTES / sizeof ROUTES[0]; i++)
        if (cmd && strcmp(ROUTES[i].cmd, cmd) == 0) {
            route = &ROUTES[i];
            break;
        }
    if (!route) return fail(ctx, ERR_BAD_REQUEST, "未知命令 %s", cmd ? cmd : "(空)");
    if (route->need_login && ctx->user_id == 0) return fail(ctx, ERR_NOT_LOGIN, "请先登录");
    if (route->need_admin && !is_admin(ctx->db, ctx->user_id)) return fail(ctx, ERR_FORBIDDEN, "需要管理员权限");

    return route->fn(ctx, req, resp);
}
