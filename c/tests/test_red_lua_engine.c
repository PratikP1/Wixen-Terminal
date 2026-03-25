/* test_red_lua_engine.c — RED tests for Lua engine internals
 *
 * The Lua engine provides sandboxed script execution for config
 * overrides and plugins. Tests exercise the API boundary.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/lua_engine.h"

TEST red_lua_create_destroy(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(e != NULL);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_exec_string(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(wixen_lua_exec_string(e, "x = 42"));
    int val = wixen_lua_get_int(e, "x", 0);
    ASSERT_EQ(42, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_exec_invalid(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT_FALSE(wixen_lua_exec_string(e, "this is not valid lua @#$"));
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_get_string(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "name = 'Wixen'");
    char *s = wixen_lua_get_string(e, "name");
    ASSERT(s != NULL);
    ASSERT_STR_EQ("Wixen", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_get_string_missing(void) {
    WixenLuaEngine *e = wixen_lua_create();
    char *s = wixen_lua_get_string(e, "nonexistent");
    ASSERT(s == NULL);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_get_int_default(void) {
    WixenLuaEngine *e = wixen_lua_create();
    int val = wixen_lua_get_int(e, "missing", -1);
    ASSERT_EQ(-1, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_get_bool(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "flag = true");
    ASSERT(wixen_lua_get_bool(e, "flag", false));
    wixen_lua_exec_string(e, "flag = false");
    ASSERT_FALSE(wixen_lua_get_bool(e, "flag", true));
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_nested_table(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "t = { inner = { value = 99 } }");
    int val = wixen_lua_get_int(e, "t.inner.value", 0);
    ASSERT_EQ(99, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_exec_file(void) {
    const char *path = "test_lua_exec.lua";
    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fprintf(f, "result = 2 + 3\n");
    fclose(f);

    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(wixen_lua_exec_file(e, path));
    ASSERT_EQ(5, wixen_lua_get_int(e, "result", 0));
    wixen_lua_destroy(e);
    remove(path);
    PASS();
}

TEST red_lua_exec_file_missing(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT_FALSE(wixen_lua_exec_file(e, "nonexistent_file.lua"));
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_call_function(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "function greet() greeting = 'hello' end");
    ASSERT(wixen_lua_call(e, "greet"));
    char *s = wixen_lua_get_string(e, "greeting");
    ASSERT_STR_EQ("hello", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_call_missing_function(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT_FALSE(wixen_lua_call(e, "nonexistent_func"));
    wixen_lua_destroy(e);
    PASS();
}

static int iter_count_g;
static void iter_counter(const char *k, const char *v, void *ud) {
    (void)k; (void)v; (void)ud;
    iter_count_g++;
}

TEST red_lua_iterate_table(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "t = { x = 'hello', y = 'world' }");
    iter_count_g = 0;
    int result = wixen_lua_iterate_table(e, "t", iter_counter, NULL);
    ASSERT_EQ(2, result);
    ASSERT_EQ(2, iter_count_g);
    wixen_lua_destroy(e);
    PASS();
}

SUITE(red_lua_engine) {
    RUN_TEST(red_lua_create_destroy);
    RUN_TEST(red_lua_exec_string);
    RUN_TEST(red_lua_exec_invalid);
    RUN_TEST(red_lua_get_string);
    RUN_TEST(red_lua_get_string_missing);
    RUN_TEST(red_lua_get_int_default);
    RUN_TEST(red_lua_get_bool);
    RUN_TEST(red_lua_nested_table);
    RUN_TEST(red_lua_exec_file);
    RUN_TEST(red_lua_exec_file_missing);
    RUN_TEST(red_lua_call_function);
    RUN_TEST(red_lua_call_missing_function);
    RUN_TEST(red_lua_iterate_table);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_lua_engine);
    GREATEST_MAIN_END();
}
