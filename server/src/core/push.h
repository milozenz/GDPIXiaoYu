// 服务器主动推送：给在线用户发消息（新私信、订单状态变了）。
#ifndef XIAOYU_PUSH_H
#define XIAOYU_PUSH_H

#include <stdint.h>

#include "cJSON.h"

// 给 user_id 的所有在线连接推送 {"push": event, "data": data}。
// 用户不在线就什么也不做。data 不会被修改，也不会被释放，调用者自己负责 cJSON_Delete。
void push_to_user(int64_t user_id, const char *event, const cJSON *data);

// 测试用：设置一个"钩子"，每次推送都会先调用它（测试程序靠它检查有没有推送）
typedef void (*PushHook)(int64_t user_id, const char *event, const cJSON *data);
void push_set_hook(PushHook hook);

#endif
