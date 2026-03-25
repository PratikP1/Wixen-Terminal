/* test_red_parser_recovery.c — RED tests for parser state recovery
 *
 * After receiving corrupt/truncated escape sequences, the parser must
 * recover to ground state and process subsequent valid input correctly.
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
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) free(actions[i].apc.data);
    }
}

TEST red_recover_after_truncated_csi(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* Truncated CSI (no final byte). 'H' is a valid final (CUP), so
     * the parser correctly interprets \x1b[31Hello as CSI 31 H + "ello".
     * Force recovery with a CAN byte (0x18) which aborts any sequence. */
    feed(&t, &p, "\x1b[31");
    feed(&t, &p, "\x18");  /* CAN aborts CSI */
    feed(&t, &p, "Hello");
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    ASSERT(strstr(row, "Hello") != NULL);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_recover_after_truncated_osc(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* Start OSC but never terminate it, then send ESC to force recovery */
    feed(&t, &p, "\x1b]999;some data");
    /* ESC starts a new sequence, forcing OSC to flush */
    feed(&t, &p, "\x1b[0mAfter");
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    ASSERT(strstr(row, "After") != NULL);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_recover_after_invalid_esc(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* ESC followed by invalid byte (not [, ], (, ), etc.) */
    feed(&t, &p, "\x1b" "\x80" "Normal");
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    ASSERT(strstr(row, "Normal") != NULL);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_recover_multiple_corruptions(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    /* Multiple corrupt sequences interspersed with valid text */
    feed(&t, &p, "A");
    feed(&t, &p, "\x1b[");          /* truncated CSI */
    feed(&t, &p, "\x1b");           /* lone ESC */
    feed(&t, &p, "B");              /* should print */
    feed(&t, &p, "\x1b]");          /* truncated OSC */
    feed(&t, &p, "\x1b[0mC");       /* valid CSI + print */
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    /* A and C should be present. B might or might not depending on parser */
    ASSERT(strstr(row, "A") != NULL);
    ASSERT(strstr(row, "C") != NULL);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_recover_overlong_params(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* CSI with way too many parameters */
    feed(&t, &p, "\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20;21;22;23;24;25;26;27;28;29;30;31;32m");
    feed(&t, &p, "OK");
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    ASSERT(strstr(row, "OK") != NULL);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_recover_csi_with_garbage_intermediates(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* CSI with invalid intermediate bytes */
    feed(&t, &p, "\x1b[?#!$1m");
    feed(&t, &p, "Fine");
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    ASSERT(strstr(row, "Fine") != NULL);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_parser_recovery) {
    RUN_TEST(red_recover_after_truncated_csi);
    RUN_TEST(red_recover_after_truncated_osc);
    RUN_TEST(red_recover_after_invalid_esc);
    RUN_TEST(red_recover_multiple_corruptions);
    RUN_TEST(red_recover_overlong_params);
    RUN_TEST(red_recover_csi_with_garbage_intermediates);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_parser_recovery);
    GREATEST_MAIN_END();
}
