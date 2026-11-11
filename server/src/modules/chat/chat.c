// 组员D：双向聊天、历史分页、会话聚合与已读状态。
#include "modules/chat/chat.h"
#include <stdio.h>
#include <string.h>
#include "core/db.h"
#include "core/jsonx.h"
#include "core/push.h"
#include "core/shared.h"
#include "core/util.h"
#include "modules/safety/safety.h"

#define MASKED_TEXT "该消息因违规已被屏蔽"

// flag：none 正常 / warn 命中二级词（接收方要显示提醒）/ masked 已屏蔽；flag_kind：命中的类别
static const char *flag_name(int mod_state) { return mod_state == 2 ? "masked" : mod_state == 1 ? "warn" : "none"; }

// 列顺序：id,from_id,to_id,goods_id,content,is_read,created_at,mod_state,mod_kind
static void write_message(sqlite3_stmt *st, int start, cJSON *out) {
    int mod_state = sqlite3_column_int(st, start + 7);
    jx_add_col_int(out, "message_id", st, start); jx_add_col_int(out, "from_id", st, start + 1);
    jx_add_col_int(out, "to_id", st, start + 2); jx_add_col_int(out, "goods_id", st, start + 3);
    if (mod_state == 2) jx_add_str(out, "content", MASKED_TEXT);  // 原文还在库里，只是不给客户端
    else jx_add_col_str(out, "content", st, start + 4);
    jx_add_bool(out, "is_read", sqlite3_column_int(st, start + 5) != 0);
    jx_add_col_int(out, "created_at", st, start + 6);
    jx_add_str(out, "flag", flag_name(mod_state)); jx_add_col_str(out, "flag_kind", st, start + 8);
}
static int peer_id(Ctx *ctx, const cJSON *req, int64_t *id) {
    return jx_int(req, "peer_id", id) && *id > 0 ? OK : fail(ctx, ERR_BAD_REQUEST, "对方用户编号须为正整数");
}

// 按RFC 3629检查UTF-8并计数，中文和emoji均按一个Unicode标量处理。
static bool message_valid(const char *content) {
    if (!content) return false;
    const unsigned char *p = (const unsigned char *)content;
    size_t count = 0;
    while (*p) {
        if (*p < 0x80) p++;
        else if (*p >= 0xC2 && *p <= 0xDF) {
            if (p[1] < 0x80 || p[1] > 0xBF) return false;
            p += 2;
        } else if (*p >= 0xE0 && *p <= 0xEF) {
            unsigned low = *p == 0xE0 ? 0xA0 : 0x80;
            unsigned high = *p == 0xED ? 0x9F : 0xBF;
            if (p[1] < low || p[1] > high || p[2] < 0x80 || p[2] > 0xBF) return false;
            p += 3;
        } else if (*p >= 0xF0 && *p <= 0xF4) {
            unsigned low = *p == 0xF0 ? 0x90 : 0x80;
            unsigned high = *p == 0xF4 ? 0x8F : 0xBF;
            if (p[1] < low || p[1] > high || p[2] < 0x80 || p[2] > 0xBF ||
                p[3] < 0x80 || p[3] > 0xBF) return false;
            p += 4;
        } else return false;
        if (++count > 500) return false;
    }
    return count > 0;
}

// 保留原有未读总数示例。
int chat_unread_count(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)req;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT COUNT(*) FROM messages WHERE to_id = ? AND is_read = 0",
                           -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return db_fail(ctx);
    }
    jx_add_col_int(resp, "count", st, 0);
    sqlite3_finalize(st);
    return OK;
}

int chat_send(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t to, goods = 0; const char *content = jx_str(req, "content");
    if (!jx_int(req, "to_user_id", &to) || to < 1) return fail(ctx, ERR_BAD_REQUEST, "收信人编号须为正整数");
    if (!message_valid(content)) return fail(ctx, ERR_BAD_REQUEST, "消息须为1–500个有效UTF-8字符");
    if (to == ctx->user_id) return fail(ctx, ERR_BAD_REQUEST, "不能给自己发私信");
    // 内容安全：先看有没有被禁言，再查敏感词（一级词直接拦，二级词照常发但打上标记）
    int mod_state; const char *mod_kind;
    int code = safety_check_muted(ctx); if (code != OK) return code;
    code = safety_check_chat(ctx, content, &mod_state, &mod_kind); if (code != OK) return code;
    // 区分“没有此用户”和查询本身失败，避免数据库故障被误报成404。
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT 1 FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, to);
    int recipient_rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (recipient_rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "对方用户不存在");
    if (recipient_rc != SQLITE_ROW) return db_fail(ctx);
    if (jx_has(req, "goods_id") && (!jx_int(req, "goods_id", &goods) || goods < 0)) return fail(ctx, ERR_BAD_REQUEST, "商品编号须为非负整数");
    if (goods > 0) {
        if (sqlite3_prepare_v2(ctx->db, "SELECT 1 FROM goods WHERE id=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
        sqlite3_bind_int64(st, 1, goods); int rc = sqlite3_step(st); sqlite3_finalize(st);
        if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "商品不存在");
        if (rc != SQLITE_ROW) return db_fail(ctx);
    }
    if (sqlite3_prepare_v2(ctx->db, "INSERT INTO messages(from_id,to_id,goods_id,content,created_at,mod_state,mod_kind) VALUES(?,?,?,?,?,?,?)", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_int64(st, 2, to); sqlite3_bind_int64(st, 3, goods);
    int64_t created = now_sec();
    sqlite3_bind_text(st, 4, content, -1, SQLITE_STATIC); sqlite3_bind_int64(st, 5, created);
    sqlite3_bind_int(st, 6, mod_state); sqlite3_bind_text(st, 7, mod_kind, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st); sqlite3_finalize(st); if (rc != SQLITE_DONE) return db_fail(ctx);
    int64_t id = sqlite3_last_insert_rowid(ctx->db);
    // INSERT已原子提交。直接构造同一份回复与推送，避免写入后再读失败。
    jx_add_int(resp, "message_id", id); jx_add_int(resp, "from_id", ctx->user_id);
    jx_add_int(resp, "to_id", to); jx_add_int(resp, "goods_id", goods);
    jx_add_str(resp, "content", content); jx_add_bool(resp, "is_read", false);
    jx_add_int(resp, "created_at", created);
    jx_add_str(resp, "flag", flag_name(mod_state)); jx_add_str(resp, "flag_kind", mod_kind);
    // 命中二级词：交给后台 AI 判断，AI 有把握判违规会自动屏蔽并禁言
    if (mod_state == 1 && safety_ai_enabled()) safety_enqueue(ctx->db, "chat", id);
    push_to_user(to, "chat.new_message", resp);
    return OK;
}

int chat_history(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t peer, before = 9007199254740992LL, limit = 20;
    int code = peer_id(ctx, req, &peer); if (code != OK) return code;
    if (jx_has(req, "before_id") && (!jx_int(req, "before_id", &before) || before < 1)) return fail(ctx, ERR_BAD_REQUEST, "历史游标须为正整数");
    if (jx_has(req, "limit") && (!jx_int(req, "limit", &limit) || limit < 1 || limit > 50)) return fail(ctx, ERR_BAD_REQUEST, "每次读取1–50条消息");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "SELECT id,from_id,to_id,goods_id,content,is_read,created_at,mod_state,mod_kind FROM ("
        "SELECT id,from_id,to_id,goods_id,content,is_read,created_at,mod_state,mod_kind FROM messages WHERE from_id=? AND to_id=? AND id<? "
        "UNION ALL "
        "SELECT id,from_id,to_id,goods_id,content,is_read,created_at,mod_state,mod_kind FROM messages WHERE from_id=? AND to_id=? AND id<? AND from_id<>to_id"
        ") ORDER BY id DESC LIMIT ?",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_int64(st, 2, peer); sqlite3_bind_int64(st, 3, before);
    sqlite3_bind_int64(st, 4, peer); sqlite3_bind_int64(st, 5, ctx->user_id); sqlite3_bind_int64(st, 6, before);
    sqlite3_bind_int64(st, 7, limit);
    cJSON *items = cJSON_AddArrayToObject(resp, "items"); int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject(); write_message(st, 0, item); cJSON_InsertItemInArray(items, 0, item);
    }
    sqlite3_finalize(st); return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

int chat_conversations(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)req; sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "WITH latest AS (SELECT CASE WHEN from_id=? THEN to_id ELSE from_id END AS peer,MAX(id) AS last_id,"
        "SUM(CASE WHEN to_id=? AND is_read=0 THEN 1 ELSE 0 END) AS unread "
        "FROM messages WHERE from_id=? OR to_id=? GROUP BY peer) "
        "SELECT l.peer,m.id,m.from_id,m.to_id,m.goods_id,m.content,m.is_read,m.created_at,m.mod_state,m.mod_kind,"
        "l.unread,COALESCE(u.nickname,'已注销用户'),COALESCE(u.avatar,'') "
        "FROM latest l JOIN messages m ON m.id=l.last_id LEFT JOIN users u ON u.id=l.peer ORDER BY m.id DESC",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    for (int i = 1; i <= 4; ++i) sqlite3_bind_int64(st, i, ctx->user_id);
    cJSON *items = cJSON_AddArrayToObject(resp, "items"); int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        cJSON *peer = cJSON_AddObjectToObject(item, "peer");
        jx_add_col_int(peer, "user_id", st, 0); jx_add_col_str(peer, "nickname", st, 11); jx_add_col_str(peer, "avatar", st, 12);
        cJSON *message = cJSON_AddObjectToObject(item, "last_message"); write_message(st, 1, message);
        jx_add_col_int(item, "unread", st, 10); cJSON_AddItemToArray(items, item);
    }
    sqlite3_finalize(st); return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

int chat_mark_read(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t peer; int code = peer_id(ctx, req, &peer); if (code != OK) return code;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "UPDATE messages SET is_read=1 WHERE from_id=? AND to_id=? AND is_read=0", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, peer); sqlite3_bind_int64(st, 2, ctx->user_id);
    int rc = sqlite3_step(st); sqlite3_finalize(st); if (rc != SQLITE_DONE) return db_fail(ctx);
    jx_add_int(resp, "updated", sqlite3_changes(ctx->db)); return OK;
}
