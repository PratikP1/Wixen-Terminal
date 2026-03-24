/* test_terminal_sgr.c — SGR (Select Graphic Rendition) attribute tests */
#include <stdbool.h>
#include <string.h>
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

TEST sgr_reset_clears_attrs(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;31m"); /* Bold + red */
    feed(&t, &p, "\x1b[0m");    /* Reset */
    feed(&t, &p, "A");
    ASSERT_FALSE(t.grid.rows[0].cells[0].attrs.bold);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_bold(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1mB");
    ASSERT(t.grid.rows[0].cells[0].attrs.bold);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_italic(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3mI");
    ASSERT(t.grid.rows[0].cells[0].attrs.italic);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_underline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[4mU");
    ASSERT(t.grid.rows[0].cells[0].attrs.underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_strikethrough(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[9mS");
    ASSERT(t.grid.rows[0].cells[0].attrs.strikethrough);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_inverse(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[7mR");
    ASSERT(t.grid.rows[0].cells[0].attrs.inverse);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_fg_8color(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31mR"); /* Red foreground */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(1, (int)t.grid.rows[0].cells[0].attrs.fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_bg_8color(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[44mB"); /* Blue background */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.bg.type);
    ASSERT_EQ(4, (int)t.grid.rows[0].cells[0].attrs.bg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_256color(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[38;5;200mX"); /* FG index 200 */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(200, (int)t.grid.rows[0].cells[0].attrs.fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_truecolor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[38;2;255;128;0mT"); /* FG RGB(255,128,0) */
    ASSERT_EQ(WIXEN_COLOR_RGB, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(255, (int)t.grid.rows[0].cells[0].attrs.fg.rgb.r);
    ASSERT_EQ(128, (int)t.grid.rows[0].cells[0].attrs.fg.rgb.g);
    ASSERT_EQ(0, (int)t.grid.rows[0].cells[0].attrs.fg.rgb.b);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_multiple_combined(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;3;4;31mC"); /* Bold+italic+underline+red */
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT(a->bold);
    ASSERT(a->italic);
    ASSERT(a->underline);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, a->fg.type);
    ASSERT_EQ(1, (int)a->fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_bright_fg(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[91mH"); /* Bright red */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(9, (int)t.grid.rows[0].cells[0].attrs.fg.index); /* 91-90+8 = 9 */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_dim(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[2mD");
    ASSERT(t.grid.rows[0].cells[0].attrs.dim);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_hidden(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[8mH");
    ASSERT(t.grid.rows[0].cells[0].attrs.hidden);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST sgr_off_bold(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1m");  /* Bold on */
    feed(&t, &p, "\x1b[22m"); /* Bold off */
    feed(&t, &p, "X");
    ASSERT_FALSE(t.grid.rows[0].cells[0].attrs.bold);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_sgr) {
    RUN_TEST(sgr_reset_clears_attrs);
    RUN_TEST(sgr_bold);
    RUN_TEST(sgr_italic);
    RUN_TEST(sgr_underline);
    RUN_TEST(sgr_strikethrough);
    RUN_TEST(sgr_inverse);
    RUN_TEST(sgr_fg_8color);
    RUN_TEST(sgr_bg_8color);
    RUN_TEST(sgr_256color);
    RUN_TEST(sgr_truecolor);
    RUN_TEST(sgr_multiple_combined);
    RUN_TEST(sgr_bright_fg);
    RUN_TEST(sgr_dim);
    RUN_TEST(sgr_hidden);
    RUN_TEST(sgr_off_bold);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_sgr);
    GREATEST_MAIN_END();
}
