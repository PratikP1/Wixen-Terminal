/* test_lua.c — Tests for Lua plugin engine */
#include <stdbool.h>
#include <string.h>
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
    ASSERT(wixen_lua_exec_string(e, "x = 42"));
    int val = wixen_lua_get_int(e, "x", 0);
    ASSERT_EQ(42, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_exec_string_var(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "name = 'Wixen'");
    char *s = wixen_lua_get_string(e, "name");
    ASSERT(s != NULL);
    ASSERT_STR_EQ("Wixen", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_exec_bool_var(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "enabled = true");
    ASSERT(wixen_lua_get_bool(e, "enabled", false));
    wixen_lua_exec_string(e, "disabled = false");
    ASSERT_FALSE(wixen_lua_get_bool(e, "disabled", true));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_get_nonexistent_returns_default(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT_EQ(99, wixen_lua_get_int(e, "nope", 99));
    ASSERT(wixen_lua_get_string(e, "nope") == NULL);
    ASSERT(wixen_lua_get_bool(e, "nope", true));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_call_function(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "result = 0\n"
                              "function set_result() result = 123 end");
    ASSERT(wixen_lua_call(e, "set_result"));
    ASSERT_EQ(123, wixen_lua_get_int(e, "result", 0));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_call_nonexistent_function(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT_FALSE(wixen_lua_call(e, "does_not_exist"));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_call_with_string_arg(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "received = ''\n"
                              "function on_output(text) received = text end");
    ASSERT(wixen_lua_call_str(e, "on_output", "Hello World"));
    char *s = wixen_lua_get_string(e, "received");
    ASSERT(s != NULL);
    ASSERT_STR_EQ("Hello World", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_sandbox_no_os(void) {
    WixenLuaEngine *e = wixen_lua_create();
    /* os should be nil in sandbox */
    ASSERT(wixen_lua_exec_string(e, "assert(os == nil)"));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_sandbox_no_io(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(wixen_lua_exec_string(e, "assert(io == nil)"));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_syntax_error_returns_false(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT_FALSE(wixen_lua_exec_string(e, "this is not valid lua {{{"));
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_string_functions_available(void) {
    WixenLuaEngine *e = wixen_lua_create();
    wixen_lua_exec_string(e, "result = string.upper('hello')");
    char *s = wixen_lua_get_string(e, "result");
    ASSERT(s != NULL);
    ASSERT_STR_EQ("HELLO", s);
    free(s);
    wixen_lua_destroy(e);
    PASS();
}

TEST lua_null_engine_safe(void) {
    ASSERT_FALSE(wixen_lua_exec_string(NULL, "x = 1"));
    ASSERT_FALSE(wixen_lua_call(NULL, "f"));
    ASSERT(wixen_lua_get_string(NULL, "x") == NULL);
    ASSERT_EQ(42, wixen_lua_get_int(NULL, "x", 42));
    wixen_lua_destroy(NULL); /* Should not crash */
    PASS();
}

SUITE(lua_tests) {
    RUN_TEST(lua_create_destroy);
    RUN_TEST(lua_exec_simple);
    RUN_TEST(lua_exec_string_var);
    RUN_TEST(lua_exec_bool_var);
    RUN_TEST(lua_get_nonexistent_returns_default);
    RUN_TEST(lua_call_function);
    RUN_TEST(lua_call_nonexistent_function);
    RUN_TEST(lua_call_with_string_arg);
    RUN_TEST(lua_sandbox_no_os);
    RUN_TEST(lua_sandbox_no_io);
    RUN_TEST(lua_syntax_error_returns_false);
    RUN_TEST(lua_string_functions_available);
    RUN_TEST(lua_null_engine_safe);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(lua_tests);
    GREATEST_MAIN_END();
}
