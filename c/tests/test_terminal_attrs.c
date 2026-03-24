/* test_terminal_attrs.c — Cell attribute persistence and inheritance */
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

TEST attr_bold_persists_across_chars(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1mABC");
    ASSERT(t.grid.rows[0].cells[0].attrs.bold);
    ASSERT(t.grid.rows[0].cells[1].attrs.bold);
    ASSERT(t.grid.rows[0].cells[2].attrs.bold);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_reset_stops_bold(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1mAB\x1b[0mCD");
    ASSERT(t.grid.rows[0].cells[0].attrs.bold);
    ASSERT(t.grid.rows[0].cells[1].attrs.bold);
    ASSERT_FALSE(t.grid.rows[0].cells[2].attrs.bold);
    ASSERT_FALSE(t.grid.rows[0].cells[3].attrs.bold);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_fg_bg_independent(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31;42mX");
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT_EQ(WIXEN_COLOR_INDEXED, a->fg.type);
    ASSERT_EQ(1, (int)a->fg.index); /* Red FG */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, a->bg.type);
    ASSERT_EQ(2, (int)a->bg.index); /* Green BG */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_erase_clears_attrs(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;31mBOLD RED");
    feed(&t, &p, "\x1b[2K"); /* Erase entire line */
    /* Erased cells should have default attrs */
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[0].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_256_fg_persists(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[38;5;123mABC");
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[i].attrs.fg.type);
        ASSERT_EQ(123, (int)t.grid.rows[0].cells[i].attrs.fg.index);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_truecolor_bg(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[48;2;50;100;150mX");
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT_EQ(WIXEN_COLOR_RGB, a->bg.type);
    ASSERT_EQ(50, (int)a->bg.rgb.r);
    ASSERT_EQ(100, (int)a->bg.rgb.g);
    ASSERT_EQ(150, (int)a->bg.rgb.b);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_dim_and_bold_exclusive(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1m"); /* Bold on */
    feed(&t, &p, "\x1b[2mX"); /* Dim on (should turn bold off in most terminals) */
    ASSERT(t.grid.rows[0].cells[0].attrs.dim);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_underline_styles(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[4mA"); /* Single underline */
    ASSERT(t.grid.rows[0].cells[0].attrs.underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_inverse_swaps_colors(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[7mI");
    ASSERT(t.grid.rows[0].cells[0].attrs.inverse);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_hidden_text(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[8mH");
    ASSERT(t.grid.rows[0].cells[0].attrs.hidden);
    feed(&t, &p, "\x1b[28mV"); /* Un-hide */
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.hidden);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_bright_colors(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[90mD"); /* Bright black (dark gray) FG */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(8, (int)t.grid.rows[0].cells[0].attrs.fg.index);
    feed(&t, &p, "\x1b[107mL"); /* Bright white BG */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[1].attrs.bg.type);
    ASSERT_EQ(15, (int)t.grid.rows[0].cells[1].attrs.bg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_default_fg_reset(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31mR\x1b[39mD"); /* Red then default FG */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[1].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST attr_default_bg_reset(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[41mR\x1b[49mD"); /* Red BG then default */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.bg.type);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[1].attrs.bg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_attrs) {
    RUN_TEST(attr_bold_persists_across_chars);
    RUN_TEST(attr_reset_stops_bold);
    RUN_TEST(attr_fg_bg_independent);
    RUN_TEST(attr_erase_clears_attrs);
    RUN_TEST(attr_256_fg_persists);
    RUN_TEST(attr_truecolor_bg);
    RUN_TEST(attr_dim_and_bold_exclusive);
    RUN_TEST(attr_underline_styles);
    RUN_TEST(attr_inverse_swaps_colors);
    RUN_TEST(attr_hidden_text);
    RUN_TEST(attr_bright_colors);
    RUN_TEST(attr_default_fg_reset);
    RUN_TEST(attr_default_bg_reset);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_attrs);
    GREATEST_MAIN_END();
}
