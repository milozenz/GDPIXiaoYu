#include "core/mail.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/log.h"
#include "core/util.h"

#ifdef XIAOYU_HAVE_CURL
#include <curl/curl.h>
#endif

static char g_url[256];
static char g_user[128];
static char g_pass_env[64];

void mail_configure(const char *smtp_url, const char *user, const char *pass_env) {
    snprintf(g_url, sizeof g_url, "%s", smtp_url ? smtp_url : "");
    snprintf(g_user, sizeof g_user, "%s", user ? user : "");
    snprintf(g_pass_env, sizeof g_pass_env, "%s", pass_env ? pass_env : "");
    if (!g_url[0]) {
        log_info("没配置 SMTP：邮箱验证码走开发模式（写进日志，不真的发邮件）");
        return;
    }
#ifndef XIAOYU_HAVE_CURL
    log_warn("编译时没有 libcurl，发不了邮件，邮箱验证码走开发模式");
    g_url[0] = '\0';
#else
    if (!getenv(g_pass_env) || !*getenv(g_pass_env)) {
        log_warn("SMTP 授权码环境变量 %s 没有设置，邮箱验证码走开发模式", g_pass_env);
        g_url[0] = '\0';
        return;
    }
    log_info("验证码邮件发件人：%s（%s）", g_user, g_url);
#endif
}

bool mail_enabled(void) { return g_url[0] != '\0'; }

#ifdef XIAOYU_HAVE_CURL
typedef struct {
    const char *data;
    size_t      left;
} Payload;

static size_t read_payload(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Payload *p = (Payload *)userdata;
    size_t n = size * nmemb;
    if (n > p->left) n = p->left;
    memcpy(ptr, p->data, n);
    p->data += n;
    p->left -= n;
    return n;
}

// 中文标题要按 RFC 2047 编码：=?UTF-8?B?<base64>?=
static void encode_header(const char *text, char *out, size_t out_size) {
    char *b64 = base64_encode((const unsigned char *)text, strlen(text));
    snprintf(out, out_size, "=?UTF-8?B?%s?=", b64 ? b64 : "");
    free(b64);
}
#endif

bool mail_send_code(const char *to, const char *code, bool reset, char *err, size_t err_size) {
    if (!mail_enabled()) {
        snprintf(err, err_size, "没有配置 SMTP");
        return false;
    }
#ifdef XIAOYU_HAVE_CURL
    char subject[256], from_name[128], date[64];
    encode_header(reset ? "广理职小鱼 重置密码验证码" : "广理职小鱼 注册验证码", subject, sizeof subject);
    encode_header("广理职小鱼", from_name, sizeof from_name);
    time_t now = time(NULL);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S +0000", gmtime(&now));

    char body[600];
    snprintf(body, sizeof body,
             "你好！\r\n\r\n你正在%s「广理职小鱼」校园二手交易平台%s，验证码是：\r\n\r\n    %s\r\n\r\n"
             "验证码 10 分钟内有效。如果不是你本人操作，请忽略这封邮件。\r\n",
             reset ? "重置" : "注册", reset ? "的登录密码" : "", code);
    char *body64 = base64_encode((const unsigned char *)body, strlen(body));
    if (!body64) {
        snprintf(err, err_size, "内存不足");
        return false;
    }
    char msg[2048];
    snprintf(msg, sizeof msg,
             "Date: %s\r\nTo: <%s>\r\nFrom: %s <%s>\r\nSubject: %s\r\nMIME-Version: 1.0\r\n"
             "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n%s\r\n",
             date, to, from_name, g_user, subject, body64);
    free(body64);

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err, err_size, "curl 初始化失败");
        return false;
    }
    char from_addr[160], to_addr[160];
    snprintf(from_addr, sizeof from_addr, "<%s>", g_user);
    snprintf(to_addr, sizeof to_addr, "<%s>", to);
    struct curl_slist *rcpt = curl_slist_append(NULL, to_addr);
    Payload p = {msg, strlen(msg)};
    curl_easy_setopt(curl, CURLOPT_URL, g_url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, g_user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, getenv(g_pass_env));
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_addr);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_payload);
    curl_easy_setopt(curl, CURLOPT_READDATA, &p);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        snprintf(err, err_size, "%s", curl_easy_strerror(rc));
        return false;
    }
    return true;
#else
    (void)to;
    (void)code;
    (void)reset;
    snprintf(err, err_size, "编译时没有 libcurl");
    return false;
#endif
}
