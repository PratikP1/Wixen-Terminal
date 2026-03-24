/* test_red_mouse_modes.c — RED tests for mouse mode transitions */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/modes.h"
#include "wixen/core/mouse.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[256];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 256);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_mouse_default_none(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    ASSERT_EQ(WIXEN_MOUSE_NONE, t.modes.mouse_tracking);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_x10_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?9h"); /* X10 mouse */
    ASSERT_EQ(WIXEN_MOUSE_X10, t.modes.mouse_tracking);
    feed(&t, &p, "\x1b[?9l"); /* Off */
    ASSERT_EQ(WIXEN_MOUSE_NONE, t.modes.mouse_tracking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_normal_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?1000h"); /* Normal tracking */
    ASSERT_EQ(WIXEN_MOUSE_NORMAL, t.modes.mouse_tracking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_button_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?1002h"); /* Button-event tracking */
    ASSERT_EQ(WIXEN_MOUSE_BUTTON, t.modes.mouse_tracking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_any_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?1003h"); /* Any-event tracking */
    ASSERT_EQ(WIXEN_MOUSE_ANY, t.modes.mouse_tracking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_sgr_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.mouse_sgr);
    feed(&t, &p, "\x1b[?1006h"); /* SGR encoding */
    ASSERT(t.modes.mouse_sgr);
    feed(&t, &p, "\x1b[?1006l");
    ASSERT_FALSE(t.modes.mouse_sgr);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_mode_override(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?1000h"); /* Normal */
    feed(&t, &p, "\x1b[?1003h"); /* Any — should override */
    ASSERT_EQ(WIXEN_MOUSE_ANY, t.modes.mouse_tracking);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_mouse_encode_none_mode(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NONE, false,
        WIXEN_MBTN_LEFT, 5, 5, false, false, true, buf, sizeof(buf));
    ASSERT_EQ(0, (int)n); /* No output in NONE mode */
    PASS();
}

TEST red_mouse_encode_sgr_release(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_LEFT, 0, 0, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Release uses lowercase 'm' */
    ASSERT(buf[n - 1] == 'm');
    PASS();
}

TEST red_focus_reporting(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.focus_reporting);
    feed(&t, &p, "\x1b[?1004h");
    ASSERT(t.modes.focus_reporting);
    feed(&t, &p, "\x1b[?1004l");
    ASSERT_FALSE(t.modes.focus_reporting);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_mouse_modes) {
    RUN_TEST(red_mouse_default_none);
    RUN_TEST(red_mouse_x10_mode);
    RUN_TEST(red_mouse_normal_mode);
    RUN_TEST(red_mouse_button_mode);
    RUN_TEST(red_mouse_any_mode);
    RUN_TEST(red_mouse_sgr_mode);
    RUN_TEST(red_mouse_mode_override);
    RUN_TEST(red_mouse_encode_none_mode);
    RUN_TEST(red_mouse_encode_sgr_release);
    RUN_TEST(red_focus_reporting);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_mouse_modes);
    GREATEST_MAIN_END();
}
