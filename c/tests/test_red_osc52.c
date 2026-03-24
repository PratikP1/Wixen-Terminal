/* test_red_osc52.c — RED tests for OSC 52 clipboard protocol
 *
 * OSC 52 ; selection ; base64-data BEL
 *   selection = "c" (clipboard), "p" (primary), "s" (select)
 *   data = "?" means query (app wants clipboard contents)
 *   data = base64 means set clipboard
 */
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

/* OSC 52 set clipboard with base64 "SGVsbG8=" (Hello) — should not crash */
TEST red_osc52_set_no_crash(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]52;c;SGVsbG8=\x07");
    /* Just checking it doesn't crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* OSC 52 query — should produce a response or at least not crash */
TEST red_osc52_query_no_crash(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]52;c;?\x07");
    /* Query may or may not produce response depending on implementation */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* OSC 52 clear clipboard — empty data */
TEST red_osc52_clear(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]52;c;\x07");
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_osc52) {
    RUN_TEST(red_osc52_set_no_crash);
    RUN_TEST(red_osc52_query_no_crash);
    RUN_TEST(red_osc52_clear);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_osc52);
    GREATEST_MAIN_END();
}
