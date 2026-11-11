// 用户模块：参数化查询、加盐哈希、登录会话与个人资料。
#include "modules/user/user.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/db.h"
#include "core/files.h"
#include "core/jsonx.h"
#include "core/log.h"
#include "core/mail.h"
#include "core/platform.h"
#include "core/util.h"
#include "modules/safety/safety.h"

#define CODE_TTL       600  // 验证码 10 分钟有效
#define CODE_INTERVAL  60   // 同一个邮箱 60 秒内只能发一次
#define CODE_MAX_TRIES 5    // 输错 5 次作废

static bool g_require_email;
void user_set_require_email(bool require) { g_require_email = require; }

// 邮箱格式：xxx@yyy.zz，只允许常见字符，最长 64；合法的话转成小写写进 out
static bool email_normalize(const char *s, char out[65]) {
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 5 || n > 64) return false;
    const char *at = strchr(s, '@');
    if (!at || at == s || strchr(at + 1, '@')) return false;
    const char *dot = strrchr(at, '.');
    if (!dot || dot == at + 1 || dot[1] == '\0' || s[n - 1] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                  c == '-' || c == '_' || c == '@' || (c == '+' && s + i < at);
        if (!ok) return false;
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[n] = '\0';
    return true;
}

// 只遍历合法长度，异常超长输入不进入哈希或数据库查询。
static bool username_valid(const char *s) {
    if (!s) return false;
    size_t length = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (++length > 20 || !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) return false;
    }
    return length >= 3;
}

// 所有可保存文字都检查有效UTF-8，字符上限按Unicode标量计算。
static bool text_valid(const char *content, size_t minimum, size_t maximum) {
    if (!content) return false;
    const unsigned char *p = (const unsigned char *)content;
    size_t count = 0;
    while (*p) {
        if (*p < 0x80) p++;
        else if (*p >= 0xC2 && *p <= 0xDF) {
            if (p[1] < 0x80 || p[1] > 0xBF) return false;
            p += 2;
        } else if (*p >= 0xE0 && *p <= 0xEF) {
            unsigned low = *p == 0xE0 ? 0xA0 : 0x80;
            unsigned high = *p == 0xED ? 0x9F : 0xBF;
            if (p[1] < low || p[1] > high || p[2] < 0x80 || p[2] > 0xBF) return false;
            p += 3;
        } else if (*p >= 0xF0 && *p <= 0xF4) {
            unsigned low = *p == 0xF0 ? 0x90 : 0x80;
            unsigned high = *p == 0xF4 ? 0x8F : 0xBF;
            if (p[1] < low || p[1] > high || p[2] < 0x80 || p[2] > 0xBF ||
                p[3] < 0x80 || p[3] > 0xBF) return false;
            p += 4;
        } else return false;
        if (++count > maximum) return false;
    }
    return count >= minimum;
}

static bool registration_password_valid(const char *s) {
    if (!text_valid(s, 8, 32)) return false;
    bool digit = false, upper = false, lower = false;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        digit |= *p >= '0' && *p <= '9';
        upper |= *p >= 'A' && *p <= 'Z';
        lower |= *p >= 'a' && *p <= 'z';
    }
    return digit && upper && lower;
}

static int hash_password(Ctx *ctx, const char *salt, const char *password, char out[65]) {
    size_t a = strlen(salt), b = strlen(password);
    char *text = malloc(a + b + 1);
    if (!text) return fail(ctx, ERR_INTERNAL, "内存不足");
    memcpy(text, salt, a);
    memcpy(text + a, password, b + 1);
    sha256_hex(text, a + b, out); // 盐在前，与协议和 t_user 一致。
    free(text);
    return OK;
}

static int write_user_info(Ctx *ctx, int64_t id, cJSON *out) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "SELECT id,username,nickname,avatar,bio,balance,created_at,email,is_admin,muted_until,nickname_locked_until "
        "FROM users WHERE id=?",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        jx_add_col_int(out, "user_id", st, 0);
        jx_add_col_str(out, "username", st, 1);
        jx_add_col_str(out, "nickname", st, 2);
        jx_add_col_str(out, "avatar", st, 3);
        jx_add_col_str(out, "bio", st, 4);
        jx_add_col_int(out, "balance", st, 5);
        jx_add_col_int(out, "created_at", st, 6);
        jx_add_col_str(out, "email", st, 7);
        jx_add_bool(out, "is_admin", sqlite3_column_int(st, 8) != 0);
        jx_add_col_int(out, "muted_until", st, 9);
        jx_add_col_int(out, "nickname_locked_until", st, 10);
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "用户不存在");
    return rc == SQLITE_ROW ? OK : db_fail(ctx);
}

// 核对邮箱验证码（必须在写事务里调用）。过期、用途不对、输错太多次都算失效；输错一次记一次。
// 返回 CODE_OK / CODE_DEAD（调用者 ROLLBACK）/ CODE_WRONG（已记次，调用者 COMMIT）/ CODE_DB（数据库出错）
enum { CODE_OK, CODE_DEAD, CODE_WRONG, CODE_DB };

static int check_code(Ctx *ctx, const char *email, const char *code, const char *purpose) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT code,tries,expires_at,purpose FROM email_codes WHERE email=?", -1, &st, NULL) != SQLITE_OK)
        return CODE_DB;
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    bool found = rc == SQLITE_ROW;
    bool match = found && strcmp((const char *)sqlite3_column_text(st, 0), code) == 0;
    bool dead = found && (sqlite3_column_int(st, 1) >= CODE_MAX_TRIES || sqlite3_column_int64(st, 2) < now_sec() ||
                          strcmp((const char *)sqlite3_column_text(st, 3), purpose) != 0);
    sqlite3_finalize(st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) return CODE_DB;
    if (!found || dead) return CODE_DEAD;
    if (match) return CODE_OK;
    if (sqlite3_prepare_v2(ctx->db, "UPDATE email_codes SET tries=tries+1 WHERE email=?", -1, &st, NULL) != SQLITE_OK)
        return CODE_DB;
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? CODE_WRONG : CODE_DB;
}

// 用过的验证码作废
static bool delete_code(Ctx *ctx, const char *email) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "DELETE FROM email_codes WHERE email=?", -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

int user_register(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *name = jx_str(req, "username"), *password = jx_str(req, "password");
    const char *nickname = jx_str(req, "nickname");
    if (!username_valid(name)) return fail(ctx, ERR_BAD_REQUEST, "用户名须为 3–20 位英文字母、数字或下划线");
    if (!registration_password_valid(password))
        return fail(ctx, ERR_BAD_REQUEST, "密码须为 8–32 位，且同时包含数字、大写字母和小写字母");
    if (!text_valid(nickname, 1, 20))
        return fail(ctx, ERR_BAD_REQUEST, "昵称须为 1–20 个字");
    int checked = safety_check_profile(ctx, nickname, NULL);  // 昵称过一级词库
    if (checked != OK) return checked;
    // 邮箱验证：配置要求时必须填；不要求时填了也照样验证
    char email[65] = "";
    const char *email_in = jx_str(req, "email"), *code = jx_str(req, "code");
    bool with_email = g_require_email || (email_in && *email_in);
    if (with_email) {
        if (!email_normalize(email_in, email)) return fail(ctx, ERR_BAD_REQUEST, "请填写正确的邮箱");
        if (!code || strlen(code) != 6) return fail(ctx, ERR_BAD_REQUEST, "请填写 6 位邮箱验证码");
    }
    char salt[33], hash[65];
    random_hex(salt, 16);
    int result = hash_password(ctx, salt, password, hash);
    if (result != OK) return result;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (with_email) {
        int c = check_code(ctx, email, code, "register");
        if (c == CODE_DB) goto db_error;
        if (c == CODE_DEAD) {
            db_exec(ctx->db, "ROLLBACK");
            return fail(ctx, ERR_BAD_REQUEST, "验证码已失效，请重新获取");
        }
        if (c == CODE_WRONG) {
            db_exec(ctx->db, "COMMIT");
            return fail(ctx, ERR_BAD_REQUEST, "验证码不对");
        }
    }
    if (sqlite3_prepare_v2(ctx->db,
        "INSERT INTO users(username,password_hash,salt,nickname,created_at,email) VALUES(?,?,?,?,?,?)",
        -1, &st, NULL) != SQLITE_OK) goto db_error;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, salt, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, nickname, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 5, now_sec());
    sqlite3_bind_text(st, 6, email, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st), extended = sqlite3_extended_errcode(ctx->db);
    bool email_taken = extended == SQLITE_CONSTRAINT_UNIQUE && strstr(sqlite3_errmsg(ctx->db), "email") != NULL;
    sqlite3_finalize(st);
    // 唯一约束同时处理并发注册，避免先查后插的竞争。
    if (extended == SQLITE_CONSTRAINT_UNIQUE) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_CONFLICT, email_taken ? "邮箱已被注册" : "用户名已被注册");
    }
    if (rc != SQLITE_DONE) goto db_error;
    int64_t user_id = sqlite3_last_insert_rowid(ctx->db);
    if (with_email && !delete_code(ctx, email)) goto db_error;  // 验证码用过就作废
    // 昵称交给 AI 再审一遍（有把握违规会被重置）
    if (safety_ai_enabled() && !safety_enqueue(ctx->db, "profile", user_id)) goto db_error;
    if (!db_exec(ctx->db, "COMMIT")) goto db_error;
    jx_add_int(resp, "user_id", user_id);
    return OK;
db_error:
    db_exec(ctx->db, "ROLLBACK");
    return db_fail(ctx);
}

// user.send_code：{email, purpose?}，purpose = register（默认，邮箱必须没注册过）/ reset（忘记密码，邮箱必须已注册）
int user_send_code(Ctx *ctx, const cJSON *req, cJSON *resp) {
    char email[65];
    if (!email_normalize(jx_str(req, "email"), email)) return fail(ctx, ERR_BAD_REQUEST, "请填写正确的邮箱");
    const char *purpose = jx_has(req, "purpose") ? jx_str(req, "purpose") : "register";
    if (!purpose || (strcmp(purpose, "register") != 0 && strcmp(purpose, "reset") != 0))
        return fail(ctx, ERR_BAD_REQUEST, "purpose 须为 register 或 reset");
    bool reset = strcmp(purpose, "reset") == 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT 1 FROM users WHERE email=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) return db_fail(ctx);
    if (!reset && rc == SQLITE_ROW) return fail(ctx, ERR_CONFLICT, "这个邮箱已经注册过了");
    if (reset && rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "这个邮箱还没有注册");
    if (sqlite3_prepare_v2(ctx->db, "SELECT sent_at FROM email_codes WHERE email=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    int64_t wait = 0;
    if (sqlite3_step(st) == SQLITE_ROW) wait = sqlite3_column_int64(st, 0) + CODE_INTERVAL - now_sec();
    sqlite3_finalize(st);
    if (wait > 0) return fail(ctx, ERR_CONFLICT, "发送太频繁，请 %lld 秒后再试", (long long)wait);

    // 6 位数字验证码
    unsigned char rnd[4] = {0};
    if (!random_bytes(rnd, sizeof rnd)) return fail(ctx, ERR_INTERNAL, "生成验证码失败");
    unsigned long v = ((unsigned long)rnd[0] << 24 | (unsigned long)rnd[1] << 16 | (unsigned long)rnd[2] << 8 | rnd[3]) % 1000000UL;
    char code[8];
    snprintf(code, sizeof code, "%06lu", v);

    if (mail_enabled()) {
        char err[256];
        if (!mail_send_code(email, code, reset, err, sizeof err)) {
            log_warn("验证码邮件发送失败（%s）：%s", email, err);
            return fail(ctx, ERR_INTERNAL, "验证码邮件发送失败，请稍后再试");
        }
    } else {
        log_info("【开发模式】邮箱 %s 的验证码是 %s", email, code);
        jx_add_str(resp, "dev_code", code);  // 没配 SMTP 时把验证码带回去，方便测试和演示
    }
    int64_t now = now_sec();
    if (sqlite3_prepare_v2(ctx->db,
        "INSERT INTO email_codes(email,code,tries,sent_at,expires_at,purpose) VALUES(?,?,0,?,?,?) "
        "ON CONFLICT(email) DO UPDATE SET code=excluded.code,tries=0,sent_at=excluded.sent_at,"
        "expires_at=excluded.expires_at,purpose=excluded.purpose",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, code, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_int64(st, 4, now + CODE_TTL);
    sqlite3_bind_text(st, 5, purpose, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return db_fail(ctx);
    jx_add_int(resp, "expires_in", CODE_TTL);
    return OK;
}

int user_login(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *name = jx_str(req, "username"), *password = jx_str(req, "password");
    if (!name || !password || !*name || !*password) return fail(ctx, ERR_BAD_REQUEST, "请输入用户名和密码");
    // 带 @ 的当邮箱登录
    char email[65];
    bool by_email = strchr(name, '@') != NULL;
    if (by_email && !email_normalize(name, email)) return fail(ctx, ERR_NOT_LOGIN, "用户名或密码错误");
    // 兼容旧账户的6位密码，同时限制异常输入；认证失败保持同一提示。
    if ((!by_email && !username_valid(name)) || !text_valid(password, 1, 32))
        return fail(ctx, ERR_NOT_LOGIN, "用户名或密码错误");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, by_email ? "SELECT id,salt,password_hash FROM users WHERE email=?"
                                             : "SELECT id,salt,password_hash FROM users WHERE username=?",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, by_email ? email : name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st), result = OK;
    int64_t id = 0;
    bool matched = false;
    if (rc == SQLITE_ROW) {
        char hash[65];
        id = sqlite3_column_int64(st, 0);
        result = hash_password(ctx, (const char *)sqlite3_column_text(st, 1), password, hash);
        matched = result == OK && strcmp(hash, (const char *)sqlite3_column_text(st, 2)) == 0;
    }
    sqlite3_finalize(st);
    if (result != OK) return result;
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) return db_fail(ctx);
    if (!matched) return fail(ctx, ERR_NOT_LOGIN, "用户名或密码错误");
    cJSON *user = cJSON_AddObjectToObject(resp, "user");
    if (!user) return fail(ctx, ERR_INTERNAL, "内存不足");
    result = write_user_info(ctx, id, user);
    if (result != OK) return result;
    char token[65];
    random_hex(token, 32);
    int64_t now = now_sec();
    if (sqlite3_prepare_v2(ctx->db,
        "INSERT INTO sessions(token,user_id,created_at,expires_at) VALUES(?,?,?,?)",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_text(st, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, id);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_int64(st, 4, now + 7 * 86400);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return db_fail(ctx);
    jx_add_str(resp, "token", token);
    return OK;
}

int user_logout(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)req; (void)resp;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "DELETE FROM sessions WHERE token=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_text(st, 1, ctx->token ? ctx->token : "", -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

int user_me(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)req;
    return write_user_info(ctx, ctx->user_id, resp);
}

int user_update_profile(Ctx *ctx, const cJSON *req, cJSON *resp) {
    const char *nickname = jx_str(req, "nickname"), *avatar = jx_str(req, "avatar"), *bio = jx_str(req, "bio");
    // 全部检查后用一个 UPDATE 修改；未提供的字段绑定 NULL。
    if (jx_has(req, "nickname") && (!text_valid(nickname, 1, 20)))
        return fail(ctx, ERR_BAD_REQUEST, "昵称须为 1–20 个字");
    if (jx_has(req, "bio") && (!text_valid(bio, 0, 100))) return fail(ctx, ERR_BAD_REQUEST, "简介最多 100 个字");
    if (jx_has(req, "avatar") && (!avatar || (*avatar && !image_exists(avatar))))
        return fail(ctx, ERR_BAD_REQUEST, "头像须为已上传图片，也可留空清除");
    // 内容安全：昵称被重置后 24 小时内不能改；昵称和简介过一级词库；改了的话再交给 AI
    sqlite3_stmt *st;
    bool nick_changed = false, bio_changed = false;
    if (sqlite3_prepare_v2(ctx->db, "SELECT nickname,bio FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        nick_changed = nickname && strcmp(nickname, (const char *)sqlite3_column_text(st, 0)) != 0;
        bio_changed = bio && strcmp(bio, (const char *)sqlite3_column_text(st, 1)) != 0;
    }
    sqlite3_finalize(st);
    int checked = nick_changed ? safety_check_nickname_lock(ctx) : OK;
    if (checked == OK) checked = safety_check_profile(ctx, nick_changed ? nickname : NULL, bio_changed ? bio : NULL);
    if (checked != OK) return checked;
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    if (sqlite3_prepare_v2(ctx->db,
        "UPDATE users SET nickname=COALESCE(?,nickname),avatar=COALESCE(?,avatar),bio=COALESCE(?,bio) WHERE id=?",
        -1, &st, NULL) != SQLITE_OK) goto error;
    sqlite3_bind_text(st, 1, nickname, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, avatar, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, bio, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, ctx->user_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) goto error;
    if ((nick_changed || bio_changed) && safety_ai_enabled() && !safety_enqueue(ctx->db, "profile", ctx->user_id))
        goto error;
    int result = write_user_info(ctx, ctx->user_id, resp);
    if (result != OK) { db_exec(ctx->db, "ROLLBACK"); return result; }
    if (!db_exec(ctx->db, "COMMIT")) goto error;
    return OK;
error:
    db_exec(ctx->db, "ROLLBACK");
    return db_fail(ctx);
}

// 换密码：新的盐 + 哈希。keep_token 不为 NULL 时保留这个登录，其他设备全部退出
static int set_password(Ctx *ctx, int64_t uid, const char *password, const char *keep_token) {
    char salt[33], hash[65];
    random_hex(salt, 16);
    int result = hash_password(ctx, salt, password, hash);
    if (result != OK) return result;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "UPDATE users SET salt=?,password_hash=? WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_text(st, 1, salt, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, uid);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return db_fail(ctx);
    if (sqlite3_prepare_v2(ctx->db, "DELETE FROM sessions WHERE user_id=? AND token<>?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, uid);
    sqlite3_bind_text(st, 2, keep_token ? keep_token : "", -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OK : db_fail(ctx);
}

// user.change_password 🔒 {old_password, new_password}：改完其他设备要重新登录
int user_change_password(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp;
    const char *old_pw = jx_str(req, "old_password"), *new_pw = jx_str(req, "new_password");
    if (!old_pw || !*old_pw) return fail(ctx, ERR_BAD_REQUEST, "请输入原密码");
    if (!registration_password_valid(new_pw))
        return fail(ctx, ERR_BAD_REQUEST, "新密码须为 8–32 位，且同时包含数字、大写字母和小写字母");
    if (strcmp(old_pw, new_pw) == 0) return fail(ctx, ERR_BAD_REQUEST, "新密码不能和原密码一样");
    if (!text_valid(old_pw, 1, 32)) return fail(ctx, ERR_BAD_REQUEST, "原密码不对");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT salt,password_hash FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return db_fail(ctx);
    sqlite3_bind_int64(st, 1, ctx->user_id);
    bool matched = false;
    int result = OK;
    if (sqlite3_step(st) == SQLITE_ROW) {
        char hash[65];
        result = hash_password(ctx, (const char *)sqlite3_column_text(st, 0), old_pw, hash);
        matched = result == OK && strcmp(hash, (const char *)sqlite3_column_text(st, 1)) == 0;
    }
    sqlite3_finalize(st);
    if (result != OK) return result;
    if (!matched) return fail(ctx, ERR_BAD_REQUEST, "原密码不对");
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    result = set_password(ctx, ctx->user_id, new_pw, ctx->token);
    if (result != OK || !db_exec(ctx->db, "COMMIT")) {
        db_exec(ctx->db, "ROLLBACK");
        return result != OK ? result : db_fail(ctx);
    }
    return OK;
}

// user.reset_password {email, code, new_password}：忘记密码，用邮箱验证码重置；所有设备都要重新登录
int user_reset_password(Ctx *ctx, const cJSON *req, cJSON *resp) {
    (void)resp;
    char email[65];
    const char *code = jx_str(req, "code"), *new_pw = jx_str(req, "new_password");
    if (!email_normalize(jx_str(req, "email"), email)) return fail(ctx, ERR_BAD_REQUEST, "请填写正确的邮箱");
    if (!code || strlen(code) != 6) return fail(ctx, ERR_BAD_REQUEST, "请填写 6 位邮箱验证码");
    if (!registration_password_valid(new_pw))
        return fail(ctx, ERR_BAD_REQUEST, "新密码须为 8–32 位，且同时包含数字、大写字母和小写字母");
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    int c = check_code(ctx, email, code, "reset");
    if (c == CODE_DB) goto db_error;
    if (c == CODE_DEAD) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_BAD_REQUEST, "验证码已失效，请重新获取");
    }
    if (c == CODE_WRONG) {
        db_exec(ctx->db, "COMMIT");
        return fail(ctx, ERR_BAD_REQUEST, "验证码不对");
    }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db, "SELECT id FROM users WHERE email=?", -1, &st, NULL) != SQLITE_OK) goto db_error;
    sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
    int64_t uid = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (uid == 0) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_BAD_REQUEST, "验证码已失效，请重新获取");
    }
    int result = set_password(ctx, uid, new_pw, NULL);
    if (result != OK) {
        db_exec(ctx->db, "ROLLBACK");
        return result;
    }
    if (!delete_code(ctx, email) || !db_exec(ctx->db, "COMMIT")) goto db_error;
    return OK;
db_error:
    db_exec(ctx->db, "ROLLBACK");
    return db_fail(ctx);
}

int user_get(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t id;
    if (!jx_int(req, "user_id", &id) || id < 1) return fail(ctx, ERR_BAD_REQUEST, "用户编号须为正整数");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "SELECT id,nickname,avatar,bio,created_at,"
        "(SELECT COUNT(*) FROM goods WHERE seller_id=users.id AND status='on_sale'),"
        "(SELECT COUNT(*) FROM goods WHERE seller_id=users.id AND status='sold') FROM users WHERE id=?",
        -1, &st, NULL) != SQLITE_OK) return db_fail(ctx);
    sqlite3_bind_int64(st, 1, id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        jx_add_col_int(resp, "user_id", st, 0);
        jx_add_col_str(resp, "nickname", st, 1);
        jx_add_col_str(resp, "avatar", st, 2);
        jx_add_col_str(resp, "bio", st, 3);
        jx_add_col_int(resp, "created_at", st, 4);
        jx_add_col_int(resp, "on_sale_count", st, 5);
        jx_add_col_int(resp, "sold_count", st, 6);
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return fail(ctx, ERR_NOT_FOUND, "用户不存在");
    return rc == SQLITE_ROW ? OK : db_fail(ctx);
}

int user_recharge(Ctx *ctx, const cJSON *req, cJSON *resp) {
    int64_t amount;
    if (!jx_int(req, "amount", &amount) || amount < 1 || amount > 1000000)
        return fail(ctx, ERR_BAD_REQUEST, "单次充值金额须为 0.01–10000 元");
    if (!db_exec(ctx->db, "BEGIN IMMEDIATE")) return db_fail(ctx);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(ctx->db,
        "UPDATE users SET balance=balance+? WHERE id=? AND balance<=9007199254740991-?",
        -1, &st, NULL) != SQLITE_OK) goto error;
    sqlite3_bind_int64(st, 1, amount);
    sqlite3_bind_int64(st, 2, ctx->user_id);
    sqlite3_bind_int64(st, 3, amount);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) goto error;
    if (sqlite3_changes(ctx->db) == 0) {
        db_exec(ctx->db, "ROLLBACK");
        return fail(ctx, ERR_CONFLICT, "账户不存在或余额已达上限");
    }
    if (sqlite3_prepare_v2(ctx->db, "SELECT balance FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK) goto error;
    sqlite3_bind_int64(st, 1, ctx->user_id);
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) jx_add_col_int(resp, "balance", st, 0);
    sqlite3_finalize(st);
    if (rc != SQLITE_ROW || !db_exec(ctx->db, "COMMIT")) goto error;
    return OK;
error:
    db_exec(ctx->db, "ROLLBACK");
    return db_fail(ctx);
}
