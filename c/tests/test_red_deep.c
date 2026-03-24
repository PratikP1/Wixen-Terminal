/* test_red_deep.c — Deep RED tests targeting complex interaction bugs */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

static const char *cell(WixenTerminal *t, size_t row, size_t col) {
    return t->grid.rows[row].cells[col].content;
}

/* DECSC saves cursor AND attributes. DECRC restores both. */
TEST red_decsc_saves_attrs(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;31m");  /* Bold + red */
    feed(&t, &p, "\x1b[5;10H");  /* Row 5, col 10 */
    feed(&t, &p, "\x1b" "7");    /* DECSC */
    feed(&t, &p, "\x1b[0m");     /* Reset attrs */
    feed(&t, &p, "\x1b[1;1H");   /* Move home */
    feed(&t, &p, "N");           /* Print with default attrs */
    ASSERT_FALSE(t.grid.rows[0].cells[0].attrs.bold);
    feed(&t, &p, "\x1b" "8");    /* DECRC */
    feed(&t, &p, "R");           /* Print with restored attrs */
    /* Cursor should be at 5,10 and attrs should be bold+red */
    ASSERT(t.grid.rows[4].cells[9].attrs.bold);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[4].cells[9].attrs.fg.type);
    ASSERT_EQ(1, (int)t.grid.rows[4].cells[9].attrs.fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Alt screen enter/exit preserves main buffer content */
TEST red_alt_screen_preserves_main(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "MainText");
    ASSERT_STR_EQ("M", cell(&t, 0, 0));
    feed(&t, &p, "\x1b[?1049h"); /* Enter alt */
    feed(&t, &p, "AltStuff");
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    feed(&t, &p, "\x1b[?1049l"); /* Leave alt */
    /* Main buffer should be restored */
    ASSERT_STR_EQ("M", cell(&t, 0, 0));
    ASSERT_STR_EQ("a", cell(&t, 0, 1));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Resize during alt screen */
TEST red_resize_in_alt_screen(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello");
    feed(&t, &p, "\x1b[?1049h"); /* Enter alt */
    wixen_terminal_resize(&t, 20, 10);
    ASSERT_EQ(20, (int)t.grid.cols);
    ASSERT_EQ(10, (int)t.grid.num_rows);
    feed(&t, &p, "\x1b[?1049l"); /* Leave alt */
    /* Should not crash. Main buffer resized too. */
    ASSERT_EQ(20, (int)t.grid.cols);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Scroll region + IL/DL interaction */
TEST red_il_within_scroll_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
    feed(&t, &p, "\x1b[2;4r"); /* Region rows 2-4 */
    feed(&t, &p, "\x1b[3;1H"); /* Row 3 (inside region) */
    feed(&t, &p, "\x1b[1L");   /* Insert 1 line */
    /* Row 0 (A) and row 4 (E) should be unchanged */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("E", cell(&t, 4, 0));
    /* Row 2 should be blank (inserted) */
    ASSERT(cell(&t, 2, 0)[0] == '\0' || cell(&t, 2, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* ECH doesn't shift — only erases in place */
TEST red_ech_no_shift(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;3H"); /* Col 3 */
    feed(&t, &p, "\x1b[3X");   /* ECH 3: erase 3 chars at cols 2-4 */
    /* Cols 0-1 intact, cols 2-4 erased, cols 5-9 intact */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT(cell(&t, 0, 2)[0] == '\0' || cell(&t, 0, 2)[0] == ' ');
    ASSERT(cell(&t, 0, 3)[0] == '\0' || cell(&t, 0, 3)[0] == ' ');
    ASSERT(cell(&t, 0, 4)[0] == '\0' || cell(&t, 0, 4)[0] == ' ');
    ASSERT_STR_EQ("F", cell(&t, 0, 5));
    /* Cursor should NOT move */
    ASSERT_EQ(2, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* DCH shifts left — different from ECH */
TEST red_dch_shifts_left(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;3H"); /* Col 3 */
    feed(&t, &p, "\x1b[3P");   /* DCH 3: delete cols 2-4, shift 5-9 left */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT_STR_EQ("F", cell(&t, 0, 2)); /* Shifted from col 5 */
    ASSERT_STR_EQ("G", cell(&t, 0, 3));
    ASSERT_STR_EQ("H", cell(&t, 0, 4));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Printing at right margin with autowrap off */
TEST red_nowrap_overwrite_last_col(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?7l"); /* DECAWM off */
    feed(&t, &p, "ABCDEFG"); /* 7 chars in 5-col grid, no wrap */
    /* Without wrap, chars past col 4 overwrite col 4 */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT_STR_EQ("C", cell(&t, 0, 2));
    ASSERT_STR_EQ("D", cell(&t, 0, 3));
    /* Col 4 should be last char written ('G') */
    ASSERT_STR_EQ("G", cell(&t, 0, 4));
    ASSERT_EQ(0, (int)t.grid.cursor.row); /* Still on row 0 */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Large SGR with 256+truecolor combination */
TEST red_complex_sgr_combo(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Bold + italic + 256-color FG (123) + truecolor BG (50,100,150) */
    feed(&t, &p, "\x1b[1;3;38;5;123;48;2;50;100;150mX");
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT(a->bold);
    ASSERT(a->italic);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, a->fg.type);
    ASSERT_EQ(123, (int)a->fg.index);
    ASSERT_EQ(WIXEN_COLOR_RGB, a->bg.type);
    ASSERT_EQ(50, (int)a->bg.rgb.r);
    ASSERT_EQ(100, (int)a->bg.rgb.g);
    ASSERT_EQ(150, (int)a->bg.rgb.b);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* LF at bottom of screen scrolls */
TEST red_lf_scrolls_at_bottom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    feed(&t, &p, "\n"); /* LF at last row → scroll */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* RI (reverse index) at top of screen scrolls down */
TEST red_ri_scrolls_down_at_top(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    feed(&t, &p, "\x1b[1;1H"); /* Move to top */
    feed(&t, &p, "\x1bM");     /* RI: reverse index at top → scroll down */
    /* Row 0 should be blank (new), A→row1, B→row2, C pushed off */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    ASSERT_STR_EQ("A", cell(&t, 1, 0));
    ASSERT_STR_EQ("B", cell(&t, 2, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_deep) {
    RUN_TEST(red_decsc_saves_attrs);
    RUN_TEST(red_alt_screen_preserves_main);
    RUN_TEST(red_resize_in_alt_screen);
    RUN_TEST(red_il_within_scroll_region);
    RUN_TEST(red_ech_no_shift);
    RUN_TEST(red_dch_shifts_left);
    RUN_TEST(red_nowrap_overwrite_last_col);
    RUN_TEST(red_complex_sgr_combo);
    RUN_TEST(red_lf_scrolls_at_bottom);
    RUN_TEST(red_ri_scrolls_down_at_top);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_deep);
    GREATEST_MAIN_END();
}
