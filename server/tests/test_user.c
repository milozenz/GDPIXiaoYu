// 用户模块测试（负责人：组员C）
// 保留原有场景与断言；按本次新密码规则更新合法注册样例，并追加边界检查。
#include <stdio.h>
#include <string.h>

#include "core/router.h"
#include "core/util.h"
#include "modules/user/user.h"
#include "t.h"

static void test_register_ok(void) {
    t_begin("注册成功");
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'alice','password':'Ab123456','nickname':'阿狸'}"));
    int64_t uid = t_int("user_id");
    CHECK(uid > 0);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE id = %lld AND nickname = '阿狸'", (long long)uid));
    CHECK_INT(0, t_sql_int("SELECT balance FROM users WHERE id = %lld", (long long)uid));
}

static void test_register_password_hashed(void) {
    t_begin("密码加盐哈希保存，不存明文");
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'alice','password':'Ab123456','nickname':'阿狸'}"));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM users WHERE password_hash LIKE '%%Ab123456%%'"));
    CHECK_INT(32, t_sql_int("SELECT length(salt) FROM users WHERE username = 'alice'"));
    CHECK_INT(64, t_sql_int("SELECT length(password_hash) FROM users WHERE username = 'alice'"));

    // 按协议规定的算法验证：sha256_hex(salt + 密码)
    sqlite3_stmt *st;
    sqlite3_prepare_v2(t_db(), "SELECT salt, password_hash FROM users WHERE username = 'alice'", -1, &st, NULL);
    CHECK(sqlite3_step(st) == SQLITE_ROW);
    char salted[128], hash[65];
    snprintf(salted, sizeof salted, "%sAb123456", (const char *)sqlite3_column_text(st, 0));
    sha256_hex(salted, strlen(salted), hash);
    CHECK_STR(hash, (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);

    // 两个人密码一样，盐不一样，哈希也不一样
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'bob','password':'Ab123456','nickname':'鲍勃'}"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(DISTINCT password_hash) = 2 FROM users"));
}

static void test_register_invalid(void) {
    t_begin("注册参数检查");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'ab','password':'Ab123456','nickname':'x'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'abcdefghijklmnopqrstu','password':'Ab123456','nickname':'x'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'al ice','password':'Ab123456','nickname':'x'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'阿狸狸','password':'Ab123456','nickname':'x'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'alice','password':'12345','nickname':'x'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'alice','password':'Ab1234567890Ab1234567890Ab1234567890123','nickname':'x'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'alice','password':'Ab123456','nickname':''}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':'alice','password':'Ab123456'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, "{'username':123,'password':'Ab123456','nickname':'x'}"));

    char json[512];
    snprintf(json, sizeof json, "{'username':'alice','password':'Ab123456','nickname':'%s'}", t_rep("鱼", 21));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
    snprintf(json, sizeof json, "{'username':'alice','password':'Ab123456','nickname':'%s'}", t_rep("鱼", 20));
    CHECK_CODE(OK, t_call(user_register, 0, json));  // 20 个汉字刚好可以

    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'A_b_9','password':'Ab123456','nickname':'x'}"));
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM users"));
}

static void test_register_duplicate(void) {
    t_begin("用户名重复");
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'alice','password':'Ab123456','nickname':'阿狸'}"));
    CHECK_CODE(ERR_CONFLICT, t_call(user_register, 0, "{'username':'alice','password':'Cd654321','nickname':'冒牌'}"));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users"));
}

static void test_login_ok(void) {
    t_begin("登录成功");
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'alice','password':'Ab123456','nickname':'阿狸'}"));
    int64_t uid = t_int("user_id");
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice','password':'Ab123456'}"));
    CHECK_INT(64, t_len_str("token"));
    CHECK_INT(uid, t_int("user.user_id"));
    CHECK_STR("alice", t_str("user.username"));
    CHECK_STR("阿狸", t_str("user.nickname"));
    CHECK_INT(0, t_int("user.balance"));

    // token 存进了 sessions，7 天后过期
    char token[65];
    snprintf(token, sizeof token, "%s", t_str("token") ? t_str("token") : "");
    CHECK_INT(uid, session_user(t_db(), token));
    int64_t days = t_sql_int("SELECT (expires_at - created_at) / 86400 FROM sessions WHERE token = '%s'", token);
    CHECK_INT(7, days);
}

static void test_login_with_test_user(void) {
    t_begin("登录 t_user 造的用户（验证哈希算法和协议一致）");
    int64_t uid = t_user("bob", "abcdef", 500);
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'bob','password':'abcdef'}"));
    CHECK_INT(uid, t_int("user.user_id"));
    CHECK_INT(500, t_int("user.balance"));
}

static void test_login_fail(void) {
    t_begin("登录失败");
    t_user("alice", "Ab123456", 0);
    CHECK_CODE(ERR_NOT_LOGIN, t_call(user_login, 0, "{'username':'alice','password':'wrong!'}"));
    char msg1[256];
    snprintf(msg1, sizeof msg1, "%s", t_msg());
    CHECK_CODE(ERR_NOT_LOGIN, t_call(user_login, 0, "{'username':'nobody','password':'Ab123456'}"));
    CHECK_STR(msg1, t_msg());  // 两种情况提示一样，不泄露用户名是否存在
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_login, 0, "{'username':'alice'}"));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM sessions"));
}

static void test_login_new_token_each_time(void) {
    t_begin("每次登录 token 都不一样");
    t_user("alice", "Ab123456", 0);
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice','password':'Ab123456'}"));
    char first[65];
    snprintf(first, sizeof first, "%s", t_str("token") ? t_str("token") : "");
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice','password':'Ab123456'}"));
    CHECK(t_str("token") && strcmp(first, t_str("token")) != 0);
}

static void test_logout(void) {
    t_begin("退出登录");
    int64_t uid = t_user("alice", "Ab123456", 0);
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'alice','password':'Ab123456'}"));
    char token[65];
    snprintf(token, sizeof token, "%s", t_str("token") ? t_str("token") : "");
    CHECK_INT(uid, session_user(t_db(), token));
    CHECK_CODE(OK, t_call_token(user_logout, uid, token, "{}"));
    CHECK_INT(0, session_user(t_db(), token));
}

static void test_me(void) {
    t_begin("查看自己的信息");
    int64_t uid = t_user("alice", "Ab123456", 12345);
    CHECK_CODE(OK, t_call(user_me, uid, "{}"));
    CHECK_INT(uid, t_int("user_id"));
    CHECK_STR("alice", t_str("username"));
    CHECK_STR("alice", t_str("nickname"));
    CHECK_STR("", t_str("avatar"));
    CHECK_STR("", t_str("bio"));
    CHECK_INT(12345, t_int("balance"));
    CHECK(t_int("created_at") > 0);
    CHECK(!t_exists("password_hash"));  // 绝对不能把密码哈希和盐发给客户端
    CHECK(!t_exists("salt"));
}

static void test_update_profile(void) {
    t_begin("修改个人资料");
    int64_t uid = t_user("alice", "Ab123456", 0);
    CHECK_CODE(OK, t_call(user_update_profile, uid, "{'nickname':'新昵称'}"));
    CHECK_STR("新昵称", t_str("nickname"));
    CHECK_STR("alice", t_str("username"));

    CHECK_CODE(OK, t_call(user_update_profile, uid, "{'bio':'大一新生，出闲置'}"));
    CHECK_STR("大一新生，出闲置", t_str("bio"));
    CHECK_STR("新昵称", t_str("nickname"));  // 没传的字段不变

    char json[512];
    const char *img = t_image();
    snprintf(json, sizeof json, "{'avatar':'%s'}", img);
    CHECK_CODE(OK, t_call(user_update_profile, uid, json));
    CHECK_STR(img, t_str("avatar"));
    CHECK_CODE(OK, t_call(user_update_profile, uid, "{'avatar':''}"));
    CHECK_STR("", t_str("avatar"));
}

static void test_update_profile_invalid(void) {
    t_begin("修改资料参数检查");
    int64_t uid = t_user("alice", "Ab123456", 0);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, uid, "{'nickname':''}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, uid, "{'avatar':'00000000000000000000000000000000.jpg'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, uid, "{'avatar':'../../secret.jpg'}"));
    char json[1024];
    snprintf(json, sizeof json, "{'bio':'%s'}", t_rep("字", 101));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, uid, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE nickname = 'alice' AND bio = ''"));  // 都没改成
}

static void test_get_user(void) {
    t_begin("查看别人的主页");
    int64_t me = t_user("alice", "Ab123456", 999);
    int64_t other = t_user("bob", "Ab123456", 888);
    t_goods(other, "台灯", 2000);
    t_goods(other, "风扇", 3000);
    int64_t sold = t_goods(other, "键盘", 5000);
    t_sql("UPDATE goods SET status = 'sold' WHERE id = %lld", (long long)sold);
    t_goods(me, "别人的商品不算", 100);

    char json[64];
    snprintf(json, sizeof json, "{'user_id':%lld}", (long long)other);
    CHECK_CODE(OK, t_call(user_get, me, json));
    CHECK_INT(other, t_int("user_id"));
    CHECK_STR("bob", t_str("nickname"));
    CHECK_INT(2, t_int("on_sale_count"));
    CHECK_INT(1, t_int("sold_count"));
    CHECK(!t_exists("balance"));   // 别人的余额不能看
    CHECK(!t_exists("password_hash"));

    CHECK_CODE(OK, t_call(user_get, 0, json));  // 没登录也能看
    CHECK_CODE(ERR_NOT_FOUND, t_call(user_get, 0, "{'user_id':99999}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_get, 0, "{}"));
}

static void test_recharge(void) {
    t_begin("充值");
    int64_t uid = t_user("alice", "Ab123456", 0);
    CHECK_CODE(OK, t_call(user_recharge, uid, "{'amount':10000}"));
    CHECK_INT(10000, t_int("balance"));
    CHECK_CODE(OK, t_call(user_recharge, uid, "{'amount':1000000}"));
    CHECK_INT(1010000, t_int("balance"));
    CHECK_INT(1010000, t_sql_int("SELECT balance FROM users WHERE id = %lld", (long long)uid));

    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_recharge, uid, "{'amount':0}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_recharge, uid, "{'amount':-100}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_recharge, uid, "{'amount':1000001}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_recharge, uid, "{'amount':'100'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_recharge, uid, "{'amount':1.5}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_recharge, uid, "{}"));
    CHECK_INT(1010000, t_sql_int("SELECT balance FROM users WHERE id = %lld", (long long)uid));
}

// 以下为新增用例；原有十四个验收用例保持不变。
static void test_profile_atomic_and_types(void) {
    t_begin("新增：资料先全部验证，类型错误不落库");
    int64_t u = t_user("alice", "Ab123456", 0);
    char json[1024];
    snprintf(json, sizeof json, "{'nickname':'不应保存','bio':'%s'}", t_rep("字", 101));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, u, json));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, u, "{'bio':null}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, u, "{'avatar':123}"));
    CHECK_CODE(OK, t_call(user_me, u, "{}"));
    CHECK_STR("alice", t_str("nickname"));
    CHECK_STR("", t_str("bio"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_get, 0, "{'user_id':0}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_get, 0, "{'user_id':1.5}"));
}

static void test_unicode_password(void) {
    t_begin("新增：中文密码按字符计数，公共主页不含用户名");
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'unicode','password':'Ab1你好世界小鱼','nickname':'小鱼'}"));
    CHECK_CODE(OK, t_call(user_login, 0, "{'username':'unicode','password':'Ab1你好世界小鱼'}"));
    int64_t u = t_int("user.user_id");
    char json[80];
    snprintf(json, sizeof json, "{'user_id':%lld}", (long long)u);
    CHECK_CODE(OK, t_call(user_get, 0, json));
    CHECK(!t_exists("username"));
    CHECK(!t_exists("salt"));
}

static void test_password_policy(void) {
    t_begin("新增：8位与32位边界、三类字符和弱密码不落库");
    const char *invalid[] = {"12345678", "abcdefgh", "ABCDEFGH", "Abcdefgh", "abc12345", "ABC12345", "Ab12345", "Ab1你好世界"};
    char json[512];
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        snprintf(json, sizeof json, "{'username':'reject','password':'%s','nickname':'测试'}", invalid[i]);
        CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
        CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM users"));
        CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM sessions"));
    }
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'exact8','password':'Ab123456','nickname':'边界'}"));
    char password[35] = "Ab1";
    memset(password + 3, 'a', 29); password[32] = 0;
    snprintf(json, sizeof json, "{'username':'exact32','password':'%s','nickname':'边界'}", password);
    CHECK_CODE(OK, t_call(user_register, 0, json));
    password[32] = 'a'; password[33] = 0;
    snprintf(json, sizeof json, "{'username':'over32','password':'%s','nickname':'边界'}", password);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM users"));
    CHECK_CODE(OK, t_call(user_register, 0, "{'username':'unicode8','password':'Ab1你好世界鱼','nickname':'八字符'}"));
}


static int review_count_reads(unsigned event, void *context, void *statement, void *sql_text) {
    (void)event; (void)sql_text;
    const char *sql = sqlite3_sql(statement);
    if (sql && (!strncmp(sql, "SELECT", 6) || !strncmp(sql, "WITH", 4))) (*(int *)context)++;
    return 0;
}

static int review_deny_profile_read(void *context, int action, const char *table, const char *column, const char *database, const char *trigger) {
    (void)column; (void)database; (void)trigger;
    bool *updated = context;
    if (action == SQLITE_UPDATE && table && strcmp(table, "users") == 0) *updated = true;
    return *updated && action == SQLITE_SELECT ? SQLITE_DENY : SQLITE_OK;
}
static void test_review_profile_rollback(void) {
    t_begin("复查：更新后的读取失败，个人资料必须整体回滚");
    int64_t u = t_user("alice", "Ab123456", 0); bool updated = false;
    sqlite3_set_authorizer(t_db(), review_deny_profile_read, &updated);
    int result = t_call(user_update_profile, u, "{'nickname':'不应保存','bio':'不应保存'}");
    sqlite3_set_authorizer(t_db(), NULL, NULL);
    CHECK_CODE(ERR_INTERNAL, result);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE id=%lld AND nickname='alice' AND bio=''", (long long)u));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
}
static void test_review_user_utf8(void) {
    t_begin("复查：用户昵称、简介和新注册密码拒绝坏UTF-8");
    char json[256];
    snprintf(json, sizeof json, "{'username':'badnick','password':'Ab123456','nickname':'%s'}", "\xff");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
    snprintf(json, sizeof json, "{'username':'badpass','password':'Ab12345%s','nickname':'正常'}", "\xff");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_register, 0, json));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM users"));
    int64_t u = t_user("alice", "Ab123456", 0);
    snprintf(json, sizeof json, "{'nickname':'不应更新','bio':'%s'}", "\x80");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_update_profile, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM users WHERE id=%lld AND nickname='alice' AND bio=''", (long long)u));
}
static void test_review_login_limits(void) {
    t_begin("复查：空登录明确拒绝，超长密码在查询前结束");
    t_user("alice", "Ab123456", 0);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(user_login, 0, "{'username':'alice','password':''}"));
    char json[256]; snprintf(json, sizeof json, "{'username':'alice','password':'%s'}", t_rep("a", 128));
    int reads = 0; sqlite3_trace_v2(t_db(), SQLITE_TRACE_STMT, review_count_reads, &reads);
    int result = t_call(user_login, 0, json);
    sqlite3_trace_v2(t_db(), 0, NULL, NULL);
    CHECK_CODE(ERR_NOT_LOGIN, result); CHECK_INT(0, reads);
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM sessions"));
}
static void test_review_balance_cap(void) {
    t_begin("复查：充值达到精确金额上限后拒绝继续增加");
    int64_t u = t_user("alice", "Ab123456", 9007199254740941LL);
    CHECK_CODE(OK, t_call(user_recharge, u, "{'amount':50}")); CHECK_INT(9007199254740991LL, t_int("balance"));
    CHECK_CODE(ERR_CONFLICT, t_call(user_recharge, u, "{'amount':1}"));
    CHECK_INT(9007199254740991LL, t_sql_int("SELECT balance FROM users WHERE id=%lld", (long long)u));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
}

int main(void) {
    t_init("用户模块测试");
    test_register_ok();
    test_register_password_hashed();
    test_register_invalid();
    test_register_duplicate();
    test_login_ok();
    test_login_with_test_user();
    test_login_fail();
    test_login_new_token_each_time();
    test_logout();
    test_me();
    test_update_profile();
    test_update_profile_invalid();
    test_get_user();
    test_recharge();
    // 在这里加你自己的用例
    test_profile_atomic_and_types();
    test_unicode_password();
    test_password_policy();
    test_review_profile_rollback();
    test_review_user_utf8();
    test_review_login_limits();
    test_review_balance_cap();
    return t_done();
}
