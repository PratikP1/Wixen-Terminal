/* test_red_terminal_reflow.c — RED tests for terminal resize with reflow
 *
 * When the terminal is resized, wrapped lines should reflow.
 * This tests the full terminal → grid → text extraction pipeline.
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

TEST red_terminal_reflow_narrow(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("ABCDEFGHIJ", r0);
    free(r0);

    wixen_terminal_resize_reflow(&t, 5, 5);
    char *r0n = wixen_terminal_extract_row_text(&t, 0);
    char *r1n = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("ABCDE", r0n);
    ASSERT_STR_EQ("FGHIJ", r1n);
    free(r0n); free(r1n);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_terminal_reflow_wider(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("ABCDE", r0);
    ASSERT_STR_EQ("FGHIJ", r1);
    free(r0); free(r1);

    wixen_terminal_resize_reflow(&t, 10, 5);
    char *r0w = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("ABCDEFGHIJ", r0w);
    free(r0w);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_terminal_reflow_preserves_newlines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB");
    wixen_terminal_resize_reflow(&t, 20, 5);
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("AAA", r0);
    ASSERT(strstr(r1, "BBB") != NULL);
    free(r0); free(r1);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_terminal_reflow_cursor_follows(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    wixen_terminal_resize_reflow(&t, 20, 5);
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(10, (int)t.grid.cursor.col);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_terminal_reflow) {
    RUN_TEST(red_terminal_reflow_narrow);
    RUN_TEST(red_terminal_reflow_wider);
    RUN_TEST(red_terminal_reflow_preserves_newlines);
    RUN_TEST(red_terminal_reflow_cursor_follows);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_terminal_reflow);
    GREATEST_MAIN_END();
}
