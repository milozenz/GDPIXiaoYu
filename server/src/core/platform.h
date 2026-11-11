// 跨平台封装：把 Windows 和 Linux 在网络、线程、锁、文件夹上的差异藏在这里。
// 其他代码只用这里的函数，就能同时在 Windows（VS 2022）和 Linux（服务器）上编译。
#ifndef XIAOYU_PLATFORM_H
#define XIAOYU_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET sock_t;
#  define BAD_SOCK INVALID_SOCKET
typedef CRITICAL_SECTION mutex_t;
#else
#  include <pthread.h>
typedef int sock_t;
#  define BAD_SOCK (-1)
typedef pthread_mutex_t mutex_t;
#endif

// ---- 网络 ----
bool   net_init(void);                       // 程序开始时调用一次（Windows 要初始化 Winsock）
sock_t net_listen(const char *bind_addr, int port);  // 在 bind_addr:port 上监听（NULL 或空 = 所有网卡），失败返回 BAD_SOCK
sock_t net_accept(sock_t listener, char *addr, size_t addr_size);  // 等一个新连接，addr 写入对方地址
bool   net_send_all(sock_t s, const void *buf, size_t len);        // 发完 len 字节才返回
bool   net_recv_all(sock_t s, void *buf, size_t len);              // 收满 len 字节才返回，断开或超时返回 false
void   net_set_timeouts(sock_t s, int recv_sec, int send_sec);
void   net_close(sock_t s);

// ---- 线程和锁 ----
bool thread_start(void (*fn)(void *), void *arg);  // 开一个后台线程，不需要等它结束
void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
void mutex_destroy(mutex_t *m);

// ---- 其他 ----
bool random_bytes(void *buf, size_t len);    // 系统级安全随机数
bool make_dir(const char *path);             // 建文件夹，已存在也算成功
void sleep_ms(int ms);
int64_t now_ms(void);                        // 单调时钟（毫秒），用来计算耗时
void console_utf8(void);                     // 让 Windows 控制台正确显示中文

#endif
