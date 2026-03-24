/* test_terminal_scroll_region.c — Scroll region (DECSTBM) tests */
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

TEST region_default_full(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(5, (int)t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST region_set_decstbm(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;7r"); /* Top=3, Bottom=7 (1-based) */
    ASSERT_EQ(2, (int)t.scroll_region.top);
    ASSERT_EQ(7, (int)t.scroll_region.bottom);
    /* Cursor moves to home */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST region_reset_decstbm(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;7r");
    feed(&t, &p, "\x1b[r"); /* Reset */
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(10, (int)t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST region_scroll_within_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 3, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    feed(&t, &p, "\x1b[2;4r"); /* Region rows 2-4 */
    feed(&t, &p, "\x1b[4;1H"); /* Move to bottom of region */
    feed(&t, &p, "\n");        /* LF at bottom of region triggers scroll */
    /* Row 0 (A) and Row 4 (E) should be unchanged */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    /* Content within region scrolled up: C moved to row 1, D to row 2 */
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    ASSERT_STR_EQ("D", cell(&t, 2, 0));
    /* Row 3 should be blank (new line scrolled in) */
    ASSERT(cell(&t, 3, 0)[0] == '\0' || cell(&t, 3, 0)[0] == ' ');
    ASSERT_STR_EQ("E", cell(&t, 4, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST region_reverse_index_at_top(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 3, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    feed(&t, &p, "\x1b[2;4r"); /* Region rows 2-4 */
    feed(&t, &p, "\x1b[2;1H"); /* Move to top of region */
    feed(&t, &p, "\x1bM");     /* RI: reverse index */
    /* Row 0 (A) should be unchanged */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    /* Row 1 should be blank (inserted by RI) */
    ASSERT(cell(&t, 1, 0)[0] == '\0' || cell(&t, 1, 0)[0] == ' ');
    /* B moved to row 2, C to row 3 */
    ASSERT_STR_EQ("B", cell(&t, 2, 0));
    ASSERT_STR_EQ("C", cell(&t, 3, 0));
    /* D pushed off bottom of region, E unchanged */
    ASSERT_STR_EQ("E", cell(&t, 4, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_scroll_region) {
    RUN_TEST(region_default_full);
    RUN_TEST(region_set_decstbm);
    RUN_TEST(region_reset_decstbm);
    RUN_TEST(region_scroll_within_region);
    RUN_TEST(region_reverse_index_at_top);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_scroll_region);
    GREATEST_MAIN_END();
}
