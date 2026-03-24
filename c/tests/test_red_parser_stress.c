/* test_red_parser_stress.c — Stress tests for parser robustness */
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
    }
}

/* Interleaved escape sequences and text */
TEST red_interleaved_escapes(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "A\x1b[31mB\x1b[0mC\x1b[1;2HD\x1b[KE\r\nF");
    /* Just no crash and terminal in consistent state */
    ASSERT(t.grid.cursor.row <= 9);
    ASSERT(t.grid.cursor.col < 40);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Rapid alternating between CSI and OSC */
TEST red_rapid_csi_osc(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    for (int i = 0; i < 100; i++) {
        feed(&t, &p, "\x1b[31mX");
        feed(&t, &p, "\x1b]0;T\x07");
        feed(&t, &p, "\x1b[0m");
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* ESC immediately followed by another ESC */
TEST red_double_esc(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b\x1b[31mRed");
    /* First ESC is aborted by second ESC, then CSI 31m processes */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Incomplete CSI at end of buffer */
TEST red_incomplete_csi_recovery(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AB\x1b["); /* Incomplete CSI */
    feed(&t, &p, "31mCD");  /* Complete it in next chunk */
    /* CD should be red */
    ASSERT_STR_EQ("C", t.grid.rows[0].cells[2].content);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[2].attrs.fg.type);
    ASSERT_EQ(1, (int)t.grid.rows[0].cells[2].attrs.fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Very long OSC string (near 4KB limit) */
TEST red_long_osc(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Build a long OSC 0 (title) */
    char buf[5000];
    int pos = 0;
    pos += sprintf(buf + pos, "\x1b]0;");
    for (int i = 0; i < 3900; i++) buf[pos++] = 'A';
    buf[pos++] = '\x07';
    buf[pos] = '\0';
    feed(&t, &p, buf);
    /* Title should be set (possibly truncated) */
    ASSERT(t.title != NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* NUL bytes interspersed */
TEST red_nul_bytes(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    uint8_t data[] = {'A', 0, 'B', 0, 0, 'C'};
    WixenAction actions[32];
    size_t n = wixen_parser_process(&p, data, sizeof(data), actions, 32);
    for (size_t i = 0; i < n; i++)
        wixen_terminal_dispatch(&t, &actions[i]);
    /* NULs should be ignored, ABC printed */
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", t.grid.rows[0].cells[1].content);
    ASSERT_STR_EQ("C", t.grid.rows[0].cells[2].content);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* CAN (0x18) cancels escape sequence */
TEST red_can_cancels_escape(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31"); /* Partial CSI */
    uint8_t can = 0x18;
    WixenAction actions[8];
    wixen_parser_process(&p, &can, 1, actions, 8);
    feed(&t, &p, "A"); /* Should print with DEFAULT attrs (CSI was cancelled) */
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[0].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* SUB (0x1A) cancels and prints error char */
TEST red_sub_cancels_escape(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31"); /* Partial CSI */
    uint8_t sub = 0x1A;
    WixenAction actions[8];
    wixen_parser_process(&p, &sub, 1, actions, 8);
    /* Parser should return to ground state */
    feed(&t, &p, "B");
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[0].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_parser_stress) {
    RUN_TEST(red_interleaved_escapes);
    RUN_TEST(red_rapid_csi_osc);
    RUN_TEST(red_double_esc);
    RUN_TEST(red_incomplete_csi_recovery);
    RUN_TEST(red_long_osc);
    RUN_TEST(red_nul_bytes);
    RUN_TEST(red_can_cancels_escape);
    RUN_TEST(red_sub_cancels_escape);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_parser_stress);
    GREATEST_MAIN_END();
}
