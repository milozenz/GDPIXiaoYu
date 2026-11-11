// 网络服务器：监听端口，每个客户端连接开一个线程，按《协议文档》第 2 节收发消息。
#ifndef XIAOYU_SERVER_H
#define XIAOYU_SERVER_H

#include "core/config.h"

// 启动服务器，正常情况下不会返回；端口被占用等启动失败时返回非 0
int server_run(const Config *cfg);

#endif
