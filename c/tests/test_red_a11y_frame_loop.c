/* test_red_a11y_frame_loop.c — RED tests for a11y frame loop integration
 *
 * The frame loop must: update a11y state atomically, fire UIA events,
 * throttle output, detect boundaries, strip prompts, and pump messages.
 * This tests the integration logic that wires all the pieces together.
 *
 * Since we can't create a real HWND + UIA in a unit test, we test the
 * frame_a11y_update() function which encapsulates the logic without Win32.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/a11y/frame_update.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* === Frame update produces correct events === */

TEST red_frame_no_change(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100); /* 100ms debounce */

    WixenFrameA11yInput input = {
        .cursor_row = 0, .cursor_col = 0,
        .visible_text = "C:\\Users\\test>",
        .shell_generation = 0,
        .has_new_output = false,
        .new_output_text = NULL,
    };

    WixenFrameA11yEvents events = {0};
    wixen_frame_a11y_update(&fs, &input, &events);

    /* First frame — text changed from empty to something */
    /* No cursor movement, no output */
    ASSERT_FALSE(events.cursor_moved);
    ASSERT_FALSE(events.should_announce_output);
    ASSERT(events.text_changed); /* Initial text set */

    wixen_frame_a11y_free(&fs);
    PASS();
}

TEST red_frame_cursor_moved(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Frame 1: initial */
    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 5,
        .visible_text = "Hello",
        .shell_generation = 0,
        .has_new_output = false,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);

    /* Frame 2: cursor moved */
    WixenFrameA11yInput in2 = {
        .cursor_row = 1, .cursor_col = 0,
        .visible_text = "Hello\nWorld",
        .shell_generation = 0,
        .has_new_output = false,
    };
    WixenFrameA11yEvents ev2 = {0};
    wixen_frame_a11y_update(&fs, &in2, &ev2);

    ASSERT(ev2.cursor_moved);
    ASSERT(ev2.row_changed);

    wixen_frame_a11y_free(&fs);
    PASS();
}

TEST red_frame_output_throttled(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Frame 1: new output arrives */
    WixenFrameA11yInput in1 = {
        .cursor_row = 1, .cursor_col = 0,
        .visible_text = "$ ls\nfile1.txt\nfile2.txt\n",
        .shell_generation = 0,
        .has_new_output = true,
        .new_output_text = "file1.txt\nfile2.txt\n",
        .now_ms = 1000,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);

    /* Multi-line output with newlines should be announced */
    ASSERT(ev1.should_announce_output);
    ASSERT(ev1.announce_text != NULL);
    ASSERT(strstr(ev1.announce_text, "file1") != NULL);
    free(ev1.announce_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

TEST red_frame_keyboard_echo_suppressed(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Initial frame */
    WixenFrameA11yInput in0 = {
        .cursor_row = 0, .cursor_col = 0,
        .visible_text = "> ",
        .shell_generation = 0, .has_new_output = false, .now_ms = 0,
    };
    WixenFrameA11yEvents ev0 = {0};
    wixen_frame_a11y_update(&fs, &in0, &ev0);
    free(ev0.announce_text);

    /* Short single-char output (keyboard echo) should NOT be announced */
    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 3,
        .visible_text = "> a",
        .shell_generation = 0,
        .has_new_output = true,
        .new_output_text = "a", /* Single char, no newline */
        .now_ms = 100,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);

    ASSERT_FALSE(ev1.should_announce_output);
    free(ev1.announce_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

TEST red_frame_line_replaced_announces(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Frame 1: prompt with command */
    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 18,
        .visible_text = "C:\\Users\\test>dir",
        .shell_generation = 0, .has_new_output = false, .now_ms = 0,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);
    free(ev1.announce_text);

    /* Frame 2: up arrow recalled different command */
    WixenFrameA11yInput in2 = {
        .cursor_row = 0, .cursor_col = 22,
        .visible_text = "C:\\Users\\test>cls",
        .shell_generation = 0, .has_new_output = false, .now_ms = 200,
    };
    WixenFrameA11yEvents ev2 = {0};
    wixen_frame_a11y_update(&fs, &in2, &ev2);

    /* Line was replaced (not appended/deleted) — should announce */
    ASSERT(ev2.should_announce_line);
    ASSERT(ev2.announce_line_text != NULL);
    /* Should be the command only, prompt stripped */
    ASSERT_STR_EQ("cls", ev2.announce_line_text);
    free(ev2.announce_text);
    free(ev2.announce_line_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

TEST red_frame_structure_changed(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 0,
        .visible_text = "text",
        .shell_generation = 1, .has_new_output = false, .now_ms = 0,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);
    free(ev1.announce_text);
    free(ev1.announce_line_text);

    /* Shell generation bumped — tree changed */
    WixenFrameA11yInput in2 = {
        .cursor_row = 0, .cursor_col = 0,
        .visible_text = "text",
        .shell_generation = 2, .has_new_output = false, .now_ms = 100,
    };
    WixenFrameA11yEvents ev2 = {0};
    wixen_frame_a11y_update(&fs, &in2, &ev2);

    ASSERT(ev2.structure_changed);
    free(ev2.announce_text);
    free(ev2.announce_line_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

TEST red_frame_password_detection(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    WixenFrameA11yInput in1 = {
        .cursor_row = 0, .cursor_col = 0,
        .visible_text = "Password: ",
        .shell_generation = 0, .has_new_output = false, .now_ms = 0,
        .echo_timeout_detected = true,
    };
    WixenFrameA11yEvents ev1 = {0};
    wixen_frame_a11y_update(&fs, &in1, &ev1);

    ASSERT(ev1.password_prompt);
    free(ev1.announce_text);
    free(ev1.announce_line_text);

    wixen_frame_a11y_free(&fs);
    PASS();
}

SUITE(red_a11y_frame_loop) {
    RUN_TEST(red_frame_no_change);
    RUN_TEST(red_frame_cursor_moved);
    RUN_TEST(red_frame_output_throttled);
    RUN_TEST(red_frame_keyboard_echo_suppressed);
    RUN_TEST(red_frame_line_replaced_announces);
    RUN_TEST(red_frame_structure_changed);
    RUN_TEST(red_frame_password_detection);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_a11y_frame_loop);
    GREATEST_MAIN_END();
}
