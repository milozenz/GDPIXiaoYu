#include "core/platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <bcrypt.h>
#  include <direct.h>
#  include <process.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <signal.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <time.h>
#  include <unistd.h>
#endif

// ======================== 网络 ========================

bool net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    signal(SIGPIPE, SIG_IGN);  // 对方断开后再写不要让整个进程退出
    return true;
#endif
}

sock_t net_listen(const char *bind_addr, int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) return BAD_SOCK;

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (bind_addr && bind_addr[0]) {
        if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {  // 不是合法的 IPv4 地址
            net_close(s);
            return BAD_SOCK;
        }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s, 128) != 0) {
        net_close(s);
        return BAD_SOCK;
    }
    return s;
}

sock_t net_accept(sock_t listener, char *addr, size_t addr_size) {
    struct sockaddr_in peer;
#ifdef _WIN32
    int len = sizeof peer;
#else
    socklen_t len = sizeof peer;
#endif
    sock_t s = accept(listener, (struct sockaddr *)&peer, &len);
    if (s == BAD_SOCK) return BAD_SOCK;

    int yes = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof yes);  // 小消息立刻发出
    if (addr && addr_size) {
        char ip[64] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        snprintf(addr, addr_size, "%s:%u", ip, (unsigned)ntohs(peer.sin_port));
    }
    return s;
}

bool net_send_all(sock_t s, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        int chunk = len > 65536 ? 65536 : (int)len;
#ifdef _WIN32
        int n = send(s, p, chunk, 0);
#else
        int n = (int)send(s, p, (size_t)chunk, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool net_recv_all(sock_t s, void *buf, size_t len) {
    char *p = (char *)buf;
    while (len > 0) {
        int chunk = len > 65536 ? 65536 : (int)len;
#ifdef _WIN32
        int n = recv(s, p, chunk, 0);
#else
        int n = (int)recv(s, p, (size_t)chunk, 0);
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n <= 0) return false;  // 0 = 对方关闭；<0 = 出错或超时
        p += n;
        len -= (size_t)n;
    }
    return true;
}

void net_set_timeouts(sock_t s, int recv_sec, int send_sec) {
#ifdef _WIN32
    DWORD r = (DWORD)recv_sec * 1000, w = (DWORD)send_sec * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&r, sizeof r);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&w, sizeof w);
#else
    struct timeval r = {recv_sec, 0}, w = {send_sec, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &r, sizeof r);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &w, sizeof w);
#endif
}

void net_close(sock_t s) {
    if (s == BAD_SOCK) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// ======================== 线程和锁 ========================

typedef struct {
    void (*fn)(void *);
    void *arg;
} ThreadStart;

#ifdef _WIN32
static unsigned __stdcall thread_entry(void *p) {
#else
static void *thread_entry(void *p) {
#endif
    ThreadStart st = *(ThreadStart *)p;
    free(p);
    st.fn(st.arg);
    return 0;
}

bool thread_start(void (*fn)(void *), void *arg) {
    ThreadStart *st = (ThreadStart *)malloc(sizeof *st);
    if (!st) return false;
    st->fn = fn;
    st->arg = arg;
#ifdef _WIN32
    uintptr_t h = _beginthreadex(NULL, 0, thread_entry, st, 0, NULL);
    if (h == 0) { free(st); return false; }
    CloseHandle((HANDLE)h);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, thread_entry, st) != 0) { free(st); return false; }
    pthread_detach(t);
#endif
    return true;
}

#ifdef _WIN32
void mutex_init(mutex_t *m)    { InitializeCriticalSection(m); }
void mutex_lock(mutex_t *m)    { EnterCriticalSection(m); }
void mutex_unlock(mutex_t *m)  { LeaveCriticalSection(m); }
void mutex_destroy(mutex_t *m) { DeleteCriticalSection(m); }
#else
void mutex_init(mutex_t *m)    { pthread_mutex_init(m, NULL); }
void mutex_lock(mutex_t *m)    { pthread_mutex_lock(m); }
void mutex_unlock(mutex_t *m)  { pthread_mutex_unlock(m); }
void mutex_destroy(mutex_t *m) { pthread_mutex_destroy(m); }
#endif

// ======================== 其他 ========================

bool random_bytes(void *buf, size_t len) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n == len;
#endif
}

bool make_dir(const char *path) {
#ifdef _WIN32
    if (_mkdir(path) == 0) return true;
#else
    if (mkdir(path, 0755) == 0) return true;
#endif
    return errno == EEXIST;
}

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

int64_t now_ms(void) {
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void console_utf8(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}
