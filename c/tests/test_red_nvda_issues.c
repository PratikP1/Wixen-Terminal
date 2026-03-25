/* test_red_nvda_issues.c — RED tests for known NVDA issues from old log
 *
 * Issues found in NVDA log when testing Wixen Terminal:
 * 1. VT escapes leaking into notification text
 * 2. ConPTY OSC 9001 error sequences leaking into speech
 * 3. cursor-char/cursor-deleted firing for every keystroke
 * 4. "blank" announced instead of useful content
 * 5. \r\n in notification text (should be stripped)
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/events.h"
#include "wixen/a11y/frame_update.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* === Issue 1: VT escapes must not appear in notification text === */

TEST red_strip_csi_from_notification(void) {
    char *s = wixen_strip_vt_escapes("\x1b[?25lHello\x1b[0m");
    ASSERT(strstr(s, "\x1b") == NULL);
    ASSERT(strstr(s, "Hello") != NULL);
    free(s);
    PASS();
}

TEST red_strip_osc_9001(void) {
    /* ConPTY sends OSC 9001 for command-not-found */
    char *s = wixen_strip_vt_escapes("\x1b]9001;CmdNotFound;ls\x1b\\");
    ASSERT(strstr(s, "\x1b") == NULL);
    ASSERT(strstr(s, "9001") == NULL);
    free(s);
    PASS();
}

TEST red_strip_cursor_hide_show(void) {
    char *s = wixen_strip_vt_escapes("\x1b[?25l\x1b[H\x1b[K\r\n\x1b[K");
    ASSERT(strstr(s, "\x1b") == NULL);
    /* After stripping, should be empty or just whitespace */
    char *clean = wixen_strip_control_chars(s);
    ASSERT(strlen(clean) == 0 || clean[0] == '\n');
    free(clean);
    free(s);
    PASS();
}

/* === Issue 2: Notification text should not contain \r === */

TEST red_strip_cr_from_notification(void) {
    char *s = wixen_strip_control_chars("Hello\r\nWorld");
    /* \r should be stripped, \n preserved */
    ASSERT(strstr(s, "\r") == NULL);
    ASSERT(strstr(s, "\n") != NULL);
    ASSERT(strstr(s, "Hello") != NULL);
    free(s);
    PASS();
}

/* === Issue 3: Single-char output should NOT generate notifications === */

TEST red_echo_suppression(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Initial frame */
    WixenFrameA11yInput in0 = {
        .cursor_row = 0, .cursor_col = 5,
        .visible_text = "test>",
        .shell_generation = 0, .has_new_output = false, .now_ms = 0,
    };
    WixenFrameA11yEvents ev0 = {0};
    wixen_frame_a11y_update(&fs, &in0, &ev0);
    free(ev0.announce_text); free(ev0.announce_line_text);

    /* Single character typed — should NOT be announced */
    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 6,
        .visible_text = "test>a",
        .shell_generation = 0,
        .has_new_output = true,
        .new_output_text = "a",
        .now_ms = 50,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);
    ASSERT_FALSE(ev1.should_announce_output);
    free(ev1.announce_text); free(ev1.announce_line_text);

    /* Backspace echo — also should NOT be announced */
    WixenFrameA11yInput in2 = {
        .cursor_row = 0, .cursor_col = 5,
        .visible_text = "test>",
        .shell_generation = 0,
        .has_new_output = true,
        .new_output_text = "\x08 \x08",
        .now_ms = 100,
    };
    WixenFrameA11yEvents ev2 = {0};
    wixen_frame_a11y_update(&fs, &in2, &ev2);
    ASSERT_FALSE(ev2.should_announce_output);
    free(ev2.announce_text); free(ev2.announce_line_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* === Issue 4: Multi-line real output SHOULD be announced === */

TEST red_real_output_announced(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    WixenFrameA11yInput in0 = {
        .cursor_row = 0, .cursor_col = 0,
        .visible_text = ">",
        .shell_generation = 0, .has_new_output = false, .now_ms = 0,
    };
    WixenFrameA11yEvents ev0 = {0};
    wixen_frame_a11y_update(&fs, &in0, &ev0);
    free(ev0.announce_text); free(ev0.announce_line_text);

    /* Command output with newlines — SHOULD be announced */
    WixenFrameA11yInput in1 = {
        .cursor_row = 3, .cursor_col = 0,
        .visible_text = "> dir\nfile1.txt\nfile2.txt\n>",
        .shell_generation = 0,
        .has_new_output = true,
        .new_output_text = "file1.txt\nfile2.txt\n",
        .now_ms = 200,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);
    ASSERT(ev1.should_announce_output);
    ASSERT(ev1.announce_text != NULL);
    /* Must NOT contain escape sequences */
    ASSERT(strstr(ev1.announce_text, "\x1b") == NULL);
    free(ev1.announce_text); free(ev1.announce_line_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* === Issue 5: "blank" should not be announced for empty lines === */

TEST red_empty_line_not_announced(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    WixenFrameA11yInput in0 = {
        .cursor_row = 0, .cursor_col = 5,
        .visible_text = "test>hello",
        .shell_generation = 0, .has_new_output = false, .now_ms = 0,
    };
    WixenFrameA11yEvents ev0 = {0};
    wixen_frame_a11y_update(&fs, &in0, &ev0);
    free(ev0.announce_text); free(ev0.announce_line_text);

    /* Line becomes empty (not a replacement, just cleared) */
    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 5,
        .visible_text = "test>",
        .shell_generation = 0, .has_new_output = false, .now_ms = 100,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);
    /* Deletion — should NOT announce as line replacement */
    ASSERT_FALSE(ev1.should_announce_line);
    free(ev1.announce_text); free(ev1.announce_line_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

SUITE(red_nvda_issues) {
    RUN_TEST(red_strip_csi_from_notification);
    RUN_TEST(red_strip_osc_9001);
    RUN_TEST(red_strip_cursor_hide_show);
    RUN_TEST(red_strip_cr_from_notification);
    RUN_TEST(red_echo_suppression);
    RUN_TEST(red_real_output_announced);
    RUN_TEST(red_empty_line_not_announced);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_nvda_issues);
    GREATEST_MAIN_END();
}
