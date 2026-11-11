// 配置文件：把端口、数据库位置这些可能要改的东西放在 server.conf 里，改了不用重新编译。
#ifndef XIAOYU_CONFIG_H
#define XIAOYU_CONFIG_H

#include <stdbool.h>

typedef struct Config {
    char bind_addr[64];   // 监听地址：0.0.0.0 = 所有网卡（默认），127.0.0.1 = 只允许本机连接
    int  port;            // 监听端口
    char db_path[256];    // 数据库文件路径
    char data_dir[256];   // 图片存放目录
    char log_file[256];   // 日志文件，空字符串 = 只打印到屏幕
    int  max_frame_mb;    // 单条消息最大多少 MB
    int  idle_timeout;    // 多少秒收不到客户端消息就断开
    // 内容审核用的 Jev 模型（见 jev.h）。没配调用源 = 不做 AI 审核，只用本地词库
    char jev_model[64];
    char jev_sources[4][320];  // 每项 "网址 环境变量名"
    int  jev_source_count;
    // 发验证码邮件用的 SMTP（见 mail.h）。smtp_url 留空 = 不真的发邮件，验证码只写进日志（演示/测试用）
    char smtp_url[256];        // 例如 smtps://smtp.qq.com:465
    char smtp_user[128];       // 发件邮箱
    char smtp_pass_env[64];    // 授权码放在哪个环境变量里（默认 XIAOYU_SMTP_PASS）
    int  require_email;        // 注册是否必须验证邮箱（1 = 必须，默认）
} Config;

// 填默认值
void config_default(Config *c);
// 读配置文件（格式：key = value，# 开头是注释）。文件不存在返回 false，c 保持原样。
bool config_load(Config *c, const char *path);

#endif
