/* test_red_origin_mode.c — RED tests for DECOM (origin mode)
 *
 * When origin mode is on, cursor positions are relative to the
 * scroll region, not the full screen. CUP(1,1) goes to the top
 * of the scroll region, not row 0.
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

TEST red_origin_cup_relative(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 20);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;15r");  /* Scroll region rows 5-15 (1-based) */
    feeds(&t, &p, "\x1b[?6h");    /* Enable origin mode */
    feeds(&t, &p, "\x1b[1;1H");   /* CUP(1,1) — should go to row 4 (top of region) */
    ASSERT_EQ(4, (int)t.grid.cursor.row); /* 0-based row 4 = 1-based row 5 */
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_origin_cup_bottom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 20);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;15r");
    feeds(&t, &p, "\x1b[?6h");
    /* CUP to bottom of region: row 11 (region has 11 rows: 5-15) */
    feeds(&t, &p, "\x1b[11;1H");
    ASSERT_EQ(14, (int)t.grid.cursor.row); /* Row 14 = bottom of region */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_origin_clamp_to_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 20);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;10r");  /* Region rows 5-10 (6 rows) */
    feeds(&t, &p, "\x1b[?6h");
    /* Try to move beyond region */
    feeds(&t, &p, "\x1b[99;1H");
    /* Should clamp to bottom of region */
    ASSERT(t.grid.cursor.row <= 9); /* Row 9 = 1-based 10 = bottom */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_origin_off_absolute(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 20);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;15r");
    feeds(&t, &p, "\x1b[?6l");   /* Origin mode OFF */
    feeds(&t, &p, "\x1b[1;1H");  /* Should go to absolute (0,0) */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_origin_homes_on_enable(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 20);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[10;1H"); /* Move to row 9 */
    feeds(&t, &p, "\x1b[5;15r"); /* Set region */
    feeds(&t, &p, "\x1b[?6h");   /* Enable origin — should home to top of region */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_origin_dsr_reports_relative(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 20);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;15r");
    feeds(&t, &p, "\x1b[?6h");
    feeds(&t, &p, "\x1b[3;10H"); /* Row 3 in region = absolute row 6 */
    feeds(&t, &p, "\x1b[6n");    /* DSR */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* In origin mode, DSR should report relative to region */
    /* Row 3, col 10 (1-based relative) */
    ASSERT(strstr(resp, "3;10R") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_origin_mode) {
    RUN_TEST(red_origin_cup_relative);
    RUN_TEST(red_origin_cup_bottom);
    RUN_TEST(red_origin_clamp_to_region);
    RUN_TEST(red_origin_off_absolute);
    RUN_TEST(red_origin_homes_on_enable);
    RUN_TEST(red_origin_dsr_reports_relative);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_origin_mode);
    GREATEST_MAIN_END();
}
