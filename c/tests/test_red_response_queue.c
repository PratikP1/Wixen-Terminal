/* test_red_response_queue.c — RED tests for terminal response queue
 *
 * DSR, DA, DECRQSS etc. generate responses queued for the PTY.
 * The queue must handle growth, ordering, and drain correctly.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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

TEST red_response_single_dsr(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[6n"); /* DSR cursor position */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "R") != NULL); /* ESC[row;colR */
    free((void *)resp);
    /* Queue should now be empty */
    ASSERT(wixen_terminal_pop_response(&t) == NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_response_ordering(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Generate multiple responses */
    feeds(&t, &p, "\x1b[1;1H"); /* Move to (1,1) */
    feeds(&t, &p, "\x1b[6n");   /* DSR → ESC[1;1R */
    feeds(&t, &p, "\x1b[5;10H"); /* Move to (5,10) */
    feeds(&t, &p, "\x1b[6n");   /* DSR → ESC[5;10R */

    /* First response should be position (1,1) */
    const char *r1 = wixen_terminal_pop_response(&t);
    ASSERT(r1 != NULL);
    ASSERT(strstr(r1, "1;1R") != NULL);
    free((void *)r1);

    /* Second response should be position (5,10) */
    const char *r2 = wixen_terminal_pop_response(&t);
    ASSERT(r2 != NULL);
    ASSERT(strstr(r2, "5;10R") != NULL);
    free((void *)r2);

    ASSERT(wixen_terminal_pop_response(&t) == NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_response_many(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Push 100 DSR responses */
    for (int i = 0; i < 100; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "\x1b[%d;%dH\x1b[6n", (i % 24) + 1, (i % 80) + 1);
        feeds(&t, &p, cmd);
    }
    /* Drain all — should get exactly 100 */
    int count = 0;
    const char *resp;
    while ((resp = wixen_terminal_pop_response(&t)) != NULL) {
        count++;
        free((void *)resp);
    }
    ASSERT_EQ(100, count);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_response_da1(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* DA1 — Device Attributes */
    feeds(&t, &p, "\x1b[c");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* Should start with ESC[ and contain ;  */
    ASSERT(resp[0] == '\x1b');
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_response_osc_query(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* OSC 12 cursor color query */
    feeds(&t, &p, "\x1b]12;?\x07");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "rgb:") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_response_queue) {
    RUN_TEST(red_response_single_dsr);
    RUN_TEST(red_response_ordering);
    RUN_TEST(red_response_many);
    RUN_TEST(red_response_da1);
    RUN_TEST(red_response_osc_query);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_response_queue);
    GREATEST_MAIN_END();
}
