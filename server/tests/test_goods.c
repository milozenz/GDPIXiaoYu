// 商品模块测试（负责人：组员C）
// 这些用例是写好的验收标准。你可以在后面加自己的用例，但不要删改已有的。
#include <stdio.h>
#include <string.h>

#include "modules/goods/goods.h"
#include "t.h"

#define LL(x) ((long long)(x))

static void test_categories(void) {
    t_begin("分类列表（例子，已经写好）");
    CHECK_CODE(OK, t_call(goods_categories, 0, "{}"));
    CHECK_INT(7, t_len("items"));
    CHECK_INT(1, t_int("items.0.category_id"));
    CHECK_STR("数码电子", t_str("items.0.name"));
    CHECK_STR("其他", t_str("items.6.name"));
}

static void test_publish_ok(void) {
    t_begin("发布商品");
    int64_t u = t_user("alice", "123456", 0);
    char img1[64], img2[64], json[1024];
    snprintf(img1, sizeof img1, "%s", t_image());
    snprintf(img2, sizeof img2, "%s", t_image());
    snprintf(json, sizeof json,
             "{'title':'九成新山地自行车','description':'骑了一学期','price':32000,'original_price':80000,"
             "'category_id':5,'condition':1,'images':['%s','%s']}", img1, img2);
    CHECK_CODE(OK, t_call(goods_publish, u, json));
    int64_t g = t_int("goods_id");
    CHECK(g > 0);
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id = %lld AND seller_id = %lld AND status = 'on_sale'"
                           " AND price = 32000 AND original_price = 80000 AND category_id = 5 AND condition = 1"
                           " AND title = '九成新山地自行车' AND description = '骑了一学期'", LL(g), LL(u)));
    CHECK_INT(2, t_sql_int("SELECT COUNT(*) FROM goods_images WHERE goods_id = %lld", LL(g)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods_images WHERE goods_id = %lld AND sort = 0 AND image = '%s'",
                           LL(g), img1));
    CHECK(t_sql_int("SELECT created_at FROM goods WHERE id = %lld", LL(g)) > 0);
}

static void test_publish_defaults(void) {
    t_begin("发布商品：可选字段用默认值");
    int64_t u = t_user("alice", "123456", 0);
    CHECK_CODE(OK, t_call(goods_publish, u, "{'title':'旧台灯','price':1500,'category_id':3}"));
    int64_t g = t_int("goods_id");
    CHECK_INT(2, t_sql_int("SELECT condition FROM goods WHERE id = %lld", LL(g)));
    CHECK_INT(0, t_sql_int("SELECT original_price FROM goods WHERE id = %lld", LL(g)));
    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM goods_images WHERE goods_id = %lld", LL(g)));
}

static void test_publish_invalid(void) {
    t_begin("发布商品参数检查");
    int64_t u = t_user("alice", "123456", 0);
    char json[8192];

    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'','price':100,'category_id':1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'price':100,'category_id':1}"));
    snprintf(json, sizeof json, "{'title':'%s','price':100,'category_id':1}", t_rep("书", 31));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, json));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':0,'category_id':1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':99999901,'category_id':1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':12.5,'category_id':1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','category_id':1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':100,'category_id':99}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':100}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':100,'category_id':1,'condition':4}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':100,'category_id':1,'condition':-1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':100,'category_id':1,'original_price':-1}"));
    snprintf(json, sizeof json, "{'title':'书','price':100,'category_id':1,'description':'%s'}", t_rep("好", 501));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, json));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u,
               "{'title':'书','price':100,'category_id':1,'images':['00000000000000000000000000000000.jpg']}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'书','price':100,'category_id':1,'images':'a.jpg'}"));

    // 10 张图太多
    int len = snprintf(json, sizeof json, "{'title':'书','price':100,'category_id':1,'images':[");
    for (int i = 0; i < 10; i++) len += snprintf(json + len, sizeof json - len, "%s'%s'", i ? "," : "", t_image());
    snprintf(json + len, sizeof json - len, "]}");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, json));

    CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM goods"));  // 上面全都不应该插进去

    // 边界值是允许的
    snprintf(json, sizeof json, "{'title':'%s','price':99999900,'category_id':7,'condition':0,'description':'%s'}",
             t_rep("书", 30), t_rep("好", 500));
    CHECK_CODE(OK, t_call(goods_publish, u, json));
    CHECK_CODE(OK, t_call(goods_publish, u, "{'title':'书','price':1,'category_id':1,'condition':3}"));
}

static int64_t publish_with_images(int64_t u, const char *title, int64_t price, int n_images, char first[64]) {
    char json[2048];
    int len = snprintf(json, sizeof json, "{'title':'%s','description':'描述%s','price':%lld,'category_id':2,"
                       "'condition':1,'original_price':9900,'images':[", title, title, LL(price));
    for (int i = 0; i < n_images; i++) {
        const char *img = t_image();
        if (i == 0 && first) snprintf(first, 64, "%s", img);
        len += snprintf(json + len, sizeof json - len, "%s'%s'", i ? "," : "", img);
    }
    snprintf(json + len, sizeof json - len, "]}");
    if (t_call(goods_publish, u, json) != OK) return 0;
    return t_int("goods_id");
}

static void test_detail(void) {
    t_begin("商品详情");
    int64_t u = t_user("alice", "123456", 0);
    char cover[64] = "";
    int64_t g = publish_with_images(u, "高数教材", 2500, 2, cover);
    CHECK(g > 0);

    char json[64];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));
    CHECK_CODE(OK, t_call(goods_detail, 0, json));
    CHECK_INT(g, t_int("goods_id"));
    CHECK_STR("高数教材", t_str("title"));
    CHECK_STR("描述高数教材", t_str("description"));
    CHECK_INT(2500, t_int("price"));
    CHECK_INT(9900, t_int("original_price"));
    CHECK_INT(2, t_int("category_id"));
    CHECK_STR("书籍教材", t_str("category_name"));
    CHECK_INT(1, t_int("condition"));
    CHECK_STR("on_sale", t_str("status"));
    CHECK_INT(2, t_len("images"));
    CHECK_STR(cover, t_str("images.0"));
    CHECK_STR(cover, t_str("cover"));
    CHECK_INT(u, t_int("seller.user_id"));
    CHECK_STR("alice", t_str("seller.nickname"));
    CHECK(t_exists("seller.avatar"));
    CHECK(t_int("created_at") > 0);
    CHECK(t_int("updated_at") > 0);
    CHECK_INT(1, t_int("view_count"));  // 先 +1 再返回

    CHECK_CODE(OK, t_call(goods_detail, u, json));
    CHECK_INT(2, t_int("view_count"));

    t_sql("UPDATE goods SET status = 'sold' WHERE id = %lld", LL(g));
    CHECK_CODE(OK, t_call(goods_detail, 0, json));  // 已卖出的也能看
    CHECK_STR("sold", t_str("status"));

    CHECK_CODE(ERR_NOT_FOUND, t_call(goods_detail, 0, "{'goods_id':99999}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_detail, 0, "{}"));
}

static void test_update(void) {
    t_begin("修改商品");
    int64_t u = t_user("alice", "123456", 0);
    int64_t other = t_user("bob", "123456", 0);
    int64_t g = publish_with_images(u, "台灯", 2000, 3, NULL);
    char json[256];

    snprintf(json, sizeof json, "{'goods_id':%lld,'price':1800,'title':'护眼台灯'}", LL(g));
    CHECK_CODE(OK, t_call(goods_update, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id = %lld AND price = 1800 AND title = '护眼台灯'"
                           " AND description = '描述台灯' AND category_id = 2", LL(g)));  // 没传的不变
    CHECK_INT(3, t_sql_int("SELECT COUNT(*) FROM goods_images WHERE goods_id = %lld", LL(g)));  // 没传 images 就不动

    snprintf(json, sizeof json, "{'goods_id':%lld,'images':['%s']}", LL(g), t_image());
    CHECK_CODE(OK, t_call(goods_update, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods_images WHERE goods_id = %lld", LL(g)));  // 整体替换

    snprintf(json, sizeof json, "{'goods_id':%lld,'price':1}", LL(g));
    CHECK_CODE(ERR_FORBIDDEN, t_call(goods_update, other, json));
    snprintf(json, sizeof json, "{'goods_id':%lld,'price':0}", LL(g));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_update, u, json));
    snprintf(json, sizeof json, "{'goods_id':%lld,'title':'%s'}", LL(g), t_rep("灯", 31));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_update, u, json));
    CHECK_INT(1800, t_sql_int("SELECT price FROM goods WHERE id = %lld", LL(g)));

    CHECK_CODE(ERR_NOT_FOUND, t_call(goods_update, u, "{'goods_id':99999,'price':1}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_update, u, "{'price':1}"));

    t_sql("UPDATE goods SET status = 'sold' WHERE id = %lld", LL(g));
    snprintf(json, sizeof json, "{'goods_id':%lld,'price':1}", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_update, u, json));
}

static void test_shelf(void) {
    t_begin("下架和重新上架");
    int64_t u = t_user("alice", "123456", 0);
    int64_t other = t_user("bob", "123456", 0);
    int64_t g = t_goods(u, "风扇", 3000);
    char json[64];
    snprintf(json, sizeof json, "{'goods_id':%lld}", LL(g));

    CHECK_CODE(ERR_FORBIDDEN, t_call(goods_off_shelf, other, json));
    CHECK_CODE(OK, t_call(goods_off_shelf, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id = %lld AND status = 'off_shelf'", LL(g)));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_off_shelf, u, json));

    CHECK_CODE(ERR_FORBIDDEN, t_call(goods_on_shelf, other, json));
    CHECK_CODE(OK, t_call(goods_on_shelf, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id = %lld AND status = 'on_sale'", LL(g)));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_on_shelf, u, json));

    t_sql("UPDATE goods SET status = 'sold' WHERE id = %lld", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_off_shelf, u, json));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_on_shelf, u, json));

    CHECK_CODE(ERR_NOT_FOUND, t_call(goods_off_shelf, u, "{'goods_id':99999}"));
    CHECK_CODE(ERR_NOT_FOUND, t_call(goods_on_shelf, u, "{'goods_id':99999}"));
}

// 造一批商品：id 越大发布越晚
static void make_search_data(int64_t *u_out, int64_t ids[6]) {
    int64_t u = t_user("alice", "123456", 0);
    ids[0] = t_goods(u, "山地自行车", 32000);
    ids[1] = t_goods(u, "公路自行车头盔", 8000);
    ids[2] = t_goods(u, "高数教材", 2500);
    ids[3] = t_goods(u, "100%新 耳机", 15000);
    ids[4] = t_goods(u, "已下架的自行车", 100);
    ids[5] = t_goods(u, "已卖出的自行车", 200);
    for (int i = 0; i < 6; i++)
        t_sql("UPDATE goods SET created_at = %lld, view_count = %d WHERE id = %lld",
              LL(1700000000 + i * 100), (i * 37) % 11, LL(ids[i]));
    t_sql("UPDATE goods SET category_id = 2 WHERE id = %lld", LL(ids[2]));
    t_sql("UPDATE goods SET description = '送一辆自行车打气筒' WHERE id = %lld", LL(ids[3]));
    t_sql("UPDATE goods SET status = 'off_shelf' WHERE id = %lld", LL(ids[4]));
    t_sql("UPDATE goods SET status = 'sold' WHERE id = %lld", LL(ids[5]));
    *u_out = u;
}

static void test_search_basic(void) {
    t_begin("搜索：默认只返回在售，最新的在前");
    int64_t u, ids[6];
    make_search_data(&u, ids);
    CHECK_CODE(OK, t_call(goods_search, 0, "{}"));
    CHECK_INT(4, t_int("total"));
    CHECK_INT(1, t_int("page"));
    CHECK_INT(20, t_int("page_size"));
    CHECK_INT(4, t_len("items"));
    CHECK_INT(ids[3], t_int("items.0.goods_id"));
    CHECK_INT(ids[0], t_int("items.3.goods_id"));
    CHECK_STR("on_sale", t_str("items.0.status"));
    CHECK_INT(u, t_int("items.0.seller.user_id"));
    CHECK_STR("alice", t_str("items.0.seller.nickname"));
    CHECK(t_exists("items.0.cover"));
    CHECK(t_exists("items.0.view_count"));
    CHECK(t_exists("items.0.created_at"));
    CHECK(!t_exists("items.0.description"));  // 列表里不需要描述，省流量
}

static void test_search_keyword(void) {
    t_begin("搜索：关键词匹配标题或描述");
    int64_t u, ids[6];
    make_search_data(&u, ids);
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'自行车'}"));
    CHECK_INT(3, t_int("total"));  // 两个标题 + 一个描述里有；下架和卖出的不算
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'教材'}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(ids[2], t_int("items.0.goods_id"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'%'}"));
    CHECK_INT(1, t_int("total"));  // % 只当普通字符，不能当通配符
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'没有这个东西'}"));
    CHECK_INT(0, t_int("total"));
    CHECK_INT(0, t_len("items"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':''}"));
    CHECK_INT(4, t_int("total"));  // 空关键词 = 不过滤
}

static void test_search_filter_sort(void) {
    t_begin("搜索：分类、价格区间、排序");
    int64_t u, ids[6];
    make_search_data(&u, ids);
    CHECK_CODE(OK, t_call(goods_search, 0, "{'category_id':2}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(ids[2], t_int("items.0.goods_id"));

    CHECK_CODE(OK, t_call(goods_search, 0, "{'min_price':8000,'max_price':15000}"));
    CHECK_INT(2, t_int("total"));  // 8000 和 15000 都包含

    CHECK_CODE(OK, t_call(goods_search, 0, "{'sort':'price_asc'}"));
    CHECK_INT(2500, t_int("items.0.price"));
    CHECK_INT(32000, t_int("items.3.price"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'sort':'price_desc'}"));
    CHECK_INT(32000, t_int("items.0.price"));
    CHECK_INT(2500, t_int("items.3.price"));

    t_sql("UPDATE goods SET view_count = 50 WHERE id = %lld", LL(ids[2]));
    t_sql("UPDATE goods SET view_count = 50 WHERE id = %lld", LL(ids[0]));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'sort':'hot'}"));
    CHECK_INT(ids[2], t_int("items.0.goods_id"));  // 浏览量一样，新发布的在前
    CHECK_INT(ids[0], t_int("items.1.goods_id"));

    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'自行车','sort':'price_asc'}"));
    CHECK_INT(8000, t_int("items.0.price"));

    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, "{'sort':'random'}"));
}

static void test_search_seller(void) {
    t_begin("搜索：按卖家筛选");
    int64_t u, ids[6];
    make_search_data(&u, ids);
    int64_t bob = t_user("bob", "123456", 0);
    int64_t g = t_goods(bob, "bob 的台灯", 1000);
    char json[64];
    snprintf(json, sizeof json, "{'seller_id':%lld}", LL(bob));
    CHECK_CODE(OK, t_call(goods_search, 0, json));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(g, t_int("items.0.goods_id"));
    snprintf(json, sizeof json, "{'seller_id':%lld}", LL(u));
    CHECK_CODE(OK, t_call(goods_search, 0, json));
    CHECK_INT(4, t_int("total"));  // 下架和卖出的不算
}

static void test_search_paging(void) {
    t_begin("搜索：分页");
    int64_t u = t_user("alice", "123456", 0);
    for (int i = 0; i < 25; i++) {
        char title[32];
        snprintf(title, sizeof title, "商品%d", i);
        int64_t g = t_goods(u, title, 100 + i);
        t_sql("UPDATE goods SET created_at = %lld WHERE id = %lld", LL(1700000000 + i), LL(g));
    }
    CHECK_CODE(OK, t_call(goods_search, 0, "{'page':1,'page_size':10}"));
    CHECK_INT(25, t_int("total"));
    CHECK_INT(10, t_len("items"));
    CHECK_STR("商品24", t_str("items.0.title"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'page':3,'page_size':10}"));
    CHECK_INT(25, t_int("total"));
    CHECK_INT(5, t_len("items"));
    CHECK_STR("商品4", t_str("items.0.title"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'page':4,'page_size':10}"));
    CHECK_INT(0, t_len("items"));

    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, "{'page':0}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, "{'page_size':51}"));
}

static void test_mine(void) {
    t_begin("我发布的");
    int64_t u = t_user("alice", "123456", 0);
    int64_t other = t_user("bob", "123456", 0);
    int64_t a = t_goods(u, "在售的", 100);
    int64_t b = t_goods(u, "卖掉的", 200);
    int64_t c = t_goods(u, "下架的", 300);
    t_goods(other, "别人的", 400);
    t_sql("UPDATE goods SET created_at = created_at + id");
    t_sql("UPDATE goods SET status = 'sold' WHERE id = %lld", LL(b));
    t_sql("UPDATE goods SET status = 'off_shelf' WHERE id = %lld", LL(c));

    CHECK_CODE(OK, t_call(goods_mine, u, "{}"));
    CHECK_INT(3, t_int("total"));
    CHECK_INT(c, t_int("items.0.goods_id"));
    CHECK_INT(a, t_int("items.2.goods_id"));
    CHECK_CODE(OK, t_call(goods_mine, u, "{'status':'sold'}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(b, t_int("items.0.goods_id"));
    CHECK_STR("sold", t_str("items.0.status"));
    CHECK_CODE(OK, t_call(goods_mine, u, "{'status':'on_sale','page_size':1}"));
    CHECK_INT(1, t_len("items"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_mine, u, "{'status':'deleted'}"));
}

// 以下为新增用例；原有十三个验收用例保持不变。
static void test_extra_types_and_priority(void) {
    t_begin("新增：严格类型检查及权限错误优先级");
    int64_t u = t_user("alice", "123456", 0), other = t_user("bob", "123456", 0);
    int64_t g = t_goods(u, "台灯", 100);
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'灯','price':100,'category_id':1,'condition':null}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, "{'title':'灯','price':100,'category_id':1,'images':[123]}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, "{'keyword':3}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, "{'min_price':200,'max_price':100}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, "{'seller_id':0}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_mine, u, "{'status':null}"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'page':9007199254740991,'page_size':50}"));
    CHECK_INT(0, t_len("items"));
    char json[128];
    snprintf(json, sizeof json, "{'goods_id':%lld,'price':0}", LL(g));
    CHECK_CODE(ERR_FORBIDDEN, t_call(goods_update, other, json));
    t_sql("UPDATE goods SET status='sold' WHERE id=%lld", LL(g));
    CHECK_CODE(ERR_CONFLICT, t_call(goods_update, u, json));
}

static void test_image_rollback(void) {
    t_begin("新增：图片写入失败时整笔事务回滚");
    int64_t u = t_user("alice", "123456", 0);
    int64_t g = publish_with_images(u, "原始商品", 100, 1, NULL);
    t_sql("CREATE TRIGGER fail_image BEFORE INSERT ON goods_images BEGIN SELECT RAISE(ABORT,'test rollback'); END;");
    char json[256];
    snprintf(json, sizeof json, "{'goods_id':%lld,'title':'不应保存','images':['%s']}", LL(g), t_image());
    CHECK_CODE(ERR_INTERNAL, t_call(goods_update, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND title='原始商品'", LL(g)));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods_images WHERE goods_id=%lld", LL(g)));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    snprintf(json, sizeof json, "{'title':'不应新增','price':100,'category_id':1,'images':['%s']}", t_image());
    CHECK_CODE(ERR_INTERNAL, t_call(goods_publish, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods"));
    CHECK(sqlite3_get_autocommit(t_db()) != 0);
    t_sql("DROP TRIGGER fail_image");
    CHECK_CODE(OK, t_call(goods_publish, u, "{'title':'恢复正常','price':100,'category_id':1}"));
}

static void test_ties_and_literal_keyword(void) {
    t_begin("新增：同时间同价格按编号降序，通配符不展开");
    int64_t u = t_user("alice", "123456", 0);
    int64_t a = t_goods(u, "带_下划线", 100);
    int64_t b = t_goods(u, "普通教材", 100);
    t_sql("UPDATE goods SET created_at=1700000000");
    CHECK_CODE(OK, t_call(goods_search, 0, "{'sort':'price_asc'}"));
    CHECK_INT(b, t_int("items.0.goods_id"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'_'}"));
    CHECK_INT(1, t_int("total"));
    CHECK_INT(a, t_int("items.0.goods_id"));
    CHECK_CODE(OK, t_call(goods_search, 0, "{'keyword':'\\u0027 OR 1=1 --'}"));
    CHECK_INT(0, t_int("total"));
}


static int review_count_reads(unsigned event, void *context, void *statement, void *sql_text) {
    (void)event; (void)sql_text;
    const char *sql = sqlite3_sql(statement);
    if (sql && (!strncmp(sql, "SELECT", 6) || !strncmp(sql, "WITH", 4))) (*(int *)context)++;
    return 0;
}

static void test_review_goods_utf8(void) {
    t_begin("复查：商品标题、描述和关键词拒绝坏UTF-8");
    int64_t u = t_user("alice", "Ab123456", 0); char json[256];
    snprintf(json, sizeof json, "{'title':'%s','price':100,'category_id':1}", "\xff");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_publish, u, json)); CHECK_INT(0, t_sql_int("SELECT COUNT(*) FROM goods"));
    int64_t g = t_goods(u, "正常标题", 100);
    snprintf(json, sizeof json, "{'goods_id':%lld,'title':'不应保存','description':'%s'}", LL(g), "\xe2\x82");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_update, u, json));
    CHECK_INT(1, t_sql_int("SELECT COUNT(*) FROM goods WHERE id=%lld AND title='正常标题'", LL(g)));
    snprintf(json, sizeof json, "{'keyword':'%s'}", "\xff");
    CHECK_CODE(ERR_BAD_REQUEST, t_call(goods_search, 0, json));
}
static void test_review_goods_query_budget(void) {
    t_begin("复查：50件商品批量返回封面与卖家，仅两条读取SQL");
    int64_t u = t_user("alice", "Ab123456", 0);
    for (int i = 0; i < 50; ++i) {
        char title[64]; snprintf(title, sizeof title, "列表商品%d", i);
        int64_t g = t_goods(u, title, 10000 + i);
        t_sql("INSERT INTO goods_images VALUES(%lld,5,'first.jpg'),(%lld,7,'later.jpg')", LL(g), LL(g));
    }
    int reads = 0; sqlite3_trace_v2(t_db(), SQLITE_TRACE_STMT, review_count_reads, &reads);
    int result = t_call(goods_search, 0, "{'page_size':50}");
    sqlite3_trace_v2(t_db(), 0, NULL, NULL);
    CHECK_CODE(OK, result); CHECK_INT(50, t_int("total")); CHECK_INT(50, t_len("items")); CHECK(reads <= 2);
    CHECK_STR("first.jpg", t_str("items.0.cover")); CHECK_STR("alice", t_str("items.0.seller.nickname"));
    CHECK_INT(u, t_int("items.49.seller.user_id"));
    printf("[查询统计] 50件商品读取SQL=%d\n", reads);
}

static void review_category_value_error(sqlite3_context *context, int argc, sqlite3_value **argv) {
    (void)argc; (void)argv;
    sqlite3_result_error(context, "category-step-injected", -1);
}

static void test_review_category_step_error(void) {
    t_begin("复查：分类查询执行失败不能伪装成空列表成功");
    CHECK_INT(SQLITE_OK, sqlite3_create_function(t_db(), "broken_category", 0, SQLITE_UTF8, NULL, review_category_value_error, NULL, NULL));
    t_sql("DROP TABLE categories");
    t_sql("CREATE VIEW categories AS SELECT 1 AS id, broken_category() AS name");
    CHECK_CODE(ERR_INTERNAL, t_call(goods_categories, 0, "{}"));
}

int main(void) {
    t_init("商品模块测试");
    test_categories();
    test_publish_ok();
    test_publish_defaults();
    test_publish_invalid();
    test_detail();
    test_update();
    test_shelf();
    test_search_basic();
    test_search_keyword();
    test_search_filter_sort();
    test_search_seller();
    test_search_paging();
    test_mine();
    // 在这里加你自己的用例
    test_extra_types_and_priority();
    test_image_rollback();
    test_ties_and_literal_keyword();
    test_review_goods_utf8();
    test_review_goods_query_budget();
    test_review_category_step_error();
    return t_done();
}
