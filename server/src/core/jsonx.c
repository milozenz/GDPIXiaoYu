#include "core/jsonx.h"

#include <math.h>

bool jx_has(const cJSON *obj, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key) != NULL;
}

const char *jx_str(const cJSON *obj, const char *key) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

bool jx_int(const cJSON *obj, const char *key, int64_t *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(it)) return false;
    double d = it->valuedouble;
    if (d != floor(d) || d > 9007199254740992.0 || d < -9007199254740992.0) return false;  // 必须是整数
    *out = (int64_t)d;
    return true;
}

int64_t jx_int_or(const cJSON *obj, const char *key, int64_t def) {
    int64_t v;
    return jx_int(obj, key, &v) ? v : def;
}

void jx_add_int(cJSON *obj, const char *key, int64_t v) {
    cJSON_AddNumberToObject(obj, key, (double)v);
}

void jx_add_str(cJSON *obj, const char *key, const char *s) {
    cJSON_AddStringToObject(obj, key, s ? s : "");
}

void jx_add_bool(cJSON *obj, const char *key, bool v) {
    cJSON_AddBoolToObject(obj, key, v);
}

void jx_add_col_int(cJSON *obj, const char *key, sqlite3_stmt *stmt, int col) {
    jx_add_int(obj, key, sqlite3_column_int64(stmt, col));
}

void jx_add_col_str(cJSON *obj, const char *key, sqlite3_stmt *stmt, int col) {
    jx_add_str(obj, key, (const char *)sqlite3_column_text(stmt, col));
}
