/* test_red_ich_dch.c — RED tests for ICH/DCH (insert/delete character)
 *
 * CSI Ps @ — ICH: insert Ps blank characters, shifting right
 * CSI Ps P — DCH: delete Ps characters, shifting left
 * These are used by readline, zsh, fish for inline editing.
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

TEST red_ich_insert_one(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDE");
    feeds(&t, &p, "\x1b[3G"); /* Move to col 2 (1-based=3) */
    feeds(&t, &p, "\x1b[@");  /* Insert 1 blank */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    /* AB_CDE (blank inserted at col 2, E may shift off) */
    ASSERT(r[0] == 'A');
    ASSERT(r[1] == 'B');
    ASSERT(r[2] == ' '); /* Inserted blank */
    ASSERT(r[3] == 'C');
    ASSERT(r[4] == 'D');
    /* E may or may not be visible (shifted to col 5) */
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ich_insert_multiple(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDEFGHIJ");
    feeds(&t, &p, "\x1b[1G");  /* Col 0 */
    feeds(&t, &p, "\x1b[3@");  /* Insert 3 blanks */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    /* First 3 chars should be spaces */
    ASSERT(r[0] == ' ');
    ASSERT(r[1] == ' ');
    ASSERT(r[2] == ' ');
    ASSERT(r[3] == 'A');
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_dch_delete_one(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDE");
    feeds(&t, &p, "\x1b[3G"); /* Col 2 */
    feeds(&t, &p, "\x1b[P");  /* Delete 1 char */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    /* AB + DE (C deleted, DE shifted left) */
    ASSERT(r[0] == 'A');
    ASSERT(r[1] == 'B');
    ASSERT(r[2] == 'D');
    ASSERT(r[3] == 'E');
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_dch_delete_multiple(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDEFGHIJ");
    feeds(&t, &p, "\x1b[1G");  /* Col 0 */
    feeds(&t, &p, "\x1b[3P");  /* Delete 3 chars */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    /* DEF... shifted to start */
    ASSERT(r[0] == 'D');
    ASSERT(r[1] == 'E');
    ASSERT(r[2] == 'F');
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ech_erase_chars(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABCDEFGHIJ");
    feeds(&t, &p, "\x1b[3G");  /* Col 2 */
    feeds(&t, &p, "\x1b[3X");  /* Erase 3 chars (no shift) */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(r[0] == 'A');
    ASSERT(r[1] == 'B');
    /* Cols 2-4 should be blank */
    ASSERT(r[2] == ' ');
    ASSERT(r[3] == ' ');
    ASSERT(r[4] == ' ');
    ASSERT(r[5] == 'F'); /* Unchanged */
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_ich_dch) {
    RUN_TEST(red_ich_insert_one);
    RUN_TEST(red_ich_insert_multiple);
    RUN_TEST(red_dch_delete_one);
    RUN_TEST(red_dch_delete_multiple);
    RUN_TEST(red_ech_erase_chars);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_ich_dch);
    GREATEST_MAIN_END();
}
