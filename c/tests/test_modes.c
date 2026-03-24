/* test_modes.c — Tests for WixenTerminalModes */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/modes.h"

TEST modes_init_defaults(void) {
    WixenTerminalModes m;
    wixen_modes_init(&m);
    ASSERT(m.auto_wrap);
    ASSERT(m.cursor_visible);
    ASSERT(m.ansi_mode);
    ASSERT_FALSE(m.cursor_keys_application);
    ASSERT_FALSE(m.alternate_screen);
    ASSERT_FALSE(m.bracketed_paste);
    ASSERT_FALSE(m.focus_reporting);
    ASSERT_EQ(WIXEN_MOUSE_NONE, m.mouse_tracking);
    ASSERT_FALSE(m.mouse_sgr);
    ASSERT_FALSE(m.insert_mode);
    ASSERT_FALSE(m.origin_mode);
    ASSERT_FALSE(m.reverse_video);
    ASSERT_FALSE(m.synchronized_output);
    ASSERT_FALSE(m.line_feed_new_line);
    PASS();
}

TEST modes_reset_restores_defaults(void) {
    WixenTerminalModes m;
    wixen_modes_init(&m);
    m.auto_wrap = false;
    m.cursor_visible = false;
    m.alternate_screen = true;
    m.bracketed_paste = true;
    m.mouse_tracking = WIXEN_MOUSE_ANY;
    wixen_modes_reset(&m);
    ASSERT(m.auto_wrap);
    ASSERT(m.cursor_visible);
    ASSERT_FALSE(m.alternate_screen);
    ASSERT_FALSE(m.bracketed_paste);
    ASSERT_EQ(WIXEN_MOUSE_NONE, m.mouse_tracking);
    PASS();
}

TEST modes_mouse_values(void) {
    ASSERT_EQ(0, WIXEN_MOUSE_NONE);
    ASSERT_EQ(1, WIXEN_MOUSE_X10);
    ASSERT_EQ(2, WIXEN_MOUSE_NORMAL);
    ASSERT_EQ(3, WIXEN_MOUSE_BUTTON);
    ASSERT_EQ(4, WIXEN_MOUSE_ANY);
    PASS();
}

SUITE(modes_tests) {
    RUN_TEST(modes_init_defaults);
    RUN_TEST(modes_reset_restores_defaults);
    RUN_TEST(modes_mouse_values);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(modes_tests);
    GREATEST_MAIN_END();
}
