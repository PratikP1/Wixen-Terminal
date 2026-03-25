/* test_red_erase_ops.c — RED tests for ED and EL erase operations
 *
 * ED (CSI Ps J): 0=below, 1=above, 2=all, 3=all+scrollback
 * EL (CSI Ps K): 0=right, 1=left, 2=whole line
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

/* === ED — Erase in Display === */

TEST red_ed0_erase_below(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    feeds(&t, &p, "\x1b[2;1H"); /* Row 1 */
    feeds(&t, &p, "\x1b[J");    /* ED 0: erase from cursor to end */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "AAA") != NULL); /* Above cursor preserved */
    free(r0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("", r1); /* Erased */
    free(r1);
    char *r3 = wixen_terminal_extract_row_text(&t, 3);
    ASSERT_STR_EQ("", r3); /* Erased */
    free(r3);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ed1_erase_above(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    feeds(&t, &p, "\x1b[3;1H"); /* Row 2 */
    feeds(&t, &p, "\x1b[1J");   /* ED 1: erase from start to cursor */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("", r0); /* Erased */
    free(r0);
    char *r3 = wixen_terminal_extract_row_text(&t, 3);
    ASSERT(strstr(r3, "DDD") != NULL); /* Below cursor preserved */
    free(r3);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ed2_erase_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    feeds(&t, &p, "\x1b[2J"); /* ED 2: erase all */
    for (size_t r = 0; r < 4; r++) {
        char *text = wixen_terminal_extract_row_text(&t, r);
        ASSERT_STR_EQ("", text);
        free(text);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ed2_preserves_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[3;5H"); /* Row 2, col 4 */
    feeds(&t, &p, "\x1b[2J");
    /* ED 2 should NOT move cursor */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    ASSERT_EQ(4, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === EL — Erase in Line === */

TEST red_el0_erase_right(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDEFGHIJ");
    feeds(&t, &p, "\x1b[4G"); /* Col 3 (1-based=4) */
    feeds(&t, &p, "\x1b[K");    /* EL 0: erase to right */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("ABC", r);
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_el1_erase_left(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDEFGHIJ");
    feeds(&t, &p, "\x1b[4G"); /* Col 3 */
    feeds(&t, &p, "\x1b[1K");   /* EL 1: erase to left (inclusive) */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    /* Cols 0-3 erased, cols 4-9 preserved */
    ASSERT(r[0] == ' ' || r[0] == '\0');
    ASSERT(strlen(r) == 0 || r[4] == 'E');
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_el2_erase_whole_line(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDEFGHIJ\r\nSecondLine");
    feeds(&t, &p, "\x1b[1;5H"); /* Row 0, col 4 */
    feeds(&t, &p, "\x1b[2K");   /* EL 2: erase whole line */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("", r0);
    free(r0);
    /* Second line should be untouched */
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT(strstr(r1, "SecondLine") != NULL);
    free(r1);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_erase_ops) {
    RUN_TEST(red_ed0_erase_below);
    RUN_TEST(red_ed1_erase_above);
    RUN_TEST(red_ed2_erase_all);
    RUN_TEST(red_ed2_preserves_cursor);
    RUN_TEST(red_el0_erase_right);
    RUN_TEST(red_el1_erase_left);
    RUN_TEST(red_el2_erase_whole_line);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_erase_ops);
    GREATEST_MAIN_END();
}
