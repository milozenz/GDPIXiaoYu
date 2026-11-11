// 订单和收藏模块（负责人：组员D）
// 每个函数对应《协议文档》6.4、6.5 节的一个命令。这个头文件不要改。
#ifndef XIAOYU_ORDER_H
#define XIAOYU_ORDER_H

#include "core/ctx.h"

int order_create(Ctx *ctx, const cJSON *req, cJSON *resp);   // order.create 🔒
int order_cancel(Ctx *ctx, const cJSON *req, cJSON *resp);   // order.cancel 🔒
int order_ship(Ctx *ctx, const cJSON *req, cJSON *resp);     // order.ship 🔒
int order_confirm(Ctx *ctx, const cJSON *req, cJSON *resp);  // order.confirm 🔒
int order_detail(Ctx *ctx, const cJSON *req, cJSON *resp);   // order.detail 🔒
int order_list(Ctx *ctx, const cJSON *req, cJSON *resp);     // order.list 🔒

// 收藏（选做）
int fav_add(Ctx *ctx, const cJSON *req, cJSON *resp);        // fav.add 🔒
int fav_remove(Ctx *ctx, const cJSON *req, cJSON *resp);     // fav.remove 🔒
int fav_check(Ctx *ctx, const cJSON *req, cJSON *resp);      // fav.check 🔒
int fav_list(Ctx *ctx, const cJSON *req, cJSON *resp);       // fav.list 🔒

#endif
