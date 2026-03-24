/* test_clipboard.c — Clipboard operations (RED tests first) */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/clipboard.h"

/* These tests run without a window handle (NULL).
 * clipboard operations with NULL hwnd should still work on Windows. */

TEST clipboard_has_text_check(void) {
    /* Just verifying the function doesn't crash — result depends on system state */
    bool has = wixen_clipboard_has_text();
    (void)has;
    PASS();
}

TEST clipboard_set_and_get_roundtrip(void) {
    bool ok = wixen_clipboard_set_text(NULL, "Hello from Wixen");
    ASSERT(ok);
    char *got = wixen_clipboard_get_text(NULL);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("Hello from Wixen", got);
    free(got);
    PASS();
}

TEST clipboard_set_empty_string(void) {
    bool ok = wixen_clipboard_set_text(NULL, "");
    ASSERT(ok);
    char *got = wixen_clipboard_get_text(NULL);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("", got);
    free(got);
    PASS();
}

TEST clipboard_set_null_fails(void) {
    bool ok = wixen_clipboard_set_text(NULL, NULL);
    ASSERT_FALSE(ok);
    PASS();
}

TEST clipboard_set_utf8(void) {
    bool ok = wixen_clipboard_set_text(NULL, "caf\xc3\xa9 \xe2\x9c\x93");
    ASSERT(ok);
    char *got = wixen_clipboard_get_text(NULL);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("caf\xc3\xa9 \xe2\x9c\x93", got);
    free(got);
    PASS();
}

TEST clipboard_overwrite(void) {
    wixen_clipboard_set_text(NULL, "First");
    wixen_clipboard_set_text(NULL, "Second");
    char *got = wixen_clipboard_get_text(NULL);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("Second", got);
    free(got);
    PASS();
}

TEST clipboard_has_text_after_set(void) {
    wixen_clipboard_set_text(NULL, "test");
    ASSERT(wixen_clipboard_has_text());
    PASS();
}

SUITE(clipboard_tests) {
    RUN_TEST(clipboard_has_text_check);
    RUN_TEST(clipboard_set_and_get_roundtrip);
    RUN_TEST(clipboard_set_empty_string);
    RUN_TEST(clipboard_set_null_fails);
    RUN_TEST(clipboard_set_utf8);
    RUN_TEST(clipboard_overwrite);
    RUN_TEST(clipboard_has_text_after_set);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(clipboard_tests);
    GREATEST_MAIN_END();
}
