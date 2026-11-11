#include "core/server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/db.h"
#include "core/jsonx.h"
#include "core/log.h"
#include "core/platform.h"
#include "core/push.h"
#include "core/router.h"

#define MAX_CONNS 1000

// 一个客户端连接
typedef struct Conn {
    sock_t       sock;
    int64_t      user_id;    // 这个连接登录的用户，推送靠它找人（读写都要持有 g_lock）
    mutex_t      send_lock;  // 回复和推送可能在不同线程同时发，发之前要加锁，否则两条消息会搅在一起
    char         addr[64];
    struct Conn *next;
} Conn;

static const Config *g_cfg;
static bool          g_started;
static mutex_t       g_lock;    // 保护连接链表 g_conns 和每个连接的 user_id
static Conn         *g_conns;
static int           g_count;
static PushHook      g_hook;

// ---------------- 连接表 ----------------

static void conns_add(Conn *c) {
    mutex_lock(&g_lock);
    c->next = g_conns;
    g_conns = c;
    g_count++;
    mutex_unlock(&g_lock);
}

static int conns_remove(Conn *c) {
    mutex_lock(&g_lock);
    for (Conn **p = &g_conns; *p; p = &(*p)->next)
        if (*p == c) {
            *p = c->next;
            g_count--;
            break;
        }
    int left = g_count;
    mutex_unlock(&g_lock);
    return left;
}

static void conn_bind_user(Conn *c, int64_t user_id) {
    mutex_lock(&g_lock);
    c->user_id = user_id;
    mutex_unlock(&g_lock);
}

// ---------------- 收发一帧 ----------------

// 帧 = 4 字节大端长度 + JSON 正文
static bool send_frame(Conn *c, const char *json) {
    size_t len = strlen(json);
    unsigned char head[4] = {
        (unsigned char)(len >> 24), (unsigned char)(len >> 16),
        (unsigned char)(len >> 8),  (unsigned char)len,
    };
    mutex_lock(&c->send_lock);
    bool ok = net_send_all(c->sock, head, 4) && net_send_all(c->sock, json, len);
    mutex_unlock(&c->send_lock);
    return ok;
}

// ---------------- 推送 ----------------

void push_set_hook(PushHook hook) { g_hook = hook; }

void push_to_user(int64_t user_id, const char *event, const cJSON *data) {
    if (g_hook) g_hook(user_id, event, data);
    if (!g_started || user_id <= 0) return;

    cJSON *msg = cJSON_CreateObject();
    jx_add_str(msg, "push", event);
    cJSON_AddItemToObject(msg, "data", data ? cJSON_Duplicate(data, 1) : cJSON_CreateObject());
    char *text = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!text) return;

    int sent = 0;
    mutex_lock(&g_lock);
    for (Conn *c = g_conns; c; c = c->next)
        if (c->user_id == user_id && send_frame(c, text)) sent++;
    mutex_unlock(&g_lock);
    cJSON_free(text);

    if (sent) log_info("推送 %s → 用户 %lld（%d 个连接）", event, (long long)user_id, sent);
}

// ---------------- 处理一个请求 ----------------

static void handle_request(Conn *c, sqlite3 *db, const char *text) {
    int64_t start = now_ms();
    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.db = db;

    cJSON *req = cJSON_Parse(text);
    cJSON *data_out = cJSON_CreateObject();
    int64_t id = 0;
    const char *cmd = NULL;
    int code;

    if (!cJSON_IsObject(req)) {
        code = fail(&ctx, ERR_BAD_REQUEST, "消息不是合法的 JSON 对象");
    } else {
        id = jx_int_or(req, "id", 0);
        cmd = jx_str(req, "cmd");
        ctx.token = jx_str(req, "token");
        const cJSON *data_in = cJSON_GetObjectItemCaseSensitive(req, "data");
        cJSON *empty = NULL;
        if (!cJSON_IsObject(data_in)) data_in = empty = cJSON_CreateObject();
        code = router_dispatch(&ctx, cmd, data_in, data_out);
        cJSON_Delete(empty);

        // 记下这个连接是谁的，推送时才找得到人
        if (code == OK && cmd && strcmp(cmd, "user.login") == 0)
            conn_bind_user(c, jx_int_or(cJSON_GetObjectItemCaseSensitive(data_out, "user"), "user_id", 0));
        else if (code == OK && cmd && strcmp(cmd, "user.logout") == 0)
            conn_bind_user(c, 0);
        else if (ctx.token)
            conn_bind_user(c, ctx.user_id);
    }

    if (code != OK) {  // 失败时 data 一律是空对象
        cJSON_Delete(data_out);
        data_out = cJSON_CreateObject();
    }
    cJSON *resp = cJSON_CreateObject();
    jx_add_int(resp, "id", id);
    jx_add_int(resp, "code", code);
    jx_add_str(resp, "msg", code == OK ? "ok" : (ctx.msg[0] ? ctx.msg : "操作失败"));
    cJSON_AddItemToObject(resp, "data", data_out);

    char *out = cJSON_PrintUnformatted(resp);
    if (out) send_frame(c, out);
    cJSON_free(out);
    cJSON_Delete(resp);

    if (!cmd || strcmp(cmd, "sys.ping") != 0) {
        long long ms = (long long)(now_ms() - start);
        if (code == OK)
            log_info("%s 用户%lld %s 成功 %lldms", c->addr, (long long)ctx.user_id, cmd ? cmd : "?", ms);
        else
            log_info("%s 用户%lld %s 失败 %d %s %lldms", c->addr, (long long)ctx.user_id, cmd ? cmd : "?",
                     code, ctx.msg, ms);
    }
    cJSON_Delete(req);
}

// ---------------- 每个连接一个线程 ----------------

static void conn_main(void *arg) {
    Conn *c = (Conn *)arg;
    size_t max_len = (size_t)g_cfg->max_frame_mb * 1024 * 1024;

    sqlite3 *db = db_open(g_cfg->db_path);  // 每个线程用自己的数据库连接
    if (db) {
        for (;;) {
            unsigned char head[4];
            if (!net_recv_all(c->sock, head, 4)) break;  // 对方断开或太久没说话
            size_t len = (size_t)head[0] << 24 | (size_t)head[1] << 16 | (size_t)head[2] << 8 | head[3];
            if (len == 0) continue;
            if (len > max_len) {
                log_warn("%s 发来的消息太大（%zu 字节），断开", c->addr, len);
                break;
            }
            char *buf = (char *)malloc(len + 1);
            if (!buf || !net_recv_all(c->sock, buf, len)) {
                free(buf);
                break;
            }
            buf[len] = '\0';
            handle_request(c, db, buf);
            free(buf);
        }
        db_close(db);
    }

    int left = conns_remove(c);
    log_info("连接断开 %s（在线 %d）", c->addr, left);
    net_close(c->sock);
    mutex_destroy(&c->send_lock);
    free(c);
}

int server_run(const Config *cfg) {
    g_cfg = cfg;
    mutex_init(&g_lock);
    g_started = true;

    sock_t listener = net_listen(cfg->bind_addr, cfg->port);
    if (listener == BAD_SOCK) {
        log_error("监听 %s:%d 失败：端口被占用了，或者 bind 不是合法的 IPv4 地址", cfg->bind_addr, cfg->port);
        return 1;
    }
    log_info("广理职小鱼服务器已启动，监听 %s:%d", cfg->bind_addr, cfg->port);

    for (;;) {
        char addr[64];
        sock_t s = net_accept(listener, addr, sizeof addr);
        if (s == BAD_SOCK) {
            sleep_ms(100);
            continue;
        }
        mutex_lock(&g_lock);
        int count = g_count;
        mutex_unlock(&g_lock);
        if (count >= MAX_CONNS) {
            log_warn("连接数已满（%d），拒绝 %s", count, addr);
            net_close(s);
            continue;
        }

        net_set_timeouts(s, cfg->idle_timeout, 10);
        Conn *c = (Conn *)calloc(1, sizeof *c);
        if (!c) {
            net_close(s);
            continue;
        }
        c->sock = s;
        snprintf(c->addr, sizeof c->addr, "%s", addr);
        mutex_init(&c->send_lock);
        conns_add(c);
        log_info("新连接 %s（在线 %d）", addr, count + 1);

        if (!thread_start(conn_main, c)) {
            log_error("开线程失败，断开 %s", addr);
            conns_remove(c);
            net_close(s);
            mutex_destroy(&c->send_lock);
            free(c);
        }
    }
}
