#include "core/shared.h"

#include <stdio.h>

#include "core/jsonx.h"

bool user_exists(sqlite3 *db, int64_t user_id) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM users WHERE id = ?", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, user_id);
    bool found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return found;
}

bool add_user_brief(sqlite3 *db, cJSON *obj, const char *key, int64_t user_id) {
    cJSON *u = cJSON_AddObjectToObject(obj, key);
    jx_add_int(u, "user_id", user_id);

    sqlite3_stmt *st;
    bool found = false;
    if (sqlite3_prepare_v2(db, "SELECT nickname, avatar FROM users WHERE id = ?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, user_id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            jx_add_col_str(u, "nickname", st, 0);
            jx_add_col_str(u, "avatar", st, 1);
            found = true;
        }
        sqlite3_finalize(st);
    }
    if (!found) {
        jx_add_str(u, "nickname", "已注销用户");
        jx_add_str(u, "avatar", "");
    }
    return found;
}

void get_goods_cover(sqlite3 *db, int64_t goods_id, char *out, size_t out_size) {
    out[0] = '\0';
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT image FROM goods_images WHERE goods_id = ? ORDER BY sort LIMIT 1",
                           -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_int64(st, 1, goods_id);
    if (sqlite3_step(st) == SQLITE_ROW)
        snprintf(out, out_size, "%s", (const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
}

bool read_paging(const cJSON *req, int64_t *page, int64_t *page_size) {
    *page = 1;
    *page_size = 20;
    if (jx_has(req, "page") && (!jx_int(req, "page", page) || *page < 1)) return false;
    if (jx_has(req, "page_size") && (!jx_int(req, "page_size", page_size) || *page_size < 1 || *page_size > 50))
        return false;
    return true;
}

cJSON *add_page(cJSON *resp, int64_t total, int64_t page, int64_t page_size) {
    jx_add_int(resp, "total", total);
    jx_add_int(resp, "page", page);
    jx_add_int(resp, "page_size", page_size);
    return cJSON_AddArrayToObject(resp, "items");
}
