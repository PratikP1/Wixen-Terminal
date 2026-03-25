/* test_red_paste_sync.c — RED tests for bracketed paste + sync output
 *
 * Bracketed paste: DECSET 2004 wraps pasted text with ESC[200~ / ESC[201~
 * Synchronized output: DECSET 2026 delays rendering until DECRST 2026
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* === Bracketed paste mode === */

TEST red_bracketed_paste_default_off(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_bracketed_paste_enable(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?2004h");
    ASSERT(t.modes.bracketed_paste);
    feeds(&t, &p, "\x1b[?2004l");
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_bracketed_paste_survives_reset(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?2004h");
    ASSERT(t.modes.bracketed_paste);
    /* DECSTR soft reset should clear it */
    feeds(&t, &p, "\x1b[!p");
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Focus reporting mode === */

TEST red_focus_report_default_off(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    ASSERT_FALSE(t.modes.focus_reporting);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_focus_report_enable(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?1004h");
    ASSERT(t.modes.focus_reporting);
    feeds(&t, &p, "\x1b[?1004l");
    ASSERT_FALSE(t.modes.focus_reporting);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Synchronized output === */

TEST red_sync_output_default_off(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    ASSERT_FALSE(t.modes.synchronized_output);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_sync_output_enable(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?2026h");
    ASSERT(t.modes.synchronized_output);
    /* Content written during sync should still be in grid */
    feeds(&t, &p, "Hello");
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "Hello") != NULL);
    free(r0);
    feeds(&t, &p, "\x1b[?2026l");
    ASSERT_FALSE(t.modes.synchronized_output);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Mouse tracking modes === */

TEST red_mouse_mode_1000(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.mouse_tracking);
    feeds(&t, &p, "\x1b[?1000h");
    ASSERT(t.modes.mouse_tracking);
    feeds(&t, &p, "\x1b[?1000l");
    ASSERT_FALSE(t.modes.mouse_tracking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_sgr_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?1006h");
    ASSERT(t.modes.mouse_sgr);
    feeds(&t, &p, "\x1b[?1006l");
    ASSERT_FALSE(t.modes.mouse_sgr);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_paste_sync) {
    RUN_TEST(red_bracketed_paste_default_off);
    RUN_TEST(red_bracketed_paste_enable);
    RUN_TEST(red_bracketed_paste_survives_reset);
    RUN_TEST(red_focus_report_default_off);
    RUN_TEST(red_focus_report_enable);
    RUN_TEST(red_sync_output_default_off);
    RUN_TEST(red_sync_output_enable);
    RUN_TEST(red_mouse_mode_1000);
    RUN_TEST(red_mouse_sgr_mode);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_paste_sync);
    GREATEST_MAIN_END();
}
