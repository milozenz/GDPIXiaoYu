// 数据库：打开连接、建表、执行简单 SQL。
// 表结构见 db.c 里的 SCHEMA_SQL，说明见《数据库设计》文档。
#ifndef XIAOYU_DB_H
#define XIAOYU_DB_H

#include <stdbool.h>

#include "sqlite3.h"

// 打开数据库并设置好参数（外键、WAL、忙等待）。path 用 ":memory:" 表示内存数据库（测试用）。
// 失败返回 NULL。
sqlite3 *db_open(const char *path);
void db_close(sqlite3 *db);

// 建表（已经建过就跳过），并写入固定的分类数据
bool db_init_schema(sqlite3 *db);

// 执行一条不需要参数、不需要结果的 SQL，比如 "BEGIN IMMEDIATE"、"COMMIT"、"ROLLBACK"
bool db_exec(sqlite3 *db, const char *sql);

#endif
