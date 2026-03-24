/* test_terminal_modes.c — Terminal mode switching tests */
#include <stdbool.h>
#include <string.h>
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

TEST mode_decawm_default_on(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    ASSERT(t.modes.auto_wrap);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_decawm_off_no_wrap(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?7l"); /* DECAWM off */
    ASSERT_FALSE(t.modes.auto_wrap);
    feed(&t, &p, "ABCDEF"); /* 6 chars in 5-col grid */
    ASSERT_EQ(0, (int)t.grid.cursor.row); /* Should stay on row 0 */
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_dectcem_hide_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT(t.modes.cursor_visible);
    feed(&t, &p, "\x1b[?25l"); /* Hide cursor */
    ASSERT_FALSE(t.modes.cursor_visible);
    feed(&t, &p, "\x1b[?25h"); /* Show cursor */
    ASSERT(t.modes.cursor_visible);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_alt_screen_switch(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.alternate_screen);
    feed(&t, &p, "Hello");
    feed(&t, &p, "\x1b[?1049h"); /* Enter alt screen */
    ASSERT(t.modes.alternate_screen);
    /* Alt screen should be clean */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    feed(&t, &p, "\x1b[?1049l"); /* Leave alt screen */
    ASSERT_FALSE(t.modes.alternate_screen);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_cursor_keys_app(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.cursor_keys_application);
    feed(&t, &p, "\x1b[?1h"); /* DECCKM: app cursor keys */
    ASSERT(t.modes.cursor_keys_application);
    feed(&t, &p, "\x1b[?1l"); /* DECCKM: normal */
    ASSERT_FALSE(t.modes.cursor_keys_application);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_origin_clamps_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 10);
    wixen_parser_init(&p);
    /* Set scroll region rows 3-7 */
    feed(&t, &p, "\x1b[4;8r");
    /* Enable origin mode */
    feed(&t, &p, "\x1b[?6h");
    /* Cursor should be at top of region */
    ASSERT_EQ(3, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_irm_insert(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    feed(&t, &p, "\x1b[1G"); /* Move to col 0 */
    feed(&t, &p, "\x1b[4h"); /* IRM on (insert mode) */
    ASSERT(t.modes.insert_mode);
    feed(&t, &p, "XY");
    /* "XY" inserted, "ABCDE" shifted right */
    ASSERT_STR_EQ("X", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("Y", t.grid.rows[0].cells[1].content);
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[2].content);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_bracketed_paste(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.bracketed_paste);
    feed(&t, &p, "\x1b[?2004h");
    ASSERT(t.modes.bracketed_paste);
    feed(&t, &p, "\x1b[?2004l");
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST mode_linefeed_newline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.line_feed_new_line);
    feed(&t, &p, "\x1b[20h"); /* LNM on */
    ASSERT(t.modes.line_feed_new_line);
    feed(&t, &p, "\x1b[20l"); /* LNM off */
    ASSERT_FALSE(t.modes.line_feed_new_line);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST title_set_via_osc(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]0;My Title\x07");
    ASSERT(t.title != NULL);
    ASSERT_STR_EQ("My Title", t.title);
    ASSERT(t.title_dirty);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST cursor_save_restore(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5;10H"); /* Move to row 5, col 10 (1-based) */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    feed(&t, &p, "\x1b" "7"); /* DECSC: save cursor */
    feed(&t, &p, "\x1b[1;1H"); /* Move to top-left */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    feed(&t, &p, "\x1b" "8"); /* DECRC: restore cursor */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_modes) {
    RUN_TEST(mode_decawm_default_on);
    RUN_TEST(mode_decawm_off_no_wrap);
    RUN_TEST(mode_dectcem_hide_cursor);
    RUN_TEST(mode_alt_screen_switch);
    RUN_TEST(mode_cursor_keys_app);
    RUN_TEST(mode_origin_clamps_cursor);
    RUN_TEST(mode_irm_insert);
    RUN_TEST(mode_bracketed_paste);
    RUN_TEST(mode_linefeed_newline);
    RUN_TEST(title_set_via_osc);
    RUN_TEST(cursor_save_restore);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_modes);
    GREATEST_MAIN_END();
}
