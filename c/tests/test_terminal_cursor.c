/* test_terminal_cursor.c — Cursor movement, positioning, tab stops */
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

TEST cursor_cup_1_1(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5;10H");
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_cup_clamps_to_grid(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[100;100H");
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_up_stops_at_top(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[2;1H"); /* Row 2 */
    feed(&t, &p, "\x1b[100A"); /* Up 100 */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_down_stops_at_bottom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[100B"); /* Down 100 */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_forward_stops_at_right(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[100C"); /* Right 100 */
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_backward_stops_at_left(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5;5H");
    feed(&t, &p, "\x1b[100D"); /* Left 100 */
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_cpl_prev_line(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;5H"); /* Row 3, col 5 */
    feed(&t, &p, "\x1b[1F");   /* CPL: up 1, col 0 */
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_cnl_next_line(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[2;5H"); /* Row 2, col 5 */
    feed(&t, &p, "\x1b[1E");   /* CNL: down 1, col 0 */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_cha_absolute_col(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[15G"); /* CHA: col 15 */
    ASSERT_EQ(14, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_vpa_absolute_row(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5d"); /* VPA: row 5 */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_cr_returns_col_zero(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello\r");
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_lf_moves_down(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hi\n");
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_tab_default_stops(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\t"); /* Tab to col 8 */
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    feed(&t, &p, "\t"); /* Tab to col 16 */
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_bs_moves_left(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AB\x08");
    ASSERT_EQ(1, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST cursor_bs_stops_at_zero(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x08\x08\x08");
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_cursor) {
    RUN_TEST(cursor_cup_1_1);
    RUN_TEST(cursor_cup_clamps_to_grid);
    RUN_TEST(cursor_up_stops_at_top);
    RUN_TEST(cursor_down_stops_at_bottom);
    RUN_TEST(cursor_forward_stops_at_right);
    RUN_TEST(cursor_backward_stops_at_left);
    RUN_TEST(cursor_cpl_prev_line);
    RUN_TEST(cursor_cnl_next_line);
    RUN_TEST(cursor_cha_absolute_col);
    RUN_TEST(cursor_vpa_absolute_row);
    RUN_TEST(cursor_cr_returns_col_zero);
    RUN_TEST(cursor_lf_moves_down);
    RUN_TEST(cursor_tab_default_stops);
    RUN_TEST(cursor_bs_moves_left);
    RUN_TEST(cursor_bs_stops_at_zero);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_cursor);
    GREATEST_MAIN_END();
}
