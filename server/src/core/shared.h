// 各模块都会用到的公共查询。放在这里，免得每个人都写一遍，也免得模块之间互相依赖。
#ifndef XIAOYU_SHARED_H
#define XIAOYU_SHARED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "sqlite3.h"

// 用户是否存在
bool user_exists(sqlite3 *db, int64_t user_id);

// 把 UserBrief {user_id, nickname, avatar} 作为 obj[key] 加进去。
// 用户不存在时加一个 {user_id, nickname:"已注销用户", avatar:""}，并返回 false。
bool add_user_brief(sqlite3 *db, cJSON *obj, const char *key, int64_t user_id);

// 取商品封面（第一张图）的图片名写到 out；没有图时写空字符串
void get_goods_cover(sqlite3 *db, int64_t goods_id, char *out, size_t out_size);

// 分页参数：从 req 里读 page / page_size，检查范围（page >= 1，page_size 1-50）。
// 不合法返回 false。默认 page = 1，page_size = 20。
bool read_paging(const cJSON *req, int64_t *page, int64_t *page_size);

// 往 resp 里写分页列表的外壳 {total, page, page_size, items: []}，返回 items 数组，往里加元素即可
cJSON *add_page(cJSON *resp, int64_t total, int64_t page, int64_t page_size);

#endif
