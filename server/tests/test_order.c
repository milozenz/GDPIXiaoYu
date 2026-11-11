// 订单和收藏模块测试（负责人：组员D）
// 这些用例是写好的验收标准。你可以在后面加自己的用例，但不要删改已有的。
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "modules/order/order.h"
#include "t.h"

#define LL(x) ((long long)(x))

static int64_t seller, buyer, g;

// 每个用例的起点：卖家有一件 300 元的自行车，买家有 1000 元
static void setup(void) {
    seller = t_user("seller", "123456", 0);
    buyer = t_user("buyer", "123456", 100000);
    g = t_goods(seller, "山地自行车", 30000);
    t_sql("INSERT INTO goods_images VALUES (%lld, 0, 'cover.jpg')", LL(g));
}

static int64_t balance(int64_t uid) { return t_sql_int("SELECT balance FROM users WHERE id = %lld", LL(uid)); }

static const char *goods_status(int64_t goods_id) {
    static char s[16];
    sqlite3_stmt *st;
    sqlite3_prepare_v2(t_db(), "SELECT status FROM goods WHERE id = ?", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, goods_id);
    snprintf(s, sizeof s, "%s", sqlite3_step(st) == SQLITE_ROW ? (const char *)sqlite3_column_text(st, 0) : "?");
    sqlite3_finalize(st);
    return s;
}

static int call_on_order(Handler h, int64_t as_user, int64_t order_id) {
    char json[64];
    snprintf(json, sizeof json, "{'order_id':%lld}", LL(order_id));
    return t_call(h, as_user, json);
}

// 买家下单，返回订单 id
static int64_t buy(int64_t as_user, int64_t goods_id) {
    char json[64];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(goods_id));
    if (t_call(order_create, as_user, json) != OK) return 0;
    return t_int("order_id");
}

static void test_create_ok(void) {
    t_begin("下单成功：扣钱、商品变已卖出、推送给卖家");
    setup();
    int64_t o = buy(buyer, g);
    CHECK(o > 0);
    CHECK_STR("paid", t_str("status"));
    CHECK_INT(30000, t_int("price"));
    CHECK_INT(g, t_int("goods_id"));
    CHECK_STR("山地自行车", t_str("goods_title"));
    CHECK_STR("cover.jpg", t_str("goods_cover"));
    CHECK_INT(buyer, t_int("buyer.user_id"));
    CHECK_STR("seller", t_str("seller.nickname"));
    CHECK(t_int("created_at") > 0);
    CHECK_INT(0, t_int("shipped_at"));
    CHECK_INT(0, t_int("completed_at"));
    CHECK_INT(0, t_int("cancelled_at"));
    CHECK_STR("", t_str("ship_note"));

    const char *no = t_str("order_no");
    CHECK(no && strlen(no) == 18);
    bool digits = no != NULL;
    for (const char *p = no; p && *p; p++) digits = digits && isdigit((unsigned char)*p);
    CHECK(digits);
    CHECK(no && strncmp(no, "20", 2) == 0);

    CHECK_INT(70000, balance(buyer));   // 钱扣了
    CHECK_INT(0, balance(seller));      // 但还没到卖家手里（担保）
    CHECK_STR("sold", goods_status(g));

    CHECK_INT(1, t_push_count);
    CHECK_INT(seller, t_push_user);
    CHECK_STR("order.updated", t_push_event);
    CHECK_INT(o, t_push_int("order_id"));
}

static void test_create_snapshot(void) {
    t_begin("订单保存下单时的标题和价格快照");
    setup();
    int64_t o = buy(buyer, g);
    t_sql("UPDATE goods SET title = '改过的标题', price = 1 WHERE id = %lld", LL(g));
    CHECK_CODE(OK, call_on_order(order_detail, buyer, o));
    CHECK_STR("山地自行车", t_str("goods_title"));
    CHECK_INT(30000, t_int("price"));
}

static void test_create_fail(void) {
    t_begin("下单失败的各种情况");
    setup();
    CHECK_CODE(ERR_NOT_FOUND, t_call(order_create, buyer, "{'goods_id':99999}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_create, buyer, "{}"));

    char json[64];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));
    CHECK_CODE(ERR_FORBIDDEN, t_call(order_create, seller, json));  // 买自己的

    t_sql("UPDATE goods SET status = 'off_shelf' WHERE id = %lld", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(order_create, buyer, json));
    t_sql("UPDATE goods SET status = 'on_sale' WHERE id = %lld", LL(g));

    int64_t poor = t_user("poor", "123456", 29999);
    CHECK_CODE(ERR_NO_MONEY, t_call(order_create, poor, json));
    CHECK_INT(29999, balance(poor));         // 钱没少
    CHECK_STR("on_sale", goods_status(g));   // 商品也没被锁住（事务回滚了）
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM orders"));
    CHECK_INT(0, t_push_count);
}

static void test_no_oversell(void) {
    t_begin("防超卖：一件商品只能卖一次");
    setup();
    int64_t buyer2 = t_user("buyer2", "123456", 100000);
    CHECK(buy(buyer, g) > 0);
    char json[64];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(order_create, buyer2, json));
    CHECK_INT(100000, balance(buyer2));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE goods_id = %lld", LL(g)));
}

static void test_ship(void) {
    t_begin("发货");
    setup();
    int64_t o = buy(buyer, g);
    CHECK_CODE(ERR_FORBIDDEN, call_on_order(order_ship, buyer, o));

    char json[1024];
    snprintf(json, sizeof json, "{'order_id':%lld,'ship_note':'明天中午食堂门口'}", LL(o));
    t_push_count = 0;
    CHECK_CODE(OK, t_call(order_ship, seller, json));
    CHECK_STR("shipped", t_str("status"));
    CHECK_STR("明天中午食堂门口", t_str("ship_note"));
    CHECK(t_int("shipped_at") > 0);
    CHECK_INT(1, t_push_count);
    CHECK_INT(buyer, t_push_user);
    CHECK_STR("shipped", t_push_str("status"));

    CHECK_CODE(ERR_CONFLICT, call_on_order(order_ship, seller, o));  // 不能发两次
    CHECK_CODE(ERR_NOT_FOUND, call_on_order(order_ship, seller, 99999));

    int64_t g2 = t_goods(seller, "台灯", 100);
    int64_t o2 = buy(buyer, g2);
    snprintf(json, sizeof json, "{'order_id':%lld,'ship_note':'%s'}", LL(o2), t_rep("字", 101));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_ship, seller, json));
    CHECK_CODE(OK, call_on_order(order_ship, seller, o2));  // 不写备注也行
}

static void test_confirm(void) {
    t_begin("确认收货：钱打给卖家");
    setup();
    int64_t o = buy(buyer, g);
    CHECK_CODE(ERR_CONFLICT, call_on_order(order_confirm, buyer, o));  // 还没发货
    CHECK_CODE(OK, call_on_order(order_ship, seller, o));
    CHECK_CODE(ERR_FORBIDDEN, call_on_order(order_confirm, seller, o));

    t_push_count = 0;
    CHECK_CODE(OK, call_on_order(order_confirm, buyer, o));
    CHECK_STR("completed", t_str("status"));
    CHECK(t_int("completed_at") > 0);
    CHECK_INT(30000, balance(seller));
    CHECK_INT(70000, balance(buyer));
    CHECK_INT(1, t_push_count);
    CHECK_INT(seller, t_push_user);

    CHECK_CODE(ERR_CONFLICT, call_on_order(order_confirm, buyer, o));  // 不能确认两次
    CHECK_INT(30000, balance(seller));                                // 钱也不能打两次
}

static void test_cancel(void) {
    t_begin("取消订单：退钱、商品重新上架");
    setup();
    int64_t stranger = t_user("stranger", "123456", 0);
    int64_t o = buy(buyer, g);
    CHECK_CODE(ERR_FORBIDDEN, call_on_order(order_cancel, stranger, o));

    t_push_count = 0;
    CHECK_CODE(OK, call_on_order(order_cancel, buyer, o));
    CHECK_STR("cancelled", t_str("status"));
    CHECK(t_int("cancelled_at") > 0);
    CHECK_INT(100000, balance(buyer));
    CHECK_STR("on_sale", goods_status(g));
    CHECK_INT(1, t_push_count);
    CHECK_INT(seller, t_push_user);  // 买家取消，通知卖家
    CHECK_CODE(ERR_CONFLICT, call_on_order(order_cancel, buyer, o));
    CHECK_INT(100000, balance(buyer));  // 不能退两次钱

    // 取消后可以重新下单；卖家也能取消，这时通知买家
    int64_t o2 = buy(buyer, g);
    CHECK(o2 > 0);
    t_push_count = 0;
    CHECK_CODE(OK, call_on_order(order_cancel, seller, o2));
    CHECK_INT(buyer, t_push_user);

    // 已发货的不能取消
    int64_t o3 = buy(buyer, g);
    CHECK_CODE(OK, call_on_order(order_ship, seller, o3));
    CHECK_CODE(ERR_CONFLICT, call_on_order(order_cancel, buyer, o3));
    CHECK_STR("sold", goods_status(g));
}

static void test_detail(void) {
    t_begin("订单详情：只有买卖双方能看");
    setup();
    int64_t stranger = t_user("stranger", "123456", 0);
    int64_t o = buy(buyer, g);
    CHECK_CODE(OK, call_on_order(order_detail, buyer, o));
    CHECK_INT(o, t_int("order_id"));
    CHECK_INT(seller, t_int("seller.user_id"));
    CHECK_CODE(OK, call_on_order(order_detail, seller, o));
    CHECK_CODE(ERR_FORBIDDEN, call_on_order(order_detail, stranger, o));
    CHECK_CODE(ERR_NOT_FOUND, call_on_order(order_detail, buyer, 99999));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_detail, buyer, "{}"));
}

static void test_list(void) {
    t_begin("我买到的 / 我卖出的");
    setup();
    int64_t g2 = t_goods(seller, "台灯", 1000);
    int64_t g3 = t_goods(buyer, "买家自己卖的书", 500);
    t_sql("UPDATE users SET balance = 10000 WHERE id = %lld", LL(seller));
    int64_t o1 = buy(buyer, g);
    int64_t o2 = buy(buyer, g2);
    int64_t o3 = buy(seller, g3);
    CHECK(o1 && o2 && o3);
    CHECK_CODE(OK, call_on_order(order_ship, seller, o1));

    CHECK_CODE(OK, t_call(order_list, buyer, "{'role':'buyer'}"));
    CHECK_INT(2, t_int("total"));
    CHECK_INT(o2, t_int("items.0.order_id"));  // 最新的在前
    CHECK_INT(o1, t_int("items.1.order_id"));
    CHECK_STR("台灯", t_str("items.0.goods_title"));

    CHECK_CODE(OK, t_call(order_list, buyer, "{'role':'seller'}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(o3, t_int("items.0.order_id"));

    CHECK_CODE(OK, t_call(order_list, buyer, "{'role':'buyer','status':'shipped'}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(o1, t_int("items.0.order_id"));

    CHECK_CODE(OK, t_call(order_list, buyer, "{'role':'buyer','page':1,'page_size':1}"));
    CHECK_INT(2, t_int("total"));
    CHECK_INT(1, t_len("items"));

    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_list, buyer, "{}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_list, buyer, "{'role':'boss'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_list, buyer, "{'role':'buyer','status':'lost'}"));
}

// ======================== 收藏（选做） ========================

static void test_fav(void) {
    t_begin_optional("[选做] 收藏");
    setup();
    char json[64];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));
    CHECK_CODE(OK, t_call(fav_check, buyer, json));
    CHECK(!t_bool("favorite"));
    CHECK_CODE(OK, t_call(fav_add, buyer, json));
    CHECK_CODE(OK, t_call(fav_add, buyer, json));  // 重复收藏也算成功
    CHECK_CODE(OK, t_call(fav_check, buyer, json));
    CHECK(t_bool("favorite"));

    int64_t g2 = t_goods(seller, "台灯", 1000);
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g2));
    CHECK_CODE(OK, t_call(fav_add, buyer, json));
    t_sql("UPDATE favorites SET created_at = created_at + goods_id");
    CHECK_CODE(OK, t_call(fav_list, buyer, "{}"));
    CHECK_INT(2, t_int("total"));
    CHECK_INT(g2, t_int("items.0.goods_id"));
    CHECK_STR("seller", t_str("items.0.seller.nickname"));

    CHECK_CODE(OK, t_call(fav_remove, buyer, json));
    CHECK_CODE(OK, t_call(fav_remove, buyer, json));  // 没收藏也算成功
    CHECK_CODE(OK, t_call(fav_list, buyer, "{}"));
    CHECK_INT(1, t_int("total"));
    CHECK_CODE(ERR_NOT_FOUND, t_call(fav_add, buyer, "{'goods_id':99999}"));
}

static void test_extra_rollback(void) {
    t_begin("新增：订单插入失败时余额与商品状态全部回滚");
    setup();
    t_sql("CREATE TRIGGER reject_order BEFORE INSERT ON orders BEGIN SELECT RAISE(ABORT,'injected'); END;");
    char json[80]; snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));
    CHECK_CODE(ERR_INTERNAL, t_call(order_create, buyer, json));
    CHECK_INT(100000, balance(buyer)); CHECK_STR("on_sale", goods_status(g)); CHECK_INT(0, t_push_count);
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    t_sql("DROP TRIGGER reject_order"); CHECK(buy(buyer, g) > 0);
}

static void test_extra_credit_rollback(void) {
    t_begin("新增：入账失败不提交订单状态，也不发送推送");
    setup(); int64_t o = buy(buyer, g);
    CHECK_CODE(OK, call_on_order(order_ship, seller, o));
    t_sql("CREATE TRIGGER reject_credit BEFORE UPDATE OF balance ON users BEGIN SELECT RAISE(ABORT,'injected'); END;");
    t_push_count = 0;
    CHECK_CODE(ERR_INTERNAL, call_on_order(order_confirm, buyer, o));
    CHECK_INT(0, balance(seller)); CHECK_INT(0, t_push_count);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='shipped'", LL(o)));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    t_sql("DROP TRIGGER reject_credit"); CHECK_CODE(OK, call_on_order(order_confirm, buyer, o));
    CHECK_INT(30000, balance(seller));
}

static void test_extra_types(void) {
    t_begin("新增：订单与收藏严格校验类型和分页"); setup();
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_create, buyer, "{'goods_id':0}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_cancel, buyer, "{'order_id':1.5}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_list, buyer, "{'role':'buyer','status':null}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_list, buyer, "{'role':'buyer','page_size':51}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(fav_add, buyer, "{'goods_id':'1'}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(fav_list, buyer, "{'page':0}"));
}


static int review_count_reads(unsigned event, void *context, void *statement, void *sql_text) {
    (void)event; (void)sql_text;
    const char *sql = sqlite3_sql(statement);
    if (sql && (!strncmp(sql, "SELECT", 6) || !strncmp(sql, "WITH", 4))) (*(int *)context)++;
    return 0;
}

static void test_review_ship_utf8(void) {
    t_begin("复查：非法UTF-8交接备注不会推进订单状态"); setup();
    int64_t o = buy(buyer, g); t_push_count = 0; char json[128];
    snprintf(json, sizeof json, "{'order_id':%lld,'ship_note':'%s'}", LL(o), "\xff");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(order_ship, seller, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='paid'", LL(o)));
    CHECK_INT(0, t_push_count); CHECK(sqlite3_get_autocommit(t_db()) != 0);
}
static void test_review_order_query_budget(void) {
    t_begin("复查：50条订单批量读取双方资料和封面，仅两条读取SQL"); setup();
    for (int i = 0; i < 50; ++i) {
        int64_t item = t_goods(seller, "订单列表商品", 500);
        t_sql("INSERT INTO goods_images VALUES(%lld,5,'order.jpg')", LL(item)); CHECK(buy(buyer,item) > 0);
    }
    int reads = 0; sqlite3_trace_v2(t_db(), SQLITE_TRACE_STMT, review_count_reads, &reads);
    int result = t_call(order_list, buyer, "{'role':'buyer','page_size':50}");
    sqlite3_trace_v2(t_db(), 0, NULL, NULL);
    CHECK_CODE(OK, result); CHECK_INT(50, t_int("total")); CHECK_INT(50, t_len("items")); CHECK(reads <= 2);
    CHECK_INT(buyer, t_int("items.0.buyer.user_id")); CHECK_INT(seller, t_int("items.49.seller.user_id"));
    CHECK_STR("seller", t_str("items.0.seller.nickname")); CHECK_STR("order.jpg", t_str("items.49.goods_cover"));
    CHECK_INT(50, t_int("items.0.order_id")); CHECK_INT(1, t_int("items.49.order_id"));
    printf("[查询统计] 50条订单读取SQL=%d\n", reads);
}
static void test_review_order_balance_cap(void) {
    t_begin("复查：结算或退款账户到达上限时，资金和状态整体回滚"); setup();
    int64_t o = buy(buyer, g); CHECK_CODE(OK, call_on_order(order_ship, seller, o));
    t_sql("UPDATE users SET balance=9007199254740991 WHERE id=%lld", LL(seller)); t_push_count = 0;
    CHECK_CODE(ERR_CONFLICT, call_on_order(order_confirm, buyer, o));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='shipped'", LL(o))); CHECK_INT(0, t_push_count);
    t_sql("UPDATE users SET balance=0 WHERE id=%lld", LL(seller)); CHECK_CODE(OK, call_on_order(order_confirm, buyer, o));
    int64_t other = t_goods(seller, "退款边界商品", 1000); int64_t cancelled = buy(buyer, other); CHECK(cancelled > 0);
    t_sql("UPDATE users SET balance=9007199254740991 WHERE id=%lld", LL(buyer)); t_push_count = 0;
    CHECK_CODE(ERR_CONFLICT, call_on_order(order_cancel, buyer, cancelled));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='paid'", LL(cancelled)));
    CHECK_STR("sold", goods_status(other)); CHECK_INT(0, t_push_count); CHECK(sqlite3_get_autocommit(t_db()) != 0);
}
static void test_review_fav_query_budget(void) {
    t_begin_optional("复查：50条收藏批量返回商品与卖家，仅两条读取SQL"); setup();
    char json[80];
    for (int i = 0; i < 50; ++i) {
        int64_t item=t_goods(seller,"收藏列表商品",500);
        t_sql("INSERT INTO goods_images VALUES(%lld,5,'fav.jpg')", LL(item));
        snprintf(json,sizeof json,"{'goods_id':%lld}",LL(item)); CHECK_CODE(OK,t_call(fav_add,buyer,json));
    }
    int reads=0;sqlite3_trace_v2(t_db(),SQLITE_TRACE_STMT,review_count_reads,&reads);
    int result=t_call(fav_list,buyer,"{'page_size':50}");sqlite3_trace_v2(t_db(),0,NULL,NULL);
    CHECK_CODE(OK,result);CHECK_INT(50,t_int("total"));CHECK_INT(50,t_len("items"));CHECK(reads<=2);
    CHECK_STR("fav.jpg",t_str("items.0.cover"));CHECK_STR("seller",t_str("items.49.seller.nickname"));
    printf("[查询统计] 50条收藏读取SQL=%d\n",reads);
}


static int next_deny_order_profile(void *arg, int action, const char *table,
                                  const char *column, const char *db, const char *trigger) {
    (void)arg; (void)db; (void)trigger;
    return action == SQLITE_READ && table && column && !strcmp(table, "users") &&
        !strcmp(column, "nickname") ? SQLITE_DENY : SQLITE_OK;
}
static int next_call_blocked(Handler handler, int64_t user, int64_t id) {
    t_push_count = 0;
    sqlite3_set_authorizer(t_db(), next_deny_order_profile, NULL);
    int code = call_on_order(handler, user, id);
    sqlite3_set_authorizer(t_db(), NULL, NULL);
    return code;
}
static void test_next_cancel_relist_rollback(void) {
    t_begin("继续复查：退款后重新上架失败时，退款和订单一起回滚"); setup();
    int64_t o = buy(buyer, g); t_push_count = 0;
    t_sql("CREATE TRIGGER reject_relist BEFORE UPDATE OF status ON goods WHEN NEW.status='on_sale' BEGIN SELECT RAISE(ABORT,'relist-injected'); END");
    CHECK_CODE(ERR_INTERNAL, call_on_order(order_cancel, buyer, o));
    CHECK_INT(70000, balance(buyer)); CHECK_STR("sold", goods_status(g)); CHECK_INT(0, t_push_count);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='paid'", LL(o)));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    t_sql("DROP TRIGGER reject_relist"); CHECK_CODE(OK, call_on_order(order_cancel, buyer, o));
    CHECK_INT(100000, balance(buyer)); CHECK_STR("on_sale", goods_status(g)); CHECK_INT(1, t_push_count);
    CHECK_CODE(ERR_CONFLICT, call_on_order(order_cancel, buyer, o)); CHECK_INT(100000, balance(buyer));
}
static void test_next_order_response_rollback(void) {
    t_begin("继续复查：四种订单操作在回复资料读取失败时均整体回滚"); setup();
    char request[80]; snprintf(request, sizeof request, "{'goods_id':%lld}", LL(g));
    sqlite3_set_authorizer(t_db(), next_deny_order_profile, NULL);
    int create_code = t_call(order_create, buyer, request);
    sqlite3_set_authorizer(t_db(), NULL, NULL);
    CHECK_CODE(ERR_INTERNAL, create_code); CHECK_INT(100000, balance(buyer));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM orders")); CHECK_STR("on_sale", goods_status(g)); CHECK_INT(0, t_push_count);
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    int64_t o = buy(buyer, g);
    CHECK_CODE(ERR_INTERNAL, next_call_blocked(order_cancel, buyer, o));
    CHECK_INT(70000, balance(buyer)); CHECK_STR("sold", goods_status(g)); CHECK_INT(0, t_push_count);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='paid'", LL(o)));
    CHECK_CODE(ERR_INTERNAL, next_call_blocked(order_ship, seller, o)); CHECK_INT(0, t_push_count);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='paid' AND shipped_at=0", LL(o)));
    CHECK_CODE(OK, call_on_order(order_ship, seller, o));
    CHECK_CODE(ERR_INTERNAL, next_call_blocked(order_confirm, buyer, o));
    CHECK_INT(0, balance(seller)); CHECK_INT(70000, balance(buyer)); CHECK_INT(0, t_push_count);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM orders WHERE id=%lld AND status='shipped' AND completed_at=0", LL(o)));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    CHECK_CODE(OK, call_on_order(order_confirm, buyer, o)); CHECK_INT(30000, balance(seller)); CHECK_INT(1, t_push_count);
}

int main(void) {
    t_init("订单模块测试");
    test_create_ok();
    test_create_snapshot();
    test_create_fail();
    test_no_oversell();
    test_ship();
    test_confirm();
    test_cancel();
    test_detail();
    test_list();
    test_fav();
    // 在这里加你自己的用例
    test_extra_rollback();
    test_extra_credit_rollback();
    test_extra_types();
    test_review_ship_utf8();
    test_review_order_query_budget();
    test_review_order_balance_cap();
    test_review_fav_query_budget();
    test_next_cancel_relist_rollback();
    test_next_order_response_rollback();
    return t_done();
}
