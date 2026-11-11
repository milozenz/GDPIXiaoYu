// 发验证码邮件（SMTP，用 libcurl）。
//
// server.conf 里配：
//   smtp_url      = smtps://smtp.qq.com:465
//   smtp_user     = 发件邮箱@qq.com
//   smtp_pass_env = XIAOYU_SMTP_PASS     （授权码放在这个环境变量里，不写进配置文件）
// 没配 smtp_url 就是"开发模式"：不真的发邮件，验证码写进日志，并在 user.send_code 的回复里带回去，
// 方便测试和没网的时候演示。
#ifndef XIAOYU_MAIL_H
#define XIAOYU_MAIL_H

#include <stdbool.h>
#include <stddef.h>

void mail_configure(const char *smtp_url, const char *user, const char *pass_env);

// 有没有配好 SMTP（false = 开发模式）
bool mail_enabled(void);

// 给 to 发一封验证码邮件（reset = true 是重置密码，否则是注册）。成功返回 true，失败把原因写进 err。
bool mail_send_code(const char *to, const char *code, bool reset, char *err, size_t err_size);

#endif
