# 广理职小鱼 · 服务器
# Windows：VS 2022 → 文件 → 打开 → 文件夹，选 server 文件夹，等它自动配置完就能编译运行
# Linux：  cmake -B build && cmake --build build -j && ./build/xiaoyu_server
cmake_minimum_required(VERSION 3.16)
project(xiaoyu_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    # /utf-8：源码和字符串都按 UTF-8 处理，中文才不会乱码
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# ---------- 第三方库 ----------
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)

# ---------- 服务器代码（底层 + 四个业务模块） ----------
add_library(xiaoyu_core STATIC
    src/core/config.c
    src/core/ctx.c
    src/core/db.c
    src/core/files.c
    src/core/jev.c
    src/core/jsonx.c
    src/core/log.c
    src/core/mail.c
    src/core/platform.c
    src/core/router.c
    src/core/server.c
    src/core/shared.c
    src/core/util.c
    src/core/words.c
    src/modules/user/user.c
    src/modules/goods/goods.c
    src/modules/order/order.c
    src/modules/chat/chat.c
    src/modules/safety/safety.c
)
target_include_directories(xiaoyu_core PUBLIC src)
target_link_libraries(xiaoyu_core PUBLIC sqlite3 cjson)
if(WIN32)
    target_link_libraries(xiaoyu_core PUBLIC ws2_32 bcrypt)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(xiaoyu_core PUBLIC Threads::Threads m)
endif()
# libcurl：调用 Jev（AI 审核）和发验证码邮件要用。找不到也能编译，只是这两项功能不可用
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(xiaoyu_core PUBLIC CURL::libcurl)
    target_compile_definitions(xiaoyu_core PUBLIC XIAOYU_HAVE_CURL)
    message(STATUS "找到 libcurl：AI 审核和邮件验证码可用")
else()
    message(STATUS "没找到 libcurl：AI 审核和邮件验证码不可用（其他功能不受影响）")
endif()
if(MSVC)
    target_compile_options(xiaoyu_core PRIVATE /W3)
else()
    target_compile_options(xiaoyu_core PRIVATE -Wall -Wextra)
endif()

add_executable(xiaoyu_server src/main.c)
target_link_libraries(xiaoyu_server PRIVATE xiaoyu_core)

# ---------- 测试程序：每个模块一个，直接运行就能看到 [通过]/[失败] ----------
enable_testing()
foreach(name util user goods order chat safety)
    add_executable(test_${name} tests/test_${name}.c tests/t.c)
    target_link_libraries(test_${name} PRIVATE xiaoyu_core)
    target_include_directories(test_${name} PRIVATE tests)
    add_test(NAME test_${name} COMMAND test_${name})
endforeach()
