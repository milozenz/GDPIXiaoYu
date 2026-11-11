// 敏感词检测：Trie（字典树）+ 文本归一化。
//
// 两级词库：
//   一级（WORD_BLOCK）：命中直接拦下，私信发不出去、商品上不了架
//   二级（WORD_WARN） ：只用于私信，照常发出，但接收方会看到提醒，后台再交给 AI 判断
// 匹配前先把文本"归一化"：去掉空格、标点、表情，全角转半角，大写转小写，
// 所以"操 你 妈""操*你*妈""ＳＢ"这类变体也能匹配到。
#ifndef XIAOYU_WORDS_H
#define XIAOYU_WORDS_H

#include <stdbool.h>

enum { WORD_ALLOW = 0, WORD_BLOCK = 1, WORD_WARN = 2 };  // 词的级别（ALLOW = 白名单短语）
enum { SCOPE_CHAT = 1, SCOPE_GOODS = 2 };                // 词用在哪里，可以按位或

typedef struct WordHit {
    int         level;     // WORD_BLOCK 或 WORD_WARN
    const char *category;  // 类别代码，比如 "abuse"、"scam"
    const char *word;      // 命中的词（词库里的原文）
} WordHit;

// 建好字典树。程序启动时调用一次（words_check 第一次被调用时也会自动调用）。
void words_init(void);

// 检查 text 在 scope 范围里有没有命中。一级词优先：有一级词就返回一级词，否则返回第一个二级词。
// 没命中返回 false。
bool words_check(const char *text, int scope, WordHit *hit);

// 类别代码 → 中文名，比如 "abuse" → "人身攻击"；不认识的返回 "违规内容"
const char *category_label(const char *code);

#endif
