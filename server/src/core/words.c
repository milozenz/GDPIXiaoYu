#include "core/words.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------- 词库 ----------------
// 词库只收"明确"的词：一级词误判会让正常消息发不出去，所以宁缺毋滥；
// 拿不准的交给二级词库或 AI。ALLOW 是白名单短语：命中位置落在白名单短语里就不算，
// 比如"习近平新时代中国特色社会主义思想"是课程教材，不能拦。

typedef struct {
    const char *word;
    unsigned char level;
    unsigned char scope;
    const char *category;
} WordDef;

#define BOTH (SCOPE_CHAT | SCOPE_GOODS)

static const WordDef WORDS[] = {
    // ---- 人身攻击（一级，私信和商品都拦）----
    {"操你妈", WORD_BLOCK, BOTH, "abuse"},   {"草泥马", WORD_BLOCK, BOTH, "abuse"},
    {"艹你妈", WORD_BLOCK, BOTH, "abuse"},   {"日你妈", WORD_BLOCK, BOTH, "abuse"},
    {"你妈死了", WORD_BLOCK, BOTH, "abuse"}, {"尼玛死了", WORD_BLOCK, BOTH, "abuse"},
    {"死全家", WORD_BLOCK, BOTH, "abuse"},   {"全家死光", WORD_BLOCK, BOTH, "abuse"},
    {"傻逼", WORD_BLOCK, BOTH, "abuse"},     {"煞笔", WORD_BLOCK, BOTH, "abuse"},
    {"沙比", WORD_BLOCK, BOTH, "abuse"},     {"傻b", WORD_BLOCK, BOTH, "abuse"},
    {"傻x", WORD_BLOCK, BOTH, "abuse"},      {"脑残", WORD_BLOCK, BOTH, "abuse"},
    {"贱人", WORD_BLOCK, BOTH, "abuse"},     {"婊子", WORD_BLOCK, BOTH, "abuse"},
    {"狗日的", WORD_BLOCK, BOTH, "abuse"},   {"杂种", WORD_BLOCK, BOTH, "abuse"},
    {"王八蛋", WORD_BLOCK, BOTH, "abuse"},   {"滚你妈", WORD_BLOCK, BOTH, "abuse"},
    {"去死吧", WORD_BLOCK, BOTH, "abuse"},   {"cnm", WORD_BLOCK, BOTH, "abuse"},
    {"nmsl", WORD_BLOCK, BOTH, "abuse"},     {"sb", WORD_BLOCK, BOTH, "abuse"},

    // ---- 色情低俗（一级）----
    {"约炮", WORD_BLOCK, BOTH, "porn"},     {"一夜情", WORD_BLOCK, BOTH, "porn"},
    {"裸聊", WORD_BLOCK, BOTH, "porn"},     {"裸照", WORD_BLOCK, BOTH, "porn"},
    {"援交", WORD_BLOCK, BOTH, "porn"},     {"黄片", WORD_BLOCK, BOTH, "porn"},
    {"色情", WORD_BLOCK, BOTH, "porn"},     {"嫖娼", WORD_BLOCK, BOTH, "porn"},
    {"卖淫", WORD_BLOCK, BOTH, "porn"},     {"招嫖", WORD_BLOCK, BOTH, "porn"},
    {"性服务", WORD_BLOCK, BOTH, "porn"},   {"成人视频", WORD_BLOCK, BOTH, "porn"},

    // ---- 政治敏感（一级）----
    {"习近平", WORD_BLOCK, BOTH, "political"},  {"彭丽媛", WORD_BLOCK, BOTH, "political"},
    {"习大大", WORD_BLOCK, BOTH, "political"},  {"法轮功", WORD_BLOCK, BOTH, "political"},
    {"法轮大法", WORD_BLOCK, BOTH, "political"}, {"六四事件", WORD_BLOCK, BOTH, "political"},
    {"八九六四", WORD_BLOCK, BOTH, "political"}, {"天安门事件", WORD_BLOCK, BOTH, "political"},
    {"台独", WORD_BLOCK, BOTH, "political"},    {"藏独", WORD_BLOCK, BOTH, "political"},
    {"疆独", WORD_BLOCK, BOTH, "political"},    {"港独", WORD_BLOCK, BOTH, "political"},
    {"推翻共产党", WORD_BLOCK, BOTH, "political"}, {"共匪", WORD_BLOCK, BOTH, "political"},
    // 白名单：思政课教材和正常用语
    {"习近平新时代", WORD_ALLOW, BOTH, NULL},   {"习近平总书记", WORD_ALLOW, BOTH, NULL},
    {"习近平谈治国理政", WORD_ALLOW, BOTH, NULL}, {"学习近", WORD_ALLOW, BOTH, NULL},

    // ---- 违法（一级）----
    {"冰毒", WORD_BLOCK, BOTH, "illegal"},    {"海洛因", WORD_BLOCK, BOTH, "illegal"},
    {"摇头丸", WORD_BLOCK, BOTH, "illegal"},  {"k粉", WORD_BLOCK, BOTH, "illegal"},
    {"代开发票", WORD_BLOCK, BOTH, "illegal"}, {"赌博网站", WORD_BLOCK, BOTH, "illegal"},

    // ---- 商品：代课代写（一级）----
    {"代课", WORD_BLOCK, SCOPE_GOODS, "cheating"},     {"代上课", WORD_BLOCK, SCOPE_GOODS, "cheating"},
    {"代考", WORD_BLOCK, SCOPE_GOODS, "cheating"},     {"替考", WORD_BLOCK, SCOPE_GOODS, "cheating"},
    {"代写", WORD_BLOCK, SCOPE_GOODS, "cheating"},     {"代做作业", WORD_BLOCK, SCOPE_GOODS, "cheating"},
    {"论文代写", WORD_BLOCK, SCOPE_GOODS, "cheating"}, {"代签到", WORD_BLOCK, SCOPE_GOODS, "cheating"},
    {"刷网课", WORD_BLOCK, SCOPE_GOODS, "cheating"},   {"代刷网课", WORD_BLOCK, SCOPE_GOODS, "cheating"},
    {"网课代刷", WORD_BLOCK, SCOPE_GOODS, "cheating"}, {"代跑步", WORD_BLOCK, SCOPE_GOODS, "cheating"},
    {"代跑", WORD_BLOCK, SCOPE_GOODS, "cheating"},

    // ---- 商品：违禁品（一级）----
    {"管制刀具", WORD_BLOCK, SCOPE_GOODS, "contraband"}, {"仿真枪", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"气枪", WORD_BLOCK, SCOPE_GOODS, "contraband"},     {"枪支", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"弹药", WORD_BLOCK, SCOPE_GOODS, "contraband"},     {"弩箭", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"电击棒", WORD_BLOCK, SCOPE_GOODS, "contraband"},   {"甩棍", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"迷药", WORD_BLOCK, SCOPE_GOODS, "contraband"},     {"处方药", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"安眠药", WORD_BLOCK, SCOPE_GOODS, "contraband"},   {"阿普唑仑", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"艾司唑仑", WORD_BLOCK, SCOPE_GOODS, "contraband"}, {"雷管", WORD_BLOCK, SCOPE_GOODS, "contraband"},
    {"炸药", WORD_BLOCK, SCOPE_GOODS, "contraband"},     {"烟花爆竹", WORD_BLOCK, SCOPE_GOODS, "contraband"},

    // ---- 商品：证件账号（一级）----
    {"身份证", WORD_BLOCK, SCOPE_GOODS, "identity"},   {"学生证", WORD_BLOCK, SCOPE_GOODS, "identity"},
    {"校园卡", WORD_BLOCK, SCOPE_GOODS, "identity"},   {"饭卡", WORD_BLOCK, SCOPE_GOODS, "identity"},
    {"银行卡", WORD_BLOCK, SCOPE_GOODS, "identity"},   {"手机卡", WORD_BLOCK, SCOPE_GOODS, "identity"},
    {"电话卡", WORD_BLOCK, SCOPE_GOODS, "identity"},   {"实名账号", WORD_BLOCK, SCOPE_GOODS, "identity"},
    {"出租账号", WORD_BLOCK, SCOPE_GOODS, "identity"},
    {"卡套", WORD_ALLOW, SCOPE_GOODS, NULL},           {"证件套", WORD_ALLOW, SCOPE_GOODS, NULL},
    {"饭卡套", WORD_ALLOW, SCOPE_GOODS, NULL},         {"校园卡套", WORD_ALLOW, SCOPE_GOODS, NULL},
    {"学生证套", WORD_ALLOW, SCOPE_GOODS, NULL},       {"身份证套", WORD_ALLOW, SCOPE_GOODS, NULL},

    // ---- 私信二级：骚扰威胁 ----
    {"弄死你", WORD_WARN, SCOPE_CHAT, "threat"},     {"打死你", WORD_WARN, SCOPE_CHAT, "threat"},
    {"砍死你", WORD_WARN, SCOPE_CHAT, "threat"},     {"捅死你", WORD_WARN, SCOPE_CHAT, "threat"},
    {"杀了你", WORD_WARN, SCOPE_CHAT, "threat"},     {"给我等着", WORD_WARN, SCOPE_CHAT, "threat"},
    {"找人打你", WORD_WARN, SCOPE_CHAT, "threat"},   {"找人弄你", WORD_WARN, SCOPE_CHAT, "threat"},
    {"知道你住哪", WORD_WARN, SCOPE_CHAT, "threat"}, {"有你好看", WORD_WARN, SCOPE_CHAT, "threat"},
    {"人肉你", WORD_WARN, SCOPE_CHAT, "threat"},     {"开盒", WORD_WARN, SCOPE_CHAT, "threat"},

    // ---- 私信二级：诈骗引流 ----
    {"加微信", WORD_WARN, SCOPE_CHAT, "scam"},   {"加我微信", WORD_WARN, SCOPE_CHAT, "scam"},
    {"微信号", WORD_WARN, SCOPE_CHAT, "scam"},   {"加v", WORD_WARN, SCOPE_CHAT, "scam"},
    {"vx", WORD_WARN, SCOPE_CHAT, "scam"},       {"wx", WORD_WARN, SCOPE_CHAT, "scam"},
    {"加qq", WORD_WARN, SCOPE_CHAT, "scam"},     {"qq号", WORD_WARN, SCOPE_CHAT, "scam"},
    {"加扣扣", WORD_WARN, SCOPE_CHAT, "scam"},   {"私下交易", WORD_WARN, SCOPE_CHAT, "scam"},
    {"私下转账", WORD_WARN, SCOPE_CHAT, "scam"}, {"线下转账", WORD_WARN, SCOPE_CHAT, "scam"},
    {"直接转账", WORD_WARN, SCOPE_CHAT, "scam"}, {"转账给我", WORD_WARN, SCOPE_CHAT, "scam"},
    {"扫码", WORD_WARN, SCOPE_CHAT, "scam"},     {"二维码", WORD_WARN, SCOPE_CHAT, "scam"},
    {"收款码", WORD_WARN, SCOPE_CHAT, "scam"},   {"先付定金", WORD_WARN, SCOPE_CHAT, "scam"},
    {"保证金", WORD_WARN, SCOPE_CHAT, "scam"},   {"退款链接", WORD_WARN, SCOPE_CHAT, "scam"},
    {"点击链接", WORD_WARN, SCOPE_CHAT, "scam"}, {"刷单", WORD_WARN, SCOPE_CHAT, "scam"},
    {"客服", WORD_WARN, SCOPE_CHAT, "scam"},     {"http", WORD_WARN, SCOPE_CHAT, "scam"},
    {"https", WORD_WARN, SCOPE_CHAT, "scam"},    {"www", WORD_WARN, SCOPE_CHAT, "scam"},
};

const char *category_label(const char *code) {
    static const struct { const char *code, *label; } LABELS[] = {
        {"abuse", "人身攻击"},  {"porn", "色情低俗"},      {"political", "政治敏感"},
        {"illegal", "违法违禁"}, {"threat", "骚扰威胁"},    {"scam", "诈骗引流"},
        {"cheating", "代课代写"}, {"contraband", "违禁品"}, {"identity", "证件账号"},
    };
    for (size_t i = 0; code && i < sizeof LABELS / sizeof LABELS[0]; i++)
        if (strcmp(LABELS[i].code, code) == 0) return LABELS[i].label;
    return "违规内容";
}

// ---------------- 归一化 ----------------

// 解码一个 UTF-8 字符，返回它占几个字节；坏字节当成一个字节跳过（*cp = 0）
static int utf8_next(const unsigned char *p, uint32_t *cp) {
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cp = (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F);
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cp = (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *cp = (uint32_t)(p[0] & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 |
              (p[3] & 0x3F);
        return 4;
    }
    *cp = 0;
    return 1;
}

static bool is_ascii_alnum(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// 把一个字符变成"标准形式"；返回 0 表示这个字符是分隔符（空格、标点、表情等），匹配时跳过
static uint32_t fold(uint32_t c) {
    if (c >= 0xFF01 && c <= 0xFF5E) c -= 0xFEE0;  // 全角 → 半角
    if (c >= 'A' && c <= 'Z') c += 32;            // 大写 → 小写
    if (c < 0x80) return is_ascii_alnum(c) ? c : 0;
    if (c <= 0xBF) return 0;                       // 拉丁补充里的标点和空格
    if (c >= 0x2000 && c <= 0x2BFF) return 0;      // 通用标点、箭头、各种符号
    if (c >= 0x3000 && c <= 0x303F) return 0;      // 中文标点
    if (c >= 0xFE30 && c <= 0xFE4F) return 0;      // 竖排标点
    if (c >= 0xFF00 && c <= 0xFFEF) return 0;      // 剩下的全角符号
    if (c >= 0x1F000 && c <= 0x1FAFF) return 0;    // 表情
    if (c >= 0xFE00 && c <= 0xFE0F) return 0;      // 变体选择符（表情后面常跟）
    return c;
}

typedef struct {
    uint32_t *ch;   // 归一化后的字符
    bool     *gap;  // 这个字符前面是不是被跳过了分隔符（判断英文词边界用）
    size_t    len;
} Folded;

static bool fold_text(const char *text, Folded *f) {
    size_t n = strlen(text);
    f->ch = (uint32_t *)malloc((n + 1) * sizeof *f->ch);
    f->gap = (bool *)malloc((n + 1) * sizeof *f->gap);
    f->len = 0;
    if (!f->ch || !f->gap) {
        free(f->ch);
        free(f->gap);
        return false;
    }
    bool pending_gap = false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t cp;
        p += utf8_next(p, &cp);
        uint32_t c = fold(cp);
        if (c == 0) {
            pending_gap = true;
            continue;
        }
        f->ch[f->len] = c;
        f->gap[f->len] = pending_gap;
        f->len++;
        pending_gap = false;
    }
    f->gap[f->len] = true;  // 结尾也算边界
    return true;
}

// ---------------- 字典树 ----------------
// 每个节点是一个字；从根往下走，走过的字连起来就是词的前缀。
// 孩子用"第一个孩子 + 下一个兄弟"的链表存，代码简单，词库几百个词足够快。

typedef struct Node {
    uint32_t       ch;
    struct Node   *child;
    struct Node   *sibling;
    const WordDef *defs[3];  // 以这个节点结尾的词：按 scope 分别记（私信 / 商品 / 白名单）
    bool           ascii;    // 这个词是不是纯英文数字（要检查词边界）
} Node;

static Node g_root;
static bool g_ready;

static Node *child_of(Node *n, uint32_t ch, bool create) {
    for (Node *c = n->child; c; c = c->sibling)
        if (c->ch == ch) return c;
    if (!create) return NULL;
    Node *c = (Node *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->ch = ch;
    c->sibling = n->child;
    n->child = c;
    return c;
}

static void insert(const WordDef *def) {
    Folded f;
    if (!fold_text(def->word, &f)) return;
    Node *n = &g_root;
    bool ascii = true;
    for (size_t i = 0; i < f.len && n; i++) {
        n = child_of(n, f.ch[i], true);
        if (f.ch[i] >= 0x80) ascii = false;
    }
    if (n && n != &g_root) {
        n->ascii = ascii;
        if (def->level == WORD_ALLOW) n->defs[2] = def;
        if (def->level != WORD_ALLOW && (def->scope & SCOPE_CHAT)) n->defs[0] = def;
        if (def->level != WORD_ALLOW && (def->scope & SCOPE_GOODS)) n->defs[1] = def;
    }
    free(f.ch);
    free(f.gap);
}

void words_init(void) {
    if (g_ready) return;
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) insert(&WORDS[i]);
    g_ready = true;
}

// 英文词要求两边是边界：前后不是英文数字，或者中间隔着空格标点。
// 这样 "usb" 里的 "sb" 不算，"加 vx 123" 里的 "vx" 算。
static bool ascii_boundary_ok(const Folded *f, size_t start, size_t end) {
    bool left = start == 0 || f->gap[start] || !is_ascii_alnum(f->ch[start - 1]);
    bool right = end == f->len || f->gap[end] || !is_ascii_alnum(f->ch[end]);
    return left && right;
}

bool words_check(const char *text, int scope, WordHit *hit) {
    if (!text || !*text) return false;
    words_init();
    Folded f;
    if (!fold_text(text, &f)) return false;

    int slot = (scope & SCOPE_GOODS) ? 1 : 0;
    size_t allow_end = 0;  // 白名单短语覆盖到哪里（不含）
    const WordDef *warn = NULL;
    const WordDef *block = NULL;

    for (size_t i = 0; i < f.len && !block; i++) {
        // 先看从 i 开始有没有白名单短语，有就把它覆盖的范围记下来
        Node *n = &g_root;
        for (size_t j = i; j < f.len && (n = child_of(n, f.ch[j], false)); j++)
            if (n->defs[2] && j + 1 > allow_end) allow_end = j + 1;
        if (i < allow_end) continue;

        n = &g_root;
        for (size_t j = i; j < f.len && (n = child_of(n, f.ch[j], false)); j++) {
            const WordDef *d = n->defs[slot];
            if (!d && slot == 1 && (scope & SCOPE_CHAT)) d = n->defs[0];
            if (!d || (n->ascii && !ascii_boundary_ok(&f, i, j + 1))) continue;
            if (d->level == WORD_BLOCK) { block = d; break; }
            if (!warn) warn = d;
        }
    }
    free(f.ch);
    free(f.gap);

    const WordDef *d = block ? block : warn;
    if (!d) return false;
    if (hit) {
        hit->level = d->level;
        hit->category = d->category;
        hit->word = d->word;
    }
    return true;
}
