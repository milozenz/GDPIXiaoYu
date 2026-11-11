// 常用小工具：时间、字数、随机数、SHA-256、Base64。
#ifndef XIAOYU_UTIL_H
#define XIAOYU_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 当前 Unix 时间戳（秒）
int64_t now_sec(void);

// UTF-8 字符串有几个"字"（一个汉字算 1 个，一个字母也算 1 个）。NULL 返回 0。
size_t utf8_len(const char *s);

// 是不是合法的 UTF-8，并且字数在 [min_chars, max_chars] 之间。NULL 返回 false。
bool utf8_valid(const char *s, size_t min_chars, size_t max_chars);

// 生成 nbytes 个随机字节，写成十六进制字符串。out 至少要 2*nbytes+1 个字节。
//   char salt[33]; random_hex(salt, 16);   // 得到 32 个十六进制字符
void random_hex(char *out, size_t nbytes);

// 计算 SHA-256，结果写成 64 个十六进制字符（out 至少 65 个字节）
void sha256_hex(const void *data, size_t len, char out[65]);

// Base64 编码/解码。返回 malloc 出来的内存，用完要 free；失败返回 NULL。
char *base64_encode(const unsigned char *data, size_t len);
unsigned char *base64_decode(const char *text, size_t *out_len);

#endif
