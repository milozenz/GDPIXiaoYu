// 用户模块（负责人：组员C）
// 每个函数对应《协议文档》6.2 节的一个命令。这个头文件不要改。
#ifndef XIAOYU_USER_H
#define XIAOYU_USER_H

#include "core/ctx.h"

int user_register(Ctx *ctx, const cJSON *req, cJSON *resp);        // user.register
int user_login(Ctx *ctx, const cJSON *req, cJSON *resp);           // user.login
int user_logout(Ctx *ctx, const cJSON *req, cJSON *resp);          // user.logout 🔒
int user_me(Ctx *ctx, const cJSON *req, cJSON *resp);              // user.me 🔒
int user_update_profile(Ctx *ctx, const cJSON *req, cJSON *resp);  // user.update_profile 🔒
int user_get(Ctx *ctx, const cJSON *req, cJSON *resp);             // user.get
int user_recharge(Ctx *ctx, const cJSON *req, cJSON *resp);        // user.recharge 🔒
int user_send_code(Ctx *ctx, const cJSON *req, cJSON *resp);       // user.send_code（发注册 / 重置密码验证码）
int user_change_password(Ctx *ctx, const cJSON *req, cJSON *resp); // user.change_password 🔒
int user_reset_password(Ctx *ctx, const cJSON *req, cJSON *resp);  // user.reset_password（忘记密码）

// 注册时是否必须填邮箱和验证码（服务器按配置文件设置；测试默认 false，老用例不受影响）
void user_set_require_email(bool require);

#endif
