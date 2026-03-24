/* test_terminal.c — Tests for WixenTerminal state machine */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

/* Helper: feed raw bytes through parser into terminal */
static WixenParser parser;

static void feed(WixenTerminal *t, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(&parser, (const uint8_t *)s, strlen(s),
                                         actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        /* Free OSC/APC data if transferred */
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH)
            free(actions[i].osc.data);
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH)
            free(actions[i].apc.data);
    }
}

static const char *row_text(const WixenTerminal *t, size_t row) {
    static char buf[4096];
    if (row >= t->grid.num_rows) return "";
    wixen_row_text(&t->grid.rows[row], buf, sizeof(buf));
    return buf;
}

static void setup(void) {
    wixen_parser_init(&parser);
}

static void teardown(void) {
    wixen_parser_free(&parser);
}

/* === Cursor Movement === */

TEST cursor_up(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.row = 5;
    feed(&t, "\x1b[3A"); /* CUU 3 */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_up_clamps_at_top(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.row = 2;
    feed(&t, "\x1b[100A");
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_down(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[5B"); /* CUD 5 */
    ASSERT_EQ(5, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_down_clamps_at_bottom(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[100B");
    ASSERT_EQ(23, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_forward(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[10C");
    ASSERT_EQ(10, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_backward(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.col = 10;
    feed(&t, "\x1b[3D");
    ASSERT_EQ(7, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_position(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[5;10H"); /* CUP row=5, col=10 (1-based) */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_home(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.row = 10; t.grid.cursor.col = 20;
    feed(&t, "\x1b[H");
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_next_line(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.col = 20; t.grid.cursor.row = 3;
    feed(&t, "\x1b[2E"); /* CNL 2 */
    ASSERT_EQ(5, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_prev_line(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.col = 20; t.grid.cursor.row = 5;
    feed(&t, "\x1b[2F"); /* CPL 2 */
    ASSERT_EQ(3, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_horizontal_absolute(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[15G"); /* CHA col=15 (1-based) */
    ASSERT_EQ(14, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Printing & Wrapping === */

TEST print_basic_text(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "Hello World");
    ASSERT_STR_EQ("Hello World", row_text(&t, 0));
    ASSERT_EQ(11, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST print_wraps_at_margin(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 3);
    setup();
    feed(&t, "ABCDE");
    /* Cursor at col 4 with pending_wrap */
    ASSERT_STR_EQ("ABCDE", row_text(&t, 0));
    ASSERT(t.pending_wrap);
    feed(&t, "F");
    ASSERT_STR_EQ("ABCDE", row_text(&t, 0));
    ASSERT_STR_EQ("F", row_text(&t, 1));
    ASSERT(t.grid.rows[0].wrapped);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST print_no_wrap_when_disabled(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 3);
    setup();
    t.modes.auto_wrap = false;
    feed(&t, "ABCDEF");
    /* All on row 0, cursor stuck at last col */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(4, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Linefeed & Carriage Return === */

TEST linefeed_moves_down(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "A\n");
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST linefeed_scrolls_at_bottom(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 3);
    setup();
    feed(&t, "A\r\nB\r\nC\r\nD");
    /* Row 0 was scrolled out, rows now: B, C, D */
    ASSERT_STR_EQ("B", row_text(&t, 0));
    ASSERT_STR_EQ("C", row_text(&t, 1));
    ASSERT_STR_EQ("D", row_text(&t, 2));
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cr_returns_to_col_0(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "Hello\rWorld");
    ASSERT_STR_EQ("World", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Erase Operations === */

TEST erase_in_line_to_end(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1);
    setup();
    feed(&t, "ABCDEFGHIJ");
    feed(&t, "\x1b[5G"); /* Move to col 5 (1-based) */
    feed(&t, "\x1b[K");  /* EL 0 — erase to end */
    ASSERT_STR_EQ("ABCD", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST erase_entire_display(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 3);
    setup();
    feed(&t, "Line1\nLine2\nLine3");
    feed(&t, "\x1b[2J"); /* ED 2 */
    ASSERT_STR_EQ("", row_text(&t, 0));
    ASSERT_STR_EQ("", row_text(&t, 1));
    ASSERT_STR_EQ("", row_text(&t, 2));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === SGR Attributes === */

TEST sgr_bold(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[1mX");
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT(c->attrs.bold);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_reset(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[1;3m"); /* bold + italic */
    ASSERT(t.grid.current_attrs.bold);
    ASSERT(t.grid.current_attrs.italic);
    feed(&t, "\x1b[0m");   /* reset */
    ASSERT_FALSE(t.grid.current_attrs.bold);
    ASSERT_FALSE(t.grid.current_attrs.italic);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_256_color(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[38;5;196mX");
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.fg.type);
    ASSERT_EQ(196, c->attrs.fg.index);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_rgb_color(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[38;2;255;128;0mX");
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_RGB, c->attrs.fg.type);
    ASSERT_EQ(255, c->attrs.fg.rgb.r);
    ASSERT_EQ(128, c->attrs.fg.rgb.g);
    ASSERT_EQ(0, c->attrs.fg.rgb.b);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_bright_fg(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[91mX"); /* Bright red */
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.fg.type);
    ASSERT_EQ(9, c->attrs.fg.index); /* 91-90+8 = 9 */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Modes === */

TEST mode_alt_screen(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 3);
    setup();
    feed(&t, "Hello");
    feed(&t, "\x1b[?1049h"); /* Enter alt screen */
    ASSERT(t.modes.alternate_screen);
    ASSERT_STR_EQ("", row_text(&t, 0)); /* Alt screen is blank */
    feed(&t, "Alt");
    ASSERT_STR_EQ("Alt", row_text(&t, 0));
    feed(&t, "\x1b[?1049l"); /* Exit alt screen */
    ASSERT_FALSE(t.modes.alternate_screen);
    ASSERT_STR_EQ("Hello", row_text(&t, 0)); /* Original content restored */
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST mode_bracketed_paste(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_FALSE(t.modes.bracketed_paste);
    feed(&t, "\x1b[?2004h");
    ASSERT(t.modes.bracketed_paste);
    feed(&t, "\x1b[?2004l");
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Scroll Region === */

TEST scroll_region_basic(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 5);
    setup();
    feed(&t, "A\r\nB\r\nC\r\nD\r\nE");
    feed(&t, "\x1b[2;4r");  /* Scroll region rows 2-4 (1-based) = [1,4) 0-based */
    feed(&t, "\x1b[4;1H");  /* Move to row 4 (1-based) = row 3 (0-based), last in region */
    /* Cursor at bottom of scroll region, LF should scroll within region */
    feed(&t, "\n");
    ASSERT_STR_EQ("A", row_text(&t, 0)); /* Outside region - unchanged */
    ASSERT_STR_EQ("C", row_text(&t, 1)); /* Was row 2, scrolled up */
    ASSERT_STR_EQ("D", row_text(&t, 2)); /* Was row 3, scrolled up */
    ASSERT_STR_EQ("", row_text(&t, 3));  /* New blank at bottom of region */
    ASSERT_STR_EQ("E", row_text(&t, 4)); /* Outside region - unchanged */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === OSC === */

TEST osc_set_title(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b]0;My Terminal\x07");
    ASSERT(t.title != NULL);
    ASSERT_STR_EQ("My Terminal", t.title);
    ASSERT(t.title_dirty);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === ESC Sequences === */

TEST esc_decaln(void) {
    WixenTerminal t; wixen_terminal_init(&t, 3, 2);
    setup();
    feed(&t, "\x1b#8"); /* DECALN — fill with E */
    ASSERT_STR_EQ("EEE", row_text(&t, 0));
    ASSERT_STR_EQ("EEE", row_text(&t, 1));
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST esc_save_restore_cursor(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[5;10H"); /* Move to row 5, col 10 */
    feed(&t, "\x1b" "7");   /* DECSC — save cursor */
    feed(&t, "\x1b[1;1H");  /* Move home */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    feed(&t, "\x1b" "8");   /* DECRC — restore cursor */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === DSR (Device Status Report) === */

TEST dsr_cursor_position_report(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.row = 4; t.grid.cursor.col = 9;
    feed(&t, "\x1b[6n"); /* DSR — request CPR */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT_STR_EQ("\x1b[5;10R", resp);
    free((void *)resp);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Bell === */

TEST bell_sets_flag(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_FALSE(t.bell_pending);
    feed(&t, "\x07"); /* BEL */
    ASSERT(t.bell_pending);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Tab stops === */

TEST tab_default_every_8(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\t"); /* Tab */
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    feed(&t, "\t");
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Backspace === */

TEST backspace_moves_left(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "ABC");
    ASSERT_EQ(3, (int)t.grid.cursor.col);
    feed(&t, "\x08"); /* BS */
    ASSERT_EQ(2, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST backspace_clamps_at_zero(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    feed(&t, "\x08");
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Insert/Delete Lines === */

TEST insert_lines(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 4);
    setup();
    feed(&t, "A\r\nB\r\nC\r\nD");
    feed(&t, "\x1b[2;1H"); /* Move to row 2 */
    feed(&t, "\x1b[1L");   /* IL 1 */
    ASSERT_STR_EQ("A", row_text(&t, 0));
    ASSERT_STR_EQ("", row_text(&t, 1)); /* Inserted blank */
    ASSERT_STR_EQ("B", row_text(&t, 2));
    ASSERT_STR_EQ("C", row_text(&t, 3));
    /* D was pushed out */
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST delete_lines(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 4);
    setup();
    feed(&t, "A\r\nB\r\nC\r\nD");
    feed(&t, "\x1b[2;1H"); /* Move to row 2 */
    feed(&t, "\x1b[1M");   /* DL 1 */
    ASSERT_STR_EQ("A", row_text(&t, 0));
    ASSERT_STR_EQ("C", row_text(&t, 1));
    ASSERT_STR_EQ("D", row_text(&t, 2));
    ASSERT_STR_EQ("", row_text(&t, 3)); /* New blank */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Reset === */

TEST terminal_reset_clears_all(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "Hello");
    t.modes.bracketed_paste = true;
    feed(&t, "\x1b" "c"); /* RIS — full reset */
    ASSERT_STR_EQ("", row_text(&t, 0));
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Insert Mode === */

TEST insert_mode(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1);
    setup();
    feed(&t, "ABCDE");
    feed(&t, "\x1b[4h");  /* Enable insert mode */
    feed(&t, "\x1b[3G");  /* Move to col 3 (1-based) */
    feed(&t, "XY");
    /* Should insert XY, pushing CDE right */
    ASSERT_STR_EQ("ABXYCDE", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Erase Characters === */

TEST erase_chars(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1);
    setup();
    feed(&t, "ABCDEFGHIJ");
    feed(&t, "\x1b[3G");  /* Col 3 (1-based) → col 2 (0-based) */
    feed(&t, "\x1b[3X");  /* Erase 3 chars at positions 2,3,4 */
    /* Cells 2,3,4 become spaces; row_text trims trailing but FGHIJ follows */
    ASSERT_STR_EQ("AB   FGHIJ", row_text(&t, 0));
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 2, 0);
    ASSERT_STR_EQ(" ", c->content);
    c = wixen_grid_cell_const(&t.grid, 5, 0);
    ASSERT_STR_EQ("F", c->content);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Vertical Position Absolute === */

TEST vertical_position_absolute(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[10d"); /* VPA row 10 (1-based) */
    ASSERT_EQ(9, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col); /* VPA doesn't change col */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Cursor Style === */

TEST cursor_style_bar(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[5 q"); /* Bar, blinking */
    ASSERT_EQ(WIXEN_CURSOR_BAR, t.grid.cursor.shape);
    ASSERT(t.grid.cursor.blinking);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST cursor_style_underline_steady(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[4 q"); /* Underline, steady */
    ASSERT_EQ(WIXEN_CURSOR_UNDERLINE, t.grid.cursor.shape);
    ASSERT_FALSE(t.grid.cursor.blinking);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Device Attributes === */

TEST da1_response(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[c"); /* DA1 */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* Should start with ESC[? */
    ASSERT_EQ('\x1b', resp[0]);
    ASSERT_EQ('[', resp[1]);
    ASSERT_EQ('?', resp[2]);
    free((void *)resp);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST dsr_status_report(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[5n"); /* DSR status */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT_STR_EQ("\x1b[0n", resp);
    free((void *)resp);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Scroll Up/Down === */

TEST scroll_up_explicit(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 3);
    setup();
    feed(&t, "A\r\nB\r\nC");
    feed(&t, "\x1b[1S"); /* Scroll up 1 */
    ASSERT_STR_EQ("B", row_text(&t, 0));
    ASSERT_STR_EQ("C", row_text(&t, 1));
    ASSERT_STR_EQ("", row_text(&t, 2));
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST scroll_down_explicit(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 3);
    setup();
    feed(&t, "A\r\nB\r\nC");
    feed(&t, "\x1b[1T"); /* Scroll down 1 */
    ASSERT_STR_EQ("", row_text(&t, 0));
    ASSERT_STR_EQ("A", row_text(&t, 1));
    ASSERT_STR_EQ("B", row_text(&t, 2));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === SGR Extended === */

TEST sgr_underline_styles(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[4m"); /* Single underline */
    ASSERT_EQ(WIXEN_UNDERLINE_SINGLE, t.grid.current_attrs.underline);
    feed(&t, "\x1b[21m"); /* Double underline */
    ASSERT_EQ(WIXEN_UNDERLINE_DOUBLE, t.grid.current_attrs.underline);
    feed(&t, "\x1b[24m"); /* No underline */
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, t.grid.current_attrs.underline);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_overline(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[53m"); /* Overline on */
    ASSERT(t.grid.current_attrs.overline);
    feed(&t, "\x1b[55m"); /* Overline off */
    ASSERT_FALSE(t.grid.current_attrs.overline);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_strikethrough(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[9m"); /* Strikethrough on */
    ASSERT(t.grid.current_attrs.strikethrough);
    feed(&t, "\x1b[29m"); /* Strikethrough off */
    ASSERT_FALSE(t.grid.current_attrs.strikethrough);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_dim(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[2m"); /* Dim */
    ASSERT(t.grid.current_attrs.dim);
    feed(&t, "\x1b[22m"); /* Normal intensity (cancels bold AND dim) */
    ASSERT_FALSE(t.grid.current_attrs.dim);
    ASSERT_FALSE(t.grid.current_attrs.bold);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_bg_color(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[48;5;220mX");
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.bg.type);
    ASSERT_EQ(220, c->attrs.bg.index);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_bg_rgb(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[48;2;10;20;30mX");
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_RGB, c->attrs.bg.type);
    ASSERT_EQ(10, c->attrs.bg.rgb.r);
    ASSERT_EQ(20, c->attrs.bg.rgb.g);
    ASSERT_EQ(30, c->attrs.bg.rgb.b);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST sgr_default_fg_bg(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[31m"); /* Red fg */
    feed(&t, "\x1b[39m"); /* Default fg */
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.current_attrs.fg.type);
    feed(&t, "\x1b[41m"); /* Red bg */
    feed(&t, "\x1b[49m"); /* Default bg */
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.current_attrs.bg.type);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === NEL (Next Line) === */

TEST nel_next_line(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.col = 10;
    feed(&t, "\x1b" "E"); /* NEL */
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Reverse Index at Top === */

TEST reverse_index_at_top_scrolls(void) {
    WixenTerminal t; wixen_terminal_init(&t, 5, 3);
    setup();
    feed(&t, "A\r\nB\r\nC");
    feed(&t, "\x1b[H"); /* Home */
    feed(&t, "\x1b" "M"); /* Reverse index at top — should scroll down */
    ASSERT_STR_EQ("", row_text(&t, 0)); /* New blank */
    ASSERT_STR_EQ("A", row_text(&t, 1));
    ASSERT_STR_EQ("B", row_text(&t, 2));
    /* C was scrolled out */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Edge Cases === */

TEST print_at_last_col_pending_wrap(void) {
    WixenTerminal t; wixen_terminal_init(&t, 3, 2);
    setup();
    feed(&t, "ABC"); /* Fills row, cursor at col 2 with pending_wrap */
    ASSERT(t.pending_wrap);
    /* Cursor movement should clear pending wrap */
    feed(&t, "\x1b[D"); /* Cursor back */
    ASSERT_FALSE(t.pending_wrap);
    ASSERT_EQ(1, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST empty_sgr_resets(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[1;3;4m"); /* Bold+italic+underline */
    ASSERT(t.grid.current_attrs.bold);
    feed(&t, "\x1b[m"); /* Empty SGR = reset */
    ASSERT_FALSE(t.grid.current_attrs.bold);
    ASSERT_FALSE(t.grid.current_attrs.italic);
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, t.grid.current_attrs.underline);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST csi_default_params(void) {
    /* CSI A with no param should default to 1 */
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    t.grid.cursor.row = 5;
    feed(&t, "\x1b[A"); /* No param → default 1 */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST multiple_linefeeds(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 5);
    setup();
    /* 10 linefeeds in a 5-row terminal — should scroll */
    for (int i = 0; i < 10; i++) feed(&t, "\n");
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Suites === */

SUITE(cursor_movement) {
    RUN_TEST(cursor_up);
    RUN_TEST(cursor_up_clamps_at_top);
    RUN_TEST(cursor_down);
    RUN_TEST(cursor_down_clamps_at_bottom);
    RUN_TEST(cursor_forward);
    RUN_TEST(cursor_backward);
    RUN_TEST(cursor_position);
    RUN_TEST(cursor_home);
    RUN_TEST(cursor_next_line);
    RUN_TEST(cursor_prev_line);
    RUN_TEST(cursor_horizontal_absolute);
}

SUITE(printing) {
    RUN_TEST(print_basic_text);
    RUN_TEST(print_wraps_at_margin);
    RUN_TEST(print_no_wrap_when_disabled);
}

SUITE(linefeed_cr) {
    RUN_TEST(linefeed_moves_down);
    RUN_TEST(linefeed_scrolls_at_bottom);
    RUN_TEST(cr_returns_to_col_0);
}

SUITE(erase_ops) {
    RUN_TEST(erase_in_line_to_end);
    RUN_TEST(erase_entire_display);
}

SUITE(sgr_attrs) {
    RUN_TEST(sgr_bold);
    RUN_TEST(sgr_reset);
    RUN_TEST(sgr_256_color);
    RUN_TEST(sgr_rgb_color);
    RUN_TEST(sgr_bright_fg);
}

SUITE(mode_tests) {
    RUN_TEST(mode_alt_screen);
    RUN_TEST(mode_bracketed_paste);
}

SUITE(scroll_region_tests) {
    RUN_TEST(scroll_region_basic);
}

SUITE(osc_tests) {
    RUN_TEST(osc_set_title);
}

SUITE(esc_tests) {
    RUN_TEST(esc_decaln);
    RUN_TEST(esc_save_restore_cursor);
}

SUITE(dsr_tests) {
    RUN_TEST(dsr_cursor_position_report);
}

SUITE(insert_mode_tests) {
    RUN_TEST(insert_mode);
}

SUITE(erase_extended) {
    RUN_TEST(erase_chars);
}

SUITE(vpa_tests) {
    RUN_TEST(vertical_position_absolute);
}

SUITE(cursor_style_tests) {
    RUN_TEST(cursor_style_bar);
    RUN_TEST(cursor_style_underline_steady);
}

SUITE(da_dsr_tests) {
    RUN_TEST(da1_response);
    RUN_TEST(dsr_status_report);
}

SUITE(scroll_explicit) {
    RUN_TEST(scroll_up_explicit);
    RUN_TEST(scroll_down_explicit);
}

SUITE(sgr_extended) {
    RUN_TEST(sgr_underline_styles);
    RUN_TEST(sgr_overline);
    RUN_TEST(sgr_strikethrough);
    RUN_TEST(sgr_dim);
    RUN_TEST(sgr_bg_color);
    RUN_TEST(sgr_bg_rgb);
    RUN_TEST(sgr_default_fg_bg);
}

SUITE(esc_extended) {
    RUN_TEST(nel_next_line);
    RUN_TEST(reverse_index_at_top_scrolls);
}

/* === Mouse Mode === */

TEST mouse_mode_normal(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_EQ(WIXEN_MOUSE_NONE, t.modes.mouse_tracking);
    feed(&t, "\x1b[?1000h");
    ASSERT_EQ(WIXEN_MOUSE_NORMAL, t.modes.mouse_tracking);
    feed(&t, "\x1b[?1000l");
    ASSERT_EQ(WIXEN_MOUSE_NONE, t.modes.mouse_tracking);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST mouse_mode_button(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[?1002h");
    ASSERT_EQ(WIXEN_MOUSE_BUTTON, t.modes.mouse_tracking);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST mouse_mode_any(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[?1003h");
    ASSERT_EQ(WIXEN_MOUSE_ANY, t.modes.mouse_tracking);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST mouse_sgr_mode(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_FALSE(t.modes.mouse_sgr);
    feed(&t, "\x1b[?1006h");
    ASSERT(t.modes.mouse_sgr);
    feed(&t, "\x1b[?1006l");
    ASSERT_FALSE(t.modes.mouse_sgr);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Focus Reporting === */

TEST focus_reporting_mode(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_FALSE(t.modes.focus_reporting);
    feed(&t, "\x1b[?1004h");
    ASSERT(t.modes.focus_reporting);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Synchronized Output === */

TEST synchronized_output(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT_FALSE(t.modes.synchronized_output);
    feed(&t, "\x1b[?2026h");
    ASSERT(t.modes.synchronized_output);
    feed(&t, "\x1b[?2026l");
    ASSERT_FALSE(t.modes.synchronized_output);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Title Changes === */

TEST title_multiple_changes(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b]0;First\x07");
    ASSERT_STR_EQ("First", t.title);
    feed(&t, "\x1b]2;Second\x07");
    ASSERT_STR_EQ("Second", t.title);
    ASSERT(t.title_dirty);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Tab Clear === */

TEST tab_clear_at_cursor(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    /* Tab at col 0, move to col 8 (default tab stop), clear it */
    feed(&t, "\x1b[9G");  /* Move to col 9 (1-based) = col 8 */
    feed(&t, "\x1b[0g");  /* Clear tab stop at cursor */
    feed(&t, "\x1b[1G");  /* Back to col 1 */
    feed(&t, "\t");        /* Tab should skip past col 8 to col 16 */
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

TEST tab_clear_all(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[3g"); /* Clear all tab stops */
    feed(&t, "\t");       /* No tab stops — go to last column */
    ASSERT_EQ(79, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Delete Characters === */

TEST delete_chars_mid_line(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1);
    setup();
    feed(&t, "ABCDEFGHIJ");
    feed(&t, "\x1b[4G");  /* Col 4 (1-based) = col 3 */
    feed(&t, "\x1b[2P");  /* Delete 2 chars at col 3 */
    ASSERT_STR_EQ("ABCFGHIJ", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Insert Characters === */

TEST insert_chars(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1);
    setup();
    feed(&t, "ABCDEFGHIJ");
    feed(&t, "\x1b[4G");  /* Col 4 */
    feed(&t, "\x1b[2@");  /* Insert 2 blanks */
    /* AB C  DEFGH (IJ pushed off) */
    const WixenCell *c3 = wixen_grid_cell_const(&t.grid, 3, 0);
    ASSERT_STR_EQ(" ", c3->content);
    const WixenCell *c5 = wixen_grid_cell_const(&t.grid, 5, 0);
    ASSERT_STR_EQ("D", c5->content);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Resize === */

TEST terminal_resize(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "Hello");
    wixen_terminal_resize(&t, 40, 12);
    ASSERT_EQ(40, (int)t.grid.cols);
    ASSERT_EQ(12, (int)t.grid.num_rows);
    ASSERT_STR_EQ("Hello", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Cursor Visibility === */

TEST cursor_show_hide(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    ASSERT(t.modes.cursor_visible);
    feed(&t, "\x1b[?25l"); /* Hide */
    ASSERT_FALSE(t.modes.cursor_visible);
    feed(&t, "\x1b[?25h"); /* Show */
    ASSERT(t.modes.cursor_visible);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Origin Mode === */

TEST origin_mode(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24);
    setup();
    feed(&t, "\x1b[5;15r");  /* Scroll region rows 5-15 */
    feed(&t, "\x1b[?6h");    /* Enable origin mode */
    feed(&t, "\x1b[1;1H");   /* Home in origin mode = top of scroll region */
    ASSERT_EQ(4, (int)t.grid.cursor.row); /* Row 5 (0-based=4) */
    feed(&t, "\x1b[?6l");    /* Disable origin mode */
    wixen_terminal_free(&t); teardown();
    PASS();
}

SUITE(mouse_mode_tests) {
    RUN_TEST(mouse_mode_normal);
    RUN_TEST(mouse_mode_button);
    RUN_TEST(mouse_mode_any);
    RUN_TEST(mouse_sgr_mode);
}

SUITE(misc_mode_tests) {
    RUN_TEST(focus_reporting_mode);
    RUN_TEST(synchronized_output);
    RUN_TEST(cursor_show_hide);
    RUN_TEST(origin_mode);
}

SUITE(title_tests) {
    RUN_TEST(title_multiple_changes);
}

SUITE(tab_clear_tests) {
    RUN_TEST(tab_clear_at_cursor);
    RUN_TEST(tab_clear_all);
}

SUITE(char_ops_tests) {
    RUN_TEST(delete_chars_mid_line);
    RUN_TEST(insert_chars);
}

SUITE(resize_tests) {
    RUN_TEST(terminal_resize);
}

SUITE(edge_cases) {
    RUN_TEST(print_at_last_col_pending_wrap);
    RUN_TEST(empty_sgr_resets);
    RUN_TEST(csi_default_params);
    RUN_TEST(multiple_linefeeds);
}

SUITE(misc_tests) {
    RUN_TEST(bell_sets_flag);
    RUN_TEST(tab_default_every_8);
    RUN_TEST(backspace_moves_left);
    RUN_TEST(backspace_clamps_at_zero);
    RUN_TEST(insert_lines);
    RUN_TEST(delete_lines);
    RUN_TEST(terminal_reset_clears_all);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(cursor_movement);
    RUN_SUITE(printing);
    RUN_SUITE(linefeed_cr);
    RUN_SUITE(erase_ops);
    RUN_SUITE(sgr_attrs);
    RUN_SUITE(mode_tests);
    RUN_SUITE(scroll_region_tests);
    RUN_SUITE(osc_tests);
    RUN_SUITE(esc_tests);
    RUN_SUITE(dsr_tests);
    RUN_SUITE(misc_tests);
    RUN_SUITE(insert_mode_tests);
    RUN_SUITE(erase_extended);
    RUN_SUITE(vpa_tests);
    RUN_SUITE(cursor_style_tests);
    RUN_SUITE(da_dsr_tests);
    RUN_SUITE(scroll_explicit);
    RUN_SUITE(sgr_extended);
    RUN_SUITE(esc_extended);
    RUN_SUITE(edge_cases);
    RUN_SUITE(mouse_mode_tests);
    RUN_SUITE(misc_mode_tests);
    RUN_SUITE(title_tests);
    RUN_SUITE(tab_clear_tests);
    RUN_SUITE(char_ops_tests);
    RUN_SUITE(resize_tests);
    GREATEST_MAIN_END();
}
