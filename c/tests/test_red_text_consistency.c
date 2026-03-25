/* test_red_text_consistency.c — RED tests: grid content == extracted text
 *
 * After any terminal operation, visible_text() and extract_row_text()
 * must match what's actually in the grid cells. This catches
 * desynchronization bugs between the grid and text extraction.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define strdup _strdup
#endif
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) free(actions[i].apc.data);
    }
}

/* Build expected text by manually reading grid cells */
static char *grid_manual_row_text(const WixenTerminal *t, size_t row) {
    if (row >= t->grid.num_rows) return strdup("");
    size_t cap = t->grid.cols * 4 + 1;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;
    for (size_t c = 0; c < t->grid.cols; c++) {
        const char *content = t->grid.rows[row].cells[c].content;
        if (content[0] && strcmp(content, " ") != 0) {
            size_t len = strlen(content);
            memcpy(buf + pos, content, len);
            pos += len;
        } else {
            buf[pos++] = ' ';
        }
    }
    buf[pos] = '\0';
    /* Trim trailing spaces */
    while (pos > 0 && buf[pos - 1] == ' ') buf[--pos] = '\0';
    return buf;
}

TEST red_consistency_simple(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello World");
    char *extracted = wixen_terminal_extract_row_text(&t, 0);
    char *manual = grid_manual_row_text(&t, 0);
    ASSERT_STR_EQ(manual, extracted);
    free(extracted); free(manual);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_consistency_after_erase(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;6H"); /* Move to col 5 */
    feed(&t, &p, "\x1b[K");    /* Erase to end of line */
    char *extracted = wixen_terminal_extract_row_text(&t, 0);
    char *manual = grid_manual_row_text(&t, 0);
    ASSERT_STR_EQ(manual, extracted);
    ASSERT_STR_EQ("ABCDE", extracted);
    free(extracted); free(manual);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_consistency_after_insert_delete(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Line0\r\nLine1\r\nLine2\r\nLine3\r\nLine4");
    feed(&t, &p, "\x1b[2;1H\x1b[M"); /* Delete line at row 1 */
    for (size_t r = 0; r < 5; r++) {
        char *extracted = wixen_terminal_extract_row_text(&t, r);
        char *manual = grid_manual_row_text(&t, r);
        ASSERT_STR_EQ(manual, extracted);
        free(extracted); free(manual);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_consistency_after_scroll(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Fill grid + scroll */
    feed(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    /* DDD caused scroll — AAA pushed to scrollback */
    for (size_t r = 0; r < 3; r++) {
        char *extracted = wixen_terminal_extract_row_text(&t, r);
        char *manual = grid_manual_row_text(&t, r);
        ASSERT_STR_EQ(manual, extracted);
        free(extracted); free(manual);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_consistency_visible_text_multiline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    char *full = wixen_terminal_visible_text(&t);
    ASSERT(full != NULL);
    /* Should contain all three lines separated by \n */
    ASSERT(strstr(full, "AAA") != NULL);
    ASSERT(strstr(full, "BBB") != NULL);
    ASSERT(strstr(full, "CCC") != NULL);
    /* Verify each extracted row matches */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    char *r2 = wixen_terminal_extract_row_text(&t, 2);
    ASSERT(strstr(full, r0) != NULL);
    ASSERT(strstr(full, r1) != NULL);
    ASSERT(strstr(full, r2) != NULL);
    free(full); free(r0); free(r1); free(r2);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_consistency_after_sgr(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* SGR changes attrs but not content */
    feed(&t, &p, "\x1b[1;31mBold Red\x1b[0m Normal");
    char *extracted = wixen_terminal_extract_row_text(&t, 0);
    char *manual = grid_manual_row_text(&t, 0);
    ASSERT_STR_EQ(manual, extracted);
    ASSERT(strstr(extracted, "Bold Red") != NULL);
    ASSERT(strstr(extracted, "Normal") != NULL);
    free(extracted); free(manual);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_consistency_after_reflow(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    wixen_terminal_resize_reflow(&t, 5, 5);
    for (size_t r = 0; r < 5; r++) {
        char *extracted = wixen_terminal_extract_row_text(&t, r);
        char *manual = grid_manual_row_text(&t, r);
        ASSERT_STR_EQ(manual, extracted);
        free(extracted); free(manual);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_text_consistency) {
    RUN_TEST(red_consistency_simple);
    RUN_TEST(red_consistency_after_erase);
    RUN_TEST(red_consistency_after_insert_delete);
    RUN_TEST(red_consistency_after_scroll);
    RUN_TEST(red_consistency_visible_text_multiline);
    RUN_TEST(red_consistency_after_sgr);
    RUN_TEST(red_consistency_after_reflow);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_text_consistency);
    GREATEST_MAIN_END();
}
