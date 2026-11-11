#include "core/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log.h"

void config_default(Config *c) {
    memset(c, 0, sizeof *c);
    snprintf(c->bind_addr, sizeof c->bind_addr, "0.0.0.0");
    c->port = 9527;
    snprintf(c->db_path, sizeof c->db_path, "data/xiaoyu.db");
    snprintf(c->data_dir, sizeof c->data_dir, "data");
    snprintf(c->log_file, sizeof c->log_file, "data/server.log");
    c->max_frame_mb = 8;
    c->idle_timeout = 120;
    snprintf(c->jev_model, sizeof c->jev_model, "jev-1.14");
    snprintf(c->smtp_pass_env, sizeof c->smtp_pass_env, "XIAOYU_SMTP_PASS");
    c->require_email = 1;
}

// 去掉首尾空白，返回新的开头
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static void copy_str(char *dst, size_t size, const char *src) {
    snprintf(dst, size, "%s", src);
}

bool config_load(Config *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) {
            log_warn("配置文件第 %d 行没有等号，跳过", lineno);
            continue;
        }
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(key, "bind") == 0)              copy_str(c->bind_addr, sizeof c->bind_addr, val);
        else if (strcmp(key, "port") == 0)         c->port = atoi(val);
        else if (strcmp(key, "db_path") == 0)      copy_str(c->db_path, sizeof c->db_path, val);
        else if (strcmp(key, "data_dir") == 0)     copy_str(c->data_dir, sizeof c->data_dir, val);
        else if (strcmp(key, "log_file") == 0)     copy_str(c->log_file, sizeof c->log_file, val);
        else if (strcmp(key, "max_frame_mb") == 0) c->max_frame_mb = atoi(val);
        else if (strcmp(key, "idle_timeout") == 0) c->idle_timeout = atoi(val);
        else if (strcmp(key, "jev_model") == 0)    copy_str(c->jev_model, sizeof c->jev_model, val);
        else if (strcmp(key, "jev_source") == 0) {
            if (c->jev_source_count < 4)
                copy_str(c->jev_sources[c->jev_source_count++], sizeof c->jev_sources[0], val);
            else
                log_warn("配置文件第 %d 行：jev_source 最多 4 个", lineno);
        }
        else if (strcmp(key, "smtp_url") == 0)      copy_str(c->smtp_url, sizeof c->smtp_url, val);
        else if (strcmp(key, "smtp_user") == 0)     copy_str(c->smtp_user, sizeof c->smtp_user, val);
        else if (strcmp(key, "smtp_pass_env") == 0) copy_str(c->smtp_pass_env, sizeof c->smtp_pass_env, val);
        else if (strcmp(key, "require_email") == 0) c->require_email = atoi(val);
        else log_warn("配置文件第 %d 行：不认识的配置项 %s", lineno, key);
    }
    fclose(f);
    return true;
}
