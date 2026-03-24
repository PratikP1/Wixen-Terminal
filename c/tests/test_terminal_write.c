/* test_terminal_write.c — Terminal character writing, wrap, overwrite */
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

static const char *cell(WixenTerminal *t, size_t row, size_t col) {
    return t->grid.rows[row].cells[col].content;
}

TEST write_fills_row(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    for (int i = 0; i < 5; i++) {
        char expected[2] = { (char)('A' + i), 0 };
        ASSERT_STR_EQ(expected, cell(&t, 0, i));
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_wraps_to_next_row(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEF"); /* 6 chars in 5-col grid */
    ASSERT_STR_EQ("F", cell(&t, 1, 0));
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    ASSERT_EQ(1, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_sets_wrapped_flag(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEF");
    ASSERT(t.grid.rows[0].wrapped);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_overwrite_in_place(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    feed(&t, &p, "\x1b[1;1H"); /* Home */
    feed(&t, &p, "XY");
    ASSERT_STR_EQ("X", cell(&t, 0, 0));
    ASSERT_STR_EQ("Y", cell(&t, 0, 1));
    ASSERT_STR_EQ("C", cell(&t, 0, 2)); /* Unchanged */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_scroll_at_bottom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD"); /* 4 lines in 3-row grid */
    /* A should have scrolled off, B→row0, C→row1, D→row2 */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    ASSERT_STR_EQ("D", cell(&t, 2, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_newline_sequence(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Line1\r\nLine2\r\nLine3");
    ASSERT_STR_EQ("L", cell(&t, 0, 0));
    ASSERT_STR_EQ("L", cell(&t, 1, 0));
    ASSERT_STR_EQ("L", cell(&t, 2, 0));
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    ASSERT_EQ(5, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_pending_wrap_resolved_by_next_char(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 3, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABC"); /* Fills row, pending_wrap = true */
    ASSERT(t.pending_wrap);
    feed(&t, &p, "D");   /* Triggers wrap, D at row 1 col 0 */
    ASSERT_FALSE(t.pending_wrap);
    ASSERT_STR_EQ("D", cell(&t, 1, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_insert_mode_shifts_right(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    feed(&t, &p, "\x1b[1;3H"); /* Col 3 */
    feed(&t, &p, "\x1b[4h");   /* IRM: insert mode */
    feed(&t, &p, "X");
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT_STR_EQ("X", cell(&t, 0, 2));
    ASSERT_STR_EQ("C", cell(&t, 0, 3));
    ASSERT_STR_EQ("D", cell(&t, 0, 4));
    ASSERT_STR_EQ("E", cell(&t, 0, 5));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_bell_char(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.bell_pending);
    feed(&t, &p, "\x07"); /* BEL */
    ASSERT(t.bell_pending);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST write_null_ignored(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "A");
    size_t col_before = t.grid.cursor.col;
    /* NUL should be ignored */
    uint8_t nul = 0;
    WixenAction actions[8];
    size_t count = wixen_parser_process(&p, &nul, 1, actions, 8);
    for (size_t i = 0; i < count; i++)
        wixen_terminal_dispatch(&t, &actions[i]);
    ASSERT_EQ(col_before, t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_write) {
    RUN_TEST(write_fills_row);
    RUN_TEST(write_wraps_to_next_row);
    RUN_TEST(write_sets_wrapped_flag);
    RUN_TEST(write_overwrite_in_place);
    RUN_TEST(write_scroll_at_bottom);
    RUN_TEST(write_newline_sequence);
    RUN_TEST(write_pending_wrap_resolved_by_next_char);
    RUN_TEST(write_insert_mode_shifts_right);
    RUN_TEST(write_bell_char);
    RUN_TEST(write_null_ignored);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_write);
    GREATEST_MAIN_END();
}
