/* test_red_echo_timeout.c — RED tests for password prompt detection
 *
 * When the terminal stops echoing typed characters (e.g., sudo password),
 * we detect it and notify the screen reader.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_echo_timeout_initial_false(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    ASSERT_FALSE(wixen_terminal_check_echo_timeout(&t));
    wixen_terminal_free(&t);
    PASS();
}

TEST red_echo_timeout_after_char(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    /* Simulate a character being sent to PTY (user typed) */
    wixen_terminal_on_char_sent(&t, 'a');
    /* Without echo within timeout, should detect password prompt */
    /* Simulate time passing (set last_char_sent_ms to old value) */
    t.last_char_sent_ms -= 2000; /* 2 seconds ago */
    ASSERT(wixen_terminal_check_echo_timeout(&t));
    wixen_terminal_free(&t);
    PASS();
}

TEST red_echo_timeout_reset_on_echo(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    wixen_terminal_on_char_sent(&t, 'a');
    /* Terminal echoes the character back */
    feed(&t, &p, "a");
    t.last_char_sent_ms -= 2000;
    /* Should NOT detect timeout because echo was received */
    ASSERT_FALSE(wixen_terminal_check_echo_timeout(&t));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_echo_timeout) {
    RUN_TEST(red_echo_timeout_initial_false);
    RUN_TEST(red_echo_timeout_after_char);
    RUN_TEST(red_echo_timeout_reset_on_echo);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_echo_timeout);
    GREATEST_MAIN_END();
}
