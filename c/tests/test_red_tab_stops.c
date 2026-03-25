/* test_red_tab_stops.c — RED tests for HTS/TBC/CHT tab stop behavior
 *
 * Tab stops: HTS sets, TBC clears, CHT advances to next.
 * Default: every 8 columns.
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

TEST red_default_tab_stops(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Tab from col 0 should go to col 8 */
    feeds(&t, &p, "\t");
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    /* Another tab → col 16 */
    feeds(&t, &p, "\t");
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_tab_from_mid_column(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Move to col 3, then tab should go to col 8 */
    feeds(&t, &p, "ABC\t");
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_hts_sets_tab_stop(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Clear all tab stops */
    feeds(&t, &p, "\x1b[3g");
    /* Set tab stop at col 5 */
    feeds(&t, &p, "\x1b[6G"); /* Move to col 5 (1-based=6) */
    feeds(&t, &p, "\x1bH");   /* HTS */
    /* Go to col 0, tab should now stop at col 5 */
    feeds(&t, &p, "\x1b[1G\t");
    ASSERT_EQ(5, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_tbc_clears_current(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Default: tab stops at 8,16,24... */
    /* Move to col 8 and clear that tab stop */
    feeds(&t, &p, "\x1b[9G"); /* col 8 (1-based=9) */
    feeds(&t, &p, "\x1b[0g"); /* TBC — clear at current position */
    /* Now from col 0, tab should skip 8 and go to 16 */
    feeds(&t, &p, "\x1b[1G\t");
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_tbc_clears_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Clear all tab stops */
    feeds(&t, &p, "\x1b[3g");
    /* Tab should go to end of line (no stops) or stay */
    feeds(&t, &p, "\t");
    /* With no tab stops, tab typically goes to last column */
    ASSERT(t.grid.cursor.col >= 79 || t.grid.cursor.col == 0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_tab_at_last_column(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Move to last column */
    feeds(&t, &p, "\x1b[80G");
    ASSERT_EQ(79, (int)t.grid.cursor.col);
    /* Tab at last col should not crash or wrap */
    feeds(&t, &p, "\t");
    ASSERT(t.grid.cursor.col <= 79);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_tab_stops) {
    RUN_TEST(red_default_tab_stops);
    RUN_TEST(red_tab_from_mid_column);
    RUN_TEST(red_hts_sets_tab_stop);
    RUN_TEST(red_tbc_clears_current);
    RUN_TEST(red_tbc_clears_all);
    RUN_TEST(red_tab_at_last_column);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_tab_stops);
    GREATEST_MAIN_END();
}
