// 组员D：担保订单、状态机与收藏。所有修改在写事务内完成，提交后才推送。
#include "modules/order/order.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "core/db.h"
#include "core/jsonx.h"
#include "core/log.h"
#include "core/push.h"
#include "core/shared.h"
#include "core/util.h"

static bool text_valid(const char *content, size_t minimum, size_t maximum) {
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
        if (++count > maximum) return false;
    }
    return count >= minimum;
}

static int rollback(Ctx *ctx, int code) { db_exec(ctx->db, "ROLLBACK"); return code; }
static int rollback_db(Ctx *ctx) {
    char error[256]; snprintf(error, sizeof error, "%s", sqlite3_errmsg(ctx->db));
    db_exec(ctx->db, "ROLLBACK");
    log_error("订单事务已回滚：%s", error);
    return fail(ctx, ERR_INTERNAL, "数据库操作失败，请稍后重试");
}
static int read_id(Ctx *ctx, const cJSON *req, const char *key, int64_t *id) {
    return jx_int(req, key, id) && *id > 0 ? OK : fail(ctx, ERR_BAD_REQUEST, "%s 须为正整数", key);
}

// 明细与列表共享同一个结果映射。关联资料和封面在本条SQL内读取。
static const char ORDER_SELECT[] =
    "SELECT o.id,o.order_no,o.goods_id,o.goods_title,o.price,o.status,o.ship_note,o.buyer_id,o.seller_id,"
    "o.created_at,o.shipped_at,o.completed_at,o.cancelled_at,"
    "COALESCE(b.nickname,'已注销用户'),COALESCE(b.avatar,''),COALESCE(s.nickname,'已注销用户'),COALESCE(s.avatar,''),"
    "COALESCE((SELECT image FROM goods_images WHERE goods_id=o.goods_id ORDER BY sort LIMIT 1),'') "
    "FROM orders o LEFT JOIN users b ON b.id=o.buyer_id LEFT JOIN users s ON s.id=o.seller_id ";

static void write_order_row(sqlite3_stmt *st, cJSON *out) {
    jx_add_col_int(out, "order_id", st, 0); jx_add_col_str(out, "order_no", st, 1);
    jx_add_col_int(out, "goods_id", st, 2); jx_add_col_str(out, "goods_title", st, 3);
    jx_add_col_int(out, "price", st, 4); jx_add_col_str(out, "status", st, 5); jx_add_col_str(out, "ship_note", st, 6);
    cJSON *buyer = cJSON_AddObjectToObject(out, "buyer");
    jx_add_col_int(buyer, "user_id", st, 7); jx_add_col_str(buyer, "nickname", st, 13); jx_add_col_str(buyer, "avatar", st, 14);
    cJSON *seller = cJSON_AddObjectToObject(out, "seller");
    jx_add_col_int(seller, "user_id", st, 8); jx_add_col_str(seller, "nickname", st, 15); jx_add_col_str(seller, "avatar", st, 16);
    jx_add_col_int(out, "created_at", st, 9); jx_add_col_int(out, "shipped_at", st, 10);
    jx_add_col_int(out, "completed_at", st, 11); jx_add_col_int(out, "cancelled_at", st, 12);
    jx_add_col_str(out, "goods_cover", st, 17);
}

static int write_order(Ctx *ctx, int64_t id, cJSON *out) {
    char sql[1536]; snprintf(sql, sizeof sql, "%s WHERE o.id=?", ORDER_SELECT);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) write_order_row(st, out);
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "订单不存在");
    return rc == SQLITE_ROW ? OK : db_fail(ctx);
}

static void make_order_no(char out[19]) {
    time_t stamp = time(NULL); struct tm local;
#ifdef _WIN32
    localtime_s(&local, &stamp);
#else
    localtime_r(&stamp, &local);
#endif
    char prefix[15], random[9];
    strftime(prefix, sizeof prefix, "%Y%m%d%H%M%S", &local);
    random_hex(random, 4);
    unsigned value = 0;
    for (int i = 0; i < 8; ++i) value = value * 16u + (unsigned)(random[i] <= '9' ? random[i] - '0' : random[i] - 'a' + 10);
    snprintf(out, 19, "%s%04u", prefix, value % 10000u);
}

static int finish_order(Ctx *ctx, int64_t id, int64_t recipient, cJSON *resp) {
    int code = write_order(ctx, id, resp);
    if (code != OK) return rollback(ctx, code);
    if (!db_exec(ctx->db, "COMMIT")) return rollback_db(ctx);
    push_to_user(recipient, "order.updated", resp);
    return OK;
}

typedef struct { int64_t buyer, seller, price, goods; char status[16]; } OrderState;
static int read_order_state(Ctx *ctx, int64_t id, OrderState *o) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT buyer_id,seller_id,price,goods_id,status FROM orders WHERE id=?",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        o->buyer = sqlite3_column_int64(st, 0); o->seller = sqlite3_column_int64(st, 1);
        o->price = sqlite3_column_int64(st, 2); o->goods = sqlite3_column_int64(st, 3);
        snprintf(o->status, sizeof o->status, "%s", (const char *)sqlite3_column_text(st, 4));
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "订单不存在");
    return rc == SQLITE_ROW ? OK : db_fail(ctx);
}

int order_create(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t goods; int code = read_id(ctx, req, "goods_id", &goods);
    if (code != OK) return code;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT seller_id,price,status FROM goods WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, goods);
    int rc = sqlite3_step(st);
    int64_t seller = 0, price = 0; bool on_sale = false;
    if (rc == SQLITE_ROW) {
        seller = sqlite3_column_int64(st, 0); price = sqlite3_column_int64(st, 1);
        on_sale = strcmp((const char *)sqlite3_column_text(st, 2), "on_sale") == 0;
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return rollback(ctx, fail(ctx, ERR_NOT_FOUND, "商品不存在"));
    if (rc != SQLITE_ROW) return rollback_db(ctx);
    if (seller == ctx->user_id) return rollback(ctx, fail(ctx, ERR_FORBIDDEN, "不能购买自己的商品"));
    if (!on_sale) return rollback(ctx, fail(ctx, ERR_CONFLICT, "商品已卖出或已下架"));
    if (sqlite3_prepare_v2(ctx->db, "UPDATE goods SET status='sold',updated_at=? WHERE id=? AND status='on_sale'",
        -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, now_sec()); sqlite3_bind_int64(st, 2, goods);
    rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_db(ctx);
    if (sqlite3_changes(ctx->db) != 1) return rollback(ctx, fail(ctx, ERR_CONFLICT, "商品已卖出或已下架"));
    if (sqlite3_prepare_v2(ctx->db, "UPDATE users SET balance=balance-? WHERE id=? AND balance>=?",
        -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, price); sqlite3_bind_int64(st, 2, ctx->user_id); sqlite3_bind_int64(st, 3, price);
    rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_db(ctx);
    if (sqlite3_changes(ctx->db) != 1) return rollback(ctx, fail(ctx, ERR_NO_MONEY, "余额不足，请先充值"));
    if (sqlite3_prepare_v2(ctx->db,
        "INSERT INTO orders(order_no,goods_id,buyer_id,seller_id,goods_title,price,status,created_at) "
        "SELECT ?,id,?,seller_id,title,price,'paid',? FROM goods WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return rollback_db(ctx);
    // 同秒内的四位随机尾号可能碰撞，只在订单号唯一约束冲突时重试。
    for (int attempt = 0; attempt < 32; ++attempt) {
        char number[19]; make_order_no(number);
        sqlite3_bind_text(st, 1, number, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, ctx->user_id); sqlite3_bind_int64(st, 3, now_sec()); sqlite3_bind_int64(st, 4, goods);
        rc = sqlite3_step(st);
        if (rc == SQLITE_DONE) break;
        bool collision = sqlite3_extended_errcode(ctx->db) == SQLITE_CONSTRAINT_UNIQUE &&
            strstr(sqlite3_errmsg(ctx->db), "orders.order_no") != NULL;
        if (!collision) break;
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_db(ctx);
    return finish_order(ctx, sqlite3_last_insert_rowid(ctx->db), seller, resp);
}

// 0 取消、1 发货、2 确认：权限与状态都在持有写锁时检查。
static int change_order(Ctx *ctx, const cJSON *req, cJSON *resp, int action) {
    int64_t id; int code = read_id(ctx, req, "order_id", &id);
    if (code != OK) return code;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    OrderState o;
    code = read_order_state(ctx, id, &o);
    if (code != OK) return rollback(ctx, code);
    bool allowed = action == 0 ? (ctx->user_id == o.buyer || ctx->user_id == o.seller) :
        ctx->user_id == (action == 1 ? o.seller : o.buyer);
    if (!allowed) return rollback(ctx, fail(ctx, ERR_FORBIDDEN, "你没有操作此订单的权限"));
    const char *from = action == 2 ? "shipped" : "paid";
    if (strcmp(o.status, from) != 0) return rollback(ctx, fail(ctx, ERR_CONFLICT, "订单当前状态不允许此操作"));
    const char *note = jx_str(req, "ship_note");
    if (action == 1 && jx_has(req, "ship_note") && (!text_valid(note, 0, 100)))
        return rollback(ctx, fail(ctx, ERR_BAD_REQUEST, "发货备注最多100个字"));
    const char *sql = action == 0 ? "UPDATE orders SET status='cancelled',cancelled_at=? WHERE id=? AND status='paid'" :
        action == 1 ? "UPDATE orders SET status='shipped',shipped_at=?,ship_note=? WHERE id=? AND status='paid'" :
        "UPDATE orders SET status='completed',completed_at=? WHERE id=? AND status='shipped'";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, now_sec());
    if (action == 1) { sqlite3_bind_text(st, 2, note ? note : "", -1, SQLITE_STATIC); sqlite3_bind_int64(st, 3, id); }
    else sqlite3_bind_int64(st, 2, id);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_db(ctx);
    if (sqlite3_changes(ctx->db) != 1) return rollback(ctx, fail(ctx, ERR_CONFLICT, "订单状态已变化，请刷新"));
    if (action != 1) {
        if (sqlite3_prepare_v2(ctx->db,
            "UPDATE users SET balance=balance+? WHERE id=? AND balance<=9007199254740991-?", -1, &st, NULL) != SQLITE_OK)
            return rollback_db(ctx);
        sqlite3_bind_int64(st, 1, o.price); sqlite3_bind_int64(st, 2, action == 0 ? o.buyer : o.seller);
        sqlite3_bind_int64(st, 3, o.price);
        rc = sqlite3_step(st); sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return rollback_db(ctx);
        if (sqlite3_changes(ctx->db) != 1) return rollback(ctx, fail(ctx, ERR_CONFLICT, "账户余额已达上限，无法入账"));
    }
    if (action == 0) {
        if (sqlite3_prepare_v2(ctx->db, "UPDATE goods SET status='on_sale',updated_at=? WHERE id=?", -1, &st, NULL) != SQLITE_OK)
            return rollback_db(ctx);
        sqlite3_bind_int64(st, 1, now_sec()); sqlite3_bind_int64(st, 2, o.goods);
        rc = sqlite3_step(st); sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return rollback_db(ctx);
    }
    return finish_order(ctx, id, action == 1 ? o.buyer : action == 2 ? o.seller :
        (ctx->user_id == o.buyer ? o.seller : o.buyer), resp);
}
int order_cancel(Ctx *ctx, const cJSON *req, cJSON *resp) { return change_order(ctx, req, resp, 0); }
int order_ship(Ctx *ctx, const cJSON *req, cJSON *resp) { return change_order(ctx, req, resp, 1); }
int order_confirm(Ctx *ctx, const cJSON *req, cJSON *resp) { return change_order(ctx, req, resp, 2); }

int order_detail(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t id; int code = read_id(ctx, req, "order_id", &id); OrderState o;
    if (code != OK) return code;
    code = read_order_state(ctx, id, &o);
    if (code != OK) return code;
    if (ctx->user_id != o.buyer && ctx->user_id != o.seller) return fail(ctx, ERR_FORBIDDEN, "只有买卖双方能查看订单");
    return write_order(ctx, id, resp);
}

int order_list(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *role = jx_str(req, "role"), *status = jx_str(req, "status"); int64_t page, size;
    if (!role || (strcmp(role, "buyer") != 0 && strcmp(role, "seller") != 0)) return fail(ctx, ERR_BAD_REQUEST, "请选择买入或卖出订单");
    if (jx_has(req, "status") && (!status || (strcmp(status, "paid") && strcmp(status, "shipped") &&
        strcmp(status, "completed") && strcmp(status, "cancelled")))) return fail(ctx, ERR_BAD_REQUEST, "订单状态不正确");
    if (!read_paging(req, &page, &size)) return fail(ctx, ERR_BAD_REQUEST, "分页参数不正确");
    const char *column = strcmp(role, "buyer") == 0 ? "buyer_id" : "seller_id";
    char sql[1536]; sqlite3_stmt *st;
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM orders WHERE %s=? AND (? IS NULL OR status=?)", column);
    if (!db_exec(ctx->db, "BEGIN")) return db_fail(ctx);
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_text(st, 2, status, -1, SQLITE_STATIC); sqlite3_bind_text(st, 3, status, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st); int64_t total = rc == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st); if (rc != SQLITE_ROW) return rollback_db(ctx);
    cJSON *items = add_page(resp, total, page, size);
    snprintf(sql, sizeof sql, "%s WHERE o.%s=? AND (? IS NULL OR o.status=?) ORDER BY o.id DESC LIMIT ? OFFSET ?", ORDER_SELECT, column);
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_text(st, 2, status, -1, SQLITE_STATIC); sqlite3_bind_text(st, 3, status, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, size); sqlite3_bind_int64(st, 5, (page - 1) * size);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject(); write_order_row(st, item);
        cJSON_AddItemToArray(items, item);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE || !db_exec(ctx->db, "COMMIT")) return rollback_db(ctx);
    return OK;
}

int fav_add(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp; int64_t id; int code = read_id(ctx, req, "goods_id", &id);
    if (code != OK) return code;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT 1 FROM goods WHERE id=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, id); int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "商品不存在");
    if (rc != SQLITE_ROW) return db_fail(ctx);
    if (sqlite3_prepare_v2(ctx->db, "INSERT OR IGNORE INTO favorites(user_id,goods_id,created_at) VALUES(?,?,?)", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_int64(st, 2, id); sqlite3_bind_int64(st, 3, now_sec());
    rc = sqlite3_step(st); sqlite3_finalize(st); return rc == SQLITE_DONE ? OK : db_fail(ctx);
}
int fav_remove(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp; int64_t id; int code = read_id(ctx, req, "goods_id", &id); if (code != OK) return code;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "DELETE FROM favorites WHERE user_id=? AND goods_id=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_int64(st, 2, id);
    int rc = sqlite3_step(st); sqlite3_finalize(st); return rc == SQLITE_DONE ? OK : db_fail(ctx);
}
int fav_check(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t id; int code = read_id(ctx, req, "goods_id", &id); if (code != OK) return code;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT 1 FROM favorites WHERE user_id=? AND goods_id=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_int64(st, 2, id);
    int rc = sqlite3_step(st); jx_add_bool(resp, "favorite", rc == SQLITE_ROW); sqlite3_finalize(st);
    return rc == SQLITE_ROW || rc == SQLITE_DONE ? OK : db_fail(ctx);
}
int fav_list(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t page, size; if (!read_paging(req, &page, &size)) return fail(ctx, ERR_BAD_REQUEST, "分页参数不正确");
    if (!db_exec(ctx->db, "BEGIN")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT COUNT(*) FROM favorites WHERE user_id=?", -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); int rc = sqlite3_step(st);
    int64_t total = rc == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0; sqlite3_finalize(st);
    if (rc != SQLITE_ROW) return rollback_db(ctx);
    cJSON *items = add_page(resp, total, page, size);
    if (sqlite3_prepare_v2(ctx->db,
        "SELECT g.id,g.title,g.price,g.status,g.view_count,g.created_at,g.seller_id,"
        "COALESCE((SELECT image FROM goods_images WHERE goods_id=g.id ORDER BY sort LIMIT 1),''),"
        "COALESCE(u.nickname,'已注销用户'),COALESCE(u.avatar,'') FROM favorites f JOIN goods g ON g.id=f.goods_id LEFT JOIN users u ON u.id=g.seller_id "
        "WHERE f.user_id=? ORDER BY f.created_at DESC,f.goods_id DESC LIMIT ? OFFSET ?", -1, &st, NULL) != SQLITE_OK) return rollback_db(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id); sqlite3_bind_int64(st, 2, size); sqlite3_bind_int64(st, 3, (page - 1) * size);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        jx_add_col_int(item, "goods_id", st, 0); jx_add_col_str(item, "title", st, 1); jx_add_col_int(item, "price", st, 2);
        jx_add_col_str(item, "status", st, 3); jx_add_col_int(item, "view_count", st, 4); jx_add_col_int(item, "created_at", st, 5);
        jx_add_col_str(item, "cover", st, 7);
        cJSON *seller = cJSON_AddObjectToObject(item, "seller");
        jx_add_col_int(seller, "user_id", st, 6); jx_add_col_str(seller, "nickname", st, 8); jx_add_col_str(seller, "avatar", st, 9);
        cJSON_AddItemToArray(items, item);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE || !db_exec(ctx->db, "COMMIT")) return rollback_db(ctx);
    return OK;
}
