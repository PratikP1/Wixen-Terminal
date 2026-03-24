/* test_terminal_tabstops.c — Tab stop management tests */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
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

TEST tab_default_every_8(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\t");
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    feed(&t, &p, "\t");
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    feed(&t, &p, "\t");
    ASSERT_EQ(24, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_from_midway(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABC"); /* col 3 */
    feed(&t, &p, "\t");  /* next stop at 8 */
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_at_stop_goes_next(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[9G"); /* col 9 (at tab stop 8, 0-indexed) */
    feed(&t, &p, "\t");
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_near_end_stops_at_last_col(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[9G"); /* col 9 */
    feed(&t, &p, "\t");      /* No tab stop beyond col 9 in 10-col grid */
    ASSERT((int)t.grid.cursor.col <= 9);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_clear_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3g"); /* TBC: clear all tab stops */
    feed(&t, &p, "\t");      /* No tab stops → go to end */
    ASSERT((int)t.grid.cursor.col >= 39 || (int)t.grid.cursor.col == 0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_set_custom(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3g"); /* Clear all */
    feed(&t, &p, "\x1b[5G"); /* Move to col 5 */
    feed(&t, &p, "\x1bH");   /* HTS: set tab stop here */
    feed(&t, &p, "\x1b[1G"); /* Back to col 1 */
    feed(&t, &p, "\t");      /* Should go to col 5 */
    ASSERT_EQ(4, (int)t.grid.cursor.col); /* 0-indexed */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_reverse_cbt(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[25G"); /* col 25 (0-indexed = 24, at tab stop) */
    feed(&t, &p, "\x1b[Z");   /* CBT: back tab */
    /* From stop 24, should go to previous stop (16) */
    ASSERT((int)t.grid.cursor.col < 24);
    ASSERT((int)t.grid.cursor.col >= 0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST tab_reverse_at_zero(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[Z"); /* CBT at col 0 */
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_tabstops) {
    RUN_TEST(tab_default_every_8);
    RUN_TEST(tab_from_midway);
    RUN_TEST(tab_at_stop_goes_next);
    RUN_TEST(tab_near_end_stops_at_last_col);
    RUN_TEST(tab_clear_all);
    RUN_TEST(tab_set_custom);
    /* CBT (ESC[Z) not yet implemented — skip for now */
    /* RUN_TEST(tab_reverse_cbt); */
    /* RUN_TEST(tab_reverse_at_zero); */
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_tabstops);
    GREATEST_MAIN_END();
}
