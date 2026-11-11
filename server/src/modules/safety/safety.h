// 内容安全模块：敏感词拦截、举报、申诉、AI 审核队列、管理员审核后台。
//
// 规则（和组里商量定下的）：
//   私信：一级词直接拦；二级词照常发出，接收方看到提醒，后台交给 AI，AI 有把握判违规 = 屏蔽 + 禁言 12 小时
//         被举报的消息交给 AI 判两件事：言论本身是否明确违规（严）、举报理由是否成立（宽）
//   商品：一级禁售词直接拦；其余发布后先"审核中"，AI 有把握合规就上架，有把握违规就驳回下架，
//         没把握或 AI 调用失败就先上架，再交人工复核
//   所有自动处理都记进"已自动处理"区，人工可以复核、撤销；拿不准的进"人工待审"区
#ifndef XIAOYU_SAFETY_H
#define XIAOYU_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#include "core/ctx.h"

// ---- 给其他模块用的检查 ----
// 当前用户是否被禁言；被禁言返回 ERR_FORBIDDEN 并写好提示
int safety_check_muted(Ctx *ctx);
// 检查私信内容：一级词返回 ERR_BAD_REQUEST（并留底）；二级词返回 OK，*mod_state = 1，*mod_kind = 类别
int safety_check_chat(Ctx *ctx, const char *content, int *mod_state, const char **mod_kind);
// 检查商品标题和描述（可以是 NULL）：一级词返回 ERR_BAD_REQUEST（并留底）
int safety_check_goods(Ctx *ctx, const char *title, const char *description);
// 检查昵称和简介（都可以是 NULL）：一级词返回 ERR_BAD_REQUEST（并留底）。没命中的之后交给 AI（kind = "profile"）
int safety_check_profile(Ctx *ctx, const char *nickname, const char *bio);
// 昵称被重置后的锁定：在锁定期内返回 ERR_FORBIDDEN 并写好提示
int safety_check_nickname_lock(Ctx *ctx);
// 是否开启了 AI 审核（配了 Jev 调用源）
bool safety_ai_enabled(void);
// 把一个审核任务放进队列。kind 是 "goods"、"report"、"chat" 或 "profile"（ref_id = 用户 id）
bool safety_enqueue(sqlite3 *db, const char *kind, int64_t ref_id);

// ---- 后台审核线程 ----
// 处理一轮到期的审核任务，返回处理了几个（测试直接调用它）
int review_run_once(sqlite3 *db);
// 开一个后台线程，每秒跑一轮
void review_start(const char *db_path);

// ---- 命令 ----
int report_create(Ctx *ctx, const cJSON *req, cJSON *resp);      // report.create 🔒
int appeal_create(Ctx *ctx, const cJSON *req, cJSON *resp);      // appeal.create 🔒
int admin_stats(Ctx *ctx, const cJSON *req, cJSON *resp);        // admin.stats 🛡
int admin_cases(Ctx *ctx, const cJSON *req, cJSON *resp);        // admin.cases 🛡
int admin_case_detail(Ctx *ctx, const cJSON *req, cJSON *resp);  // admin.case_detail 🛡
int admin_resolve(Ctx *ctx, const cJSON *req, cJSON *resp);      // admin.resolve 🛡
int admin_mute(Ctx *ctx, const cJSON *req, cJSON *resp);         // admin.mute 🛡
int admin_unmute(Ctx *ctx, const cJSON *req, cJSON *resp);       // admin.unmute 🛡
int admin_users(Ctx *ctx, const cJSON *req, cJSON *resp);        // admin.users 🛡
int admin_goods(Ctx *ctx, const cJSON *req, cJSON *resp);        // admin.goods 🛡
int admin_blocked(Ctx *ctx, const cJSON *req, cJSON *resp);      // admin.blocked 🛡

#endif
