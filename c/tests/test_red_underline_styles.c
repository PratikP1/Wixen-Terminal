/* test_red_underline_styles.c — RED tests for SGR underline variants
 *
 * SGR 4 = single underline
 * SGR 4:0 = none, 4:1 = single, 4:2 = double, 4:3 = curly, 4:4 = dotted, 4:5 = dashed
 * SGR 21 = double underline (legacy)
 * SGR 24 = underline off
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/cell.h"
#include "wixen/vt/parser.h"

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_sgr4_single_underline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[4mU");
    ASSERT_EQ(WIXEN_UNDERLINE_SINGLE, t.grid.rows[0].cells[0].attrs.underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr24_underline_off(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[4m\x1b[24mN");
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, t.grid.rows[0].cells[0].attrs.underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr21_double_underline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[21mD");
    ASSERT_EQ(WIXEN_UNDERLINE_DOUBLE, t.grid.rows[0].cells[0].attrs.underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_bold_italic_underline_combo(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[1;3;4mC");
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT(a->bold);
    ASSERT(a->italic);
    ASSERT_EQ(WIXEN_UNDERLINE_SINGLE, a->underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_strikethrough(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[9mS");
    ASSERT(t.grid.rows[0].cells[0].attrs.strikethrough);
    feeds(&t, &p, "\x1b[29mN");
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.strikethrough);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_dim(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[2mD");
    ASSERT(t.grid.rows[0].cells[0].attrs.dim);
    feeds(&t, &p, "\x1b[22mN"); /* 22 = bold+dim off */
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.dim);
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.bold);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_invisible(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[8mH");
    ASSERT(t.grid.rows[0].cells[0].attrs.hidden);
    feeds(&t, &p, "\x1b[28mV");
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.hidden);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_inverse(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[7mI");
    ASSERT(t.grid.rows[0].cells[0].attrs.inverse);
    feeds(&t, &p, "\x1b[27mN");
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.inverse);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_reset_clears_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[1;2;3;4;7;8;9m\x1b[0mR");
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT_FALSE(a->bold);
    ASSERT_FALSE(a->dim);
    ASSERT_FALSE(a->italic);
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, a->underline);
    ASSERT_FALSE(a->inverse);
    ASSERT_FALSE(a->hidden);
    ASSERT_FALSE(a->strikethrough);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_underline_styles) {
    RUN_TEST(red_sgr4_single_underline);
    RUN_TEST(red_sgr24_underline_off);
    RUN_TEST(red_sgr21_double_underline);
    RUN_TEST(red_sgr_bold_italic_underline_combo);
    RUN_TEST(red_sgr_strikethrough);
    RUN_TEST(red_sgr_dim);
    RUN_TEST(red_sgr_invisible);
    RUN_TEST(red_sgr_inverse);
    RUN_TEST(red_sgr_reset_clears_all);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_underline_styles);
    GREATEST_MAIN_END();
}
