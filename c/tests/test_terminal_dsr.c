/* test_terminal_dsr.c — Device Status Report tests */
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

TEST dsr_cursor_position(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5;10H"); /* Move to row 5, col 10 */
    feed(&t, &p, "\x1b[6n");    /* DSR: report cursor position */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT_STR_EQ("\x1b[5;10R", resp);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST dsr_device_status(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5n"); /* DSR: device status */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT_STR_EQ("\x1b[0n", resp); /* 0 = ready */
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST dsr_no_response_for_unknown(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[99n"); /* Unknown DSR */
    const char *resp = wixen_terminal_pop_response(&t);
    /* Should be NULL — no response for unknown */
    if (resp) free((void *)resp);
    PASS();
}

TEST dsr_cursor_at_origin(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[6n"); /* DSR: report at (0,0) → "1;1" */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT_STR_EQ("\x1b[1;1R", resp);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST dsr_multiple_responses(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5n"); /* Device status */
    feed(&t, &p, "\x1b[6n"); /* Cursor position */
    const char *r1 = wixen_terminal_pop_response(&t);
    const char *r2 = wixen_terminal_pop_response(&t);
    ASSERT(r1 != NULL);
    ASSERT(r2 != NULL);
    free((void *)r1);
    free((void *)r2);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST response_queue_empty(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 10);
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp == NULL);
    wixen_terminal_free(&t);
    PASS();
}

TEST dsr_da_primary(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[c"); /* DA: Device Attributes */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* Should start with ESC [ ? */
    ASSERT(resp[0] == '\x1b');
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_dsr) {
    RUN_TEST(dsr_cursor_position);
    RUN_TEST(dsr_device_status);
    RUN_TEST(dsr_no_response_for_unknown);
    RUN_TEST(dsr_cursor_at_origin);
    RUN_TEST(dsr_multiple_responses);
    RUN_TEST(response_queue_empty);
    RUN_TEST(dsr_da_primary);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_dsr);
    GREATEST_MAIN_END();
}
