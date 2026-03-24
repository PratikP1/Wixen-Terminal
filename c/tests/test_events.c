/* test_events.c — Tests for event throttling and VT escape stripping */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/events.h"

/* === VT Escape Stripping === */

TEST strip_plain_text(void) {
    char *s = wixen_strip_vt_escapes("Hello World");
    ASSERT_STR_EQ("Hello World", s);
    free(s);
    PASS();
}

TEST strip_csi_sequence(void) {
    char *s = wixen_strip_vt_escapes("\x1b[31mRed\x1b[0m");
    ASSERT_STR_EQ("Red", s);
    free(s);
    PASS();
}

TEST strip_osc_title(void) {
    char *s = wixen_strip_vt_escapes("\x1b]0;Title\x07Text");
    ASSERT_STR_EQ("Text", s);
    free(s);
    PASS();
}

TEST strip_multiple_sequences(void) {
    char *s = wixen_strip_vt_escapes("\x1b[1m\x1b[31mBold Red\x1b[0m Normal");
    ASSERT_STR_EQ("Bold Red Normal", s);
    free(s);
    PASS();
}

TEST strip_empty(void) {
    char *s = wixen_strip_vt_escapes("");
    ASSERT_STR_EQ("", s);
    free(s);
    PASS();
}

TEST strip_null(void) {
    char *s = wixen_strip_vt_escapes(NULL);
    ASSERT_STR_EQ("", s);
    free(s);
    PASS();
}

TEST strip_cursor_movement(void) {
    char *s = wixen_strip_vt_escapes("\x1b[5;10HHello");
    ASSERT_STR_EQ("Hello", s);
    free(s);
    PASS();
}

/* === Control Character Stripping === */

TEST strip_ctrl_backspace(void) {
    char *s = wixen_strip_control_chars("A\x08" "B");
    ASSERT_STR_EQ("AB", s);
    free(s);
    PASS();
}

TEST strip_ctrl_bell(void) {
    char *s = wixen_strip_control_chars("Hello\x07World");
    ASSERT_STR_EQ("HelloWorld", s);
    free(s);
    PASS();
}

TEST strip_ctrl_preserves_newline(void) {
    char *s = wixen_strip_control_chars("Line1\nLine2");
    ASSERT_STR_EQ("Line1\nLine2", s);
    free(s);
    PASS();
}

TEST strip_ctrl_preserves_tab(void) {
    char *s = wixen_strip_control_chars("A\tB");
    ASSERT_STR_EQ("A\tB", s);
    free(s);
    PASS();
}

TEST strip_ctrl_null_input(void) {
    char *s = wixen_strip_control_chars(NULL);
    ASSERT_STR_EQ("", s);
    free(s);
    PASS();
}

/* === Throttler === */

TEST throttler_init(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    ASSERT_FALSE(wixen_throttler_is_streaming(&et));
    char *text = wixen_throttler_take_pending(&et);
    ASSERT(text == NULL);
    wixen_throttler_free(&et);
    PASS();
}

TEST throttler_fires_first_event(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    bool fire = wixen_throttler_on_output(&et, "Hello\n", 0);
    ASSERT(fire); /* First event always fires (0ms since last = 0) */
    char *text = wixen_throttler_take_pending(&et);
    ASSERT(text != NULL);
    ASSERT_STR_EQ("Hello\n", text);
    free(text);
    wixen_throttler_free(&et);
    PASS();
}

TEST throttler_debounces(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    wixen_throttler_on_output(&et, "A\n", 0);
    wixen_throttler_take_pending(&et); /* Clear */

    /* Within debounce window — should not fire */
    bool fire = wixen_throttler_on_output(&et, "B\n", 50);
    ASSERT_FALSE(fire);

    /* After debounce window — should fire */
    fire = wixen_throttler_on_output(&et, "C\n", 150);
    ASSERT(fire);

    char *text = wixen_throttler_take_pending(&et);
    ASSERT(text != NULL);
    /* Should contain both B and C */
    ASSERT(strstr(text, "B") != NULL);
    ASSERT(strstr(text, "C") != NULL);
    free(text);
    wixen_throttler_free(&et);
    PASS();
}

TEST throttler_streaming_detection(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    /* Feed 15 lines in one batch */
    for (int i = 0; i < 15; i++) {
        wixen_throttler_on_output(&et, "line\n", 0);
    }
    ASSERT(wixen_throttler_is_streaming(&et));
    free(wixen_throttler_take_pending(&et));
    wixen_throttler_free(&et);
    PASS();
}

TEST throttler_not_streaming_few_lines(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    wixen_throttler_on_output(&et, "line1\nline2\n", 0);
    ASSERT_FALSE(wixen_throttler_is_streaming(&et));
    free(wixen_throttler_take_pending(&et));
    wixen_throttler_free(&et);
    PASS();
}

TEST throttler_empty_output_ignored(void) {
    WixenEventThrottler et;
    wixen_throttler_init(&et, 100);
    ASSERT_FALSE(wixen_throttler_on_output(&et, "", 0));
    ASSERT_FALSE(wixen_throttler_on_output(&et, NULL, 0));
    wixen_throttler_free(&et);
    PASS();
}

SUITE(vt_stripping) {
    RUN_TEST(strip_plain_text);
    RUN_TEST(strip_csi_sequence);
    RUN_TEST(strip_osc_title);
    RUN_TEST(strip_multiple_sequences);
    RUN_TEST(strip_empty);
    RUN_TEST(strip_null);
    RUN_TEST(strip_cursor_movement);
}

SUITE(ctrl_stripping) {
    RUN_TEST(strip_ctrl_backspace);
    RUN_TEST(strip_ctrl_bell);
    RUN_TEST(strip_ctrl_preserves_newline);
    RUN_TEST(strip_ctrl_preserves_tab);
    RUN_TEST(strip_ctrl_null_input);
}

SUITE(throttler_tests) {
    RUN_TEST(throttler_init);
    RUN_TEST(throttler_fires_first_event);
    RUN_TEST(throttler_debounces);
    RUN_TEST(throttler_streaming_detection);
    RUN_TEST(throttler_not_streaming_few_lines);
    RUN_TEST(throttler_empty_output_ignored);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(vt_stripping);
    RUN_SUITE(ctrl_stripping);
    RUN_SUITE(throttler_tests);
    GREATEST_MAIN_END();
}
