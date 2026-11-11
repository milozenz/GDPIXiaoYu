// 聊天模块测试（负责人：组员D）
// 这些用例是写好的验收标准。你可以在后面加自己的用例，但不要删改已有的。
#include <stdio.h>
#include <string.h>

#include "modules/chat/chat.h"
#include "t.h"

#define LL(x) ((long long)(x))

static int64_t a, b, c;

static void setup(void) {
    a = t_user("alice", "123456", 0);
    b = t_user("bob", "123456", 0);
    c = t_user("carol", "123456", 0);
}

// 直接往库里插一条消息，返回消息 id
static int64_t msg(int64_t from, int64_t to, const char *content, int is_read) {
    t_sql("INSERT INTO messages (from_id, to_id, content, is_read, created_at) VALUES (%lld, %lld, '%s', %d, %lld)",
          LL(from), LL(to), content, is_read, LL(1700000000 + t_sql_int("SELECT COUNT(*) FROM messages")));
    return t_sql_int("SELECT MAX(id) FROM messages");
}

static int say(int64_t from, int64_t to, const char *content) {
    char json[4096];
    snprintf(json, sizeof json, "{'to_user_id':%lld,'content':'%s'}", LL(to), content);
    return t_call(chat_send, from, json);
}

static void test_unread_count(void) {
    t_begin("未读总数（例子，已经写好）");
    setup();
    CHECK_CODE(OK, t_call(chat_unread_count, a, "{}"));
    CHECK_INT(0, t_int("count"));
    msg(b, a, "在吗", 0);
    msg(c, a, "你好", 0);
    msg(b, a, "看过了", 1);
    msg(a, b, "我发的不算", 0);
    CHECK_CODE(OK, t_call(chat_unread_count, a, "{}"));
    CHECK_INT(2, t_int("count"));
}

static void test_send_ok(void) {
    t_begin("发私信：保存并推送给对方");
    setup();
    CHECK_CODE(OK, say(a, b, "自行车还在吗？"));
    CHECK(t_int("message_id") > 0);
    CHECK_INT(a, t_int("from_id"));
    CHECK_INT(b, t_int("to_id"));
    CHECK_STR("自行车还在吗？", t_str("content"));
    CHECK_INT(0, t_int("goods_id"));
    CHECK(t_exists("is_read") && !t_bool("is_read"));
    CHECK(t_int("created_at") > 0);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM messages WHERE from_id = %lld AND to_id = %lld AND is_read = 0",
                           LL(a), LL(b)));

    CHECK_INT(1, t_push_count);
    CHECK_INT(b, t_push_user);
    CHECK_STR("chat.new_message", t_push_event);
    CHECK_STR("自行车还在吗？", t_push_str("content"));
    CHECK_INT(a, t_push_int("from_id"));
}

static void test_send_with_goods(void) {
    t_begin("发私信：带上商品");
    setup();
    int64_t g = t_goods(b, "台灯", 2000);
    char json[256];
    snprintf(json, sizeof json, "{'to_user_id':%lld,'content':'这个台灯还在吗','goods_id':%lld}", LL(b), LL(g));
    CHECK_CODE(OK, t_call(chat_send, a, json));
    CHECK_INT(g, t_int("goods_id"));
    snprintf(json, sizeof json, "{'to_user_id':%lld,'content':'不存在的商品','goods_id':99999}", LL(b));
    CHECK_CODE(ERR_NOT_FOUND, t_call(chat_send, a, json));
}

static void test_send_invalid(void) {
    t_begin("发私信参数检查");
    setup();
    CHECK_CODE(ERR_BAD_REQUEST, say(a, a, "自言自语"));
    CHECK_CODE(ERR_NOT_FOUND, say(a, 99999, "你是谁"));
    CHECK_CODE(ERR_BAD_REQUEST, say(a, b, ""));
    CHECK_CODE(ERR_BAD_REQUEST, say(a, b, t_rep("啊", 501)));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_send, a, "{'content':'发给谁'}"));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM messages"));
    CHECK_INT(0, t_push_count);
    CHECK_CODE(OK, say(a, b, t_rep("啊", 500)));  // 500 个字刚好可以
}

static void test_history(void) {
    t_begin("聊天记录：最近 N 条，从早到晚");
    setup();
    int64_t m[6];
    m[0] = msg(a, b, "1", 1);
    m[1] = msg(b, a, "2", 1);
    msg(a, c, "和 carol 的不算", 0);
    m[2] = msg(a, b, "3", 1);
    m[3] = msg(b, a, "4", 0);
    m[4] = msg(a, b, "5", 0);
    (void)m[0];

    char json[128];
    snprintf(json, sizeof json, "{'peer_id':%lld}", LL(b));
    CHECK_CODE(OK, t_call(chat_history, a, json));
    CHECK_INT(5, t_len("items"));
    CHECK_STR("1", t_str("items.0.content"));
    CHECK_STR("5", t_str("items.4.content"));

    snprintf(json, sizeof json, "{'peer_id':%lld,'limit':3}", LL(b));
    CHECK_CODE(OK, t_call(chat_history, a, json));
    CHECK_INT(3, t_len("items"));
    CHECK_INT(m[2], t_int("items.0.message_id"));
    CHECK_INT(m[4], t_int("items.2.message_id"));
    CHECK_INT(b, t_int("items.1.from_id"));

    snprintf(json, sizeof json, "{'peer_id':%lld,'limit':3,'before_id':%lld}", LL(b), LL(m[2]));
    CHECK_CODE(OK, t_call(chat_history, a, json));
    CHECK_INT(2, t_len("items"));
    CHECK_STR("1", t_str("items.0.content"));
    CHECK_STR("2", t_str("items.1.content"));

    snprintf(json, sizeof json, "{'peer_id':%lld}", LL(a));
    CHECK_CODE(OK, t_call(chat_history, b, json));  // 对方看到的是同一段对话
    CHECK_INT(5, t_len("items"));

    snprintf(json, sizeof json, "{'peer_id':%lld,'limit':51}", LL(b));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_history, a, json));
    snprintf(json, sizeof json, "{'peer_id':%lld,'limit':0}", LL(b));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_history, a, json));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_history, a, "{}"));
}

static void test_conversations(void) {
    t_begin("会话列表");
    setup();
    msg(b, a, "b 的第一条", 0);
    msg(a, b, "回复 b", 0);
    msg(c, a, "c 的消息", 0);
    msg(b, a, "b 的最后一条", 0);
    msg(b, c, "b 和 c 的对话不该出现", 0);
    msg(c, a, "c 又说了一句", 1);

    CHECK_CODE(OK, t_call(chat_conversations, a, "{}"));
    CHECK_INT(2, t_len("items"));
    CHECK_INT(c, t_int("items.0.peer.user_id"));  // 最近有消息的在前
    CHECK_STR("carol", t_str("items.0.peer.nickname"));
    CHECK_STR("c 又说了一句", t_str("items.0.last_message.content"));
    CHECK_INT(1, t_int("items.0.unread"));
    CHECK_INT(b, t_int("items.1.peer.user_id"));
    CHECK_STR("b 的最后一条", t_str("items.1.last_message.content"));
    CHECK_INT(2, t_int("items.1.unread"));  // 我发的"回复 b"不算未读

    CHECK_CODE(OK, t_call(chat_conversations, b, "{}"));
    CHECK_INT(2, t_len("items"));
    CHECK_INT(c, t_int("items.0.peer.user_id"));
    CHECK_INT(0, t_int("items.0.unread"));
    CHECK_INT(1, t_int("items.1.unread"));  // a 发给 b 的"回复 b"

    int64_t lonely = t_user("lonely", "123456", 0);
    CHECK_CODE(OK, t_call(chat_conversations, lonely, "{}"));
    CHECK_INT(0, t_len("items"));
}

static void test_mark_read(void) {
    t_begin("标记已读");
    setup();
    msg(b, a, "1", 0);
    msg(b, a, "2", 0);
    msg(b, a, "3", 0);
    msg(c, a, "c 的不动", 0);
    msg(a, b, "我发给 b 的不动", 0);

    char json[64];
    snprintf(json, sizeof json, "{'peer_id':%lld}", LL(b));
    CHECK_CODE(OK, t_call(chat_mark_read, a, json));
    CHECK_INT(3, t_int("updated"));
    CHECK_CODE(OK, t_call(chat_mark_read, a, json));
    CHECK_INT(0, t_int("updated"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM messages WHERE to_id = %lld AND is_read = 0", LL(a)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM messages WHERE to_id = %lld AND is_read = 0", LL(b)));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_mark_read, a, "{}"));
}

static void test_extra_types(void) {
    t_begin("新增：聊天字段类型、游标与Unicode边界"); setup();
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_send, a, "{'to_user_id':true,'content':'你好'}"));
    char json[256]; snprintf(json, sizeof json, "{'to_user_id':%lld,'content':'你好','goods_id':null}", LL(b));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_send, a, json));
    snprintf(json, sizeof json, "{'peer_id':%lld,'before_id':0}", LL(b));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_history, a, json));
    snprintf(json, sizeof json, "{'peer_id':%lld,'limit':1.5}", LL(b));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_history, a, json));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(chat_mark_read, a, "{'peer_id':-1}"));
    CHECK_CODE(OK, say(a, b, "中文与emoji😀不丢失"));
    CHECK_STR("中文与emoji😀不丢失", t_str("content"));
}


static void test_cursor_under_new_messages(void) {
    t_begin("新增：125条历史跨游标分页，新消息不挤入旧页"); setup();
    msg(c, a, "另一个会话不能混入", 0);
    int64_t ids[125];
    for (int i = 0; i < 125; ++i) ids[i] = msg(i % 2 ? a : b, i % 2 ? b : a, "分页", 0);
    int remaining = 125; int64_t cursor = 0; bool inserted = false;
    char json[160], path[80];
    while (remaining) {
        if (cursor) snprintf(json, sizeof json, "{'peer_id':%lld,'before_id':%lld,'limit':17}", LL(b), LL(cursor));
        else snprintf(json, sizeof json, "{'peer_id':%lld,'limit':17}", LL(b));
        CHECK_CODE(OK, t_call(chat_history, a, json));
        int n = t_len("items"); CHECK_INT(remaining < 17 ? remaining : 17, n);
        for (int i = 0; i < n; ++i) {
            snprintf(path, sizeof path, "items.%d.message_id", i);
            CHECK_INT(ids[remaining - n + i], t_int(path));
        }
        cursor = t_int("items.0.message_id"); remaining -= n;
        if (!inserted) { msg(a, b, "翻页中新消息", 0); inserted = true; }
    }
    snprintf(json, sizeof json, "{'peer_id':%lld,'before_id':%lld,'limit':17}", LL(b), LL(cursor));
    CHECK_CODE(OK, t_call(chat_history, a, json)); CHECK_INT(0, t_len("items"));
}

static int reject_read_after_insert(void *context, int operation, const char *first, const char *second, const char *database, const char *trigger) {
    (void)second; (void)database; (void)trigger;
    bool *inserted = context;
    if (operation == SQLITE_INSERT && first && strcmp(first, "messages") == 0) *inserted = true;
    return *inserted && operation == SQLITE_SELECT ? SQLITE_DENY : SQLITE_OK;
}

static void test_send_does_not_depend_on_followup_read(void) {
    t_begin("新增：写入后的读取故障不会造成已存消息却报发送失败"); setup();
    bool inserted = false;
    sqlite3_set_authorizer(t_db(), reject_read_after_insert, &inserted);
    int result = say(a, b, "不要写入成功又报失败");
    sqlite3_set_authorizer(t_db(), NULL, NULL);
    CHECK_CODE(OK, result); CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM messages"));
    CHECK_INT(1, t_push_count); CHECK_STR("不要写入成功又报失败", t_str("content"));
}

static void test_send_write_failure(void) {
    t_begin("新增：数据库写入失败不保存消息、不发送成功推送"); setup();
    t_sql("CREATE TRIGGER reject_message BEFORE INSERT ON messages BEGIN SELECT RAISE(ABORT,'chat injected'); END;");
    CHECK_CODE(ERR_INTERNAL, say(a, b, "写入应失败"));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM messages")); CHECK_INT(0, t_push_count);
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    t_sql("DROP TRIGGER reject_message");
    CHECK_CODE(OK, say(a, b, "故障恢复")); CHECK_INT(1, t_push_count);
}

static void test_unicode_boundaries(void) {
    t_begin("新增：500个emoji可发送，501个拒绝，特殊字符保持原文"); setup();
    CHECK_CODE(OK, say(a, b, t_rep("😀", 500)));
    CHECK_INT(2000, t_len_str("content"));
    CHECK_CODE(ERR_BAD_REQUEST, say(a, b, t_rep("😀", 501)));
    char json[256];
    snprintf(json, sizeof json, "{'to_user_id':%lld,'content':'中文\\n\\u0027 OR 1=1 -- 100%%_'}", LL(b));
    CHECK_CODE(OK, t_call(chat_send, a, json));
    CHECK_STR("中文\n' OR 1=1 -- 100%_", t_str("content"));
    CHECK_INT(3, t_sql_int("SELECT COUNT(*) FROM users"));
}

static void test_malformed_utf8(void) {
    t_begin("新增：拒绝非法UTF-8，避免保存和推送乱码字节"); setup();
    const char *bad[] = {"\xff", "\x80", "\xc0\xaf", "\xe2\x82", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        CHECK_CODE(ERR_BAD_REQUEST, say(a, b, bad[i]));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM messages")); CHECK_INT(0, t_push_count);
}

static int count_reads(unsigned event, void *context, void *statement, void *sql_text) {
    (void)event; (void)sql_text;
    const char *sql = sqlite3_sql(statement);
    if (sql && (!strncmp(sql, "SELECT", 6) || !strncmp(sql, "WITH", 4))) (*(int *)context)++;
    return 0;
}

static void test_conversations_query_budget(void) {
    t_begin("新增：80联系人会话列表避免N+1查询，资料和未读仍正确"); setup();
    int64_t first = 0;
    for (int i = 0; i < 80; ++i) {
        char name[32]; snprintf(name, sizeof name, "peer%d", i);
        int64_t peer = t_user(name, "Ab123456", 0); if (!i) first = peer;
        msg(peer, a, "收到未读", 0); msg(a, peer, "我发的不算", 0); msg(peer, a, "收到已读", 1);
    }
    int reads = 0; sqlite3_trace_v2(t_db(), SQLITE_TRACE_STMT, count_reads, &reads);
    int result = t_call(chat_conversations, a, "{}");
    sqlite3_trace_v2(t_db(), 0, NULL, NULL);
    CHECK_CODE(OK, result); CHECK_INT(80, t_len("items")); CHECK(reads <= 2);
    CHECK_INT(1, t_int("items.0.unread")); CHECK_INT(1, t_int("items.79.unread"));
    CHECK_INT(first, t_int("items.79.peer.user_id"));
    CHECK_STR("peer0", t_str("items.79.peer.nickname"));
    CHECK_CODE(OK, t_call(chat_unread_count, a, "{}")); CHECK_INT(80, t_int("count"));
    printf("[查询统计] 80联系人会话列表独立读取SQL=%d\n", reads);
}


static int next_deny_recipient(void *arg, int action, const char *table,
                               const char *column, const char *db, const char *trigger) {
    (void)arg; (void)column; (void)db; (void)trigger;
    return action == SQLITE_READ && table && !strcmp(table, "users") ? SQLITE_DENY : SQLITE_OK;
}
static int next_interrupt_query(void *arg) { (void)arg; return 1; }

static void test_next_recipient_query_failure(void) {
    t_begin("继续复查：收信人查询故障返回500，不能误报用户不存在"); setup();
    sqlite3_set_authorizer(t_db(), next_deny_recipient, NULL);
    int prepare_code = say(a, b, "准备失败不能发送");
    sqlite3_set_authorizer(t_db(), NULL, NULL);
    CHECK_CODE(ERR_INTERNAL, prepare_code);
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM messages")); CHECK_INT(0, t_push_count);
    sqlite3_progress_handler(t_db(), 1, next_interrupt_query, NULL);
    int step_code = say(a, b, "执行中断不能发送");
    sqlite3_progress_handler(t_db(), 0, NULL, NULL);
    CHECK_CODE(ERR_INTERNAL, step_code);
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM messages")); CHECK_INT(0, t_push_count);
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    CHECK_CODE(ERR_NOT_FOUND, say(a, 999999, "真正不存在仍返回404"));
    CHECK_CODE(OK, say(a, b, "恢复后只保存一次"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM messages")); CHECK_INT(1, t_push_count);
}

int main(void) {
    t_init("聊天模块测试");
    test_unread_count();
    test_send_ok();
    test_send_with_goods();
    test_send_invalid();
    test_history();
    test_conversations();
    test_mark_read();
    // 在这里加你自己的用例
    test_extra_types();
    test_cursor_under_new_messages();
    test_send_does_not_depend_on_followup_read();
    test_send_write_failure();
    test_unicode_boundaries();
    test_malformed_utf8();
    test_conversations_query_budget();
    test_next_recipient_query_failure();
    return t_done();
}
