/* test_red_focus_strategy.c -- RED-GREEN TDD tests for focus acquisition */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/ui/focus.h"

/* --- focus_method_name tests --- */

TEST method_name_returns_non_null_for_all_enum_values(void) {
    ASSERT(wixen_focus_method_name(FOCUS_NONE) != NULL);
    ASSERT(wixen_focus_method_name(FOCUS_DIRECT) != NULL);
    ASSERT(wixen_focus_method_name(FOCUS_ALLOC_CONSOLE) != NULL);
    ASSERT(wixen_focus_method_name(FOCUS_ATTACH_THREAD) != NULL);
    ASSERT(wixen_focus_method_name(FOCUS_FLASH_ONLY) != NULL);
    PASS();
}

TEST method_name_none_is_none(void) {
    ASSERT_STR_EQ("none", wixen_focus_method_name(FOCUS_NONE));
    PASS();
}

TEST method_name_direct(void) {
    ASSERT_STR_EQ("direct", wixen_focus_method_name(FOCUS_DIRECT));
    PASS();
}

TEST method_name_alloc_console(void) {
    ASSERT_STR_EQ("alloc_console", wixen_focus_method_name(FOCUS_ALLOC_CONSOLE));
    PASS();
}

TEST method_name_attach_thread(void) {
    ASSERT_STR_EQ("attach_thread", wixen_focus_method_name(FOCUS_ATTACH_THREAD));
    PASS();
}

TEST method_name_flash_only(void) {
    ASSERT_STR_EQ("flash_only", wixen_focus_method_name(FOCUS_FLASH_ONLY));
    PASS();
}

/* --- WixenFocusResult struct initialization --- */

TEST result_struct_zero_init(void) {
    WixenFocusResult r = { FOCUS_NONE, false, 0 };
    ASSERT_EQ(FOCUS_NONE, (int)r.method_used);
    ASSERT_FALSE(r.success);
    ASSERT_EQ(0, r.attempts);
    PASS();
}

TEST result_struct_can_hold_values(void) {
    WixenFocusResult r;
    r.method_used = FOCUS_ATTACH_THREAD;
    r.success = true;
    r.attempts = 3;
    ASSERT_EQ(FOCUS_ATTACH_THREAD, (int)r.method_used);
    ASSERT(r.success);
    ASSERT_EQ(3, r.attempts);
    PASS();
}

/* --- Win32-only tests for is_foreground and acquire --- */
#ifdef _WIN32

TEST is_foreground_null_hwnd(void) {
    ASSERT_FALSE(wixen_focus_is_foreground(NULL));
    PASS();
}

TEST is_foreground_invalid_hwnd(void) {
    /* 0xDEAD is almost certainly not a valid window handle */
    ASSERT_FALSE(wixen_focus_is_foreground((HWND)(uintptr_t)0xDEAD));
    PASS();
}

TEST acquire_null_hwnd_returns_failure(void) {
    WixenFocusResult r = wixen_focus_acquire(NULL);
    ASSERT_FALSE(r.success);
    ASSERT_EQ(FOCUS_NONE, (int)r.method_used);
    ASSERT_EQ(0, r.attempts);
    PASS();
}

#endif /* _WIN32 */

/* --- Test suites --- */

SUITE(focus_method_name_tests) {
    RUN_TEST(method_name_returns_non_null_for_all_enum_values);
    RUN_TEST(method_name_none_is_none);
    RUN_TEST(method_name_direct);
    RUN_TEST(method_name_alloc_console);
    RUN_TEST(method_name_attach_thread);
    RUN_TEST(method_name_flash_only);
}

SUITE(focus_result_tests) {
    RUN_TEST(result_struct_zero_init);
    RUN_TEST(result_struct_can_hold_values);
}

#ifdef _WIN32
SUITE(focus_win32_tests) {
    RUN_TEST(is_foreground_null_hwnd);
    RUN_TEST(is_foreground_invalid_hwnd);
    RUN_TEST(acquire_null_hwnd_returns_failure);
}
#endif

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(focus_method_name_tests);
    RUN_SUITE(focus_result_tests);
#ifdef _WIN32
    RUN_SUITE(focus_win32_tests);
#endif
    GREATEST_MAIN_END();
}
