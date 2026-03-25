/* test_red_misc_sequences.c — RED tests for miscellaneous VT sequences
 *
 * DECALN, REP, SU/SD, RI, NEL, IND, DECBI, DECFI
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

/* === DECALN — Screen Alignment Pattern (ESC # 8) === */

TEST red_decaln_fills_with_E(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b#8");
    /* Every cell should contain 'E' */
    for (size_t r = 0; r < 3; r++) {
        for (size_t c = 0; c < 5; c++) {
            ASSERT_STR_EQ("E", t.grid.rows[r].cells[c].content);
        }
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_decaln_resets_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[3;5H"); /* Move cursor */
    feeds(&t, &p, "\x1b#8");
    /* DECALN should home cursor */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === REP — Repeat Last Character (CSI Ps b) === */

TEST red_rep_repeat_char(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "X\x1b[4b"); /* Print X then repeat 4 times */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("XXXXX", r); /* 1 original + 4 repeated */
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_rep_default_one(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "A\x1b[b"); /* No param = repeat 1 */
    char *r = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("AA", r);
    free(r);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === SU/SD — Scroll Up/Down (CSI Ps S / CSI Ps T) === */

TEST red_su_scroll_up(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    feeds(&t, &p, "\x1b[1S"); /* Scroll up 1 */
    /* Row 0 should now have BBB (AAA scrolled off) */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "BBB") != NULL);
    free(r0);
    /* Bottom row should be blank */
    char *r3 = wixen_terminal_extract_row_text(&t, 3);
    ASSERT_STR_EQ("", r3);
    free(r3);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sd_scroll_down(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    feeds(&t, &p, "\x1b[1T"); /* Scroll down 1 */
    /* Row 0 should be blank (everything shifted down) */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("", r0);
    free(r0);
    /* Row 1 should have AAA */
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT(strstr(r1, "AAA") != NULL);
    free(r1);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === RI — Reverse Index (ESC M) === */

TEST red_ri_at_top(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC\r\nDDD");
    feeds(&t, &p, "\x1b[1;1H"); /* Home */
    feeds(&t, &p, "\x1bM");     /* Reverse index at top → scroll down */
    /* Row 0 should be blank (content shifted down) */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("", r0);
    free(r0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT(strstr(r1, "AAA") != NULL);
    free(r1);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ri_not_at_top(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[3;1H"); /* Row 2 */
    feeds(&t, &p, "\x1bM");     /* Reverse index — just moves up */
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === NEL — Next Line (ESC E) === */

TEST red_nel(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 4);
    wixen_parser_init(&p);
    feeds(&t, &p, "ABC");
    feeds(&t, &p, "\x1b" "E"); /* NEL = CR + LF */
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === IND — Index (ESC D) === */

TEST red_ind_at_bottom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "AAA\r\nBBB\r\nCCC");
    /* Cursor at row 2 (last row) */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    feeds(&t, &p, "\x1b" "D"); /* IND at bottom → scroll up */
    /* Cursor stays at row 2, content shifted up */
    ASSERT_EQ(2, (int)t.grid.cursor.row);
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "BBB") != NULL);
    free(r0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_misc_sequences) {
    RUN_TEST(red_decaln_fills_with_E);
    RUN_TEST(red_decaln_resets_cursor);
    RUN_TEST(red_rep_repeat_char);
    RUN_TEST(red_rep_default_one);
    RUN_TEST(red_su_scroll_up);
    RUN_TEST(red_sd_scroll_down);
    RUN_TEST(red_ri_at_top);
    RUN_TEST(red_ri_not_at_top);
    RUN_TEST(red_nel);
    RUN_TEST(red_ind_at_bottom);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_misc_sequences);
    GREATEST_MAIN_END();
}
