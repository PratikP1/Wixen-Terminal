/* test_red_cursor_shape.c — RED tests for DECSCUSR cursor shape changes
 *
 * CSI Ps SP q — Set cursor style
 *   0 = blinking block (default), 1 = blinking block, 2 = steady block
 *   3 = blinking underline, 4 = steady underline
 *   5 = blinking bar, 6 = steady bar
 */
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

TEST red_cursor_default_block(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_EQ(WIXEN_CURSOR_BLOCK, t.grid.cursor.shape);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_steady_block(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[2 q"); /* Steady block */
    ASSERT_EQ(WIXEN_CURSOR_BLOCK, t.grid.cursor.shape);
    ASSERT_FALSE(t.grid.cursor.blinking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_blinking_underline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3 q"); /* Blinking underline */
    ASSERT_EQ(WIXEN_CURSOR_UNDERLINE, t.grid.cursor.shape);
    ASSERT(t.grid.cursor.blinking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_steady_underline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[4 q"); /* Steady underline */
    ASSERT_EQ(WIXEN_CURSOR_UNDERLINE, t.grid.cursor.shape);
    ASSERT_FALSE(t.grid.cursor.blinking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_blinking_bar(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5 q"); /* Blinking bar */
    ASSERT_EQ(WIXEN_CURSOR_BAR, t.grid.cursor.shape);
    ASSERT(t.grid.cursor.blinking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_steady_bar(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[6 q"); /* Steady bar */
    ASSERT_EQ(WIXEN_CURSOR_BAR, t.grid.cursor.shape);
    ASSERT_FALSE(t.grid.cursor.blinking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cursor_reset_default(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[6 q"); /* Steady bar */
    feed(&t, &p, "\x1b[0 q"); /* Reset to default */
    ASSERT_EQ(WIXEN_CURSOR_BLOCK, t.grid.cursor.shape);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_cursor_shape) {
    RUN_TEST(red_cursor_default_block);
    RUN_TEST(red_cursor_steady_block);
    RUN_TEST(red_cursor_blinking_underline);
    RUN_TEST(red_cursor_steady_underline);
    RUN_TEST(red_cursor_blinking_bar);
    RUN_TEST(red_cursor_steady_bar);
    RUN_TEST(red_cursor_reset_default);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_cursor_shape);
    GREATEST_MAIN_END();
}
