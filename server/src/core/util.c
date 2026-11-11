#include "core/util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/platform.h"

int64_t now_sec(void) { return (int64_t)time(NULL); }

size_t utf8_len(const char *s) {
    if (!s) return 0;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;  // 只数每个字的第一个字节
    return n;
}

bool utf8_valid(const char *s, size_t min_chars, size_t max_chars) {
    if (!s) return false;
    const unsigned char *p = (const unsigned char *)s;
    size_t count = 0;
    while (*p) {
        int extra;
        unsigned low = 0x80, high = 0xBF;
        if (*p < 0x80) extra = 0;
        else if (*p >= 0xC2 && *p <= 0xDF) extra = 1;
        else if (*p >= 0xE0 && *p <= 0xEF) {
            extra = 2;
            if (*p == 0xE0) low = 0xA0;   // 排除过长编码
            if (*p == 0xED) high = 0x9F;  // 排除代理区
        } else if (*p >= 0xF0 && *p <= 0xF4) {
            extra = 3;
            if (*p == 0xF0) low = 0x90;
            if (*p == 0xF4) high = 0x8F;
        } else return false;
        for (int i = 1; i <= extra; i++) {
            unsigned lo = i == 1 ? low : 0x80, hi = i == 1 ? high : 0xBF;
            if (p[i] < lo || p[i] > hi) return false;
        }
        p += 1 + extra;
        if (++count > max_chars) return false;
    }
    return count >= min_chars;
}

void random_hex(char *out, size_t nbytes) {
    static const char HEX[] = "0123456789abcdef";
    unsigned char buf[64];
    if (nbytes > sizeof buf) nbytes = sizeof buf;
    if (!random_bytes(buf, nbytes)) {
        // 系统随机数不可用时退而求其次（几乎不会发生）
        for (size_t i = 0; i < nbytes; i++) buf[i] = (unsigned char)(rand() ^ (int)time(NULL));
    }
    for (size_t i = 0; i < nbytes; i++) {
        out[2 * i]     = HEX[buf[i] >> 4];
        out[2 * i + 1] = HEX[buf[i] & 15];
    }
    out[2 * nbytes] = '\0';
}

// ======================== SHA-256 ========================
// 按 FIPS 180-4 标准实现。

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const unsigned char *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_hex(const void *data, size_t len, char out[65]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const unsigned char *p = (const unsigned char *)data;
    size_t left = len;
    while (left >= 64) {
        sha256_block(h, p);
        p += 64;
        left -= 64;
    }
    // 补位：0x80，然后补 0，最后 8 字节写总比特数
    unsigned char tail[128] = {0};
    memcpy(tail, p, left);
    tail[left] = 0x80;
    size_t tail_len = left < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tail_len - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha256_block(h, tail);
    if (tail_len == 128) sha256_block(h, tail + 64);

    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) {
            unsigned char byte = (unsigned char)(h[i] >> (24 - 8 * j));
            out[8 * i + 2 * j]     = HEX[byte >> 4];
            out[8 * i + 2 * j + 1] = HEX[byte & 15];
        }
    out[64] = '\0';
}

// ======================== Base64 ========================

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *data, size_t len) {
    size_t out_len = (len + 2) / 3 * 4;
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    return out;
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

unsigned char *base64_decode(const char *text, size_t *out_len) {
    size_t len = strlen(text);
    unsigned char *out = (unsigned char *)malloc(len / 4 * 3 + 3);
    if (!out) return NULL;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '=') break;
        if (c == '\r' || c == '\n' || c == ' ') continue;
        int v = b64_value(c);
        if (v < 0) { free(out); return NULL; }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)(acc >> bits);
        }
    }
    *out_len = o;
    return out;
}
