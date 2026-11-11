// 图片文件：上传、下载、检查是否存在。图片存在 <data_dir>/images/ 下。
#ifndef XIAOYU_FILES_H
#define XIAOYU_FILES_H

#include <stdbool.h>

#include "core/ctx.h"

// 设置数据目录（程序启动时调用一次），会自动建好 images 子目录
void files_set_dir(const char *data_dir);

// 图片名格式对不对：32 位小写十六进制 + "." + jpg/png/gif/webp。防止有人传 "../../xxx" 读别的文件。
bool image_name_valid(const char *name);

// 图片是否已经上传过（格式不对也返回 false）
bool image_exists(const char *name);

// file.upload / file.get 两个命令
int file_upload(Ctx *ctx, const cJSON *req, cJSON *resp);
int file_get(Ctx *ctx, const cJSON *req, cJSON *resp);

#endif
