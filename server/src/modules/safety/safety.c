// 内容安全模块（规则见 safety.h）
#include "modules/safety/safety.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/db.h"
#include "core/jev.h"
#include "core/jsonx.h"
#include "core/log.h"
#include "core/platform.h"
#include "core/push.h"
#include "core/shared.h"
#include "core/util.h"
#include "core/words.h"

#define AI_SURE          0.9            // 阈值：概率 >= 0.9 才算 AI "有把握"
#define MUTE_SECONDS     (12 * 3600)    // AI 自动禁言 12 小时
#define JOB_MAX_ATTEMPTS 5              // AI 调用失败最多重试几次，之后转人工
#define CONTEXT_MESSAGES 5              // 给 AI 看被判断消息前后各几条
#define NICKNAME_LOCK    (24 * 3600)    // 昵称因违规被重置后，24 小时内不能再改

// ======================================================================
// 小工具
// ======================================================================

static int64_t user_muted_until(sqlite3 *db, int64_t uid) {
    sqlite3_stmt *st;
    int64_t until = 0;
    if (sqlite3_prepare_v2(db, "SELECT muted_until FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, uid);
    if (sqlite3_step(st) == SQLITE_ROW) until = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return until;
}

// 执行一条带参数的 SQL。fmt 每个字母对应一个参数：i = int64_t，s = 字符串，d = double
static bool exec_bind(sqlite3 *db, const char *sql, const char *fmt, ...) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        log_error("SQL 准备失败: %s (%s)", sqlite3_errmsg(db), sql);
        return false;
    }
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == 'i') sqlite3_bind_int64(st, i + 1, va_arg(ap, int64_t));
        else if (fmt[i] == 'd') sqlite3_bind_double(st, i + 1, va_arg(ap, double));
        else sqlite3_bind_text(st, i + 1, va_arg(ap, const char *), -1, SQLITE_TRANSIENT);
    }
    va_end(ap);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) log_error("SQL 执行失败: %s (%s)", sqlite3_errmsg(db), sql);
    return rc == SQLITE_DONE;
}

static int64_t add_case(sqlite3 *db, const char *area, const char *kind, int64_t ref_id, int64_t user_id,
                        const char *summary, const char *ai_label, double ai_score) {
    if (!exec_bind(db,
                   "INSERT INTO mod_cases(area,kind,ref_id,user_id,summary,ai_label,ai_score,created_at)"
                   " VALUES(?,?,?,?,?,?,?,?)",
                   "ssiissdi", area, kind, ref_id, user_id, summary, ai_label ? ai_label : "", ai_score, now_sec()))
        return 0;
    return sqlite3_last_insert_rowid(db);
}

static void log_blocked(sqlite3 *db, const char *kind, int64_t uid, const WordHit *h, const char *content) {
    exec_bind(db, "INSERT INTO blocked_logs(kind,user_id,category,word,content,created_at) VALUES(?,?,?,?,?,?)",
              "sisssi", kind, uid, h->category, h->word, content, now_sec());
}

// ======================================================================
// 给其他模块用的检查
// ======================================================================

bool safety_ai_enabled(void) { return jev_enabled(); }

int safety_check_muted(Ctx *ctx) {
    int64_t left = user_muted_until(ctx->db, ctx->user_id) - now_sec();
    if (left <= 0) return OK;
    long long h = left / 3600, m = (left % 3600 + 59) / 60;
    if (m == 60) { h++; m = 0; }
    return fail(ctx, ERR_FORBIDDEN, "你因违规被禁言，还剩 %lld 小时 %lld 分钟，可以在消息页提交申诉", h, m);
}

int safety_check_chat(Ctx *ctx, const char *content, int *mod_state, const char **mod_kind) {
    *mod_state = 0;
    *mod_kind = "";
    WordHit h;
    if (!words_check(content, SCOPE_CHAT, &h)) return OK;
    if (h.level == WORD_BLOCK) {
        log_blocked(ctx->db, "chat", ctx->user_id, &h, content);
        return fail(ctx, ERR_BAD_REQUEST, "消息包含违规内容（%s），发送失败", category_label(h.category));
    }
    *mod_state = 1;
    *mod_kind = h.category;
    return OK;
}

int safety_check_goods(Ctx *ctx, const char *title, const char *description) {
    const char *texts[2] = {title, description};
    for (int i = 0; i < 2; i++) {
        WordHit h;
        if (!texts[i] || !words_check(texts[i], SCOPE_GOODS, &h) || h.level != WORD_BLOCK) continue;
        char content[1200];
        snprintf(content, sizeof content, "%s\n%s", title ? title : "", description ? description : "");
        log_blocked(ctx->db, "goods", ctx->user_id, &h, content);
        return fail(ctx, ERR_BAD_REQUEST, "商品包含禁售内容（%s：「%s」），不能发布", category_label(h.category), h.word);
    }
    return OK;
}

int safety_check_profile(Ctx *ctx, const char *nickname, const char *bio) {
    const char *texts[2] = {nickname, bio};
    const char *names[2] = {"昵称", "简介"};
    for (int i = 0; i < 2; i++) {
        WordHit h;
        // 用私信那套一级词（脏话、色情、政治、违法），二级词不拦，交给 AI
        if (!texts[i] || !words_check(texts[i], SCOPE_CHAT, &h) || h.level != WORD_BLOCK) continue;
        log_blocked(ctx->db, "profile", ctx->user_id, &h, texts[i]);
        return fail(ctx, ERR_BAD_REQUEST, "%s包含违规内容（%s），请换一个", names[i], category_label(h.category));
    }
    return OK;
}

int safety_check_nickname_lock(Ctx *ctx) {
    sqlite3_stmt *st;
    int64_t until = 0;
    if (sqlite3_prepare_v2(ctx->db, "SELECT nickname_locked_until FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id);
    if (sqlite3_step(st) == SQLITE_ROW) until = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    int64_t left = until - now_sec();
    if (left <= 0) return OK;
    return fail(ctx, ERR_FORBIDDEN, "昵称因违规被重置，%lld 小时 %lld 分钟内不能修改", (long long)(left / 3600),
                (long long)((left % 3600 + 59) / 60));
}

bool safety_enqueue(sqlite3 *db, const char *kind, int64_t ref_id) {
    int64_t now = now_sec();
    return exec_bind(db, "INSERT INTO review_jobs(kind,ref_id,next_at,created_at) VALUES(?,?,?,?)", "siii", kind,
                     ref_id, now, now);
}

// ======================================================================
// 问 AI
// ======================================================================

// 私信：这一条消息本身是否明确违规（严格）
static const char *Q_MESSAGE =
    "{\"violation\":{\"type\":\"choice\","
    "\"instructions\":\"这是校园二手交易平台上的私信。判断 target_message 这一条消息本身是否包含明确的违规内容。"
    "conversation 只用来帮助理解语境。语气冲、不耐烦、阴阳怪气、讨价还价但没有明确辱骂或威胁，算 none。\","
    "\"criteria\":{\"none\":\"没有明确违规内容\",\"abuse\":\"人身攻击：辱骂、诅咒、歧视\","
    "\"porn\":\"色情低俗、性骚扰\",\"political\":\"政治敏感或违法内容（赌博、毒品等）\","
    "\"threat\":\"骚扰威胁：恐吓、威胁人身安全\",\"scam\":\"诈骗引流：诱导站外交易、私下转账、冒充客服\"}}}";

// 举报：举报理由描述的问题在【被举报】这条消息上是否存在（宽松，不友善也算）
static const char *Q_REASON =
    "{\"reason_holds\":{\"type\":\"noul\","
    "\"instructions\":\"举报人举报了对话中标着【被举报】的那一条消息，举报类型是 report_type，理由是 report_reason。"
    "只判断【被举报】这一条消息是否确实有举报理由描述的问题，其他消息只用来理解语境，不要因为别的消息有问题就算成立。"
    "这条消息不友善、阴阳怪气、不尊重人，也算成立。\","
    "\"criteria\":{\"true\":\"【被举报】的这条消息确实有举报理由描述的问题\","
    "\"false\":\"【被举报】的这条消息没有举报理由描述的问题，属于误报或恶意举报\"}}}";

// 商品：是否属于禁售
static const char *Q_GOODS =
    "{\"verdict\":{\"type\":\"choice\","
    "\"instructions\":\"这是校园二手交易平台上一件待上架的商品（title 标题、description 描述、category 分类）。"
    "判断它是否属于平台禁止发布的商品或信息。正常的二手物品（书、电子产品、衣服、生活用品等）算 ok。\","
    "\"criteria\":{\"ok\":\"正常的二手商品，可以上架\","
    "\"cheating\":\"代课代写：代课、代考、代写作业或论文、代签到、刷网课\","
    "\"contraband\":\"违禁品：管制刀具、枪支或仿真枪、药品、易燃易爆物\","
    "\"identity\":\"证件账号：身份证、学生证、校园卡、手机卡、银行卡、账号买卖\","
    "\"scam\":\"诈骗类：刷单兼职、高薪日结、校园贷、虚假商品\",\"porn\":\"色情低俗\",\"political\":\"政治敏感内容\"}}}";

// 昵称和简介
static const char *Q_PROFILE =
    "{\"violation\":{\"type\":\"choice\","
    "\"instructions\":\"这是校园二手交易平台上一个用户的昵称（nickname）和个人简介（bio，可能为空）。"
    "判断它们是否包含违规内容。正常的名字、外号、兴趣爱好、专业年级介绍都算 none。\","
    "\"criteria\":{\"none\":\"没有违规内容\",\"abuse\":\"辱骂、歧视、侮辱他人\",\"porn\":\"色情低俗\","
    "\"political\":\"政治敏感或违法内容\",\"scam\":\"广告引流：留微信号或 QQ 号、招兼职刷单、推销\"}}}";

static bool ask(const cJSON *state, const char *questions_json, cJSON **answers) {
    cJSON *q = cJSON_Parse(questions_json);
    char err[300] = "";
    bool ok = q && jev_ask(state, q, answers, err, sizeof err);
    if (!ok) log_warn("AI 审核调用失败：%s", err[0] ? err : "问题格式错误");
    cJSON_Delete(q);
    return ok;
}

// 读选择题答案：safe_key 是"没问题"那个选项。*p_bad = 1 - P(没问题)，label = 最可能的违规类别
static bool read_choice(const cJSON *answers, const char *qid, const char *safe_key, double *p_bad, char *label,
                        size_t label_size) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(answers, qid);
    const char *type = jx_str(a, "type");
    if (!cJSON_IsObject(a) || !type || strcmp(type, "choice") != 0) return false;
    const cJSON *probs = cJSON_GetObjectItemCaseSensitive(a, "probabilities");
    const char *choice = jx_str(a, "choice");
    label[0] = '\0';
    if (cJSON_IsObject(probs)) {
        double p_safe = 0, best = -1;
        const cJSON *it;
        cJSON_ArrayForEach(it, probs) {
            if (!cJSON_IsNumber(it)) continue;
            if (strcmp(it->string, safe_key) == 0) p_safe = it->valuedouble;
            else if (it->valuedouble > best) {
                best = it->valuedouble;
                snprintf(label, label_size, "%s", it->string);
            }
        }
        *p_bad = 1.0 - p_safe;
        return true;
    }
    if (!choice) return false;
    const cJSON *conf = cJSON_GetObjectItemCaseSensitive(a, "confidence");
    double c = cJSON_IsNumber(conf) ? conf->valuedouble : 1.0;
    bool safe = strcmp(choice, safe_key) == 0;
    *p_bad = safe ? 1.0 - c : c;
    if (!safe) snprintf(label, label_size, "%s", choice);
    return true;
}

static bool read_noul(const cJSON *answers, const char *qid, double *p) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(answers, qid);
    const char *type = jx_str(a, "type");
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, "noul");
    if (!type || strcmp(type, "noul") != 0 || !cJSON_IsNumber(v)) return false;
    *p = v->valuedouble;
    return true;
}

// 取被判断消息前后的对话（同一对用户之间），返回字符串数组，target 那条加标记
static cJSON *conversation(sqlite3 *db, int64_t msg_id, int64_t speaker, int64_t other, const char *speaker_label,
                           const char *other_label, const char *mark, bool with_after) {
    cJSON *lines = cJSON_CreateArray();
    static const char *SQL[2] = {
        "SELECT id,from_id,content,mod_state FROM messages WHERE ((from_id=? AND to_id=?) OR (from_id=? AND to_id=?))"
        " AND id<? ORDER BY id DESC LIMIT ?",
        "SELECT id,from_id,content,mod_state FROM messages WHERE ((from_id=? AND to_id=?) OR (from_id=? AND to_id=?))"
        " AND id>=? ORDER BY id ASC LIMIT ?",
    };
    for (int part = 0; part < 2; part++) {
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(db, SQL[part], -1, &st, NULL) != SQLITE_OK) continue;
        sqlite3_bind_int64(st, 1, speaker);
        sqlite3_bind_int64(st, 2, other);
        sqlite3_bind_int64(st, 3, other);
        sqlite3_bind_int64(st, 4, speaker);
        sqlite3_bind_int64(st, 5, msg_id);
        sqlite3_bind_int64(st, 6, part == 0 ? CONTEXT_MESSAGES : (with_after ? CONTEXT_MESSAGES + 1 : 1));
        int insert_at = cJSON_GetArraySize(lines);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int64_t id = sqlite3_column_int64(st, 0);
            bool target = id == msg_id;
            const char *who = sqlite3_column_int64(st, 1) == speaker ? speaker_label : other_label;
            const char *text = sqlite3_column_int(st, 3) == 2 && !target ? "[已屏蔽的消息]"
                                                                           : (const char *)sqlite3_column_text(st, 2);
            char line[2200];
            snprintf(line, sizeof line, "%s: %s%s", who, text, target ? mark : "");
            cJSON *item = cJSON_CreateString(line);
            if (part == 0) cJSON_InsertItemInArray(lines, insert_at, item);  // 倒序取的，插到前面恢复顺序
            else cJSON_AddItemToArray(lines, item);
        }
        sqlite3_finalize(st);
    }
    return lines;
}

// ======================================================================
// 处罚：屏蔽消息 + 禁言
// ======================================================================

typedef struct {
    int64_t id, from_id, to_id;
    int     mod_state;
    char    mod_kind[32];
    char   *content;
} Msg;

static bool load_message(sqlite3 *db, int64_t id, Msg *m) {
    memset(m, 0, sizeof *m);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT from_id,to_id,mod_state,mod_kind,content FROM messages WHERE id=?", -1, &st,
                           NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        m->id = id;
        m->from_id = sqlite3_column_int64(st, 0);
        m->to_id = sqlite3_column_int64(st, 1);
        m->mod_state = sqlite3_column_int(st, 2);
        snprintf(m->mod_kind, sizeof m->mod_kind, "%s", (const char *)sqlite3_column_text(st, 3));
        const char *c = (const char *)sqlite3_column_text(st, 4);
        m->content = (char *)malloc(strlen(c) + 1);
        if (m->content) strcpy(m->content, c);
    }
    sqlite3_finalize(st);
    return found && m->content;
}

static void push_masked(const Msg *m, bool masked) {
    cJSON *d = cJSON_CreateObject();
    jx_add_int(d, "message_id", m->id);
    jx_add_int(d, "from_id", m->from_id);
    jx_add_int(d, "to_id", m->to_id);
    jx_add_bool(d, "masked", masked);
    push_to_user(m->from_id, "chat.message_masked", d);
    push_to_user(m->to_id, "chat.message_masked", d);
    cJSON_Delete(d);
}

static void push_muted(sqlite3 *db, int64_t uid) {
    cJSON *d = cJSON_CreateObject();
    jx_add_int(d, "muted_until", user_muted_until(db, uid));
    push_to_user(uid, "user.muted", d);
    cJSON_Delete(d);
}

// 屏蔽消息并禁言发送者 seconds 秒（在原有禁言基础上取更晚的那个）
static bool punish(sqlite3 *db, const Msg *m, int64_t seconds) {
    int64_t until = now_sec() + seconds;
    if (!db_exec(db, "BEGIN IMMEDIATE")) return false;
    bool ok = exec_bind(db, "UPDATE messages SET mod_state=2 WHERE id=?", "i", m->id) &&
              exec_bind(db, "UPDATE users SET muted_until=MAX(muted_until,?) WHERE id=?", "ii", until, m->from_id);
    if (!ok || !db_exec(db, "COMMIT")) {
        db_exec(db, "ROLLBACK");
        return false;
    }
    push_masked(m, true);
    push_muted(db, m->from_id);
    return true;
}

// 撤销处罚：恢复消息、解除禁言
static bool unpunish(sqlite3 *db, const Msg *m) {
    if (!db_exec(db, "BEGIN IMMEDIATE")) return false;
    bool ok = exec_bind(db, "UPDATE messages SET mod_state=CASE WHEN mod_kind<>'' THEN 1 ELSE 0 END WHERE id=?", "i",
                        m->id) &&
              exec_bind(db, "UPDATE users SET muted_until=0 WHERE id=?", "i", m->from_id);
    if (!ok || !db_exec(db, "COMMIT")) {
        db_exec(db, "ROLLBACK");
        return false;
    }
    push_masked(m, false);
    push_muted(db, m->from_id);
    return true;
}

// ======================================================================
// 审核任务
// ======================================================================

enum { JOB_DONE, JOB_RETRY };

static void push_goods_reviewed(int64_t seller, int64_t goods_id, const char *status) {
    cJSON *d = cJSON_CreateObject();
    jx_add_int(d, "goods_id", goods_id);
    jx_add_str(d, "status", status);
    push_to_user(seller, "goods.reviewed", d);
    cJSON_Delete(d);
}

// 审核中的商品先放出去（AI 没把握或调用失败时）
static void release_goods(sqlite3 *db, int64_t goods_id, int64_t seller) {
    if (exec_bind(db, "UPDATE goods SET status='on_sale',updated_at=? WHERE id=? AND status='reviewing'", "ii",
                  now_sec(), goods_id) &&
        sqlite3_changes(db) > 0)
        push_goods_reviewed(seller, goods_id, "on_sale");
}

// 商品的违规类别名（和私信的叫法稍有不同）
static const char *goods_label(const char *code) {
    if (strcmp(code, "scam") == 0) return "诈骗类（刷单兼职、校园贷等）";
    if (strcmp(code, "political") == 0) return "政治敏感";
    return category_label(code);
}

static int job_goods(sqlite3 *db, int64_t goods_id, bool last_try) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,
                           "SELECT g.title,g.description,c.name,g.status,g.seller_id FROM goods g "
                           "JOIN categories c ON c.id=g.category_id WHERE g.id=?",
                           -1, &st, NULL) != SQLITE_OK)
        return JOB_RETRY;
    sqlite3_bind_int64(st, 1, goods_id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return JOB_DONE;
    }
    cJSON *state = cJSON_CreateObject();
    jx_add_col_str(state, "title", st, 0);
    jx_add_col_str(state, "description", st, 1);
    jx_add_col_str(state, "category", st, 2);
    char status[16], title[200];
    snprintf(status, sizeof status, "%s", (const char *)sqlite3_column_text(st, 3));
    snprintf(title, sizeof title, "%s", (const char *)sqlite3_column_text(st, 0));
    int64_t seller = sqlite3_column_int64(st, 4);
    sqlite3_finalize(st);
    if (strcmp(status, "sold") == 0) {  // 已经卖掉了，不再处理
        cJSON_Delete(state);
        return JOB_DONE;
    }

    cJSON *answers = NULL;
    bool ok = ask(state, Q_GOODS, &answers);
    cJSON_Delete(state);
    double p_bad;
    char label[32];
    if (ok) ok = read_choice(answers, "verdict", "ok", &p_bad, label, sizeof label);
    cJSON_Delete(answers);

    char summary[400];
    if (!ok) {
        release_goods(db, goods_id, seller);  // 调用失败：先放出去，稍后重试
        if (!last_try) return JOB_RETRY;
        snprintf(summary, sizeof summary, "商品《%s》AI 审核多次失败，已先上架，请人工复核", title);
        add_case(db, "manual", "goods", goods_id, seller, summary, "", -1);
        return JOB_DONE;
    }
    if (p_bad >= AI_SURE) {
        char reason[128];
        snprintf(reason, sizeof reason, "AI 审核判定为%s", goods_label(label));
        exec_bind(db,
                  "UPDATE goods SET status='rejected',reject_reason=?,updated_at=? WHERE id=? AND status<>'sold'",
                  "sii", reason, now_sec(), goods_id);
        snprintf(summary, sizeof summary, "商品《%s》AI 判定为%s（概率 %.2f），已驳回下架", title,
                 goods_label(label), p_bad);
        add_case(db, "auto", "goods", goods_id, seller, summary, label, p_bad);
    } else if (1.0 - p_bad >= AI_SURE) {
        release_goods(db, goods_id, seller);
    } else {
        release_goods(db, goods_id, seller);
        snprintf(summary, sizeof summary, "商品《%s》AI 没把握（违规概率 %.2f，最可能是%s），已先上架，请人工复核", title,
                 p_bad, goods_label(label));
        add_case(db, "manual", "goods", goods_id, seller, summary, label, p_bad);
    }
    return JOB_DONE;
}

// 二级词命中的私信：AI 有把握判违规，就和举报成立一样处理
static int job_chat(sqlite3 *db, int64_t message_id, bool last_try) {
    Msg m;
    if (!load_message(db, message_id, &m)) return JOB_DONE;
    if (m.mod_state == 2) {
        free(m.content);
        return JOB_DONE;
    }
    cJSON *state = cJSON_CreateObject();
    cJSON_AddItemToObject(state, "conversation",
                          conversation(db, m.id, m.from_id, m.to_id, "发送者", "接收者", "  【待判断】", false));
    jx_add_str(state, "target_message", m.content);
    cJSON *answers = NULL;
    bool ok = ask(state, Q_MESSAGE, &answers);
    cJSON_Delete(state);
    double p;
    char label[32];
    if (ok) ok = read_choice(answers, "violation", "none", &p, label, sizeof label);
    cJSON_Delete(answers);
    if (!ok) {
        free(m.content);
        return last_try ? JOB_DONE : JOB_RETRY;  // 二级词只是提醒，AI 一直失败就算了
    }
    if (p >= AI_SURE && punish(db, &m, MUTE_SECONDS)) {
        char summary[400];
        snprintf(summary, sizeof summary, "私信命中二级词（%s），AI 判定为%s（概率 %.2f），已屏蔽并禁言 12 小时",
                 category_label(m.mod_kind), category_label(label), p);
        add_case(db, "auto", "chat", m.id, m.from_id, summary, label, p);
    }
    free(m.content);
    return JOB_DONE;
}

// 重置昵称和简介，并锁定昵称 24 小时。
// expect_nick 不为 NULL 时，只有昵称和简介还和 AI 判断时一样才重置（用户已经改掉了就不动，新内容会有新的审核任务）
static bool reset_profile(sqlite3 *db, int64_t uid, const char *expect_nick, const char *expect_bio, const char *reason) {
    char nick[64];
    snprintf(nick, sizeof nick, "小鱼用户%lld", (long long)uid);
    int64_t until = now_sec() + NICKNAME_LOCK;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,
                           "UPDATE users SET nickname=?1,bio='',nickname_locked_until=?2 WHERE id=?3 AND "
                           "(?4 IS NULL OR (nickname=?4 AND bio=?5))",
                           -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, nick, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, until);
    sqlite3_bind_int64(st, 3, uid);
    if (expect_nick) {
        sqlite3_bind_text(st, 4, expect_nick, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 5, expect_bio ? expect_bio : "", -1, SQLITE_STATIC);
    }
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE || sqlite3_changes(db) == 0) return false;
    cJSON *d = cJSON_CreateObject();
    jx_add_str(d, "nickname", nick);
    jx_add_str(d, "reason", reason);
    jx_add_int(d, "nickname_locked_until", until);
    push_to_user(uid, "user.profile_reset", d);
    cJSON_Delete(d);
    return true;
}

// 昵称、简介：AI 有把握判违规 → 重置并锁 24 小时，进已自动处理区；没把握 → 人工
static int job_profile(sqlite3 *db, int64_t uid, bool last_try) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT nickname,bio FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK) return JOB_RETRY;
    sqlite3_bind_int64(st, 1, uid);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return JOB_DONE;
    }
    char nick[128], bio[512];
    snprintf(nick, sizeof nick, "%s", (const char *)sqlite3_column_text(st, 0));
    snprintf(bio, sizeof bio, "%s", (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);

    cJSON *state = cJSON_CreateObject();
    jx_add_str(state, "nickname", nick);
    jx_add_str(state, "bio", bio);
    cJSON *answers = NULL;
    bool ok = ask(state, Q_PROFILE, &answers);
    cJSON_Delete(state);
    double p;
    char label[32];
    if (ok) ok = read_choice(answers, "violation", "none", &p, label, sizeof label);
    cJSON_Delete(answers);

    char summary[900];
    if (!ok) {
        if (!last_try) return JOB_RETRY;
        snprintf(summary, sizeof summary, "昵称「%s」/ 简介「%s」AI 审核多次失败，请人工复核", nick, bio);
        add_case(db, "manual", "profile", uid, uid, summary, "", -1);
        return JOB_DONE;
    }
    if (p >= AI_SURE) {
        char reason[128];
        snprintf(reason, sizeof reason, "AI 审核判定昵称或简介含%s", category_label(label));
        if (reset_profile(db, uid, nick, bio, reason)) {
            snprintf(summary, sizeof summary,
                     "昵称「%s」/ 简介「%s」AI 判定为%s（概率 %.2f），已重置昵称、清空简介，24 小时内不能改昵称", nick, bio,
                     category_label(label), p);
            add_case(db, "auto", "profile", uid, uid, summary, label, p);
        }
    } else if (p > 1.0 - AI_SURE) {
        snprintf(summary, sizeof summary, "昵称「%s」/ 简介「%s」AI 没把握（违规概率 %.2f，最可能是%s），请人工复核", nick,
                 bio, p, category_label(label));
        add_case(db, "manual", "profile", uid, uid, summary, label, p);
    }
    return JOB_DONE;
}

static void push_report_result(int64_t reporter, int64_t report_id, const char *status) {
    cJSON *d = cJSON_CreateObject();
    jx_add_int(d, "report_id", report_id);
    jx_add_str(d, "status", status);
    push_to_user(reporter, "report.result", d);
    cJSON_Delete(d);
}

static const char *report_type_label(const char *type) {
    return strcmp(type, "other") == 0 ? "其他" : category_label(type);
}

// 举报：两次独立的 AI 判断
//   第 1 次（严）：这条消息本身是否明确违规 —— 只有它能触发自动禁言
//   第 2 次（宽）：举报理由描述的问题是否存在 —— 不友善也算
static int job_report(sqlite3 *db, int64_t report_id, bool last_try) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT reporter_id,message_id,type,reason,status FROM reports WHERE id=?", -1, &st,
                           NULL) != SQLITE_OK)
        return JOB_RETRY;
    sqlite3_bind_int64(st, 1, report_id);
    if (sqlite3_step(st) != SQLITE_ROW || strcmp((const char *)sqlite3_column_text(st, 4), "pending") != 0) {
        sqlite3_finalize(st);
        return JOB_DONE;
    }
    int64_t reporter = sqlite3_column_int64(st, 0), message_id = sqlite3_column_int64(st, 1);
    char type[16], reason[800];
    snprintf(type, sizeof type, "%s", (const char *)sqlite3_column_text(st, 2));
    snprintf(reason, sizeof reason, "%s", (const char *)sqlite3_column_text(st, 3));
    sqlite3_finalize(st);

    Msg m;
    if (!load_message(db, message_id, &m)) return JOB_DONE;
    char summary[1200];
    if (m.mod_state == 2) {  // 已经被别的举报处理掉了
        exec_bind(db, "UPDATE reports SET status='upheld',handled_at=? WHERE id=?", "ii", now_sec(), report_id);
        push_report_result(reporter, report_id, "upheld");
        free(m.content);
        return JOB_DONE;
    }

    cJSON *conv = conversation(db, m.id, m.from_id, m.to_id, "被举报人", "举报人", "  【被举报】", true);
    cJSON *s1 = cJSON_CreateObject();
    cJSON_AddItemToObject(s1, "conversation", cJSON_Duplicate(conv, 1));
    jx_add_str(s1, "target_message", m.content);
    cJSON *s2 = cJSON_CreateObject();
    cJSON_AddItemToObject(s2, "conversation", conv);
    jx_add_str(s2, "report_type", report_type_label(type));
    jx_add_str(s2, "report_reason", reason);

    cJSON *a1 = NULL, *a2 = NULL;
    double pv = 0, pr = 0;
    char label[32] = "";
    bool ok = ask(s1, Q_MESSAGE, &a1) && read_choice(a1, "violation", "none", &pv, label, sizeof label) &&
              ask(s2, Q_REASON, &a2) && read_noul(a2, "reason_holds", &pr);
    cJSON_Delete(s1);
    cJSON_Delete(s2);
    cJSON_Delete(a1);
    cJSON_Delete(a2);

    if (!ok) {
        if (!last_try) {
            free(m.content);
            return JOB_RETRY;
        }
        exec_bind(db, "UPDATE reports SET status='manual' WHERE id=?", "i", report_id);
        snprintf(summary, sizeof summary, "举报（%s：%s）AI 审核多次失败，请人工判断", report_type_label(type), reason);
        add_case(db, "manual", "report", report_id, m.from_id, summary, "", -1);
        push_report_result(reporter, report_id, "manual");
        free(m.content);
        return JOB_DONE;
    }

    exec_bind(db, "UPDATE reports SET ai_violation=?,ai_category=?,ai_reason=? WHERE id=?", "dsdi", pv, label, pr,
              report_id);
    const char *result;
    if (pv >= AI_SURE && punish(db, &m, MUTE_SECONDS)) {
        result = "upheld";
        snprintf(summary, sizeof summary, "AI 判定言论为%s（概率 %.2f），举报成立：已屏蔽消息并禁言 12 小时",
                 category_label(label), pv);
        add_case(db, "auto", "report", report_id, m.from_id, summary, label, pv);
    } else if (pv <= 1.0 - AI_SURE && pr <= 1.0 - AI_SURE) {
        result = "rejected";
        snprintf(summary, sizeof summary, "AI 判定言论正常（违规概率 %.2f），举报理由不成立（%.2f），已自动驳回", pv,
                 pr);
        add_case(db, "auto", "report", report_id, m.from_id, summary, "none", pv);
    } else {
        result = "manual";
        if (pr >= AI_SURE)
            snprintf(summary, sizeof summary,
                     "AI 判定举报理由成立（%.2f），但言论没有明确违规（违规概率 %.2f），请人工判断", pr, pv);
        else
            snprintf(summary, sizeof summary, "AI 没把握（言论违规概率 %.2f，举报理由成立 %.2f），请人工判断", pv, pr);
        add_case(db, "manual", "report", report_id, m.from_id, summary, label, pv);
    }
    exec_bind(db, "UPDATE reports SET status=?,handled_at=? WHERE id=?", "sii", result,
              strcmp(result, "manual") == 0 ? 0 : now_sec(), report_id);
    push_report_result(reporter, report_id, result);
    free(m.content);
    return JOB_DONE;
}

int review_run_once(sqlite3 *db) {
    typedef struct {
        int64_t id, ref_id, attempts;
        char    kind[16];
    } Job;
    Job jobs[16];
    int n = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT id,kind,ref_id,attempts FROM review_jobs WHERE next_at<=? ORDER BY id LIMIT 16",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int64(st, 1, now_sec());
    while (n < 16 && sqlite3_step(st) == SQLITE_ROW) {
        jobs[n].id = sqlite3_column_int64(st, 0);
        snprintf(jobs[n].kind, sizeof jobs[n].kind, "%s", (const char *)sqlite3_column_text(st, 1));
        jobs[n].ref_id = sqlite3_column_int64(st, 2);
        jobs[n].attempts = sqlite3_column_int64(st, 3);
        n++;
    }
    sqlite3_finalize(st);

    for (int i = 0; i < n; i++) {
        Job *j = &jobs[i];
        bool last_try = j->attempts + 1 >= JOB_MAX_ATTEMPTS;
        int r = JOB_DONE;
        if (strcmp(j->kind, "goods") == 0) r = job_goods(db, j->ref_id, last_try);
        else if (strcmp(j->kind, "chat") == 0) r = job_chat(db, j->ref_id, last_try);
        else if (strcmp(j->kind, "report") == 0) r = job_report(db, j->ref_id, last_try);
        else if (strcmp(j->kind, "profile") == 0) r = job_profile(db, j->ref_id, last_try);
        if (r == JOB_DONE)
            exec_bind(db, "DELETE FROM review_jobs WHERE id=?", "i", j->id);
        else  // 30 秒、60 秒、90 秒……后重试
            exec_bind(db, "UPDATE review_jobs SET attempts=attempts+1,next_at=? WHERE id=?", "ii",
                      now_sec() + 30 * (j->attempts + 1), j->id);
    }
    return n;
}

static char g_db_path[256];

static void review_thread(void *arg) {
    (void)arg;
    sqlite3 *db = db_open(g_db_path);
    if (!db) {
        log_error("审核线程打不开数据库，AI 审核停止");
        return;
    }
    log_info("AI 审核线程已启动");
    for (;;) {
        if (review_run_once(db) == 0) sleep_ms(1000);
    }
}

void review_start(const char *db_path) {
    snprintf(g_db_path, sizeof g_db_path, "%s", db_path);
    if (!thread_start(review_thread, NULL)) log_error("开审核线程失败");
}

// ======================================================================
// 用户命令：举报、申诉
// ======================================================================

static bool valid_report_type(const char *t) {
    static const char *TYPES[] = {"abuse", "porn", "political", "threat", "scam", "other"};
    for (size_t i = 0; t && i < sizeof TYPES / sizeof TYPES[0]; i++)
        if (strcmp(TYPES[i], t) == 0) return true;
    return false;
}

int report_create(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t message_id;
    const char *type = jx_str(req, "type"), *reason = jx_str(req, "reason");
    if (!jx_int(req, "message_id", &message_id) || message_id < 1)
        return fail(ctx, ERR_BAD_REQUEST, "消息编号须为正整数");
    if (!valid_report_type(type)) return fail(ctx, ERR_BAD_REQUEST, "请选择举报类型");
    if (!utf8_valid(reason, 1, 200)) return fail(ctx, ERR_BAD_REQUEST, "举报理由须为 1–200 个字");

    Msg m;
    if (!load_message(ctx->db, message_id, &m)) return fail(ctx, ERR_NOT_FOUND, "消息不存在");
    int64_t from = m.from_id, to = m.to_id;
    int mod_state = m.mod_state;
    free(m.content);
    if (to != ctx->user_id) return fail(ctx, ERR_FORBIDDEN, "只能举报别人发给你的消息");
    if (mod_state == 2) return fail(ctx, ERR_CONFLICT, "这条消息已经被处理了");

    bool ai = safety_ai_enabled();
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
                           "INSERT INTO reports(reporter_id,reported_id,message_id,type,reason,status,created_at)"
                           " VALUES(?,?,?,?,?,?,?)",
                           -1, &st, NULL) != SQLITE_OK) {
        db_exec(ctx->db, "ROLLBACK");
        return db_fail(ctx);
    }
    sqlite3_bind_int64(st, 1, ctx->user_id);
    sqlite3_bind_int64(st, 2, from);
    sqlite3_bind_int64(st, 3, message_id);
    sqlite3_bind_text(st, 4, type, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, reason, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, ai ? "pending" : "manual", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 7, now_sec());
    int rc = sqlite3_step(st), ext = sqlite3_extended_errcode(ctx->db);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        db_exec(ctx->db, "ROLLBACK");
        if (ext == SQLITE_CONSTRAINT_UNIQUE) return fail(ctx, ERR_CONFLICT, "你已经举报过这条消息了");
        return db_fail(ctx);
    }
    int64_t report_id = sqlite3_last_insert_rowid(ctx->db);
    bool ok;
    if (ai) {
        ok = safety_enqueue(ctx->db, "report", report_id);
    } else {
        char summary[400];
        snprintf(summary, sizeof summary, "举报（%s：%s），未开启 AI 审核，请人工判断", report_type_label(type), reason);
        ok = add_case(ctx->db, "manual", "report", report_id, from, summary, "", -1) > 0;
    }
    if (!ok || !db_exec(ctx->db, "COMMIT")) {
        db_exec(ctx->db, "ROLLBACK");
        return db_fail(ctx);
    }
    jx_add_int(resp, "report_id", report_id);
    jx_add_str(resp, "status", ai ? "pending" : "manual");
    return OK;
}

int appeal_create(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *kind = jx_str(req, "kind"), *content = jx_str(req, "content");
    int64_t goods_id = 0;
    if (!kind || (strcmp(kind, "mute") != 0 && strcmp(kind, "goods") != 0))
        return fail(ctx, ERR_BAD_REQUEST, "申诉类型须为 mute 或 goods");
    if (!utf8_valid(content, 1, 200)) return fail(ctx, ERR_BAD_REQUEST, "申诉内容须为 1–200 个字");

    char summary[600], title[200] = "";
    if (strcmp(kind, "mute") == 0) {
        if (user_muted_until(ctx->db, ctx->user_id) <= now_sec()) return fail(ctx, ERR_CONFLICT, "你现在没有被禁言");
        snprintf(summary, sizeof summary, "申诉禁言：%s", content);
    } else {
        if (!jx_int(req, "goods_id", &goods_id) || goods_id < 1) return fail(ctx, ERR_BAD_REQUEST, "商品编号须为正整数");
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(ctx->db, "SELECT seller_id,status,title FROM goods WHERE id=?", -1, &st, NULL) !=
            SQLITE_OK)
            return db_fail(ctx);
        sqlite3_bind_int64(st, 1, goods_id);
        int rc = sqlite3_step(st);
        int64_t seller = rc == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
        bool rejected = rc == SQLITE_ROW && strcmp((const char *)sqlite3_column_text(st, 1), "rejected") == 0;
        if (rc == SQLITE_ROW) snprintf(title, sizeof title, "%s", (const char *)sqlite3_column_text(st, 2));
        sqlite3_finalize(st);
        if (rc != SQLITE_ROW) return fail(ctx, ERR_NOT_FOUND, "商品不存在");
        if (seller != ctx->user_id) return fail(ctx, ERR_FORBIDDEN, "只能申诉自己的商品");
        if (!rejected) return fail(ctx, ERR_CONFLICT, "只有被驳回的商品可以申诉");
        snprintf(summary, sizeof summary, "申诉商品《%s》被驳回：%s", title, content);
    }

    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
                           "SELECT COUNT(*) FROM appeals WHERE user_id=? AND kind=? AND goods_id=? AND status='open'",
                           -1, &st, NULL) != SQLITE_OK) {
        db_exec(ctx->db, "ROLLBACK");
        return db_fail(ctx);
    }
    sqlite3_bind_int64(st, 1, ctx->user_id);
    sqlite3_bind_text(st, 2, kind, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, goods_id);
    int open = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    if (open > 0) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_CONFLICT, "已经提交过申诉，请等待处理");
    }
    bool ok = exec_bind(ctx->db, "INSERT INTO appeals(user_id,kind,goods_id,content,created_at) VALUES(?,?,?,?,?)",
                        "isisi", ctx->user_id, kind, goods_id, content, now_sec());
    int64_t appeal_id = sqlite3_last_insert_rowid(ctx->db);
    ok = ok && add_case(ctx->db, "manual", "appeal", appeal_id, ctx->user_id, summary, "", -1) > 0;
    if (!ok || !db_exec(ctx->db, "COMMIT")) {
        db_exec(ctx->db, "ROLLBACK");
        return db_fail(ctx);
    }
    jx_add_int(resp, "appeal_id", appeal_id);
    return OK;
}

// ======================================================================
// 管理员命令
// ======================================================================

static int64_t count_of(sqlite3 *db, const char *sql) {
    sqlite3_stmt *st;
    int64_t n = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, now_sec());  // 用不到参数的 SQL 绑定会被忽略
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

int admin_stats(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)req;
    jx_add_int(resp, "manual_open", count_of(ctx->db, "SELECT COUNT(*) FROM mod_cases WHERE area='manual' AND status='open'"));
    jx_add_int(resp, "auto_open", count_of(ctx->db, "SELECT COUNT(*) FROM mod_cases WHERE area='auto' AND status='open'"));
    jx_add_int(resp, "reviewing_goods", count_of(ctx->db, "SELECT COUNT(*) FROM goods WHERE status='reviewing'"));
    jx_add_int(resp, "rejected_goods", count_of(ctx->db, "SELECT COUNT(*) FROM goods WHERE status='rejected'"));
    jx_add_int(resp, "muted_users", count_of(ctx->db, "SELECT COUNT(*) FROM users WHERE muted_until>?"));
    jx_add_int(resp, "blocked_total", count_of(ctx->db, "SELECT COUNT(*) FROM blocked_logs"));
    jx_add_int(resp, "reports_total", count_of(ctx->db, "SELECT COUNT(*) FROM reports"));
    jx_add_int(resp, "pending_jobs", count_of(ctx->db, "SELECT COUNT(*) FROM review_jobs"));
    jx_add_bool(resp, "ai_enabled", safety_ai_enabled());
    return OK;
}

static void write_case(sqlite3 *db, sqlite3_stmt *st, cJSON *o) {
    jx_add_col_int(o, "case_id", st, 0);
    jx_add_col_str(o, "area", st, 1);
    jx_add_col_str(o, "kind", st, 2);
    jx_add_col_int(o, "ref_id", st, 3);
    add_user_brief(db, o, "user", sqlite3_column_int64(st, 4));
    jx_add_col_str(o, "summary", st, 5);
    jx_add_col_str(o, "ai_label", st, 6);
    cJSON_AddNumberToObject(o, "ai_score", sqlite3_column_double(st, 7));
    jx_add_col_str(o, "status", st, 8);
    jx_add_col_str(o, "result", st, 9);
    jx_add_col_int(o, "created_at", st, 10);
    jx_add_col_int(o, "handled_at", st, 11);
}

#define CASE_COLUMNS "id,area,kind,ref_id,user_id,summary,ai_label,ai_score,status,result,created_at,handled_at"

int admin_cases(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t page, size;
    if (!read_paging(req, &page, &size)) return fail(ctx, ERR_BAD_REQUEST, "分页参数不正确");
    const char *area = jx_str(req, "area"), *status = jx_str(req, "status");
    if (jx_has(req, "area") && (!area || (strcmp(area, "auto") != 0 && strcmp(area, "manual") != 0)))
        return fail(ctx, ERR_BAD_REQUEST, "area 须为 auto 或 manual");
    if (jx_has(req, "status") && (!status || (strcmp(status, "open") != 0 && strcmp(status, "closed") != 0)))
        return fail(ctx, ERR_BAD_REQUEST, "status 须为 open 或 closed");
    const char *where = " WHERE (?1 IS NULL OR area=?1) AND (?2 IS NULL OR status=?2)";
    char sql[512];
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM mod_cases%s", where);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, area, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, status, -1, SQLITE_STATIC);
    int64_t total = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    cJSON *items = add_page(resp, total, page, size);
    snprintf(sql, sizeof sql, "SELECT " CASE_COLUMNS " FROM mod_cases%s ORDER BY id DESC LIMIT ?3 OFFSET ?4", where);
    if (sqlite3_prepare_v2(ctx->db, sql, -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, area, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, status, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, size);
    sqlite3_bind_int64(st, 4, (page - 1) * size);
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *o = cJSON_CreateObject();
        write_case(ctx->db, st, o);
        cJSON_AddItemToArray(items, o);
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

typedef struct {
    int64_t id, ref_id, user_id;
    char    kind[16], status[16];
} Case;

static int load_case(Ctx *ctx, const cJSON *req, Case *c) {
    if (!jx_int(req, "case_id", &c->id) || c->id < 1) return fail(ctx, ERR_BAD_REQUEST, "案件编号须为正整数");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT kind,ref_id,user_id,status FROM mod_cases WHERE id=?", -1, &st, NULL) !=
        SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, c->id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        snprintf(c->kind, sizeof c->kind, "%s", (const char *)sqlite3_column_text(st, 0));
        c->ref_id = sqlite3_column_int64(st, 1);
        c->user_id = sqlite3_column_int64(st, 2);
        snprintf(c->status, sizeof c->status, "%s", (const char *)sqlite3_column_text(st, 3));
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "案件不存在");
    return rc == SQLITE_ROW ? OK : db_fail(ctx);
}

// 一条消息和它前后的对话（后台展示用，屏蔽的消息也显示原文）
static void add_message_detail(sqlite3 *db, cJSON *out, int64_t message_id) {
    Msg m;
    if (!load_message(db, message_id, &m)) return;
    cJSON *msg = cJSON_AddObjectToObject(out, "message");
    jx_add_int(msg, "message_id", m.id);
    jx_add_int(msg, "from_id", m.from_id);
    jx_add_int(msg, "to_id", m.to_id);
    jx_add_str(msg, "content", m.content);
    jx_add_str(msg, "flag", m.mod_state == 2 ? "masked" : m.mod_state == 1 ? "warn" : "none");
    jx_add_str(msg, "flag_kind", m.mod_kind);
    add_user_brief(db, out, "sender", m.from_id);
    add_user_brief(db, out, "receiver", m.to_id);
    jx_add_int(out, "sender_muted_until", user_muted_until(db, m.from_id));

    cJSON *ctx_arr = cJSON_AddArrayToObject(out, "context");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,
                           "SELECT id,from_id,content,mod_state,created_at FROM ("
                           " SELECT * FROM messages WHERE ((from_id=?1 AND to_id=?2) OR (from_id=?2 AND to_id=?1))"
                           " AND id<?3 ORDER BY id DESC LIMIT 5) "
                           "UNION ALL SELECT id,from_id,content,mod_state,created_at FROM ("
                           " SELECT * FROM messages WHERE ((from_id=?1 AND to_id=?2) OR (from_id=?2 AND to_id=?1))"
                           " AND id>=?3 ORDER BY id ASC LIMIT 6) ORDER BY id",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, m.from_id);
        sqlite3_bind_int64(st, 2, m.to_id);
        sqlite3_bind_int64(st, 3, m.id);
        while (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *o = cJSON_CreateObject();
            jx_add_col_int(o, "message_id", st, 0);
            jx_add_col_int(o, "from_id", st, 1);
            jx_add_col_str(o, "content", st, 2);
            jx_add_bool(o, "masked", sqlite3_column_int(st, 3) == 2);
            jx_add_col_int(o, "created_at", st, 4);
            jx_add_bool(o, "is_target", sqlite3_column_int64(st, 0) == m.id);
            cJSON_AddItemToArray(ctx_arr, o);
        }
        sqlite3_finalize(st);
    }
    free(m.content);
}

static void add_goods_detail(sqlite3 *db, cJSON *out, int64_t goods_id) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,
                           "SELECT g.id,g.title,g.description,g.price,g.status,g.reject_reason,c.name,g.seller_id,"
                           "g.created_at FROM goods g JOIN categories c ON c.id=g.category_id WHERE g.id=?",
                           -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_int64(st, 1, goods_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        cJSON *g = cJSON_AddObjectToObject(out, "goods");
        jx_add_col_int(g, "goods_id", st, 0);
        jx_add_col_str(g, "title", st, 1);
        jx_add_col_str(g, "description", st, 2);
        jx_add_col_int(g, "price", st, 3);
        jx_add_col_str(g, "status", st, 4);
        jx_add_col_str(g, "reject_reason", st, 5);
        jx_add_col_str(g, "category_name", st, 6);
        jx_add_col_int(g, "created_at", st, 8);
        add_user_brief(db, g, "seller", sqlite3_column_int64(st, 7));
        cJSON *imgs = cJSON_AddArrayToObject(g, "images");
        sqlite3_stmt *s2;
        if (sqlite3_prepare_v2(db, "SELECT image FROM goods_images WHERE goods_id=? ORDER BY sort", -1, &s2, NULL) ==
            SQLITE_OK) {
            sqlite3_bind_int64(s2, 1, goods_id);
            while (sqlite3_step(s2) == SQLITE_ROW)
                cJSON_AddItemToArray(imgs, cJSON_CreateString((const char *)sqlite3_column_text(s2, 0)));
            sqlite3_finalize(s2);
        }
    }
    sqlite3_finalize(st);
}

int admin_case_detail(Ctx *ctx, const cJSON *req, cJSON *resp) {
    Case c;
    int r = load_case(ctx, req, &c);
    if (r != OK) return r;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT " CASE_COLUMNS " FROM mod_cases WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, c.id);
    if (sqlite3_step(st) == SQLITE_ROW) write_case(ctx->db, st, cJSON_AddObjectToObject(resp, "case"));
    sqlite3_finalize(st);

    if (strcmp(c.kind, "goods") == 0) {
        add_goods_detail(ctx->db, resp, c.ref_id);
    } else if (strcmp(c.kind, "chat") == 0) {
        add_message_detail(ctx->db, resp, c.ref_id);
    } else if (strcmp(c.kind, "report") == 0) {
        if (sqlite3_prepare_v2(ctx->db,
                               "SELECT id,reporter_id,message_id,type,reason,status,ai_violation,ai_category,ai_reason,"
                               "created_at FROM reports WHERE id=?",
                               -1, &st, NULL) != SQLITE_OK)
            return db_fail(ctx);
        sqlite3_bind_int64(st, 1, c.ref_id);
        int64_t message_id = 0;
        if (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *rp = cJSON_AddObjectToObject(resp, "report");
            jx_add_col_int(rp, "report_id", st, 0);
            add_user_brief(ctx->db, rp, "reporter", sqlite3_column_int64(st, 1));
            message_id = sqlite3_column_int64(st, 2);
            jx_add_col_str(rp, "type", st, 3);
            jx_add_str(rp, "type_label", report_type_label((const char *)sqlite3_column_text(st, 3)));
            jx_add_col_str(rp, "reason", st, 4);
            jx_add_col_str(rp, "status", st, 5);
            cJSON_AddNumberToObject(rp, "ai_violation", sqlite3_column_double(st, 6));
            jx_add_col_str(rp, "ai_category", st, 7);
            cJSON_AddNumberToObject(rp, "ai_reason", sqlite3_column_double(st, 8));
            jx_add_col_int(rp, "created_at", st, 9);
        }
        sqlite3_finalize(st);
        if (message_id) add_message_detail(ctx->db, resp, message_id);
    } else if (strcmp(c.kind, "profile") == 0) {
        if (sqlite3_prepare_v2(ctx->db,
                               "SELECT id,username,nickname,bio,email,muted_until,nickname_locked_until,created_at "
                               "FROM users WHERE id=?",
                               -1, &st, NULL) != SQLITE_OK)
            return db_fail(ctx);
        sqlite3_bind_int64(st, 1, c.ref_id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *u = cJSON_AddObjectToObject(resp, "profile");
            jx_add_col_int(u, "user_id", st, 0);
            jx_add_col_str(u, "username", st, 1);
            jx_add_col_str(u, "nickname", st, 2);
            jx_add_col_str(u, "bio", st, 3);
            jx_add_col_str(u, "email", st, 4);
            jx_add_col_int(u, "muted_until", st, 5);
            jx_add_col_int(u, "nickname_locked_until", st, 6);
            jx_add_col_int(u, "created_at", st, 7);
        }
        sqlite3_finalize(st);
    } else if (strcmp(c.kind, "appeal") == 0) {
        if (sqlite3_prepare_v2(ctx->db, "SELECT id,kind,goods_id,content,status,created_at FROM appeals WHERE id=?", -1,
                               &st, NULL) != SQLITE_OK)
            return db_fail(ctx);
        sqlite3_bind_int64(st, 1, c.ref_id);
        int64_t goods_id = 0;
        if (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *ap = cJSON_AddObjectToObject(resp, "appeal");
            jx_add_col_int(ap, "appeal_id", st, 0);
            jx_add_col_str(ap, "kind", st, 1);
            goods_id = sqlite3_column_int64(st, 2);
            jx_add_col_int(ap, "goods_id", st, 2);
            jx_add_col_str(ap, "content", st, 3);
            jx_add_col_str(ap, "status", st, 4);
            jx_add_col_int(ap, "created_at", st, 5);
        }
        sqlite3_finalize(st);
        jx_add_int(resp, "user_muted_until", user_muted_until(ctx->db, c.user_id));
        if (goods_id) add_goods_detail(ctx->db, resp, goods_id);
    }
    return OK;
}

// 给商品"驳回"或"恢复上架"
static bool set_goods_rejected(sqlite3 *db, int64_t goods_id, bool reject, const char *reason) {
    if (reject)
        return exec_bind(db, "UPDATE goods SET status='rejected',reject_reason=?,updated_at=? WHERE id=? AND status<>'sold'",
                         "sii", reason, now_sec(), goods_id);
    return exec_bind(db,
                     "UPDATE goods SET status='on_sale',reject_reason='',updated_at=? WHERE id=? AND status IN "
                     "('rejected','reviewing')",
                     "ii", now_sec(), goods_id);
}

static int64_t report_message(sqlite3 *db, int64_t report_id) {
    sqlite3_stmt *st;
    int64_t id = 0;
    if (sqlite3_prepare_v2(db, "SELECT message_id FROM reports WHERE id=?", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, report_id);
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return id;
}

// admin.resolve：人工处理一个案件
//   通用      close   维持现状，结案
//   举报/私信 punish  屏蔽消息 + 禁言 hours 小时（默认 12）
//             revert  恢复消息 + 解除禁言（撤销自动处理）
//   商品      reject  驳回下架（note 作为原因） / restore 恢复上架
//   申诉      accept  同意（解除禁言 / 恢复商品） / reject 驳回申诉
//   资料      reset   重置昵称、清空简介、锁 24 小时
int admin_resolve(Ctx *ctx, const cJSON *req, cJSON *resp) {
    Case c;
    int r = load_case(ctx, req, &c);
    if (r != OK) return r;
    const char *action = jx_str(req, "action"), *note = jx_str(req, "note");
    if (!action) return fail(ctx, ERR_BAD_REQUEST, "请选择处理方式");
    if (jx_has(req, "note") && !utf8_valid(note, 0, 200)) return fail(ctx, ERR_BAD_REQUEST, "备注最多 200 个字");
    int64_t hours = 12;
    if (jx_has(req, "hours") && (!jx_int(req, "hours", &hours) || hours < 1 || hours > 720))
        return fail(ctx, ERR_BAD_REQUEST, "禁言时长须为 1–720 小时");
    if (strcmp(c.status, "open") != 0) return fail(ctx, ERR_CONFLICT, "这个案件已经处理过了");

    bool is_msg = strcmp(c.kind, "report") == 0 || strcmp(c.kind, "chat") == 0;
    bool is_goods = strcmp(c.kind, "goods") == 0, is_appeal = strcmp(c.kind, "appeal") == 0;
    bool is_profile = strcmp(c.kind, "profile") == 0;
    bool known = strcmp(action, "close") == 0 ||
                 (is_msg && (strcmp(action, "punish") == 0 || strcmp(action, "revert") == 0)) ||
                 (is_goods && (strcmp(action, "reject") == 0 || strcmp(action, "restore") == 0)) ||
                 (is_appeal && (strcmp(action, "accept") == 0 || strcmp(action, "reject") == 0)) ||
                 (is_profile && strcmp(action, "reset") == 0);
    if (!known) return fail(ctx, ERR_BAD_REQUEST, "这类案件不支持操作 %s", action);

    char result[300];
    const char *note_text = note && *note ? note : "";
    bool ok = true;
    if (is_msg) {
        int64_t message_id = strcmp(c.kind, "chat") == 0 ? c.ref_id : report_message(ctx->db, c.ref_id);
        Msg m;
        if (!load_message(ctx->db, message_id, &m)) return fail(ctx, ERR_NOT_FOUND, "消息不存在");
        const char *report_status = NULL;
        if (strcmp(action, "punish") == 0) {
            ok = punish(ctx->db, &m, hours * 3600);
            snprintf(result, sizeof result, "人工处理：屏蔽消息并禁言 %lld 小时。%s", (long long)hours, note_text);
            report_status = "upheld";
        } else if (strcmp(action, "revert") == 0) {
            ok = unpunish(ctx->db, &m);
            snprintf(result, sizeof result, "人工处理：恢复消息、解除禁言（判定不违规）。%s", note_text);
            report_status = "rejected";
        } else {
            snprintf(result, sizeof result, "人工处理：维持现状。%s", note_text);
            report_status = m.mod_state == 2 ? "upheld" : "rejected";
        }
        free(m.content);
        if (ok && strcmp(c.kind, "report") == 0) {
            exec_bind(ctx->db, "UPDATE reports SET status=?,handled_at=? WHERE id=?", "sii", report_status, now_sec(),
                      c.ref_id);
            sqlite3_stmt *st;
            if (sqlite3_prepare_v2(ctx->db, "SELECT reporter_id FROM reports WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(st, 1, c.ref_id);
                if (sqlite3_step(st) == SQLITE_ROW)
                    push_report_result(sqlite3_column_int64(st, 0), c.ref_id, report_status);
                sqlite3_finalize(st);
            }
        }
    } else if (is_goods) {
        if (strcmp(action, "reject") == 0) {
            char reason[256];
            snprintf(reason, sizeof reason, "人工审核：%s", *note_text ? note_text : "违反平台规定");
            ok = set_goods_rejected(ctx->db, c.ref_id, true, reason);
            snprintf(result, sizeof result, "人工处理：驳回下架。%s", note_text);
        } else if (strcmp(action, "restore") == 0) {
            ok = set_goods_rejected(ctx->db, c.ref_id, false, NULL);
            snprintf(result, sizeof result, "人工处理：恢复上架。%s", note_text);
        } else {
            snprintf(result, sizeof result, "人工处理：维持现状。%s", note_text);
        }
    } else if (is_profile) {
        if (strcmp(action, "reset") == 0) {
            reset_profile(ctx->db, c.ref_id, NULL, NULL, *note_text ? note_text : "管理员审核：昵称或简介违规");
            snprintf(result, sizeof result, "人工处理：重置昵称、清空简介，24 小时内不能改昵称。%s", note_text);
        } else {
            snprintf(result, sizeof result, "人工处理：维持现状。%s", note_text);
        }
    } else if (is_appeal) {
        sqlite3_stmt *st;
        char kind[16] = "";
        int64_t goods_id = 0;
        if (sqlite3_prepare_v2(ctx->db, "SELECT kind,goods_id FROM appeals WHERE id=?", -1, &st, NULL) != SQLITE_OK)
            return db_fail(ctx);
        sqlite3_bind_int64(st, 1, c.ref_id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            snprintf(kind, sizeof kind, "%s", (const char *)sqlite3_column_text(st, 0));
            goods_id = sqlite3_column_int64(st, 1);
        }
        sqlite3_finalize(st);
        bool accept = strcmp(action, "accept") == 0;
        if (accept && strcmp(kind, "mute") == 0) {
            ok = exec_bind(ctx->db, "UPDATE users SET muted_until=0 WHERE id=?", "i", c.user_id);
            if (ok) push_muted(ctx->db, c.user_id);
        } else if (accept && strcmp(kind, "goods") == 0) {
            ok = set_goods_rejected(ctx->db, goods_id, false, NULL);
        }
        if (ok)
            exec_bind(ctx->db, "UPDATE appeals SET status=?,handled_at=? WHERE id=?", "sii",
                      accept ? "accepted" : "rejected", now_sec(), c.ref_id);
        snprintf(result, sizeof result, "人工处理：%s申诉。%s", accept ? "同意" : "驳回", note_text);
    }
    if (!ok) return db_fail(ctx);
    if (!exec_bind(ctx->db, "UPDATE mod_cases SET status='closed',result=?,handled_by=?,handled_at=? WHERE id=?",
                   "siii", result, ctx->user_id, now_sec(), c.id))
        return db_fail(ctx);
    jx_add_str(resp, "result", result);
    return OK;
}

// admin.mute：禁言加时（从现在或原禁言结束时间往后加 hours 小时）
int admin_mute(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t uid, hours;
    if (!jx_int(req, "user_id", &uid) || uid < 1) return fail(ctx, ERR_BAD_REQUEST, "用户编号须为正整数");
    if (!jx_int(req, "hours", &hours) || hours < 1 || hours > 720) return fail(ctx, ERR_BAD_REQUEST, "禁言时长须为 1–720 小时");
    if (!user_exists(ctx->db, uid)) return fail(ctx, ERR_NOT_FOUND, "用户不存在");
    if (!exec_bind(ctx->db, "UPDATE users SET muted_until=MAX(muted_until,?)+? WHERE id=?", "iii", now_sec(),
                   hours * 3600, uid))
        return db_fail(ctx);
    push_muted(ctx->db, uid);
    jx_add_int(resp, "muted_until", user_muted_until(ctx->db, uid));
    return OK;
}

int admin_unmute(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp;
    int64_t uid;
    if (!jx_int(req, "user_id", &uid) || uid < 1) return fail(ctx, ERR_BAD_REQUEST, "用户编号须为正整数");
    if (!user_exists(ctx->db, uid)) return fail(ctx, ERR_NOT_FOUND, "用户不存在");
    if (!exec_bind(ctx->db, "UPDATE users SET muted_until=0 WHERE id=?", "i", uid)) return db_fail(ctx);
    push_muted(ctx->db, uid);
    return OK;
}

// 通用分页列表：count_sql 和 list_sql 都用 ?1 表示筛选参数（可以是 NULL），list_sql 还要有 ?2 = LIMIT、?3 = OFFSET
typedef void (*RowWriter)(sqlite3 *db, sqlite3_stmt *st, cJSON *out);

static int page_list(Ctx *ctx, const cJSON *req, const char *filter, const char *count_sql, const char *list_sql,
                     RowWriter writer, cJSON *resp) {
    int64_t page, size;
    if (!read_paging(req, &page, &size)) return fail(ctx, ERR_BAD_REQUEST, "分页参数不正确");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, count_sql, -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, filter, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, now_sec());
    int64_t total = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    cJSON *items = add_page(resp, total, page, size);
    if (sqlite3_prepare_v2(ctx->db, list_sql, -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, filter, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, size);
    sqlite3_bind_int64(st, 3, (page - 1) * size);
    sqlite3_bind_int64(st, 4, now_sec());
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        cJSON *o = cJSON_CreateObject();
        writer(ctx->db, st, o);
        cJSON_AddItemToArray(items, o);
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

static void write_user_row(sqlite3 *db, sqlite3_stmt *st, cJSON *o) {
    (void)db;
    jx_add_col_int(o, "user_id", st, 0);
    jx_add_col_str(o, "username", st, 1);
    jx_add_col_str(o, "nickname", st, 2);
    jx_add_col_str(o, "email", st, 3);
    jx_add_bool(o, "is_admin", sqlite3_column_int(st, 4) != 0);
    jx_add_col_int(o, "muted_until", st, 5);
    jx_add_col_int(o, "created_at", st, 6);
    jx_add_col_int(o, "violations", st, 7);
}

// admin.users：{keyword?, muted_only?}
int admin_users(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *kw = jx_str(req, "keyword");
    if (jx_has(req, "keyword") && !utf8_valid(kw, 0, 30)) return fail(ctx, ERR_BAD_REQUEST, "关键词最多 30 个字");
    if (kw && !*kw) kw = NULL;
    const cJSON *mo = cJSON_GetObjectItemCaseSensitive(req, "muted_only");
    bool muted_only = cJSON_IsTrue(mo);
    const char *where = muted_only
        ? " WHERE muted_until>?4 AND (?1 IS NULL OR instr(username,?1)>0 OR instr(nickname,?1)>0)"
        : " WHERE (?1 IS NULL OR instr(username,?1)>0 OR instr(nickname,?1)>0)";
    char count_sql[400], list_sql[800];
    snprintf(count_sql, sizeof count_sql, "SELECT COUNT(*) FROM users%s", where);
    snprintf(list_sql, sizeof list_sql,
             "SELECT id,username,nickname,email,is_admin,muted_until,created_at,"
             "(SELECT COUNT(*) FROM mod_cases c WHERE c.user_id=users.id AND c.kind IN ('report','chat')) "
             "FROM users%s ORDER BY id LIMIT ?2 OFFSET ?3",
             where);
    return page_list(ctx, req, kw, count_sql, list_sql, write_user_row, resp);
}

static void write_goods_row(sqlite3 *db, sqlite3_stmt *st, cJSON *o) {
    jx_add_col_int(o, "goods_id", st, 0);
    jx_add_col_str(o, "title", st, 1);
    jx_add_col_int(o, "price", st, 2);
    jx_add_col_str(o, "status", st, 3);
    jx_add_col_str(o, "reject_reason", st, 4);
    jx_add_col_int(o, "created_at", st, 6);
    add_user_brief(db, o, "seller", sqlite3_column_int64(st, 5));
}

// admin.goods：所有商品（包括被驳回、审核中的），{status?}
int admin_goods(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *status = jx_str(req, "status");
    if (jx_has(req, "status") && !status) return fail(ctx, ERR_BAD_REQUEST, "商品状态须为字符串");
    if (status && !*status) status = NULL;
    return page_list(ctx, req, status, "SELECT COUNT(*) FROM goods WHERE ?1 IS NULL OR status=?1",
                     "SELECT id,title,price,status,reject_reason,seller_id,created_at FROM goods "
                     "WHERE ?1 IS NULL OR status=?1 ORDER BY id DESC LIMIT ?2 OFFSET ?3",
                     write_goods_row, resp);
}

static void write_blocked_row(sqlite3 *db, sqlite3_stmt *st, cJSON *o) {
    jx_add_col_int(o, "id", st, 0);
    jx_add_col_str(o, "kind", st, 1);
    add_user_brief(db, o, "user", sqlite3_column_int64(st, 2));
    jx_add_col_str(o, "category", st, 3);
    jx_add_str(o, "category_label", category_label((const char *)sqlite3_column_text(st, 3)));
    jx_add_col_str(o, "word", st, 4);
    jx_add_col_str(o, "content", st, 5);
    jx_add_col_int(o, "created_at", st, 6);
}

// admin.blocked：一级词拦截记录，{kind?: chat / goods / profile}
int admin_blocked(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *kind = jx_str(req, "kind");
    if (jx_has(req, "kind") &&
        (!kind || (strcmp(kind, "chat") != 0 && strcmp(kind, "goods") != 0 && strcmp(kind, "profile") != 0)))
        return fail(ctx, ERR_BAD_REQUEST, "kind 须为 chat、goods 或 profile");
    return page_list(ctx, req, kind, "SELECT COUNT(*) FROM blocked_logs WHERE ?1 IS NULL OR kind=?1",
                     "SELECT id,kind,user_id,category,word,content,created_at FROM blocked_logs "
                     "WHERE ?1 IS NULL OR kind=?1 ORDER BY id DESC LIMIT ?2 OFFSET ?3",
                     write_blocked_row, resp);
}
