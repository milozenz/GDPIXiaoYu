#include "core/jev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log.h"

#ifdef XIAOYU_HAVE_CURL
#include <curl/curl.h>
#endif

typedef struct {
    char url[256];
    char key_env[64];
} Source;

static char    g_model[64] = "jev-1.14";
static Source  g_sources[JEV_MAX_SOURCES];
static int     g_count;
static JevFake g_fake;

void jev_global_init(void) {
#ifdef XIAOYU_HAVE_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

void jev_configure(const char *model, const char sources[][320], int count) {
    if (model && *model) snprintf(g_model, sizeof g_model, "%s", model);
    g_count = 0;
    for (int i = 0; i < count && g_count < JEV_MAX_SOURCES; i++) {
        Source *s = &g_sources[g_count];
        if (sscanf(sources[i], "%255s %63s", s->url, s->key_env) != 2) {
            log_warn("jev_source 格式不对（应为：网址 环境变量名）：%s", sources[i]);
            continue;
        }
        if (!getenv(s->key_env) || !*getenv(s->key_env)) {
            log_warn("Jev 调用源 %s 的密钥环境变量 %s 没有设置，跳过", s->url, s->key_env);
            continue;
        }
        log_info("Jev 调用源 %d：%s（模型 %s）", g_count + 1, s->url, g_model);
        g_count++;
    }
#ifndef XIAOYU_HAVE_CURL
    if (g_count) log_warn("编译时没有 libcurl，Jev 调用全部会失败（审核走放行 + 人工）");
#endif
}

bool jev_enabled(void) { return g_fake != NULL || g_count > 0; }

void jev_set_fake(JevFake fake) { g_fake = fake; }

#ifdef XIAOYU_HAVE_CURL
typedef struct {
    char  *data;
    size_t len;
} Buf;

static size_t on_body(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Buf *b = (Buf *)userdata;
    size_t n = size * nmemb;
    if (b->len + n > 1024 * 1024) return 0;  // 回复不可能这么大，超了就当出错
    char *p = (char *)realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    memcpy(p + b->len, ptr, n);
    b->data = p;
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

// 向一个调用源发请求。成功返回 true 并给出 answers。
static bool ask_one(const Source *s, const char *body, cJSON **answers, char *err, size_t err_size) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err, err_size, "curl 初始化失败");
        return false;
    }
    char auth[512];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s", getenv(s->key_env) ? getenv(s->key_env) : "");
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    Buf buf = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, s->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // 多线程程序里必须加

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    bool ok = false;
    if (rc != CURLE_OK) {
        snprintf(err, err_size, "网络错误：%s", curl_easy_strerror(rc));
    } else if (status != 200) {
        snprintf(err, err_size, "HTTP %ld：%.200s", status, buf.data ? buf.data : "");
    } else {
        cJSON *root = cJSON_Parse(buf.data ? buf.data : "");
        cJSON *a = cJSON_DetachItemFromObjectCaseSensitive(root, "answers");
        if (cJSON_IsObject(a)) {
            *answers = a;
            ok = true;
        } else {
            cJSON_Delete(a);
            snprintf(err, err_size, "回复里没有 answers");
        }
        cJSON_Delete(root);
    }
    free(buf.data);
    return ok;
}
#endif

bool jev_ask(const cJSON *state, const cJSON *questions, cJSON **answers, char *err, size_t err_size) {
    *answers = NULL;
    if (g_fake) {
        if (g_fake(state, questions, answers)) return true;
        snprintf(err, err_size, "测试替身返回失败");
        return false;
    }
    if (g_count == 0) {
        snprintf(err, err_size, "没有配置 Jev 调用源");
        return false;
    }
#ifdef XIAOYU_HAVE_CURL
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", g_model);
    cJSON_AddItemToObject(req, "state", cJSON_Duplicate(state, 1));
    cJSON_AddItemToObject(req, "questions", cJSON_Duplicate(questions, 1));
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) {
        snprintf(err, err_size, "内存不足");
        return false;
    }
    bool ok = false;
    for (int i = 0; i < g_count && !ok; i++) {
        ok = ask_one(&g_sources[i], body, answers, err, err_size);
        if (!ok) log_warn("Jev 调用源 %d 失败：%s", i + 1, err);
    }
    cJSON_free(body);
    return ok;
#else
    snprintf(err, err_size, "编译时没有 libcurl");
    return false;
#endif
}
