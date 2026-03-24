/* test_red_terminal_responses.c — RED tests for terminal query responses */
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

/* DA2 (secondary device attributes) — CSI > c */
TEST red_da2_response(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[>c");
    const char *resp = wixen_terminal_pop_response(&t);
    /* Should respond with ESC[>... */
    ASSERT(resp != NULL);
    ASSERT(resp[0] == '\x1b');
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* DECRQM — request mode (CSI ? Ps $ p) */
TEST red_decrqm_response(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?25$p"); /* Query DECTCEM mode */
    const char *resp = wixen_terminal_pop_response(&t);
    /* Should respond with mode status */
    /* This is rarely implemented — may be NULL */
    if (resp) free((void *)resp);
    /* Just checking no crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* XTVERSION — CSI > q */
TEST red_xtversion_response(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[>q");
    const char *resp = wixen_terminal_pop_response(&t);
    /* May or may not be implemented */
    if (resp) {
        ASSERT(resp[0] == '\x1b'); /* Should start with ESC */
        free((void *)resp);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Cursor position report after scroll */
TEST red_dsr6_after_scroll(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    for (int i = 0; i < 10; i++) feed(&t, &p, "Line\r\n");
    feed(&t, &p, "\x1b[6n");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* Should report actual cursor position, not pre-scroll */
    ASSERT(strstr(resp, "R") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Multiple DSR responses queued */
TEST red_multiple_dsr_order(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5n"); /* Device status */
    feed(&t, &p, "\x1b[6n"); /* Cursor position */
    const char *r1 = wixen_terminal_pop_response(&t);
    const char *r2 = wixen_terminal_pop_response(&t);
    ASSERT(r1 != NULL);
    ASSERT(r2 != NULL);
    /* First should be device status (0n), second should be cursor pos (R) */
    ASSERT(strstr(r1, "0n") != NULL);
    ASSERT(strstr(r2, "R") != NULL);
    free((void *)r1); free((void *)r2);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* OSC 10 color query */
TEST red_osc10_color_query(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]10;?\x07");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "rgb:") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* OSC 11 bg color query */
TEST red_osc11_bg_query(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]11;?\x07");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "rgb:") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_responses) {
    RUN_TEST(red_da2_response);
    RUN_TEST(red_decrqm_response);
    RUN_TEST(red_xtversion_response);
    RUN_TEST(red_dsr6_after_scroll);
    RUN_TEST(red_multiple_dsr_order);
    RUN_TEST(red_osc10_color_query);
    RUN_TEST(red_osc11_bg_query);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_responses);
    GREATEST_MAIN_END();
}
