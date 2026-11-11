#include "core/db.h"

#include <stddef.h>
#include <stdio.h>

#include "core/log.h"

// 金额都用"分"存整数，时间都用 Unix 秒存整数。
static const char *SCHEMA_SQL =
    // 用户
    "CREATE TABLE IF NOT EXISTS users ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username      TEXT    NOT NULL UNIQUE,"            // 登录名
    "  password_hash TEXT    NOT NULL,"                   // sha256_hex(salt + 密码)
    "  salt          TEXT    NOT NULL,"                   // 32 位随机十六进制
    "  nickname      TEXT    NOT NULL,"
    "  avatar        TEXT    NOT NULL DEFAULT '',"         // 头像图片名
    "  bio           TEXT    NOT NULL DEFAULT '',"
    "  balance       INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0),"  // 余额（分）
    "  email         TEXT    NOT NULL DEFAULT '',"         // 邮箱（小写），空 = 没绑定
    "  is_admin      INTEGER NOT NULL DEFAULT 0,"          // 1 = 管理员，可以用审核后台
    "  muted_until   INTEGER NOT NULL DEFAULT 0,"          // 禁言到什么时候（Unix 秒），0 = 没禁言
    "  nickname_locked_until INTEGER NOT NULL DEFAULT 0,"  // 昵称因违规被重置后，到这个时间之前不能改
    "  created_at    INTEGER NOT NULL"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_users_email ON users(email) WHERE email <> '';"
    // 登录令牌
    "CREATE TABLE IF NOT EXISTS sessions ("
    "  token      TEXT    PRIMARY KEY,"                   // 64 位随机十六进制
    "  user_id    INTEGER NOT NULL REFERENCES users(id),"
    "  created_at INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);"
    // 商品分类（固定数据）
    "CREATE TABLE IF NOT EXISTS categories ("
    "  id   INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL"
    ");"
    "INSERT OR IGNORE INTO categories (id, name) VALUES"
    "  (1, '数码电子'), (2, '书籍教材'), (3, '生活用品'), (4, '服饰鞋包'),"
    "  (5, '运动户外'), (6, '美妆个护'), (7, '其他');"
    // 商品
    "CREATE TABLE IF NOT EXISTS goods ("
    "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  seller_id      INTEGER NOT NULL REFERENCES users(id),"
    "  title          TEXT    NOT NULL,"
    "  description    TEXT    NOT NULL DEFAULT '',"
    "  price          INTEGER NOT NULL CHECK (price > 0),"  // 售价（分）
    "  original_price INTEGER NOT NULL DEFAULT 0,"          // 原价（分），0 = 没填
    "  category_id    INTEGER NOT NULL REFERENCES categories(id),"
    "  condition      INTEGER NOT NULL DEFAULT 2,"          // 成色 0-3
    "  status         TEXT    NOT NULL DEFAULT 'on_sale'"   // on_sale / sold / off_shelf / reviewing / rejected
    "                 CHECK (status IN ('on_sale', 'sold', 'off_shelf', 'reviewing', 'rejected')),"
    "  reject_reason  TEXT    NOT NULL DEFAULT '',"         // 被驳回的原因
    "  view_count     INTEGER NOT NULL DEFAULT 0,"
    "  created_at     INTEGER NOT NULL,"
    "  updated_at     INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_goods_status_time ON goods(status, created_at);"
    "CREATE INDEX IF NOT EXISTS idx_goods_category    ON goods(category_id, status);"
    "CREATE INDEX IF NOT EXISTS idx_goods_seller      ON goods(seller_id, status);"
    // 商品图片（一件商品多张图，sort = 0 的是封面）
    "CREATE TABLE IF NOT EXISTS goods_images ("
    "  goods_id INTEGER NOT NULL REFERENCES goods(id),"
    "  sort     INTEGER NOT NULL,"
    "  image    TEXT    NOT NULL,"
    "  PRIMARY KEY (goods_id, sort)"
    ");"
    // 订单
    "CREATE TABLE IF NOT EXISTS orders ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_no     TEXT    NOT NULL UNIQUE,"              // 18 位订单号
    "  goods_id     INTEGER NOT NULL REFERENCES goods(id),"
    "  buyer_id     INTEGER NOT NULL REFERENCES users(id),"
    "  seller_id    INTEGER NOT NULL REFERENCES users(id),"
    "  goods_title  TEXT    NOT NULL,"                     // 下单时的标题快照
    "  price        INTEGER NOT NULL,"                     // 成交价快照（分）
    "  status       TEXT    NOT NULL"                      // paid / shipped / completed / cancelled
    "               CHECK (status IN ('paid', 'shipped', 'completed', 'cancelled')),"
    "  ship_note    TEXT    NOT NULL DEFAULT '',"
    "  created_at   INTEGER NOT NULL,"
    "  shipped_at   INTEGER NOT NULL DEFAULT 0,"
    "  completed_at INTEGER NOT NULL DEFAULT 0,"
    "  cancelled_at INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_orders_buyer  ON orders(buyer_id, created_at);"
    "CREATE INDEX IF NOT EXISTS idx_orders_seller ON orders(seller_id, created_at);"
    // 防超卖的第二道保险：同一件商品最多只有一个没取消的订单
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_orders_goods_active ON orders(goods_id)"
    "  WHERE status <> 'cancelled';"
    // 私信
    "CREATE TABLE IF NOT EXISTS messages ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  from_id    INTEGER NOT NULL REFERENCES users(id),"
    "  to_id      INTEGER NOT NULL REFERENCES users(id),"
    "  goods_id   INTEGER NOT NULL DEFAULT 0,"             // 关于哪件商品，0 = 无
    "  content    TEXT    NOT NULL,"
    "  is_read    INTEGER NOT NULL DEFAULT 0,"
    "  mod_state  INTEGER NOT NULL DEFAULT 0,"             // 0 正常 / 1 命中二级词，接收方看到提醒 / 2 已屏蔽
    "  mod_kind   TEXT    NOT NULL DEFAULT '',"            // 命中的类别，比如 scam、threat
    "  created_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_messages_pair   ON messages(from_id, to_id, id);"
    "CREATE INDEX IF NOT EXISTS idx_messages_unread ON messages(to_id, is_read);"
    // 收藏
    "CREATE TABLE IF NOT EXISTS favorites ("
    "  user_id    INTEGER NOT NULL REFERENCES users(id),"
    "  goods_id   INTEGER NOT NULL REFERENCES goods(id),"
    "  created_at INTEGER NOT NULL,"
    "  PRIMARY KEY (user_id, goods_id)"
    ");"
    // ======== 内容安全 ========
    // 举报：一个人对同一条消息只能举报一次
    "CREATE TABLE IF NOT EXISTS reports ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  reporter_id  INTEGER NOT NULL REFERENCES users(id),"
    "  reported_id  INTEGER NOT NULL REFERENCES users(id),"
    "  message_id   INTEGER NOT NULL REFERENCES messages(id),"
    "  type         TEXT    NOT NULL,"                     // abuse / porn / political / threat / scam / other
    "  reason       TEXT    NOT NULL,"
    "  status       TEXT    NOT NULL DEFAULT 'pending'"    // pending 等 AI / upheld 成立 / rejected 驳回 / manual 等人工
    "               CHECK (status IN ('pending', 'upheld', 'rejected', 'manual')),"
    "  ai_violation REAL    NOT NULL DEFAULT -1,"          // AI 判断言论违规的概率，-1 = 还没判
    "  ai_category  TEXT    NOT NULL DEFAULT '',"
    "  ai_reason    REAL    NOT NULL DEFAULT -1,"          // AI 判断举报理由成立的概率
    "  created_at   INTEGER NOT NULL,"
    "  handled_at   INTEGER NOT NULL DEFAULT 0,"
    "  UNIQUE (reporter_id, message_id)"
    ");"
    // 申诉：被禁言的人、商品被驳回的卖家可以申诉，进人工审核
    "CREATE TABLE IF NOT EXISTS appeals ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  user_id    INTEGER NOT NULL REFERENCES users(id),"
    "  kind       TEXT    NOT NULL CHECK (kind IN ('mute', 'goods')),"
    "  goods_id   INTEGER NOT NULL DEFAULT 0,"
    "  content    TEXT    NOT NULL,"
    "  status     TEXT    NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'accepted', 'rejected')),"
    "  created_at INTEGER NOT NULL,"
    "  handled_at INTEGER NOT NULL DEFAULT 0"
    ");"
    // 审核后台的案件：area = auto（已自动处理，给人复核）/ manual（等人工处理）
    "CREATE TABLE IF NOT EXISTS mod_cases ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  area       TEXT    NOT NULL CHECK (area IN ('auto', 'manual')),"
    "  kind       TEXT    NOT NULL CHECK (kind IN ('goods', 'report', 'chat', 'appeal', 'profile')),"
    "  ref_id     INTEGER NOT NULL,"                       // 商品 id / 举报 id / 消息 id / 申诉 id / 用户 id
    "  user_id    INTEGER NOT NULL,"                       // 当事人（被处理或被审核的人）
    "  summary    TEXT    NOT NULL,"                       // 一句话说明
    "  ai_label   TEXT    NOT NULL DEFAULT '',"
    "  ai_score   REAL    NOT NULL DEFAULT -1,"
    "  status     TEXT    NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'closed')),"
    "  result     TEXT    NOT NULL DEFAULT '',"            // 人工处理结果
    "  handled_by INTEGER NOT NULL DEFAULT 0,"
    "  created_at INTEGER NOT NULL,"
    "  handled_at INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_cases_list ON mod_cases(area, status, id);"
    // 后台审核队列：服务器重启也不会丢
    "CREATE TABLE IF NOT EXISTS review_jobs ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  kind       TEXT    NOT NULL CHECK (kind IN ('goods', 'report', 'chat', 'profile')),"
    "  ref_id     INTEGER NOT NULL,"
    "  attempts   INTEGER NOT NULL DEFAULT 0,"
    "  next_at    INTEGER NOT NULL,"
    "  created_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_jobs_due ON review_jobs(next_at);"
    // 一级词拦截记录：被拦下的内容也留底
    "CREATE TABLE IF NOT EXISTS blocked_logs ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  kind       TEXT    NOT NULL CHECK (kind IN ('chat', 'goods', 'profile')),"
    "  user_id    INTEGER NOT NULL,"
    "  category   TEXT    NOT NULL,"
    "  word       TEXT    NOT NULL,"
    "  content    TEXT    NOT NULL,"
    "  created_at INTEGER NOT NULL"
    ");"
    // 邮箱验证码
    "CREATE TABLE IF NOT EXISTS email_codes ("
    "  email      TEXT    PRIMARY KEY,"
    "  code       TEXT    NOT NULL,"
    "  purpose    TEXT    NOT NULL DEFAULT 'register' CHECK (purpose IN ('register', 'reset')),"  // 注册 / 重置密码
    "  tries      INTEGER NOT NULL DEFAULT 0,"             // 输错几次了，5 次作废
    "  sent_at    INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL"
    ");";

// 表结构版本：改了表结构就加 1。旧版本的数据库不会自动升级，要删掉重建。
#define SCHEMA_VERSION 3

sqlite3 *db_open(const char *path) {
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        log_error("打不开数据库 %s: %s", path, db ? sqlite3_errmsg(db) : "内存不足");
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 5000);  // 别的连接正在写时，最多等 5 秒
    db_exec(db, "PRAGMA foreign_keys = ON;");
    db_exec(db, "PRAGMA journal_mode = WAL;");  // 读写可以同时进行
    db_exec(db, "PRAGMA synchronous = NORMAL;");
    return db;
}

void db_close(sqlite3 *db) { sqlite3_close(db); }

bool db_init_schema(sqlite3 *db) {
    sqlite3_stmt *st;
    int version = 0, tables = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) version = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='users'",
                           -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) tables = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (tables > 0 && version < SCHEMA_VERSION) {
        log_error("数据库是旧版本（版本 %d，现在需要 %d），表结构不兼容：请备份后删除数据库文件，重新启动会自动建新库",
                  version, SCHEMA_VERSION);
        return false;
    }
    if (!db_exec(db, SCHEMA_SQL)) return false;
    char sql[64];
    snprintf(sql, sizeof sql, "PRAGMA user_version = %d", SCHEMA_VERSION);
    return db_exec(db, sql);
}

bool db_exec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        log_error("SQL 执行失败: %s", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}
