// cJSON 的小帮手：读写字段少写几行。
#ifndef XIAOYU_JSONX_H
#define XIAOYU_JSONX_H

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "sqlite3.h"

// ---- 读字段 ----
// 字段存在吗（不管是什么类型）
bool jx_has(const cJSON *obj, const char *key);
// 读字符串：字段不存在或不是字符串，返回 NULL
const char *jx_str(const cJSON *obj, const char *key);
// 读整数：字段存在并且是整数才返回 true
bool jx_int(const cJSON *obj, const char *key, int64_t *out);
// 读整数，字段不存在时用默认值 def。字段存在但不是整数，也返回 def（需要严格检查请用 jx_int）
int64_t jx_int_or(const cJSON *obj, const char *key, int64_t def);

// ---- 写字段 ----
void jx_add_int(cJSON *obj, const char *key, int64_t v);
void jx_add_str(cJSON *obj, const char *key, const char *s);   // s 为 NULL 时写空字符串
void jx_add_bool(cJSON *obj, const char *key, bool v);

// ---- 把 SQLite 查询结果的一列写进 JSON ----
// col 是列号（从 0 开始，和 SELECT 后面的顺序一致）
void jx_add_col_int(cJSON *obj, const char *key, sqlite3_stmt *stmt, int col);
void jx_add_col_str(cJSON *obj, const char *key, sqlite3_stmt *stmt, int col);

#endif
