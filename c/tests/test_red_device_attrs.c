/* test_red_device_attrs.c — RED tests for device attribute responses
 *
 * DA1 (CSI c): primary device attributes
 * DA2 (CSI > c): secondary device attributes
 * DA3 (CSI = c): tertiary device attributes
 * DECRQM (CSI ? Ps $ p): request mode value
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

TEST red_da1_response(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[c");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* DA1 response: ESC [ ? 6 2 ; <features> c */
    ASSERT(resp[0] == '\x1b');
    ASSERT(resp[1] == '[');
    ASSERT(strstr(resp, "c") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_da2_response(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[>c");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* DA2 response: ESC [ > Pp ; Pv ; Pc c */
    ASSERT(resp[0] == '\x1b');
    ASSERT(strstr(resp, ">") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_dsr_status(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* DSR 5 — status report ("I'm OK") */
    feeds(&t, &p, "\x1b[5n");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* Should respond ESC [ 0 n (terminal OK) */
    ASSERT(strstr(resp, "0n") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_dsr_cursor_position(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[10;20H"); /* Move to row 10, col 20 */
    feeds(&t, &p, "\x1b[6n");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "10;20R") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_xtversion(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* XTVERSION: CSI > 0 q */
    feeds(&t, &p, "\x1b[>0q");
    const char *resp = wixen_terminal_pop_response(&t);
    /* May or may not generate response — just shouldn't crash */
    if (resp) {
        ASSERT(strstr(resp, "Wixen") != NULL || strlen(resp) > 0);
        free((void *)resp);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_decrqm_private(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Request mode: is bracketed paste enabled? */
    feeds(&t, &p, "\x1b[?2004$p");
    const char *resp = wixen_terminal_pop_response(&t);
    /* May respond with DECRPM: ESC[?2004;Ps$y */
    /* Ps=1 set, 2 reset */
    if (resp) {
        ASSERT(resp[0] == '\x1b');
        free((void *)resp);
    }
    /* Shouldn't crash regardless */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_device_attrs) {
    RUN_TEST(red_da1_response);
    RUN_TEST(red_da2_response);
    RUN_TEST(red_dsr_status);
    RUN_TEST(red_dsr_cursor_position);
    RUN_TEST(red_xtversion);
    RUN_TEST(red_decrqm_private);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_device_attrs);
    GREATEST_MAIN_END();
}
