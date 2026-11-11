#include "core/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/jsonx.h"
#include "core/log.h"
#include "core/platform.h"
#include "core/util.h"

#define MAX_IMAGE_BYTES (5 * 1024 * 1024)

static char g_images_dir[300] = "images";

void files_set_dir(const char *data_dir) {
    make_dir(data_dir);
    snprintf(g_images_dir, sizeof g_images_dir, "%s/images", data_dir);
    make_dir(g_images_dir);
}

static bool ext_allowed(const char *ext) {
    return strcmp(ext, "jpg") == 0 || strcmp(ext, "png") == 0 ||
           strcmp(ext, "gif") == 0 || strcmp(ext, "webp") == 0;
}

bool image_name_valid(const char *name) {
    if (!name || strlen(name) < 34) return false;
    for (int i = 0; i < 32; i++) {
        char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return name[32] == '.' && ext_allowed(name + 33);
}

static void image_path(const char *name, char *out, size_t size) {
    snprintf(out, size, "%s/%s", g_images_dir, name);
}

bool image_exists(const char *name) {
    if (!image_name_valid(name)) return false;
    char path[400];
    image_path(name, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

int file_upload(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *ext = jx_str(req, "ext");
    const char *data = jx_str(req, "data");
    if (!ext || !ext_allowed(ext)) return fail(ctx, ERR_BAD_REQUEST, "图片格式只能是 jpg/png/gif/webp");
    if (!data || !data[0]) return fail(ctx, ERR_BAD_REQUEST, "图片内容不能为空");
    if (strlen(data) / 4 * 3 > MAX_IMAGE_BYTES + 3) return fail(ctx, ERR_TOO_LARGE, "图片不能超过 5 MB");

    size_t len = 0;
    unsigned char *bytes = base64_decode(data, &len);
    if (!bytes) return fail(ctx, ERR_BAD_REQUEST, "图片不是合法的 Base64");
    if (len == 0 || len > MAX_IMAGE_BYTES) {
        free(bytes);
        return fail(ctx, len ? ERR_TOO_LARGE : ERR_BAD_REQUEST, len ? "图片不能超过 5 MB" : "图片内容不能为空");
    }

    char name[64], path[400];
    random_hex(name, 16);
    snprintf(name + 32, sizeof name - 32, ".%s", ext);
    image_path(name, path, sizeof path);

    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(bytes, 1, len, f) == len;
    if (f) fclose(f);
    free(bytes);
    if (!ok) {
        log_error("保存图片失败: %s", path);
        return fail(ctx, ERR_INTERNAL, "保存图片失败");
    }
    log_info("用户 %lld 上传图片 %s（%zu 字节）", (long long)ctx->user_id, name, len);
    jx_add_str(resp, "image", name);
    return OK;
}

int file_get(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *name = jx_str(req, "image");
    if (!image_name_valid(name)) return fail(ctx, ERR_BAD_REQUEST, "图片名格式不对");

    char path[400];
    image_path(name, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return fail(ctx, ERR_NOT_FOUND, "图片不存在");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > MAX_IMAGE_BYTES) {
        fclose(f);
        return fail(ctx, ERR_INTERNAL, "图片文件异常");
    }
    unsigned char *bytes = (unsigned char *)malloc((size_t)size + 1);
    size_t got = bytes ? fread(bytes, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!bytes || got != (size_t)size) {
        free(bytes);
        return fail(ctx, ERR_INTERNAL, "读取图片失败");
    }
    char *b64 = base64_encode(bytes, got);
    free(bytes);
    if (!b64) return fail(ctx, ERR_INTERNAL, "内存不足");
    jx_add_str(resp, "data", b64);
    free(b64);
    return OK;
}
