/* test_terminal_scroll.c — Scrolling, viewport, and scrollback interaction */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
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

TEST scroll_newline_at_bottom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    /* A scrolled off, B→0, C→1, D→2 */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    ASSERT_STR_EQ("D", cell(&t, 2, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_su_command(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    feed(&t, &p, "\x1b[1S"); /* Scroll up 1 */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    /* Row 2 should be blank */
    ASSERT(cell(&t, 2, 0)[0] == '\0' || cell(&t, 2, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_sd_command(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    feed(&t, &p, "\x1b[1T"); /* Scroll down 1 */
    /* Row 0 should be blank */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    ASSERT_STR_EQ("A", cell(&t, 1, 0));
    ASSERT_STR_EQ("B", cell(&t, 2, 0));
    /* C pushed off */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_multiple_su(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    feed(&t, &p, "\x1b[2S"); /* Scroll up 2 */
    ASSERT_STR_EQ("C", cell(&t, 0, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_within_region_only(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    feed(&t, &p, "\x1b[2;4r"); /* Region rows 2-4 */
    feed(&t, &p, "\x1b[4;1H\n"); /* LF at bottom of region */
    /* Row 0 (A) unchanged */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    /* Within region: B→gone, C→row1, D→row2, blank→row3 */
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    ASSERT_STR_EQ("D", cell(&t, 2, 0));
    ASSERT(cell(&t, 3, 0)[0] == '\0' || cell(&t, 3, 0)[0] == ' ');
    /* Row 4 (E) unchanged */
    ASSERT_STR_EQ("E", cell(&t, 4, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_ri_at_top_of_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    feed(&t, &p, "\x1b[2;4r"); /* Region rows 2-4 */
    feed(&t, &p, "\x1b[2;1H"); /* Top of region */
    feed(&t, &p, "\x1bM");     /* Reverse index */
    /* Row 0 (A) unchanged */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    /* Row 1 should be blank (inserted) */
    ASSERT(cell(&t, 1, 0)[0] == '\0' || cell(&t, 1, 0)[0] == ' ');
    /* B→row2, C→row3 */
    ASSERT_STR_EQ("B", cell(&t, 2, 0));
    ASSERT_STR_EQ("C", cell(&t, 3, 0));
    /* E unchanged */
    ASSERT_STR_EQ("E", cell(&t, 4, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_many_lines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    for (int i = 0; i < 100; i++) {
        char line[32];
        snprintf(line, sizeof(line), "Line%03d\r\n", i);
        feed(&t, &p, line);
    }
    /* Should not crash. Last 5 lines visible. */
    ASSERT_EQ(5, (int)t.grid.num_rows);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_preserves_cursor_col(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;5H"); /* Row 3, col 5 */
    feed(&t, &p, "\x1b[1S");   /* Scroll up */
    /* Cursor col should be preserved */
    ASSERT_EQ(4, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_su_zero_is_noop(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    feed(&t, &p, "\x1b[0S"); /* SU 0 = default 1 */
    /* Default param (0) means 1 line of scroll */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_large_su_clears_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    feed(&t, &p, "\x1b[100S"); /* Scroll up 100 */
    /* All content should be gone */
    for (int r = 0; r < 3; r++)
        ASSERT(cell(&t, r, 0)[0] == '\0' || cell(&t, r, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_scroll) {
    RUN_TEST(scroll_newline_at_bottom);
    RUN_TEST(scroll_su_command);
    RUN_TEST(scroll_sd_command);
    RUN_TEST(scroll_multiple_su);
    RUN_TEST(scroll_within_region_only);
    RUN_TEST(scroll_ri_at_top_of_region);
    RUN_TEST(scroll_many_lines);
    RUN_TEST(scroll_preserves_cursor_col);
    RUN_TEST(scroll_su_zero_is_noop);
    RUN_TEST(scroll_large_su_clears_all);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_scroll);
    GREATEST_MAIN_END();
}
