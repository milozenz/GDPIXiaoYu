// 商品模块（负责人：组员C）
// 每个函数对应《协议文档》6.3 节的一个命令。这个头文件不要改。
#ifndef XIAOYU_GOODS_H
#define XIAOYU_GOODS_H

#include "core/ctx.h"

int goods_categories(Ctx *ctx, const cJSON *req, cJSON *resp);  // goods.categories（已写好，当例子看）
int goods_publish(Ctx *ctx, const cJSON *req, cJSON *resp);     // goods.publish 🔒
int goods_update(Ctx *ctx, const cJSON *req, cJSON *resp);      // goods.update 🔒
int goods_off_shelf(Ctx *ctx, const cJSON *req, cJSON *resp);   // goods.off_shelf 🔒
int goods_on_shelf(Ctx *ctx, const cJSON *req, cJSON *resp);    // goods.on_shelf 🔒
int goods_detail(Ctx *ctx, const cJSON *req, cJSON *resp);      // goods.detail
int goods_search(Ctx *ctx, const cJSON *req, cJSON *resp);      // goods.search
int goods_mine(Ctx *ctx, const cJSON *req, cJSON *resp);        // goods.mine 🔒

#endif
