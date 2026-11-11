// 底层工具的测试（公共部分）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/files.h"
#include "core/jsonx.h"
#include "core/router.h"
#include "core/shared.h"
#include "core/util.h"
#include "t.h"

static void test_sha256(void) {
    t_begin("SHA-256 标准测试向量");
    char out[65];
    sha256_hex("", 0, out);
    CHECK_STR("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out);
    sha256_hex("abc", 3, out);
    CHECK_STR("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", out);
    const char *s56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256_hex(s56, strlen(s56), out);
    CHECK_STR("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", out);
}

static void test_base64(void) {
    t_begin("Base64 编码解码");
    char *e = base64_encode((const unsigned char *)"Man", 3);
    CHECK_STR("TWFu", e);
    free(e);
    e = base64_encode((const unsigned char *)"Ma", 2);
    CHECK_STR("TWE=", e);
    free(e);
    size_t n = 0;
    unsigned char *d = base64_decode("5bm/55CG6IGM5bCP6bG8", &n);
    CHECK(d && n == 15 && memcmp(d, "广理职小鱼", 15) == 0);
    free(d);
    CHECK(base64_decode("不是base64", &n) == NULL);
}

static void test_utf8_len(void) {
    t_begin("UTF-8 字数");
    CHECK_INT(0, utf8_len(""));
    CHECK_INT(3, utf8_len("abc"));
    CHECK_INT(5, utf8_len("广理职小鱼"));
    CHECK_INT(4, utf8_len("a小b鱼"));
}

static void test_image_name(void) {
    t_begin("图片名检查");
    CHECK(image_name_valid("0123456789abcdef0123456789abcdef.jpg"));
    CHECK(!image_name_valid("0123456789abcdef0123456789abcdef.exe"));
    CHECK(!image_name_valid("../../etc/passwd"));
    CHECK(!image_name_valid("0123456789ABCDEF0123456789abcdef.jpg"));
    CHECK(!image_name_valid(NULL));
    const char *img = t_image();
    CHECK(image_exists(img));
    CHECK(!image_exists("00000000000000000000000000000000.png"));
}

static void test_upload(void) {
    t_begin("上传和下载图片");
    int64_t u = t_user("alice", "123456", 0);
    CHECK_CODE(OK, t_call(file_upload, u, "{'ext':'png','data':'aGVsbG8='}"));
    char name[64];
    snprintf(name, sizeof name, "%s", t_str("image") ? t_str("image") : "");
    CHECK(image_exists(name));
    char json[128];
    snprintf(json, sizeof json, "{'image':'%s'}", name);
    CHECK_CODE(OK, t_call(file_get, 0, json));
    CHECK_STR("aGVsbG8=", t_str("data"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(file_upload, u, "{'ext':'exe','data':'aGVsbG8='}"));
    CHECK_CODE(ERR_BAD_REQUEST, t_call(file_get, 0, "{'image':'../server.conf'}"));
    CHECK_CODE(ERR_NOT_FOUND, t_call(file_get, 0, "{'image':'00000000000000000000000000000000.png'}"));
}

static void test_session(void) {
    t_begin("token 校验");
    int64_t u = t_user("alice", "123456", 0);
    t_sql("INSERT INTO sessions VALUES ('good', %lld, 0, %lld)", (long long)u, (long long)(now_sec() + 100));
    t_sql("INSERT INTO sessions VALUES ('old', %lld, 0, %lld)", (long long)u, (long long)(now_sec() - 1));
    CHECK_INT(u, session_user(t_db(), "good"));
    CHECK_INT(0, session_user(t_db(), "old"));   // 过期了
    CHECK_INT(0, session_user(t_db(), "nope"));
    CHECK_INT(0, session_user(t_db(), NULL));
}

static void test_paging(void) {
    t_begin("分页参数");
    int64_t p, s;
    cJSON *req = cJSON_Parse("{}");
    CHECK(read_paging(req, &p, &s) && p == 1 && s == 20);
    cJSON_Delete(req);
    req = cJSON_Parse("{\"page\":0}");
    CHECK(!read_paging(req, &p, &s));
    cJSON_Delete(req);
    req = cJSON_Parse("{\"page\":2,\"page_size\":50}");
    CHECK(read_paging(req, &p, &s) && p == 2 && s == 50);
    cJSON_Delete(req);
    req = cJSON_Parse("{\"page_size\":51}");
    CHECK(!read_paging(req, &p, &s));
    cJSON_Delete(req);
    req = cJSON_Parse("{\"page\":1.5}");
    CHECK(!read_paging(req, &p, &s));
    cJSON_Delete(req);
}

int main(void) {
    t_init("底层工具测试");
    test_sha256();
    test_base64();
    test_utf8_len();
    test_image_name();
    test_upload();
    test_session();
    test_paging();
    return t_done();
}
