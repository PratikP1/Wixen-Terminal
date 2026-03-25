/* test_red_selection_wide.c — RED tests for selection with wide chars and wraps
 *
 * Selection text extraction must handle:
 * - Wide (CJK) characters that occupy 2 cells
 * - Soft-wrapped lines that should join without newline
 * - Mixed ASCII + wide in the same selection
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/selection.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_select_wrapped_line(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 5);
    wixen_parser_init(&p);
    /* "ABCDEFGHIJ" wraps at col 5 into rows 0-1 */
    feed(&t, &p, "ABCDEFGHIJ");
    ASSERT(t.grid.rows[0].wrapped);

    /* Select entire wrapped region */
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 4, 1);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    /* Wrapped lines should join WITHOUT newline */
    ASSERT(strstr(text, "ABCDE") != NULL);
    ASSERT(strstr(text, "FGHIJ") != NULL);
    /* Should NOT have a newline between them */
    ASSERT(strstr(text, "ABCDEFGHIJ") != NULL);
    free(text);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_select_hard_newline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    /* Two distinct lines (hard newline) */
    feed(&t, &p, "AAA\r\nBBB");
    ASSERT_FALSE(t.grid.rows[0].wrapped);

    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 2, 1);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    /* Hard newline — should have \n between them */
    ASSERT(strstr(text, "AAA\nBBB") != NULL);
    free(text);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_select_wide_char(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Place a CJK char: 世 (U+4E16, 3 bytes UTF-8, width 2) */
    feed(&t, &p, "A");
    /* Manually write a wide char at col 1-2 */
    wixen_cell_set_content(&t.grid.rows[0].cells[1], "\xe4\xb8\x96");
    t.grid.rows[0].cells[1].width = 2;
    wixen_cell_set_content(&t.grid.rows[0].cells[2], "");
    t.grid.rows[0].cells[2].width = 0; /* continuation */
    wixen_cell_set_content(&t.grid.rows[0].cells[3], "B");

    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 3, 0);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "A") != NULL);
    ASSERT(strstr(text, "\xe4\xb8\x96") != NULL); /* 世 */
    ASSERT(strstr(text, "B") != NULL);
    /* Continuation cell should not produce extra space */
    ASSERT(strstr(text, "A\xe4\xb8\x96" "B") != NULL);
    free(text);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_select_block_with_wide(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "0123456789\r\nABCDEFGHIJ\r\nKLMNOPQRST");

    /* Block selection cols 3-6 */
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 3, 0, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 6, 2);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "3456") != NULL);
    ASSERT(strstr(text, "DEFG") != NULL);
    ASSERT(strstr(text, "NOPQ") != NULL);
    free(text);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_select_empty_rows(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    /* Select across empty rows */
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 9, 4);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    /* Should not crash, text may be empty or whitespace */
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

SUITE(red_selection_wide) {
    RUN_TEST(red_select_wrapped_line);
    RUN_TEST(red_select_hard_newline);
    RUN_TEST(red_select_wide_char);
    RUN_TEST(red_select_block_with_wide);
    RUN_TEST(red_select_empty_rows);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_selection_wide);
    GREATEST_MAIN_END();
}
