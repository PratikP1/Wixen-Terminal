/* test_red_sync_output.c — RED tests for synchronized output + misc modes */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[256];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 256);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_sync_output_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.synchronized_output);
    feed(&t, &p, "\x1b[?2026h");
    ASSERT(t.modes.synchronized_output);
    feed(&t, &p, "\x1b[?2026l");
    ASSERT_FALSE(t.modes.synchronized_output);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_reverse_video_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.reverse_video);
    feed(&t, &p, "\x1b[?5h"); /* DECSCNM */
    ASSERT(t.modes.reverse_video);
    feed(&t, &p, "\x1b[?5l");
    ASSERT_FALSE(t.modes.reverse_video);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_blink_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?12h"); /* ATT610: blink on */
    ASSERT(t.grid.cursor.blinking);
    feed(&t, &p, "\x1b[?12l"); /* Blink off */
    ASSERT_FALSE(t.grid.cursor.blinking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_origin_mode_cursor_clamp(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;7r");  /* Scroll region rows 3-7 */
    feed(&t, &p, "\x1b[?6h");   /* Origin mode on */
    /* Cursor should be at top of region (row 2, 0-indexed) */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    /* CUP in origin mode is relative to region */
    feed(&t, &p, "\x1b[1;1H");  /* Should go to top of region */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_multiple_mode_set_at_once(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    /* Set multiple modes in one CSI */
    feed(&t, &p, "\x1b[?25l");  /* Hide cursor */
    feed(&t, &p, "\x1b[?7l");   /* No autowrap */
    ASSERT_FALSE(t.modes.cursor_visible);
    ASSERT_FALSE(t.modes.auto_wrap);
    /* Reset both */
    feed(&t, &p, "\x1b[?25h");
    feed(&t, &p, "\x1b[?7h");
    ASSERT(t.modes.cursor_visible);
    ASSERT(t.modes.auto_wrap);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_insert_mode_off(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.insert_mode);
    feed(&t, &p, "\x1b[4h"); /* IRM on */
    ASSERT(t.modes.insert_mode);
    feed(&t, &p, "\x1b[4l"); /* IRM off */
    ASSERT_FALSE(t.modes.insert_mode);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_sync_output) {
    RUN_TEST(red_sync_output_mode);
    RUN_TEST(red_reverse_video_mode);
    RUN_TEST(red_cursor_blink_mode);
    RUN_TEST(red_origin_mode_cursor_clamp);
    RUN_TEST(red_multiple_mode_set_at_once);
    RUN_TEST(red_insert_mode_off);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_sync_output);
    GREATEST_MAIN_END();
}
