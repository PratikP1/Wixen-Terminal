/* test_lua_extended.c — Lua plugin engine edge cases */
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/config/lua_engine.h"

TEST lua_create_destroy(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(e != NULL);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_exec_simple(void) {
    WixenLuaEngine *e = wixen_lua_create();
    bool ok = wixen_lua_exec_string(e, "x = 42");
    ASSERT(ok);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_exec_syntax_error(void) {
    WixenLuaEngine *e = wixen_lua_create();
    bool ok = wixen_lua_exec_string(e, "this is not valid lua !!!");
    ASSERT_FALSE(ok);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_get_string(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "result = 'hello'");
    char *s = wixen_lua_get_string(e, "result");
    ASSERT(s != NULL);
    ASSERT_STR_EQ("hello", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_get_int(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "num = 42");
    int val = wixen_lua_get_int(e, "num", 0);
    ASSERT_EQ(42, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_get_int_default(void) {
    WixenLuaEngine *e = wixen_lua_create();
    int val = wixen_lua_get_int(e, "nonexistent", 99);
    ASSERT_EQ(99, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_get_bool(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "flag = true");
    bool val = wixen_lua_get_bool(e, "flag", false);
    ASSERT(val);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_file_nonexistent(void) {
    WixenLuaEngine *e = wixen_lua_create();
    bool ok = wixen_lua_exec_file(e, "nonexistent_file_XXXX.lua");
    ASSERT_FALSE(ok);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_load_config_file(void) {
    const char *path = "test_lua_temp.lua";
    FILE *f = fopen(path, "w");
    if (!f) SKIP();
    fprintf(f, "config_font_size = 14\nconfig_theme = 'dark'\n");
    fclose(f);

    WixenLuaEngine *e = wixen_lua_create();
    bool ok = wixen_lua_exec_file(e, path);
    ASSERT(ok);

    int fs = wixen_lua_get_int(e, "config_font_size", 0);
    ASSERT_EQ(14, fs);

    char *theme = wixen_lua_get_string(e, "config_theme");
    ASSERT(theme != NULL);
    ASSERT_STR_EQ("dark", theme);
    free(theme);

    wixen_lua_destroy(e);
    remove(path);
    PASS();
}

TEST lua_multiple_execs(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "a = 1");
    wixen_lua_exec_string(e, "b = 2");
    wixen_lua_exec_string(e, "c = a + b");
    int val = wixen_lua_get_int(e, "c", 0);
    ASSERT_EQ(3, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_string_concat(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "greeting = 'Hello' .. ' ' .. 'World'");
    char *s = wixen_lua_get_string(e, "greeting");
    ASSERT(s != NULL);
    ASSERT_STR_EQ("Hello World", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

SUITE(lua_extended) {
    RUN_TEST(lua_create_destroy);
    RUN_TEST(lua_exec_simple);
    RUN_TEST(lua_exec_syntax_error);
    RUN_TEST(lua_get_string);
    RUN_TEST(lua_get_int);
    RUN_TEST(lua_get_int_default);
    RUN_TEST(lua_get_bool);
    RUN_TEST(lua_file_nonexistent);
    RUN_TEST(lua_load_config_file);
    RUN_TEST(lua_multiple_execs);
    RUN_TEST(lua_string_concat);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(lua_extended);
    GREATEST_MAIN_END();
}
