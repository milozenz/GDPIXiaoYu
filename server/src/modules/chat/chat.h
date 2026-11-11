// 聊天模块（负责人：组员D）
// 每个函数对应《协议文档》6.6 节的一个命令。这个头文件不要改。
#ifndef XIAOYU_CHAT_H
#define XIAOYU_CHAT_H

#include "core/ctx.h"

int chat_send(Ctx *ctx, const cJSON *req, cJSON *resp);           // chat.send 🔒
int chat_history(Ctx *ctx, const cJSON *req, cJSON *resp);        // chat.history 🔒
int chat_conversations(Ctx *ctx, const cJSON *req, cJSON *resp);  // chat.conversations 🔒
int chat_mark_read(Ctx *ctx, const cJSON *req, cJSON *resp);      // chat.mark_read 🔒
int chat_unread_count(Ctx *ctx, const cJSON *req, cJSON *resp);   // chat.unread_count 🔒（已写好，当例子看）

#endif
