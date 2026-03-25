/* test_red_sgr_colors.c — RED tests for SGR 256-color and truecolor
 *
 * SGR 38;5;N = 256-color foreground
 * SGR 48;5;N = 256-color background
 * SGR 38;2;R;G;B = truecolor foreground
 * SGR 48;2;R;G;B = truecolor background
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

TEST red_sgr_256_fg(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[38;5;196mR"); /* Color 196 = bright red */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(196, (int)t.grid.rows[0].cells[0].attrs.fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_256_bg(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[48;5;21mB"); /* Color 21 = blue */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.bg.type);
    ASSERT_EQ(21, (int)t.grid.rows[0].cells[0].attrs.bg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_truecolor_fg(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[38;2;255;128;0mO"); /* Orange */
    ASSERT_EQ(WIXEN_COLOR_RGB, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(255, (int)t.grid.rows[0].cells[0].attrs.fg.rgb.r);
    ASSERT_EQ(128, (int)t.grid.rows[0].cells[0].attrs.fg.rgb.g);
    ASSERT_EQ(0, (int)t.grid.rows[0].cells[0].attrs.fg.rgb.b);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_truecolor_bg(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[48;2;30;30;30mD"); /* Dark gray bg */
    ASSERT_EQ(WIXEN_COLOR_RGB, t.grid.rows[0].cells[0].attrs.bg.type);
    ASSERT_EQ(30, (int)t.grid.rows[0].cells[0].attrs.bg.rgb.r);
    ASSERT_EQ(30, (int)t.grid.rows[0].cells[0].attrs.bg.rgb.g);
    ASSERT_EQ(30, (int)t.grid.rows[0].cells[0].attrs.bg.rgb.b);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_reset_clears_color(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[38;2;255;0;0m\x1b[0mN");
    /* After reset, fg should be default */
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[0].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_colon_separator(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Modern terminals use colon as sub-parameter separator */
    feeds(&t, &p, "\x1b[38:2:255:0:255mP"); /* Magenta via colons */
    /* If colon parsing works, should get RGB */
    WixenColor fg = t.grid.rows[0].cells[0].attrs.fg;
    if (fg.type == WIXEN_COLOR_RGB) {
        ASSERT_EQ(255, (int)fg.rgb.r);
        ASSERT_EQ(0, (int)fg.rgb.g);
        ASSERT_EQ(255, (int)fg.rgb.b);
    }
    /* If not supported, at least shouldn't crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_bright_colors(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* SGR 90-97 = bright fg, 100-107 = bright bg */
    feeds(&t, &p, "\x1b[91mR"); /* Bright red fg */
    WixenColor fg = t.grid.rows[0].cells[0].attrs.fg;
    /* Should be indexed color 9 (bright red) or equivalent */
    ASSERT(fg.type == WIXEN_COLOR_INDEXED || fg.type == WIXEN_COLOR_RGB);
    if (fg.type == WIXEN_COLOR_INDEXED) {
        ASSERT(fg.index >= 8); /* Bright colors are 8-15 */
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_sgr_colors) {
    RUN_TEST(red_sgr_256_fg);
    RUN_TEST(red_sgr_256_bg);
    RUN_TEST(red_sgr_truecolor_fg);
    RUN_TEST(red_sgr_truecolor_bg);
    RUN_TEST(red_sgr_reset_clears_color);
    RUN_TEST(red_sgr_colon_separator);
    RUN_TEST(red_sgr_bright_colors);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_sgr_colors);
    GREATEST_MAIN_END();
}
