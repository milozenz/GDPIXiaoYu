#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "core/platform.h"

static FILE   *g_file;
static int     g_quiet;
static int     g_inited;
static mutex_t g_lock;

static void ensure_init(void) {
    if (!g_inited) {
        mutex_init(&g_lock);
        g_inited = 1;
    }
}

void log_open(const char *path) {
    ensure_init();
    if (path && path[0]) {
        g_file = fopen(path, "a");
        if (!g_file) fprintf(stderr, "打不开日志文件 %s，只打印到屏幕\n", path);
    }
}

void log_close(void) {
    if (g_file) fclose(g_file);
    g_file = NULL;
}

void log_set_quiet(int quiet) { g_quiet = quiet; }

void log_write(const char *level, const char *fmt, ...) {
    ensure_init();

    char stamp[32];
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);

    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);

    mutex_lock(&g_lock);
    if (!g_quiet) {
        printf("%s [%s] %s\n", stamp, level, text);
        fflush(stdout);
    }
    if (g_file) {
        fprintf(g_file, "%s [%s] %s\n", stamp, level, text);
        fflush(g_file);
    }
    mutex_unlock(&g_lock);
}
