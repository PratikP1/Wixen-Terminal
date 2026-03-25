/* test_red_grid_lines.c — RED tests for grid insert_line / delete_line
 *
 * These operations are used by CSI L (IL) and CSI M (DL) sequences.
 * They insert/delete lines within the scroll region, shifting content.
 */
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

TEST red_insert_line_shifts_down(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Line0\r\nLine1\r\nLine2\r\nLine3\r\nLine4");
    /* Move to row 1 */
    feed(&t, &p, "\x1b[2;1H");
    /* Insert 1 line (CSI L) */
    feed(&t, &p, "\x1b[L");
    /* Row 1 should now be blank, old Line1 pushed to row 2 */
    char *row1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("", row1);
    free(row1);
    char *row2 = wixen_terminal_extract_row_text(&t, 2);
    ASSERT(strstr(row2, "Line1") != NULL);
    free(row2);
    /* Line4 should have been pushed off the bottom */
    char *row4 = wixen_terminal_extract_row_text(&t, 4);
    ASSERT(strstr(row4, "Line3") != NULL);
    free(row4);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_insert_multiple_lines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    feed(&t, &p, "\x1b[1;1H"); /* Row 0 */
    feed(&t, &p, "\x1b[2L");   /* Insert 2 lines */
    char *row0 = wixen_terminal_extract_row_text(&t, 0);
    char *row1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("", row0);
    ASSERT_STR_EQ("", row1);
    free(row0); free(row1);
    char *row2 = wixen_terminal_extract_row_text(&t, 2);
    ASSERT(strstr(row2, "AAA") != NULL);
    free(row2);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_delete_line_shifts_up(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Line0\r\nLine1\r\nLine2\r\nLine3\r\nLine4");
    feed(&t, &p, "\x1b[2;1H"); /* Row 1 */
    feed(&t, &p, "\x1b[M");    /* Delete 1 line */
    /* Row 1 should now have Line2 */
    char *row1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT(strstr(row1, "Line2") != NULL);
    free(row1);
    /* Bottom row should be blank */
    char *row4 = wixen_terminal_extract_row_text(&t, 4);
    ASSERT_STR_EQ("", row4);
    free(row4);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_delete_multiple_lines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    feed(&t, &p, "\x1b[1;1H"); /* Row 0 */
    feed(&t, &p, "\x1b[3M");   /* Delete 3 lines */
    char *row0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(row0, "DDD") != NULL);
    free(row0);
    char *row1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT(strstr(row1, "EEE") != NULL);
    free(row1);
    /* Rows 2-4 blank */
    char *row2 = wixen_terminal_extract_row_text(&t, 2);
    ASSERT_STR_EQ("", row2);
    free(row2);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_insert_within_scroll_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 6);
    wixen_parser_init(&p);
    feed(&t, &p, "Row0\r\nRow1\r\nRow2\r\nRow3\r\nRow4\r\nRow5");
    /* Set scroll region to rows 2-4 (1-based: 3;5) */
    feed(&t, &p, "\x1b[3;5r");
    /* Move to row 2 (inside region) */
    feed(&t, &p, "\x1b[3;1H");
    /* Insert line — should only shift within region */
    feed(&t, &p, "\x1b[L");
    /* Row 0 and 1 should be unchanged */
    char *row0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(row0, "Row0") != NULL);
    free(row0);
    char *row1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT(strstr(row1, "Row1") != NULL);
    free(row1);
    /* Row 5 should be unchanged (outside region) */
    char *row5 = wixen_terminal_extract_row_text(&t, 5);
    ASSERT(strstr(row5, "Row5") != NULL);
    free(row5);
    /* Row 2 should now be blank */
    char *row2 = wixen_terminal_extract_row_text(&t, 2);
    ASSERT_STR_EQ("", row2);
    free(row2);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_grid_lines) {
    RUN_TEST(red_insert_line_shifts_down);
    RUN_TEST(red_insert_multiple_lines);
    RUN_TEST(red_delete_line_shifts_up);
    RUN_TEST(red_delete_multiple_lines);
    RUN_TEST(red_insert_within_scroll_region);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_grid_lines);
    GREATEST_MAIN_END();
}
