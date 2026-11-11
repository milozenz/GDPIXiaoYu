// 日志：同时打印到屏幕和写进日志文件，每行带时间和级别。
#ifndef XIAOYU_LOG_H
#define XIAOYU_LOG_H

// 打开日志文件（追加写）。path 为 NULL 或空字符串时只打印到屏幕。
void log_open(const char *path);
void log_close(void);
// 安静模式：不打印到屏幕（测试程序用，免得刷屏）
void log_set_quiet(int quiet);

void log_write(const char *level, const char *fmt, ...);

#define log_info(...)  log_write("INFO", __VA_ARGS__)
#define log_warn(...)  log_write("WARN", __VA_ARGS__)
#define log_error(...) log_write("ERROR", __VA_ARGS__)

#endif
