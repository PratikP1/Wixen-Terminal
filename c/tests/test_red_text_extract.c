/* test_red_text_extract.c — RED tests for extract_row_text + selected_text
 *
 * These are critical for a11y (NVDA reads via text provider)
 * and clipboard (Ctrl+C copies selection).
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

/* === extract_row_text === */

TEST red_extract_row_simple(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello World");
    char *text = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "Hello World") != NULL);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_extract_row_trims_trailing_spaces(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hi");
    char *text = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(text != NULL);
    /* Should NOT have 18 trailing spaces */
    ASSERT_EQ(2, (int)strlen(text));
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_extract_row_empty(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    char *text = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(text != NULL);
    ASSERT_EQ(0, (int)strlen(text));
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_extract_row_out_of_bounds(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    char *text = wixen_terminal_extract_row_text(&t, 999);
    /* Should return empty string, not crash */
    ASSERT(text != NULL);
    ASSERT_EQ(0, (int)strlen(text));
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_extract_row_with_wide_chars(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* Write some ASCII then a CJK char (U+4E16 = 世, 3 bytes UTF-8) */
    feed(&t, &p, "AB");
    /* Manually set a wide char */
    wixen_cell_set_content(&t.grid.rows[0].cells[2], "\xe4\xb8\x96");
    t.grid.rows[0].cells[2].width = 2;
    char *text = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "AB") != NULL);
    ASSERT(strstr(text, "\xe4\xb8\x96") != NULL);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === visible_text (full grid as single string) === */

TEST red_visible_text(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "Line1\r\nLine2\r\nLine3");
    char *text = wixen_terminal_visible_text(&t);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "Line1") != NULL);
    ASSERT(strstr(text, "Line2") != NULL);
    ASSERT(strstr(text, "Line3") != NULL);
    /* Lines should be separated by \n */
    ASSERT(strstr(text, "Line1\n") != NULL);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === selected_text === */

TEST red_selected_text_normal(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello World!");
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 4, 0);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    ASSERT_STR_EQ("Hello", text);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_selected_text_multiline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAA\r\nBBBB\r\nCCCC");
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 2, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 1, 2);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    /* Should contain end of line 0, all of line 1, start of line 2 */
    ASSERT(strstr(text, "AA") != NULL);
    ASSERT(strstr(text, "BBBB") != NULL);
    ASSERT(strstr(text, "CC") != NULL);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_selected_text_no_selection(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    WixenSelection sel;
    wixen_selection_init(&sel);
    /* No selection active */
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    ASSERT_EQ(0, (int)strlen(text));
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_selected_text_block(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "0123456789\r\nABCDEFGHIJ\r\nKLMNOPQRST");
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 2, 0, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 5, 2);
    char *text = wixen_terminal_selected_text(&t, &sel);
    ASSERT(text != NULL);
    /* Block cols 2-5 across rows 0-2: "2345", "CDEF", "MNOP" */
    ASSERT(strstr(text, "2345") != NULL);
    ASSERT(strstr(text, "CDEF") != NULL);
    ASSERT(strstr(text, "MNOP") != NULL);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_text_extract) {
    RUN_TEST(red_extract_row_simple);
    RUN_TEST(red_extract_row_trims_trailing_spaces);
    RUN_TEST(red_extract_row_empty);
    RUN_TEST(red_extract_row_out_of_bounds);
    RUN_TEST(red_extract_row_with_wide_chars);
    RUN_TEST(red_visible_text);
    RUN_TEST(red_selected_text_normal);
    RUN_TEST(red_selected_text_multiline);
    RUN_TEST(red_selected_text_no_selection);
    RUN_TEST(red_selected_text_block);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_text_extract);
    GREATEST_MAIN_END();
}
