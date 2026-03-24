/* test_stress_minimal.c — Minimal stress tests to isolate memory bugs */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/vt/parser.h"
#include "wixen/core/terminal.h"

/* Test each component in isolation to find the crash */

TEST stress_grid_scroll_only(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    for (int i = 0; i < 100; i++) {
        wixen_grid_scroll_up(&g, 1);
    }
    wixen_grid_free(&g);
    PASS();
}

TEST stress_terminal_print_100(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    for (int i = 0; i < 100; i++) {
        WixenAction a = { .type = WIXEN_ACTION_PRINT, .codepoint = 'A' + (i % 26) };
        wixen_terminal_dispatch(&t, &a);
    }
    wixen_terminal_free(&t);
    PASS();
}

TEST stress_terminal_linefeed_100(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    for (int i = 0; i < 100; i++) {
        WixenAction a = { .type = WIXEN_ACTION_EXECUTE, .control_byte = 0x0A };
        wixen_terminal_dispatch(&t, &a);
    }
    wixen_terminal_free(&t);
    PASS();
}

TEST stress_parser_then_terminal_10(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    WixenParser p;
    wixen_parser_init(&p);
    WixenAction actions[64];
    for (int i = 0; i < 10; i++) {
        char line[32];
        int len = sprintf(line, "Line %d\r\n", i);
        size_t count = wixen_parser_process(&p, (uint8_t *)line, (size_t)len, actions, 64);
        for (size_t j = 0; j < count; j++) {
            wixen_terminal_dispatch(&t, &actions[j]);
        }
    }
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST stress_parser_then_terminal_50(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    WixenParser p;
    wixen_parser_init(&p);
    WixenAction actions[64];
    for (int i = 0; i < 50; i++) {
        char line[32];
        int len = sprintf(line, "Line %02d\r\n", i);
        size_t count = wixen_parser_process(&p, (uint8_t *)line, (size_t)len, actions, 64);
        for (size_t j = 0; j < count; j++) {
            wixen_terminal_dispatch(&t, &actions[j]);
        }
    }
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

SUITE(minimal_stress) {
    RUN_TEST(stress_grid_scroll_only);
    RUN_TEST(stress_terminal_print_100);
    RUN_TEST(stress_terminal_linefeed_100);
    RUN_TEST(stress_parser_then_terminal_10);
    RUN_TEST(stress_parser_then_terminal_50);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(minimal_stress);
    GREATEST_MAIN_END();
}
