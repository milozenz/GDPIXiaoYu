// 广理职小鱼 · 服务器入口
// 用法：
//   xiaoyu_server [配置文件]                      启动服务器。不写配置文件就读当前目录的 server.conf，没有就用默认配置
//   xiaoyu_server [配置文件] --set-admin 用户名   把这个用户设成管理员（能登录审核后台），然后退出
#include <stdio.h>
#include <string.h>

#include "core/config.h"
#include "core/db.h"
#include "core/files.h"
#include "core/jev.h"
#include "core/log.h"
#include "core/mail.h"
#include "core/platform.h"
#include "core/server.h"
#include "core/words.h"
#include "modules/safety/safety.h"
#include "modules/user/user.h"

static int set_admin(sqlite3 *db, const char *username) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "UPDATE users SET is_admin=1 WHERE username=?", -1, &st, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_text(st, 1, username, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return 1;
    if (sqlite3_changes(db) == 0) {
        printf("没有这个用户：%s（先在客户端注册）\n", username);
        return 1;
    }
    printf("已把 %s 设为管理员\n", username);
    return 0;
}

int main(int argc, char **argv) {
    console_utf8();

    const char *conf_path = "server.conf";
    const char *admin_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--set-admin") == 0 && i + 1 < argc) admin_name = argv[++i];
        else conf_path = argv[i];
    }

    Config cfg;
    config_default(&cfg);
    bool loaded = config_load(&cfg, conf_path);

    make_dir(cfg.data_dir);
    if (!admin_name) log_open(cfg.log_file);
    if (loaded) log_info("读取配置文件 %s", conf_path);
    else        log_info("没找到配置文件 %s，使用默认配置", conf_path);

    sqlite3 *db = db_open(cfg.db_path);
    if (!db || !db_init_schema(db)) {
        log_error("数据库初始化失败：%s", cfg.db_path);
        return 1;
    }
    if (admin_name) {
        int rc = set_admin(db, admin_name);
        db_close(db);
        return rc;
    }
    db_close(db);
    log_info("数据库 %s 就绪", cfg.db_path);

    if (!net_init()) {
        log_error("网络初始化失败");
        return 1;
    }
    files_set_dir(cfg.data_dir);

    // 内容安全：词库、AI 审核、邮箱验证
    words_init();
    jev_global_init();
    jev_configure(cfg.jev_model, (const char (*)[320])cfg.jev_sources, cfg.jev_source_count);
    if (jev_enabled()) review_start(cfg.db_path);
    else log_info("没有配置 Jev 调用源：只用本地词库，不做 AI 审核");
    mail_configure(cfg.smtp_url, cfg.smtp_user, cfg.smtp_pass_env);
    user_set_require_email(cfg.require_email != 0);

    return server_run(&cfg);
}
