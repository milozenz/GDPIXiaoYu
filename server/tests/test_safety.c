// 内容安全测试：敏感词、AI 审核（用假的 Jev 替身，不走网络）、举报、申诉、管理员后台、邮箱验证码
#include <stdio.h>
#include <string.h>

#include "core/jev.h"
#include "core/jsonx.h"
#include "core/router.h"
#include "core/util.h"
#include "core/words.h"
#include "modules/chat/chat.h"
#include "modules/goods/goods.h"
#include "modules/safety/safety.h"
#include "modules/user/user.h"
#include "t.h"

#define LL(x) ((long long)(x))

// ---------------- 假的 Jev：按全局变量回答 ----------------
static double g_violation;     // 私信违规概率
static double g_reason;        // 举报理由成立概率
static double g_goods_bad;     // 商品违规概率
static const char *g_label = "abuse";
static bool g_fail;            // 模拟调用失败
static int  g_calls;
static bool g_q1_saw_reason;   // 第 1 次判断（言论）有没有看到举报理由——不应该看到
static char g_during_ai[256];  // 模拟"AI 判断的这段时间里用户又改了东西"：调用替身时执行这条 SQL

static void add_choice(cJSON *answers, const char *qid, const char *safe, double p_bad) {
    cJSON *a = cJSON_AddObjectToObject(answers, qid);
    jx_add_str(a, "type", "choice");
    jx_add_str(a, "choice", p_bad >= 0.5 ? g_label : safe);
    cJSON *p = cJSON_AddObjectToObject(a, "probabilities");
    cJSON_AddNumberToObject(p, safe, 1.0 - p_bad);
    cJSON_AddNumberToObject(p, g_label, p_bad);
    cJSON_AddNumberToObject(a, "confidence", p_bad >= 0.5 ? p_bad : 1.0 - p_bad);
}

static bool fake_jev(const cJSON *state, const cJSON *q, cJSON **answers) {
    g_calls++;
    if (g_during_ai[0]) {
        t_sql("%s", g_during_ai);
        g_during_ai[0] = '\0';
    }
    if (g_fail) return false;
    cJSON *a = cJSON_CreateObject();
    if (cJSON_GetObjectItemCaseSensitive(q, "violation")) {
        add_choice(a, "violation", "none", g_violation);
        if (cJSON_GetObjectItemCaseSensitive(state, "report_reason")) g_q1_saw_reason = true;
    }
    if (cJSON_GetObjectItemCaseSensitive(q, "reason_holds")) {
        cJSON *r = cJSON_AddObjectToObject(a, "reason_holds");
        jx_add_str(r, "type", "noul");
        cJSON_AddNumberToObject(r, "noul", g_reason);
    }
    if (cJSON_GetObjectItemCaseSensitive(q, "verdict")) add_choice(a, "verdict", "ok", g_goods_bad);
    *answers = a;
    return true;
}

static void ai_on(void) {
    jev_set_fake(fake_jev);
    g_violation = g_reason = g_goods_bad = 0;
    g_label = "abuse";
    g_fail = false;
    g_calls = 0;
    g_q1_saw_reason = false;
}

static void ai_off(void) { jev_set_fake(NULL); }

static int64_t a, b, admin;

static void setup(void) {
    a = t_user("alice", "Ab123456", 0);
    b = t_user("bob", "Ab123456", 0);
    admin = t_user("admin", "Ab123456", 0);
    t_sql("UPDATE users SET is_admin=1 WHERE id=%lld", LL(admin));
}

static int say(int64_t from, int64_t to, const char *content) {
    char json[2048];
    snprintf(json, sizeof json, "{'to_user_id':%lld,'content':'%s'}", LL(to), content);
    return t_call(chat_send, from, json);
}

static int64_t last_case(void) { return t_sql_int("SELECT COALESCE(MAX(id),0) FROM mod_cases"); }

// ======================================================================
// 词库
// ======================================================================

static void hit_is(const char *text, int scope, int level, const char *category) {
    WordHit h;
    bool got = words_check(text, scope, &h);
    if (level == 0) {
        if (got) printf("    不该命中却命中了：%s → %s\n", text, h.word);
        CHECK(!got);
        return;
    }
    if (!got) printf("    该命中却没命中：%s\n", text);
    CHECK(got);
    if (got) {
        CHECK_INT(level, h.level);
        CHECK_STR(category, h.category);
    }
}

static void test_words(void) {
    t_begin("词库：变体、白名单、英文词边界");
    hit_is("操你妈", SCOPE_CHAT, WORD_BLOCK, "abuse");
    hit_is("操 你 妈", SCOPE_CHAT, WORD_BLOCK, "abuse");
    hit_is("操*你*妈！！", SCOPE_CHAT, WORD_BLOCK, "abuse");
    hit_is("傻😀逼", SCOPE_CHAT, WORD_BLOCK, "abuse");
    hit_is("你是ＳＢ吧", SCOPE_CHAT, WORD_BLOCK, "abuse");          // 全角
    hit_is("你是SB吧", SCOPE_CHAT, WORD_BLOCK, "abuse");            // 大写
    hit_is("出个usb数据线", SCOPE_CHAT, 0, NULL);                    // 英文词边界：usb 里的 sb 不算
    hit_is("加 vx abc123 聊", SCOPE_CHAT, WORD_WARN, "scam");
    hit_is("加微信详聊", SCOPE_CHAT, WORD_WARN, "scam");
    hit_is("我知道你住哪栋", SCOPE_CHAT, WORD_WARN, "threat");
    hit_is("加微信 傻逼", SCOPE_CHAT, WORD_BLOCK, "abuse");         // 一级优先
    hit_is("自行车还在吗？", SCOPE_CHAT, 0, NULL);
    hit_is("代写作业", SCOPE_GOODS, WORD_BLOCK, "cheating");
    hit_is("代写作业", SCOPE_CHAT, 0, NULL);                         // 商品词不管私信
    hit_is("加微信", SCOPE_GOODS, 0, NULL);                          // 二级词只管私信
    hit_is("习近平夫妇写真", SCOPE_GOODS, WORD_BLOCK, "political");
    hit_is("习近平新时代中国特色社会主义思想概论 教材", SCOPE_GOODS, 0, NULL);  // 白名单：课程教材
    hit_is("出饭卡 余额200", SCOPE_GOODS, WORD_BLOCK, "identity");
    hit_is("可爱饭卡套 全新", SCOPE_GOODS, 0, NULL);                 // 白名单：卡套
    hit_is("仿真枪", SCOPE_GOODS, WORD_BLOCK, "contraband");
    CHECK_STR("诈骗引流", category_label("scam"));
}

// ======================================================================
// 私信
// ======================================================================

static void test_chat_block(void) {
    t_begin("私信：一级词直接拦，不保存，留底");
    setup();
    ai_off();
    CHECK_CODE(ERR_BAD_REQUEST, say(a, b, "你个傻 逼"));
    CHECK(strstr(t_msg(), "人身攻击") != NULL);
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM messages"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM blocked_logs WHERE kind='chat' AND user_id=%lld AND word='傻逼'", LL(a)));
    CHECK_INT(0, t_push_count);
}

static void test_chat_warn(void) {
    t_begin("私信：二级词照常发出，打上提醒标记，交给 AI");
    setup();
    ai_on();
    CHECK_CODE(OK, say(a, b, "平台抽成高 加我vx 便宜点"));
    CHECK_STR("warn", t_str("flag"));
    CHECK_STR("scam", t_str("flag_kind"));
    CHECK_INT(1, t_push_count);
    CHECK_STR("warn", t_push_str("flag"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs WHERE kind='chat'"));
    CHECK_CODE(OK, say(a, b, "自行车还在吗"));
    CHECK_STR("none", t_str("flag"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs"));  // 正常消息不进队列

    ai_off();
    CHECK_CODE(OK, say(a, b, "加微信吧"));
    CHECK_STR("warn", t_str("flag"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs"));  // 没开 AI 只提醒
}

static void test_chat_warn_ai(void) {
    t_begin("私信：二级词 AI 有把握判违规 = 屏蔽 + 禁言 12 小时；没把握不处理");
    setup();
    ai_on();
    g_violation = 0.97;
    g_label = "scam";
    CHECK_CODE(OK, say(a, b, "加我vx 私下转账"));
    int64_t m1 = t_int("message_id");
    CHECK_INT(1, review_run_once(t_db()));
    CHECK_INT(2, t_sql_int("SELECT mod_state FROM messages WHERE id=%lld", LL(m1)));
    int64_t until = t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(a));
    CHECK(until > now_sec() + 11 * 3600 && until <= now_sec() + 12 * 3600);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='auto' AND kind='chat' AND ref_id=%lld", LL(m1)));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM review_jobs"));
    CHECK_STR("user.muted", t_push_event);

    // 被禁言后发不出去
    CHECK_CODE(ERR_FORBIDDEN, say(a, b, "在吗"));
    CHECK(strstr(t_msg(), "禁言") != NULL);
    // 历史里显示"已屏蔽"，原文还在库里
    CHECK_CODE(OK, t_call(chat_history, b, "{'peer_id':1}"));
    CHECK_STR("masked", t_str("items.0.flag"));
    CHECK_STR("该消息因违规已被屏蔽", t_str("items.0.content"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM messages WHERE content='加我vx 私下转账'"));
    CHECK_CODE(OK, t_call(chat_conversations, b, "{}"));
    CHECK_STR("该消息因违规已被屏蔽", t_str("items.0.last_message.content"));

    // 没把握：不处理
    g_violation = 0.6;
    CHECK_CODE(OK, say(b, a, "加微信聊"));
    int64_t m2 = t_int("message_id");
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT mod_state FROM messages WHERE id=%lld", LL(m2)));
    CHECK_INT(0, t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(b)));
}

// ======================================================================
// 商品
// ======================================================================

static void test_goods_block(void) {
    t_begin("商品：一级禁售词不让发布，留底");
    setup();
    ai_on();
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, a, "{'title':'代写作业 包过','price':5000,'category_id':7}"));
    CHECK(strstr(t_msg(), "代课代写") != NULL);
    CHECK_CODE(ERR_BAD_REQUEST,
               t_call(goods_publish, a, "{'title':'九成新台灯','description':'顺便出校园卡','price':500,'category_id':3}"));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM goods"));
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM blocked_logs WHERE kind='goods'"));
    CHECK_INT(0, g_calls);
}

static void test_goods_review(void) {
    t_begin("商品：审核中 → AI 有把握合规就上架并推送");
    setup();
    ai_on();
    CHECK_CODE(OK, t_call(goods_publish, a, "{'title':'高数教材','price':2000,'category_id':2}"));
    int64_t g = t_int("goods_id");
    CHECK_STR("reviewing", t_str("status"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{}"));
    CHECK_INT(0, t_int("total"));  // 审核中搜不到
    g_goods_bad = 0.01;
    CHECK_INT(1, review_run_once(t_db()));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND status='on_sale'", LL(g)));
    CHECK_STR("goods.reviewed", t_push_event);
    CHECK_INT(a, t_push_user);
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM mod_cases"));

    ai_off();
    CHECK_CODE(OK, t_call(goods_publish, a, "{'title':'台灯','price':2000,'category_id':3}"));
    CHECK_STR("on_sale", t_str("status"));  // 没开 AI 直接上架
}

static void test_goods_reject(void) {
    t_begin("商品：AI 有把握违规 → 驳回下架、不推送、进已自动处理区；不能自己重新上架");
    setup();
    ai_on();
    g_goods_bad = 0.98;
    g_label = "scam";
    CHECK_CODE(OK, t_call(goods_publish, a, "{'title':'招兼职 日结300','price':100,'category_id':7}"));
    int64_t g = t_int("goods_id");
    int pushes = t_push_count;
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND status='rejected' AND reject_reason LIKE '%%诈骗%%'", LL(g)));
    CHECK_INT(pushes, t_push_count);  // 驳回不推送给卖家
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='auto' AND kind='goods' AND ref_id=%lld", LL(g)));
    char json[128];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_on_shelf, a, json));
    CHECK_CODE(OK, t_call(goods_mine, a, "{'status':'rejected'}"));
    CHECK_INT(1, t_int("total"));
    CHECK(strstr(t_str("items.0.reject_reason"), "诈骗") != NULL);
    CHECK_CODE(OK, t_call(goods_detail, a, json));
    CHECK(strstr(t_str("reject_reason"), "诈骗") != NULL);
}

static void test_goods_unsure_and_fail(void) {
    t_begin("商品：AI 没把握 → 先上架 + 人工待审；调用失败 → 先上架 + 稍后重试");
    setup();
    ai_on();
    g_goods_bad = 0.5;
    CHECK_CODE(OK, t_call(goods_publish, a, "{'title':'神秘礼包','price':100,'category_id':7}"));
    int64_t g1 = t_int("goods_id");
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND status='on_sale'", LL(g1)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='manual' AND kind='goods' AND ref_id=%lld", LL(g1)));

    g_fail = true;
    CHECK_CODE(OK, t_call(goods_publish, a, "{'title':'二手键盘','price':100,'category_id':1}"));
    int64_t g2 = t_int("goods_id");
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND status='on_sale'", LL(g2)));
    CHECK_INT(1, t_sql_int("SELECT attempts FROM review_jobs WHERE ref_id=%lld", LL(g2)));
    // 一直失败：第 5 次后转人工
    for (int i = 0; i < 4; i++) {
        t_sql("UPDATE review_jobs SET next_at=0");
        review_run_once(t_db());
    }
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM review_jobs"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='manual' AND ref_id=%lld AND summary LIKE '%%多次失败%%'", LL(g2)));
}

static void test_goods_update_rereview(void) {
    t_begin("商品：改了内容要重新审核；改成禁售词直接拦");
    setup();
    ai_on();
    int64_t g = t_goods(a, "台灯", 1000);
    char json[256];
    snprintf(json, sizeof json, "{'goods_id':%lld,'title':'护眼台灯'}", LL(g));
    CHECK_CODE(OK, t_call(goods_update, a, json));
    CHECK_STR("reviewing", t_str("status"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs WHERE kind='goods' AND ref_id=%lld", LL(g)));
    snprintf(json, sizeof json, "{'goods_id':%lld,'price':900}", LL(g));
    CHECK_CODE(OK, t_call(goods_update, a, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs"));  // 只改价格不重审
    snprintf(json, sizeof json, "{'goods_id':%lld,'description':'可以代写作业'}", LL(g));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_update, a, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND description=''", LL(g)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM blocked_logs WHERE kind='goods'"));  // 拦截记录没跟着回滚
}

// ======================================================================
// 举报
// ======================================================================

static int64_t report(int64_t who, int64_t msg, const char *type, const char *reason) {
    char json[512];
    snprintf(json, sizeof json, "{'message_id':%lld,'type':'%s','reason':'%s'}", LL(msg), type, reason);
    return t_call(report_create, who, json);
}

static void test_report_rules(void) {
    t_begin("举报：只能举报发给自己的消息，同一条只能举报一次");
    setup();
    ai_on();
    CHECK_CODE(OK, say(a, b, "最低 80 不讲价"));
    int64_t m = t_int("message_id");
    CHECK_CODE(ERR_FORBIDDEN, report(a, m, "abuse", "我举报我自己"));
    CHECK_CODE(ERR_BAD_REQUEST, report(b, m, "xxx", "理由"));
    CHECK_CODE(ERR_BAD_REQUEST, report(b, m, "abuse", ""));
    CHECK_CODE(ERR_NOT_FOUND, report(b, 999, "abuse", "理由"));
    CHECK_CODE(OK, report(b, m, "abuse", "态度很差"));
    CHECK_STR("pending", t_str("status"));
    CHECK_CODE(ERR_CONFLICT, report(b, m, "abuse", "再举报一次"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs WHERE kind='report'"));
}

static void test_report_upheld(void) {
    t_begin("举报：AI 判言论明确违规 → 屏蔽 + 禁言 12 小时，进已自动处理区；言论判断看不到举报理由");
    setup();
    ai_on();
    CHECK_CODE(OK, say(a, b, "你是不是脑子有病 滚"));
    int64_t m = t_int("message_id");
    CHECK_CODE(OK, report(b, m, "abuse", "他骂我"));
    int64_t r = t_int("report_id");
    g_violation = 0.99;
    g_reason = 0.99;
    review_run_once(t_db());
    CHECK_INT(2, g_calls);          // 两次独立请求
    CHECK(!g_q1_saw_reason);         // 判言论时不给 AI 看举报理由，免得被带偏
    CHECK_INT(2, t_sql_int("SELECT mod_state FROM messages WHERE id=%lld", LL(m)));
    CHECK(t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(a)) > now_sec());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM reports WHERE id=%lld AND status='upheld'", LL(r)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='auto' AND kind='report' AND user_id=%lld", LL(a)));
    CHECK_STR("report.result", t_push_event);
    CHECK_INT(b, t_push_user);
    CHECK_STR("upheld", t_push_str("status"));
}

static void test_report_manual(void) {
    t_begin("举报：理由成立但言论没有明确违规 → 人工待审，不禁言；两项都不成立 → 自动驳回");
    setup();
    ai_on();
    CHECK_CODE(OK, say(a, b, "哦哦 那您可真是大善人呢"));
    int64_t m1 = t_int("message_id");
    CHECK_CODE(OK, report(b, m1, "abuse", "阴阳怪气"));
    g_violation = 0.10;
    g_reason = 0.96;
    review_run_once(t_db());
    CHECK_INT(0, t_sql_int("SELECT mod_state FROM messages WHERE id=%lld", LL(m1)));
    CHECK_INT(0, t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(a)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM reports WHERE status='manual'"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='manual' AND kind='report' AND summary LIKE '%%理由成立%%'"));

    CHECK_CODE(OK, say(a, b, "可以的 我送过去"));
    int64_t m2 = t_int("message_id");
    CHECK_CODE(OK, report(b, m2, "abuse", "他骂我"));
    g_violation = 0.03;
    g_reason = 0.02;
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM reports WHERE message_id=%lld AND status='rejected'", LL(m2)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='auto' AND summary LIKE '%%自动驳回%%'"));

    ai_off();
    CHECK_CODE(OK, say(a, b, "随便"));
    CHECK_CODE(OK, report(b, t_int("message_id"), "other", "没开 AI"));
    CHECK_STR("manual", t_str("status"));
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='manual'"));
}

// ======================================================================
// 申诉 + 管理员
// ======================================================================

static void test_appeal(void) {
    t_begin("申诉：被禁言才能申诉禁言，被驳回才能申诉商品，同一件事只能有一个待处理申诉");
    setup();
    CHECK_CODE(ERR_CONFLICT, t_call(appeal_create, a, "{'kind':'mute','content':'冤枉'}"));
    t_sql("UPDATE users SET muted_until=%lld WHERE id=%lld", LL(now_sec() + 3600), LL(a));
    CHECK_CODE(OK, t_call(appeal_create, a, "{'kind':'mute','content':'我没骂人'}"));
    CHECK_CODE(ERR_CONFLICT, t_call(appeal_create, a, "{'kind':'mute','content':'再申诉'}"));
    int64_t g = t_goods(a, "台灯", 1000);
    char json[256];
    snprintf(json, sizeof json, "{'kind':'goods','goods_id':%lld,'content':'这是正常商品'}", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(appeal_create, a, json));
    t_sql("UPDATE goods SET status='rejected' WHERE id=%lld", LL(g));
    CHECK_CODE(ERR_FORBIDDEN, t_call(appeal_create, b, json));
    CHECK_CODE(OK, t_call(appeal_create, a, json));
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='manual' AND kind='appeal'"));
}

static void test_admin_router(void) {
    t_begin("管理员：普通用户调不了 admin.* 命令");
    setup();
    t_sql("INSERT INTO sessions(token,user_id,created_at,expires_at) VALUES('tok_a',%lld,0,%lld)", LL(a), LL(now_sec() + 3600));
    t_sql("INSERT INTO sessions(token,user_id,created_at,expires_at) VALUES('tok_admin',%lld,0,%lld)", LL(admin), LL(now_sec() + 3600));
    cJSON *req = cJSON_CreateObject();
    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.db = t_db();
    ctx.token = "tok_a";
    cJSON *resp = cJSON_CreateObject();
    CHECK_CODE(ERR_FORBIDDEN, router_dispatch(&ctx, "admin.stats", req, resp));
    cJSON_Delete(resp);
    resp = cJSON_CreateObject();
    ctx.token = "tok_admin";
    CHECK_CODE(OK, router_dispatch(&ctx, "admin.stats", req, resp));
    cJSON_Delete(resp);
    cJSON_Delete(req);
}

static void test_admin_resolve(void) {
    t_begin("管理员：人工处罚、撤销自动处理、处理申诉、禁言加时");
    setup();
    ai_on();
    // 人工待审的举报 → 人工判定违规，禁言 24 小时
    CHECK_CODE(OK, say(a, b, "呵呵 穷就别买"));
    int64_t m = t_int("message_id");
    CHECK_CODE(OK, report(b, m, "abuse", "嘲讽我"));
    g_violation = 0.2;
    g_reason = 0.95;
    review_run_once(t_db());
    int64_t c = last_case();
    char json[256];
    CHECK_CODE(OK, t_call(admin_cases, admin, "{'area':'manual','status':'open'}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(c, t_int("items.0.case_id"));
    snprintf(json, sizeof json, "{'case_id':%lld}", LL(c));
    CHECK_CODE(OK, t_call(admin_case_detail, admin, json));
    CHECK_STR("呵呵 穷就别买", t_str("message.content"));
    CHECK(t_len("context") >= 1);
    CHECK_STR("嘲讽我", t_str("report.reason"));
    snprintf(json, sizeof json, "{'case_id':%lld,'action':'punish','hours':24,'note':'嘲讽'}", LL(c));
    CHECK_CODE(OK, t_call(admin_resolve, admin, json));
    CHECK(t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(a)) > now_sec() + 23 * 3600);
    CHECK_INT(2, t_sql_int("SELECT mod_state FROM messages WHERE id=%lld", LL(m)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM reports WHERE status='upheld'"));
    CHECK_CODE(ERR_CONFLICT, t_call(admin_resolve, admin, json));  // 已结案

    // 自动处理的商品 → 人工撤销，恢复上架
    g_goods_bad = 0.95;
    CHECK_CODE(OK, t_call(goods_publish, b, "{'title':'二手吉他','price':30000,'category_id':5}"));
    int64_t g = t_int("goods_id");
    review_run_once(t_db());
    c = last_case();
    snprintf(json, sizeof json, "{'case_id':%lld,'action':'punish'}", LL(c));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(admin_resolve, admin, json));  // 商品案件没有 punish
    snprintf(json, sizeof json, "{'case_id':%lld,'action':'restore','note':'AI 误判'}", LL(c));
    CHECK_CODE(OK, t_call(admin_resolve, admin, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND status='on_sale' AND reject_reason=''", LL(g)));

    // 申诉禁言 → 同意，解除禁言
    CHECK_CODE(OK, t_call(appeal_create, a, "{'kind':'mute','content':'知道错了'}"));
    c = last_case();
    snprintf(json, sizeof json, "{'case_id':%lld,'action':'accept'}", LL(c));
    CHECK_CODE(OK, t_call(admin_resolve, admin, json));
    CHECK_INT(0, t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(a)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM appeals WHERE status='accepted'"));

    // 加时：从现在起往后加
    snprintf(json, sizeof json, "{'user_id':%lld,'hours':2}", LL(b));
    CHECK_CODE(OK, t_call(admin_mute, admin, json));
    snprintf(json, sizeof json, "{'user_id':%lld,'hours':3}", LL(b));
    CHECK_CODE(OK, t_call(admin_mute, admin, json));
    int64_t until = t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(b));
    CHECK(until >= now_sec() + 5 * 3600 - 5 && until <= now_sec() + 5 * 3600);
    CHECK_CODE(OK, t_call(admin_users, admin, "{'muted_only':true}"));
    CHECK_INT(1, t_int("total"));
    snprintf(json, sizeof json, "{'user_id':%lld}", LL(b));
    CHECK_CODE(OK, t_call(admin_unmute, admin, json));
    CHECK_INT(0, t_sql_int("SELECT muted_until FROM users WHERE id=%lld", LL(b)));

    CHECK_CODE(OK, t_call(admin_stats, admin, "{}"));
    CHECK(t_exists("manual_open") && t_exists("ai_enabled"));
    CHECK_CODE(OK, t_call(admin_goods, admin, "{}"));
    CHECK_INT(1, t_int("total"));
    CHECK_CODE(OK, t_call(admin_blocked, admin, "{}"));
}

// ======================================================================
// 邮箱验证码（开发模式：没配 SMTP，验证码带在回复里）
// ======================================================================

static void test_email_register(void) {
    t_begin("邮箱：发验证码、输错计次、注册成功后验证码作废、邮箱登录");
    user_set_require_email(true);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_send_code, 0, "{'email':'not-an-email'}"));
    CHECK_CODE(OK, t_call(user_send_code, 0, "{'email':'Stu01@Example.com'}"));
    char code[8];
    snprintf(code, sizeof code, "%s", t_str("dev_code") ? t_str("dev_code") : "");
    CHECK_INT(6, (int)strlen(code));
    CHECK_CODE(ERR_CONFLICT, t_call(user_send_code, 0, "{'email':'stu01@example.com'}"));  // 60 秒内
    CHECK(strstr(t_msg(), "秒后再试") != NULL);

    CHECK_CODE(ERR_BAD_REQUEST,
               t_call(user_register, 0, "{'username':'stu01','password':'Ab123456','nickname':'小明'}"));  // 没填邮箱
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0,
                                       "{'username':'stu01','password':'Ab123456','nickname':'小明',"
                                       "'email':'stu01@example.com','code':'000000x'}"));
    char json[512];
    const char *wrong = strcmp(code, "111111") == 0 ? "222222" : "111111";
    snprintf(json, sizeof json,
             "{'username':'stu01','password':'Ab123456','nickname':'小明','email':'stu01@example.com','code':'%s'}", wrong);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
    CHECK_STR("验证码不对", t_msg());
    CHECK_INT(1, t_sql_int("SELECT tries FROM email_codes"));
    snprintf(json, sizeof json,
             "{'username':'stu01','password':'Ab123456','nickname':'小明','email':'STU01@example.com','code':'%s'}", code);
    CHECK_CODE(OK, t_call(user_register, 0, json));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM email_codes"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE email='stu01@example.com'"));
    CHECK_CODE(ERR_CONFLICT, t_call(user_send_code, 0, "{'email':'stu01@example.com'}"));  // 已注册

    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'Stu01@Example.COM','password':'Ab123456'}"));
    CHECK_STR("stu01", t_str("user.username"));
    CHECK_STR("stu01@example.com", t_str("user.email"));
    CHECK(t_exists("user.muted_until") && t_exists("user.is_admin"));
    CHECK_CODE(ERR_NOT_LOGIN, t_call(user_login, 0, "{'username':'stu01@example.com','password':'Ab1234567'}"));

    // 输错 5 次作废
    CHECK_CODE(OK, t_call(user_send_code, 0, "{'email':'stu02@example.com'}"));
    t_sql("UPDATE email_codes SET tries=5");
    snprintf(json, sizeof json,
             "{'username':'stu02','password':'Ab123456','nickname':'小红','email':'stu02@example.com','code':'%s'}",
             t_str("dev_code"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
    CHECK(strstr(t_msg(), "失效") != NULL);
    user_set_require_email(false);
}

// ======================================================================
// 昵称、简介
// ======================================================================

static void test_profile(void) {
    t_begin("昵称/简介：一级词直接拦；AI 判违规 → 重置昵称、清空简介、24 小时内不能改昵称");
    setup();
    ai_on();
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'bad01','password':'Ab123456','nickname':'傻 逼本人'}"));
    CHECK(strstr(t_msg(), "昵称") != NULL);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, a, "{'nickname':'阿狸','bio':'约炮加我'}"));
    CHECK(strstr(t_msg(), "简介") != NULL);
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM blocked_logs WHERE kind='profile'"));

    // 正常注册也会交给 AI 审一遍昵称
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'good01','password':'Ab123456','nickname':'小明'}"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM review_jobs WHERE kind='profile'"));
    review_run_once(t_db());

    // 改资料：AI 判违规
    CHECK_CODE(OK, t_call(user_update_profile, a, "{'nickname':'加我vx领红包','bio':'招兼职 日结'}"));
    g_violation = 0.97;
    g_label = "scam";
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE id=%lld AND nickname='小鱼用户%lld' AND bio=''", LL(a), LL(a)));
    CHECK(t_sql_int("SELECT nickname_locked_until FROM users WHERE id=%lld", LL(a)) > now_sec() + 23 * 3600);
    CHECK_STR("user.profile_reset", t_push_event);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE area='auto' AND kind='profile' AND ref_id=%lld", LL(a)));
    // 锁定期：改昵称不行，只改简介可以（客户端会把没变的昵称一起提交）
    CHECK_CODE(ERR_FORBIDDEN, t_call(user_update_profile, a, "{'nickname':'阿狸'}"));
    CHECK(strstr(t_msg(), "不能修改") != NULL);
    char json[256];
    snprintf(json, sizeof json, "{'nickname':'小鱼用户%lld','bio':'正常简介'}", LL(a));
    CHECK_CODE(OK, t_call(user_update_profile, a, json));
    CHECK(t_exists("nickname_locked_until"));

    // AI 判断的这段时间里用户又改了：不能误伤新内容（新内容会有自己的审核任务）
    g_violation = 0.01;
    review_run_once(t_db());  // 先把上面 alice 改简介产生的任务处理掉
    g_violation = 0.99;
    CHECK_CODE(OK, t_call(user_update_profile, b, "{'nickname':'违规昵称'}"));
    snprintf(g_during_ai, sizeof g_during_ai, "UPDATE users SET nickname='已经改好了' WHERE id=%lld", LL(b));
    review_run_once(t_db());
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE id=%lld AND nickname='已经改好了'", LL(b)));

    // 没把握 → 人工
    g_violation = 0.5;
    CHECK_CODE(OK, t_call(user_update_profile, b, "{'bio':'懂的来'}"));
    review_run_once(t_db());
    int64_t c = last_case();
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM mod_cases WHERE id=%lld AND area='manual' AND kind='profile'", LL(c)));
    snprintf(json, sizeof json, "{'case_id':%lld}", LL(c));
    CHECK_CODE(OK, t_call(admin_case_detail, admin, json));
    CHECK_STR("懂的来", t_str("profile.bio"));
    snprintf(json, sizeof json, "{'case_id':%lld,'action':'reset'}", LL(c));
    CHECK_CODE(OK, t_call(admin_resolve, admin, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE id=%lld AND bio='' AND nickname_locked_until>0", LL(b)));
}

// ======================================================================
// 修改密码、忘记密码
// ======================================================================

static void test_passwords(void) {
    t_begin("密码：改密码要原密码、其他设备下线；忘记密码用邮箱验证码重置、全部下线");
    ai_off();
    a = t_user("alice", "Ab123456", 0);
    t_sql("UPDATE users SET email='alice@example.com' WHERE id=%lld", LL(a));
    t_sql("INSERT INTO sessions(token,user_id,created_at,expires_at) VALUES('tok1',%lld,0,%lld)", LL(a), LL(now_sec() + 3600));
    t_sql("INSERT INTO sessions(token,user_id,created_at,expires_at) VALUES('tok2',%lld,0,%lld)", LL(a), LL(now_sec() + 3600));

    CHECK_CODE(ERR_BAD_REQUEST, t_call_token(user_change_password, a, "tok1", "{'old_password':'Wrong1234','new_password':'Cd123456'}"));
    CHECK_STR("原密码不对", t_msg());
    CHECK_CODE(ERR_BAD_REQUEST, t_call_token(user_change_password, a, "tok1", "{'old_password':'Ab123456','new_password':'weak'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call_token(user_change_password, a, "tok1", "{'old_password':'Ab123456','new_password':'Ab123456'}"));
    CHECK_CODE(OK, t_call_token(user_change_password, a, "tok1", "{'old_password':'Ab123456','new_password':'Cd123456'}"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM sessions WHERE user_id=%lld", LL(a)));     // 只留当前这个
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM sessions WHERE token='tok1'"));
    CHECK_CODE(ERR_NOT_LOGIN, t_call(user_login, 0, "{'username':'alice','password':'Ab123456'}"));
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice','password':'Cd123456'}"));

    // 忘记密码
    CHECK_CODE(ERR_NOT_FOUND, t_call(user_send_code, 0, "{'email':'nobody@example.com','purpose':'reset'}"));
    CHECK_CODE(ERR_CONFLICT, t_call(user_send_code, 0, "{'email':'alice@example.com'}"));  // 注册用途：已注册
    CHECK_CODE(OK, t_call(user_send_code, 0, "{'email':'Alice@Example.com','purpose':'reset'}"));
    char code[8], json[256];
    snprintf(code, sizeof code, "%s", t_str("dev_code"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_reset_password, 0, "{'email':'alice@example.com','code':'000000','new_password':'weak'}"));
    snprintf(json, sizeof json, "{'email':'alice@example.com','code':'%s','new_password':'Ef123456'}", strcmp(code, "111111") == 0 ? "222222" : "111111");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_reset_password, 0, json));
    CHECK_STR("验证码不对", t_msg());
    snprintf(json, sizeof json, "{'email':'alice@example.com','code':'%s','new_password':'Ef123456'}", code);
    CHECK_CODE(OK, t_call(user_reset_password, 0, json));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM sessions WHERE user_id=%lld", LL(a)));     // 全部下线
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM email_codes"));
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice@example.com','password':'Ef123456'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_reset_password, 0, json));                       // 验证码用过就作废

    // 注册用的验证码不能拿来重置密码
    CHECK_CODE(OK, t_call(user_send_code, 0, "{'email':'new@example.com'}"));
    t_sql("UPDATE users SET email='new@example.com' WHERE id=%lld", LL(a));                  // 假装这个邮箱后来被注册了
    snprintf(json, sizeof json, "{'email':'new@example.com','code':'%s','new_password':'Gh123456'}", t_str("dev_code"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_reset_password, 0, json));
    CHECK(strstr(t_msg(), "失效") != NULL);
}

int main(void) {
    t_init("内容安全模块");
    test_words();
    test_chat_block();
    test_chat_warn();
    test_chat_warn_ai();
    test_goods_block();
    test_goods_review();
    test_goods_reject();
    test_goods_unsure_and_fail();
    test_goods_update_rereview();
    test_report_rules();
    test_report_upheld();
    test_report_manual();
    test_appeal();
    test_admin_router();
    test_admin_resolve();
    test_email_register();
    test_profile();
    test_passwords();
    ai_off();
    return t_done();
}
