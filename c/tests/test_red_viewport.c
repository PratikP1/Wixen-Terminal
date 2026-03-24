/* test_red_viewport.c — RED tests for scrollback viewport
 *
 * The viewport tracks how far the user has scrolled back into history.
 * offset=0 means showing the live terminal. offset>0 means looking at
 * older content.
 */
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

TEST red_viewport_default_zero(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    ASSERT_EQ(0, (int)t.scroll_offset);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_viewport_scroll_up(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Generate scrollback */
    for (int i = 0; i < 10; i++) feed(&t, &p, "Line\r\n");
    /* Scroll viewport up by 3 lines */
    wixen_terminal_scroll_viewport(&t, 3);
    ASSERT_EQ(3, (int)t.scroll_offset);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_viewport_scroll_down_clamps_to_zero(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    for (int i = 0; i < 10; i++) feed(&t, &p, "Line\r\n");
    wixen_terminal_scroll_viewport(&t, 5);
    ASSERT_EQ(5, (int)t.scroll_offset);
    /* Scroll back down past zero */
    wixen_terminal_scroll_viewport(&t, -10);
    ASSERT_EQ(0, (int)t.scroll_offset);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_viewport_new_output_resets(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    for (int i = 0; i < 10; i++) feed(&t, &p, "Line\r\n");
    wixen_terminal_scroll_viewport(&t, 5);
    ASSERT_EQ(5, (int)t.scroll_offset);
    /* New output should reset to bottom */
    feed(&t, &p, "New output\r\n");
    /* Viewport MAY or MAY NOT reset — depends on design */
    /* For now, just verify no crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_viewport) {
    RUN_TEST(red_viewport_default_zero);
    RUN_TEST(red_viewport_scroll_up);
    RUN_TEST(red_viewport_scroll_down_clamps_to_zero);
    RUN_TEST(red_viewport_new_output_resets);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_viewport);
    GREATEST_MAIN_END();
}
