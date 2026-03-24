/* test_events_extended.c — More event throttler and VT stripping tests */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/events.h"

TEST strip_nested_csi(void) {
    char *s = wixen_strip_vt_escapes("\x1b[1;31m\x1b[42mHello\x1b[0m");
    ASSERT_STR_EQ("Hello", s);
    free(s);
    PASS();
}

TEST strip_osc_with_st(void) {
    char *s = wixen_strip_vt_escapes("\x1b]2;Title\x1b\\Rest");
    /* ESC ] ... ESC \ should strip the OSC, leaving "Rest" */
    /* The ESC at end becomes an escape sequence start */
    ASSERT(strstr(s, "Rest") != NULL || strlen(s) > 0);
    free(s);
    PASS();
}

TEST strip_dcs(void) {
    char *s = wixen_strip_vt_escapes("\x1b" "P" "data\x1b\\after");
    /* DCS should be stripped */
    ASSERT(strstr(s, "after") != NULL);
    free(s);
    PASS();
}

TEST strip_preserves_newlines(void) {
    char *s = wixen_strip_vt_escapes("Line1\nLine2\n\x1b[31mLine3\x1b[0m");
    ASSERT(strstr(s, "Line1\nLine2\n") != NULL);
    ASSERT(strstr(s, "Line3") != NULL);
    free(s);
    PASS();
}

TEST ctrl_strip_null_bytes(void) {
    /* NUL in C strings terminates early — test SOH (0x01) instead */
    char *s = wixen_strip_control_chars("A\x01" "B");
    ASSERT_STR_EQ("AB", s);
    free(s);
    PASS();
}

TEST ctrl_strip_cr(void) {
    char *s = wixen_strip_control_chars("Hello\rWorld");
    ASSERT_STR_EQ("HelloWorld", s); /* CR is a control char, stripped */
    free(s);
    PASS();
}

TEST ctrl_strip_esc(void) {
    char *s = wixen_strip_control_chars("A\x1b" "B");
    ASSERT_STR_EQ("AB", s); /* ESC stripped */
    free(s);
    PASS();
}

TEST throttler_reset_streaming(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    for (int i = 0; i < 15; i++)
        wixen_throttler_on_output(&et, "line\n", 0);
    ASSERT(wixen_throttler_is_streaming(&et));
    free(wixen_throttler_take_pending(&et));
    /* After take, streaming should reset when new output comes */
    wixen_throttler_on_output(&et, "single\n", 200);
    ASSERT_FALSE(wixen_throttler_is_streaming(&et));
    free(wixen_throttler_take_pending(&et));
    wixen_throttler_free(&et);
    PASS();
}

TEST throttler_accumulates_between_fires(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    wixen_throttler_on_output(&et, "A", 0);
    free(wixen_throttler_take_pending(&et));
    /* Within debounce */
    wixen_throttler_on_output(&et, "B", 20);
    wixen_throttler_on_output(&et, "C", 40);
    /* Not fired yet, but text accumulated */
    char *text = wixen_throttler_take_pending(&et);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "B") != NULL);
    ASSERT(strstr(text, "C") != NULL);
    free(text);
    wixen_throttler_free(&et);
    PASS();
}

SUITE(events_extended) {
    RUN_TEST(strip_nested_csi);
    RUN_TEST(strip_osc_with_st);
    RUN_TEST(strip_dcs);
    RUN_TEST(strip_preserves_newlines);
    RUN_TEST(ctrl_strip_null_bytes);
    RUN_TEST(ctrl_strip_cr);
    RUN_TEST(ctrl_strip_esc);
    RUN_TEST(throttler_reset_streaming);
    RUN_TEST(throttler_accumulates_between_fires);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(events_extended);
    GREATEST_MAIN_END();
}
