// 商品模块：字段检查、权限、事务、图片替换和分页查询。
#include "modules/goods/goods.h"
#include <stdio.h>
#include <string.h>
#include "core/db.h"
#include "core/files.h"
#include "core/jsonx.h"
#include "core/shared.h"
#include "core/util.h"
#include "modules/safety/safety.h"

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

typedef struct {
    const char *title, *description;
    const cJSON *images;
    int64_t price, original_price, category_id, condition;
} Fields;

static int check_fields(Ctx *ctx, const cJSON *req, bool publish, Fields *f) {
    memset(f, 0, sizeof *f);
    f->title = jx_str(req, "title");
    f->description = jx_str(req, "description");
    f->condition = 2;
    if ((publish || jx_has(req, "title")) && (!text_valid(f->title, 1, 30)))
        return fail(ctx, ERR_BAD_REQUEST, "标题须为 1–30 个字");
    if (jx_has(req, "description") && (!text_valid(f->description, 0, 500)))
        return fail(ctx, ERR_BAD_REQUEST, "描述最多 500 个字");
    if ((publish || jx_has(req, "price")) && (!jx_int(req, "price", &f->price) || f->price < 1 || f->price > 99999900))
        return fail(ctx, ERR_BAD_REQUEST, "售价须为 0.01–999999 元");
    if (jx_has(req, "original_price") && (!jx_int(req, "original_price", &f->original_price) ||
        f->original_price < 0 || f->original_price > 99999900)) return fail(ctx, ERR_BAD_REQUEST, "原价须为 0–999999 元");
    if (jx_has(req, "condition") && (!jx_int(req, "condition", &f->condition) || f->condition < 0 || f->condition > 3))
        return fail(ctx, ERR_BAD_REQUEST, "成色须为 0–3");
    if (publish || jx_has(req, "category_id")) {
        if (!jx_int(req, "category_id", &f->category_id) || f->category_id < 1)
            return fail(ctx, ERR_BAD_REQUEST, "请选择有效分类");
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(ctx->db, "SELECT 1 FROM categories WHERE id=?", -1, &st, NULL) != SQLITE_OK)
            return db_fail(ctx);
        sqlite3_bind_int64(st, 1, f->category_id);
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc == SQLITE_DONE) return fail(ctx, ERR_BAD_REQUEST, "分类不存在");
        if (rc != SQLITE_ROW) return db_fail(ctx);
    }
    f->images = cJSON_GetObjectItemCaseSensitive(req, "images");
    if (f->images) {
        if (!cJSON_IsArray(f->images) || cJSON_GetArraySize(f->images) > 9)
            return fail(ctx, ERR_BAD_REQUEST, "图片须为数组，最多 9 张");
        const cJSON *img;
        cJSON_ArrayForEach(img, f->images)
            if (!cJSON_IsString(img) || !image_exists(img->valuestring))
                return fail(ctx, ERR_BAD_REQUEST, "商品图片须先上传");
    }
    return OK;
}

static int save_images(Ctx *ctx, int64_t id, const cJSON *images) {
    if (!images) return OK;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "INSERT INTO goods_images(goods_id,sort,image) VALUES(?,?,?)",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    const cJSON *img;
    int index = 0, rc = SQLITE_DONE;
    cJSON_ArrayForEach(img, images) {
        sqlite3_bind_int64(st, 1, id);
        sqlite3_bind_int(st, 2, index++);
        sqlite3_bind_text(st, 3, img->valuestring, -1, SQLITE_STATIC);
        rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) break;
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

static int rollback_error(Ctx *ctx) {
    db_exec(ctx->db, "ROLLBACK");
    return db_fail(ctx);
}

static int read_id(Ctx *ctx, const cJSON *req, int64_t *id) {
    if (!jx_int(req, "goods_id", id) || *id < 1) return fail(ctx, ERR_BAD_REQUEST, "商品编号须为正整数");
    return OK;
}

// 写事务中检查，避免检查后商品状态被另一个连接改变。
static int check_owner(Ctx *ctx, int64_t id, const char *required_status) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT seller_id,status FROM goods WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st), result;
    if (rc == SQLITE_DONE) result = fail(ctx, ERR_NOT_FOUND, "商品不存在");
    else if (rc != SQLITE_ROW) result = db_fail(ctx);
    else if (sqlite3_column_int64(st, 0) != ctx->user_id) result = fail(ctx, ERR_FORBIDDEN, "只能操作自己的商品");
    else {
        const char *status = (const char *)sqlite3_column_text(st, 1);
        bool conflict = required_status ? strcmp(status, required_status) != 0 : strcmp(status, "sold") == 0;
        result = conflict ? fail(ctx, ERR_CONFLICT, "商品当前状态不允许此操作") : OK;
    }
    sqlite3_finalize(st);
    return result;
}

// 列表SQL已带封面和卖家资料，不再为每件商品重复prepare查询。
static void write_goods_brief(sqlite3_stmt *st, cJSON *out, int cover_column) {
    jx_add_col_int(out, "goods_id", st, 0); jx_add_col_str(out, "title", st, 1);
    jx_add_col_int(out, "price", st, 2); jx_add_col_str(out, "status", st, 3);
    jx_add_col_int(out, "view_count", st, 4); jx_add_col_int(out, "created_at", st, 5);
    jx_add_col_str(out, "cover", st, cover_column);
    cJSON *seller = cJSON_AddObjectToObject(out, "seller");
    jx_add_col_int(seller, "user_id", st, 6); jx_add_col_str(seller, "nickname", st, cover_column + 1); jx_add_col_str(seller, "avatar", st, cover_column + 2);
}

// 保留分类列表示例的接口，并完整检查查询执行结果。
int goods_categories(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)req;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT id, name FROM categories ORDER BY id", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);

    cJSON *items = cJSON_AddArrayToObject(resp, "items");
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        jx_add_col_int(item, "category_id", st, 0);
        jx_add_col_str(item, "name", st, 1);
        cJSON_AddItemToArray(items, item);
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

int goods_publish(Ctx *ctx, const cJSON *req, cJSON *resp) {
    Fields f;
    int result = check_fields(ctx, req, true, &f);
    if (result != OK) return result;
    // 内容安全：一级禁售词直接拦；开了 AI 审核的话先进"审核中"，后台审完再上架
    result = safety_check_goods(ctx, f.title, f.description);
    if (result != OK) return result;
    bool review = safety_ai_enabled();
    const char *status = review ? "reviewing" : "on_sale";
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "INSERT INTO goods(seller_id,title,description,price,original_price,category_id,condition,created_at,updated_at,status)"
        " VALUES(?,?,?,?,?,?,?,?,?,?)", -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id);
    sqlite3_bind_text(st, 2, f.title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, f.description ? f.description : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, f.price);
    sqlite3_bind_int64(st, 5, f.original_price);
    sqlite3_bind_int64(st, 6, f.category_id);
    sqlite3_bind_int64(st, 7, f.condition);
    int64_t now = now_sec();
    sqlite3_bind_int64(st, 8, now);
    sqlite3_bind_int64(st, 9, now);
    sqlite3_bind_text(st, 10, status, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_error(ctx);
    int64_t id = sqlite3_last_insert_rowid(ctx->db);
    if (save_images(ctx, id, f.images) != OK) return rollback_error(ctx);
    if (review && !safety_enqueue(ctx->db, "goods", id)) return rollback_error(ctx);
    if (!db_exec(ctx->db, "COMMIT")) return rollback_error(ctx);
    jx_add_int(resp, "goods_id", id);
    jx_add_str(resp, "status", status);
    return OK;
}

int goods_update(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t id;
    int result = read_id(ctx, req, &id);
    if (result != OK) return result;
    // 敏感词放在事务外面查：被拦下的记录要留底，不能跟着事务一起回滚
    result = safety_check_goods(ctx, jx_str(req, "title"), jx_str(req, "description"));
    if (result != OK) return result;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    result = check_owner(ctx, id, NULL);
    Fields f;
    if (result == OK) result = check_fields(ctx, req, false, &f);
    if (result != OK) { db_exec(ctx->db, "ROLLBACK"); return result; }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "UPDATE goods SET title=COALESCE(?,title),description=COALESCE(?,description),"
        "price=COALESCE(?,price),original_price=COALESCE(?,original_price),"
        "category_id=COALESCE(?,category_id),condition=COALESCE(?,condition),updated_at=? WHERE id=?",
        -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
    sqlite3_bind_text(st, 1, f.title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, f.description, -1, SQLITE_STATIC);
    if (jx_has(req, "price")) sqlite3_bind_int64(st, 3, f.price);
    if (jx_has(req, "original_price")) sqlite3_bind_int64(st, 4, f.original_price);
    if (jx_has(req, "category_id")) sqlite3_bind_int64(st, 5, f.category_id);
    if (jx_has(req, "condition")) sqlite3_bind_int64(st, 6, f.condition);
    sqlite3_bind_int64(st, 7, now_sec());
    sqlite3_bind_int64(st, 8, id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_error(ctx);
    if (f.images) {
        if (sqlite3_prepare_v2(ctx->db, "DELETE FROM goods_images WHERE goods_id=?", -1, &st, NULL) != SQLITE_OK)
            return rollback_error(ctx);
        sqlite3_bind_int64(st, 1, id);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE || save_images(ctx, id, f.images) != OK) return rollback_error(ctx);
    }
    // 改了内容就要重新审核：在售/被驳回/审核中的变成"审核中"（没开 AI 就直接在售），下架的保持下架但也审一遍
    bool changed = f.title || f.description || f.images || jx_has(req, "category_id");
    if (changed) {
        bool review = safety_ai_enabled();
        if (sqlite3_prepare_v2(ctx->db,
            "UPDATE goods SET status=CASE WHEN status IN ('on_sale','rejected','reviewing') THEN ? ELSE status END,"
            "reject_reason='' WHERE id=?", -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
        sqlite3_bind_text(st, 1, review ? "reviewing" : "on_sale", -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 2, id);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE || (review && !safety_enqueue(ctx->db, "goods", id))) return rollback_error(ctx);
    }
    if (sqlite3_prepare_v2(ctx->db, "SELECT status FROM goods WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return rollback_error(ctx);
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) jx_add_col_str(resp, "status", st, 0);
    sqlite3_finalize(st);
    return db_exec(ctx->db, "COMMIT") ? OK : rollback_error(ctx);
}

static int change_shelf(Ctx *ctx, const cJSON *req, const char *from, const char *to) {
    int64_t id;
    int result = read_id(ctx, req, &id);
    if (result != OK) return result;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    result = check_owner(ctx, id, from);
    if (result != OK) { db_exec(ctx->db, "ROLLBACK"); return result; }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "UPDATE goods SET status=?,updated_at=? WHERE id=? AND status=?",
        -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
    sqlite3_bind_text(st, 1, to, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, now_sec());
    sqlite3_bind_int64(st, 3, id);
    sqlite3_bind_text(st, 4, from, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_error(ctx);
    if (sqlite3_changes(ctx->db) == 0) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_CONFLICT, "商品状态已变化，请刷新");
    }
    return db_exec(ctx->db, "COMMIT") ? OK : rollback_error(ctx);
}

int goods_off_shelf(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp;
    return change_shelf(ctx, req, "on_sale", "off_shelf");
}

int goods_on_shelf(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp;
    return change_shelf(ctx, req, "off_shelf", "on_sale");
}

int goods_detail(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t id;
    int result = read_id(ctx, req, &id);
    if (result != OK) return result;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "UPDATE goods SET view_count=view_count+1 WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return rollback_error(ctx);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rollback_error(ctx);
    if (sqlite3_changes(ctx->db) == 0) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_NOT_FOUND, "商品不存在");
    }
    if (sqlite3_prepare_v2(ctx->db,
        "SELECT g.id,g.title,g.price,g.status,g.view_count,g.created_at,g.seller_id,"
        "g.description,g.original_price,g.category_id,c.name,g.condition,g.updated_at,"
        "COALESCE((SELECT image FROM goods_images WHERE goods_id=g.id ORDER BY sort LIMIT 1),''),"
        "COALESCE(u.nickname,'已注销用户'),COALESCE(u.avatar,''),g.reject_reason "
        "FROM goods g JOIN categories c ON c.id=g.category_id LEFT JOIN users u ON u.id=g.seller_id WHERE g.id=?", -1, &st, NULL) != SQLITE_OK)
        return rollback_error(ctx);
    sqlite3_bind_int64(st, 1, id);
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        write_goods_brief(st, resp, 13);
        jx_add_col_str(resp, "description", st, 7);
        jx_add_col_int(resp, "original_price", st, 8);
        jx_add_col_int(resp, "category_id", st, 9);
        jx_add_col_str(resp, "category_name", st, 10);
        jx_add_col_int(resp, "condition", st, 11);
        jx_add_col_int(resp, "updated_at", st, 12);
        jx_add_col_str(resp, "reject_reason", st, 16);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_ROW) return rollback_error(ctx);
    if (sqlite3_prepare_v2(ctx->db, "SELECT image FROM goods_images WHERE goods_id=? ORDER BY sort",
        -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
    sqlite3_bind_int64(st, 1, id);
    cJSON *images = cJSON_AddArrayToObject(resp, "images");
    while ((rc = sqlite3_step(st)) == SQLITE_ROW)
        cJSON_AddItemToArray(images, cJSON_CreateString((const char *)sqlite3_column_text(st, 0)));
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE || !db_exec(ctx->db, "COMMIT")) return rollback_error(ctx);
    return OK;
}

typedef struct {
    const char *keyword, *status, *order;
    bool category, seller, min, max;
    int64_t category_id, seller_id, min_price, max_price, page, size;
} Filter;

static void bind_filter(sqlite3_stmt *st, const Filter *f) {
    int n = 1;
    if (f->status) sqlite3_bind_text(st, n++, f->status, -1, SQLITE_STATIC);
    if (f->keyword && *f->keyword) {
        sqlite3_bind_text(st, n++, f->keyword, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, n++, f->keyword, -1, SQLITE_STATIC);
    }
    if (f->category) sqlite3_bind_int64(st, n++, f->category_id);
    if (f->seller) sqlite3_bind_int64(st, n++, f->seller_id);
    if (f->min) sqlite3_bind_int64(st, n++, f->min_price);
    if (f->max) sqlite3_bind_int64(st, n++, f->max_price);
}

static int list_goods(Ctx *ctx, const Filter *f, cJSON *resp) {
    char where[512] = " WHERE 1=1";
    // 拼接内容只有常量；用户输入全部绑定。
    if (f->status) strcat(where, " AND g.status=?");
    if (f->keyword && *f->keyword) strcat(where, " AND (instr(g.title,?)>0 OR instr(g.description,?)>0)");
    if (f->category) strcat(where, " AND g.category_id=?");
    if (f->seller) strcat(where, " AND g.seller_id=?");
    if (f->min) strcat(where, " AND g.price>=?");
    if (f->max) strcat(where, " AND g.price<=?");
    char sql[1024];
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM goods g%s", where);
    // 总数和当前页共用一个快照。
    if (!db_exec(ctx->db, "BEGIN")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
    bind_filter(st, f);
    int rc = sqlite3_step(st);
    int64_t total = rc == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (rc != SQLITE_ROW) return rollback_error(ctx);
    cJSON *items = add_page(resp, total, f->page, f->size);
    snprintf(sql, sizeof sql,
        "SELECT g.id,g.title,g.price,g.status,g.view_count,g.created_at,g.seller_id,"
        "COALESCE((SELECT image FROM goods_images WHERE goods_id=g.id ORDER BY sort LIMIT 1),''),"
        "COALESCE(u.nickname,'已注销用户'),COALESCE(u.avatar,''),g.reject_reason FROM goods g LEFT JOIN users u ON u.id=g.seller_id%s ORDER BY %s LIMIT ? OFFSET ?",
        where, f->order);
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return rollback_error(ctx);
    bind_filter(st, f);
    int n = sqlite3_bind_parameter_count(st);
    sqlite3_bind_int64(st, n - 1, f->size);
    sqlite3_bind_int64(st, n, (f->page - 1) * f->size);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        write_goods_brief(st, item, 7);
        jx_add_col_str(item, "reject_reason", st, 10);
        cJSON_AddItemToArray(items, item);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE || !db_exec(ctx->db, "COMMIT")) return rollback_error(ctx);
    return OK;
}

int goods_search(Ctx *ctx, const cJSON *req, cJSON *resp) {
    Filter f = {0};
    f.status = "on_sale";
    f.order = "g.created_at DESC,g.id DESC";
    if (!read_paging(req, &f.page, &f.size)) return fail(ctx, ERR_BAD_REQUEST, "分页参数不正确");
    f.keyword = jx_str(req, "keyword");
    if (jx_has(req, "keyword") && (!text_valid(f.keyword, 0, 30))) return fail(ctx, ERR_BAD_REQUEST, "关键词最多 30 个字");
    f.category = jx_has(req, "category_id");
    f.seller = jx_has(req, "seller_id");
    f.min = jx_has(req, "min_price");
    f.max = jx_has(req, "max_price");
    if ((f.category && (!jx_int(req, "category_id", &f.category_id) || f.category_id < 1)) ||
        (f.seller && (!jx_int(req, "seller_id", &f.seller_id) || f.seller_id < 1)))
        return fail(ctx, ERR_BAD_REQUEST, "分类和卖家编号须为正整数");
    if ((f.min && (!jx_int(req, "min_price", &f.min_price) || f.min_price < 0 || f.min_price > 99999900)) ||
        (f.max && (!jx_int(req, "max_price", &f.max_price) || f.max_price < 0 || f.max_price > 99999900)) ||
        (f.min && f.max && f.min_price > f.max_price)) return fail(ctx, ERR_BAD_REQUEST, "价格区间不正确");
    const char *sort = jx_str(req, "sort");
    if (jx_has(req, "sort")) {
        if (!sort) return fail(ctx, ERR_BAD_REQUEST, "排序参数须为字符串");
        if (strcmp(sort, "price_asc") == 0) f.order = "g.price ASC,g.created_at DESC,g.id DESC";
        else if (strcmp(sort, "price_desc") == 0) f.order = "g.price DESC,g.created_at DESC,g.id DESC";
        else if (strcmp(sort, "hot") == 0) f.order = "g.view_count DESC,g.created_at DESC,g.id DESC";
        else if (strcmp(sort, "newest") != 0) return fail(ctx, ERR_BAD_REQUEST, "不支持的排序方式");
    }
    return list_goods(ctx, &f, resp);
}

int goods_mine(Ctx *ctx, const cJSON *req, cJSON *resp) {
    Filter f = {0};
    f.seller = true;
    f.seller_id = ctx->user_id;
    f.order = "g.created_at DESC,g.id DESC";
    if (!read_paging(req, &f.page, &f.size)) return fail(ctx, ERR_BAD_REQUEST, "分页参数不正确");
    f.status = jx_str(req, "status");
    if (jx_has(req, "status") && (!f.status || (strcmp(f.status, "on_sale") != 0 &&
        strcmp(f.status, "sold") != 0 && strcmp(f.status, "off_shelf") != 0 &&
        strcmp(f.status, "reviewing") != 0 && strcmp(f.status, "rejected") != 0)))
        return fail(ctx, ERR_BAD_REQUEST, "商品状态不正确");
    return list_goods(ctx, &f, resp);
}
