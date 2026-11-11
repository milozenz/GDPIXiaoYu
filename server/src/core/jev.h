// Jev 决策模型客户端（TypeSafe 公司的 System One 接口）。
//
// Jev 不写文章，只回答"判断题 / 选择题 / 打分题"，每个答案都带概率，正好用来做内容审核：
// 概率够高就自动处理，不够就交给人工。
//
// 可以配置多个调用源（比如 Jev 官方、OpenRouter），按顺序试，前一个失败就换下一个。
// 调用源和模型写在 server.conf 里；密钥不写进配置文件，而是从环境变量读：
//   jev_model  = jev-1.14
//   jev_source = https://openrouter.ai/api/v1/systemone OPENROUTER_API_KEY
//   jev_source = https://api.typesafe.ai/v1/systemone TYPESAFE_API_KEY
// 编译时没找到 libcurl 的话（比如 Windows 上没装），jev_ask 总是返回失败，审核自动走"放行 + 稍后重试"。
#ifndef XIAOYU_JEV_H
#define XIAOYU_JEV_H

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#define JEV_MAX_SOURCES 4

// 程序启动时调用一次（初始化 libcurl）
void jev_global_init(void);

// 设置模型和调用源。sources[i] 的格式是 "网址 环境变量名"。
void jev_configure(const char *model, const char sources[][320], int count);

// 有没有可用的调用源（或者测试替身）。没有的话，审核功能整个跳过。
bool jev_enabled(void);

// 问一次 Jev。state 是给它看的材料，questions 是问题（格式见 System One 文档），都不会被修改。
// 成功返回 true，*answers 是 answers 对象（调用者负责 cJSON_Delete）。
// 失败返回 false，原因写进 err。
bool jev_ask(const cJSON *state, const cJSON *questions, cJSON **answers, char *err, size_t err_size);

// 测试用：设置一个替身，jev_ask 直接调用它，不走网络。传 NULL 取消。
typedef bool (*JevFake)(const cJSON *state, const cJSON *questions, cJSON **answers);
void jev_set_fake(JevFake fake);

#endif
