#include "t.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/db.h"
#include "core/files.h"
#include "core/log.h"
#include "core/platform.h"
#include "core/push.h"
#include "core/util.h"

#define TEST_DATA_DIR "test_data"

static sqlite3 *g_db;
static cJSON   *g_resp;
static char     g_msg[256];

static const char *g_suite;
static const char *g_case;
static bool        g_case_optional;
static int         g_case_failures;
static int         g_passed, g_failed, g_opt_passed, g_opt_failed;

int     t_push_count;
int64_t t_push_user;
char    t_push_event[64];
static cJSON *g_push_data;

// ---------------- 流程 ----------------

static void finish_case(void) {
    if (!g_case) return;
    if (g_case_failures == 0) {
        printf("[通过] %s\n", g_case);
        if (g_case_optional) g_opt_passed++; else g_passed++;
    } else {
        printf("[失败] %s\n", g_case);
        if (g_case_optional) g_opt_failed++; else g_failed++;
    }
    g_case = NULL;
}

static void on_push(int64_t user_id, const char *event, const cJSON *data) {
    t_push_count++;
    t_push_user = user_id;
    snprintf(t_push_event, sizeof t_push_event, "%s", event ? event : "");
    cJSON_Delete(g_push_data);
    g_push_data = data ? cJSON_Duplicate(data, 1) : NULL;
}

void t_init(const char *suite_name) {
    console_utf8();
    setvbuf(stdout, NULL, _IONBF, 0);
    g_suite = suite_name;
    files_set_dir(TEST_DATA_DIR);
    push_set_hook(on_push);
    printf("======== %s ========\n", suite_name);
}

static void begin(const char *case_name, bool optional) {
    finish_case();
    g_case = case_name;
    g_case_optional = optional;
    g_case_failures = 0;

    if (g_db) db_close(g_db);
    g_db = db_open(":memory:");
    if (!g_db || !db_init_schema(g_db)) {
        printf("!! 测试数据库建不起来，检查 db.c\n");
        exit(2);
    }
    cJSON_Delete(g_resp);
    g_resp = NULL;
    g_msg[0] = '\0';

    t_push_count = 0;
    t_push_user = 0;
    t_push_event[0] = '\0';
    cJSON_Delete(g_push_data);
    g_push_data = NULL;
}

void t_begin(const char *case_name) { begin(case_name, false); }
void t_begin_optional(const char *case_name) { begin(case_name, true); }

int t_done(void) {
    finish_case();
    printf("======== %s：通过 %d 个，失败 %d 个", g_suite, g_passed, g_failed);
    if (g_opt_passed + g_opt_failed) printf("；选做通过 %d 个，未通过 %d 个", g_opt_passed, g_opt_failed);
    printf(" ========\n");
    if (g_db) db_close(g_db);
    g_db = NULL;
    return g_failed == 0 ? 0 : 1;
}

// ---------------- 调用 ----------------

int t_call_token(Handler h, int64_t as_user, const char *token, const char *json) {
    char *text = (char *)malloc(strlen(json) + 1);
    strcpy(text, json);
    for (char *p = text; *p; p++)
        if (*p == '\'') *p = '"';
    cJSON *req = cJSON_Parse(text);
    if (!cJSON_IsObject(req)) {
        printf("!! 测试代码里的 JSON 写错了：%s\n", json);
        exit(2);
    }
    free(text);

    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.db = g_db;
    ctx.user_id = as_user;
    ctx.token = token;

    cJSON_Delete(g_resp);
    g_resp = cJSON_CreateObject();
    int code = h(&ctx, req, g_resp);
    snprintf(g_msg, sizeof g_msg, "%s", ctx.msg);
    cJSON_Delete(req);
    return code;
}

int t_call(Handler h, int64_t as_user, const char *json) { return t_call_token(h, as_user, NULL, json); }

const char *t_msg(void) { return g_msg; }

// ---------------- 读结果 ----------------

static const cJSON *lookup(const cJSON *root, const char *path) {
    if (!root || !path) return NULL;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", path);
    const cJSON *cur = root;
    for (char *seg = strtok(buf, "."); seg && cur; seg = strtok(NULL, ".")) {
        if (cJSON_IsArray(cur)) cur = cJSON_GetArrayItem(cur, atoi(seg));
        else cur = cJSON_GetObjectItemCaseSensitive(cur, seg);
    }
    return cur;
}

bool t_exists(const char *path) { return lookup(g_resp, path) != NULL; }

static int64_t to_int(const cJSON *it) { return cJSON_IsNumber(it) ? (int64_t)it->valuedouble : -999999; }
static const char *to_str(const cJSON *it) { return cJSON_IsString(it) ? it->valuestring : NULL; }

int64_t t_int(const char *path) { return to_int(lookup(g_resp, path)); }
const char *t_str(const char *path) { return to_str(lookup(g_resp, path)); }
bool t_bool(const char *path) { return cJSON_IsTrue(lookup(g_resp, path)); }

int t_len(const char *path) {
    const cJSON *it = lookup(g_resp, path);
    return cJSON_IsArray(it) ? cJSON_GetArraySize(it) : -1;
}

int t_len_str(const char *path) {
    const char *s = t_str(path);
    return s ? (int)strlen(s) : -1;
}

void t_dump(void) {
    char *s = g_resp ? cJSON_Print(g_resp) : NULL;
    printf("    返回的 data = %s\n", s ? s : "(空)");
    cJSON_free(s);
}

int64_t t_push_int(const char *path) { return to_int(lookup(g_push_data, path)); }
const char *t_push_str(const char *path) { return to_str(lookup(g_push_data, path)); }

// ---------------- 造数据 ----------------

sqlite3 *t_db(void) { return g_db; }

static void vsql(const char *fmt, va_list ap, sqlite3_stmt **out) {
    char sql[4096];
    vsnprintf(sql, sizeof sql, fmt, ap);
    if (sqlite3_prepare_v2(g_db, sql, -1, out, NULL) != SQLITE_OK) {
        printf("!! 测试里的 SQL 有错：%s\n   %s\n", sql, sqlite3_errmsg(g_db));
        exit(2);
    }
}

void t_sql(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sqlite3_stmt *st;
    vsql(fmt, ap, &st);
    va_end(ap);
    while (sqlite3_step(st) == SQLITE_ROW) {}
    sqlite3_finalize(st);
}

int64_t t_sql_int(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sqlite3_stmt *st;
    vsql(fmt, ap, &st);
    va_end(ap);
    int64_t v = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : -999999;
    sqlite3_finalize(st);
    return v;
}

int64_t t_user(const char *username, const char *password, int64_t balance) {
    char salt[33], hash[65], salted[256];
    random_hex(salt, 16);
    snprintf(salted, sizeof salted, "%s%s", salt, password);
    sha256_hex(salted, strlen(salted), hash);

    sqlite3_stmt *st;
    sqlite3_prepare_v2(g_db,
                       "INSERT INTO users (username, password_hash, salt, nickname, balance, created_at)"
                       " VALUES (?, ?, ?, ?, ?, ?)", -1, &st, NULL);
    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, salt, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, username, -1, SQLITE_TRANSIENT);  // 昵称默认和用户名一样
    sqlite3_bind_int64(st, 5, balance);
    sqlite3_bind_int64(st, 6, now_sec());
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(g_db);
}

int64_t t_goods(int64_t seller_id, const char *title, int64_t price) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(g_db,
                       "INSERT INTO goods (seller_id, title, price, category_id, created_at, updated_at)"
                       " VALUES (?, ?, ?, 1, ?, ?)", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, seller_id);
    sqlite3_bind_text(st, 2, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, price);
    sqlite3_bind_int64(st, 4, now_sec());
    sqlite3_bind_int64(st, 5, now_sec());
    if (sqlite3_step(st) != SQLITE_DONE) {
        printf("!! t_goods 插入失败：%s\n", sqlite3_errmsg(g_db));
        exit(2);
    }
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(g_db);
}

const char *t_image(void) {
    static char names[16][64];
    static int next;
    char *name = names[next++ % 16];
    random_hex(name, 16);
    strcat(name, ".jpg");
    char path[256];
    snprintf(path, sizeof path, TEST_DATA_DIR "/images/%s", name);
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs("fake image", f);
        fclose(f);
    }
    return name;
}

const char *t_rep(const char *unit, int n) {
    static char bufs[4][8192];
    static int next;
    char *buf = bufs[next++ % 4];
    buf[0] = '\0';
    size_t len = 0, ulen = strlen(unit);
    for (int i = 0; i < n && len + ulen < sizeof bufs[0]; i++) {
        memcpy(buf + len, unit, ulen);
        len += ulen;
    }
    buf[len] = '\0';
    return buf;
}

// ---------------- 断言 ----------------

static void report(const char *file, int line) {
    const char *base = strrchr(file, '/');
    const char *base2 = strrchr(file, '\\');
    if (base2 > base) base = base2;
    g_case_failures++;
    printf("    %s:%d  ", base ? base + 1 : file, line);
}

void t_check(bool ok, const char *expr, const char *file, int line) {
    if (ok) return;
    report(file, line);
    printf("条件不成立：%s\n", expr);
}

void t_check_code(int expect, int got, const char *expr, const char *file, int line) {
    if (expect == got) return;
    report(file, line);
    printf("期望错误码 %d，实际 %d（提示：%s）\n        调用：%s\n", expect, got, g_msg[0] ? g_msg : "无", expr);
}

void t_check_int(int64_t expect, int64_t got, const char *expr, const char *file, int line) {
    if (expect == got) return;
    report(file, line);
    printf("%s 期望 %lld，实际 %lld\n", expr, (long long)expect, (long long)got);
}

void t_check_str(const char *expect, const char *got, const char *expr, const char *file, int line) {
    if (expect && got && strcmp(expect, got) == 0) return;
    report(file, line);
    printf("%s 期望 \"%s\"，实际 %s%s%s\n", expr, expect ? expect : "(NULL)",
           got ? "\"" : "", got ? got : "(NULL)", got ? "\"" : "");
}
