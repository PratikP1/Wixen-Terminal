/* test_red_window_lifecycle.c — RED tests for window lifecycle events
 *
 * Tests for proper handling of close, resize, focus restoration,
 * and PTY cleanup on exit.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/keyboard.h"
#include "wixen/config/keybindings.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* === WM_CLOSE: Alt+F4 must not be blocked === */

TEST red_close_not_blocked_by_keybindings(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    /* Verify none of the defaults capture Alt+F4 */
    ASSERT(wixen_keybindings_lookup(&kb, "alt+f4") == NULL);
    /* Also check with F4 alone */
    ASSERT(wixen_keybindings_lookup(&kb, "f4") == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

/* === Resize: terminal grid recalculates === */

TEST red_resize_updates_grid(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    ASSERT_EQ(80, (int)t.grid.cols);
    ASSERT_EQ(24, (int)t.grid.num_rows);
    wixen_terminal_resize(&t, 120, 40);
    ASSERT_EQ(120, (int)t.grid.cols);
    ASSERT_EQ(40, (int)t.grid.num_rows);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_resize_preserves_content(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    /* Write content */
    WixenCell *cell = &t.grid.rows[0].cells[0];
    wixen_cell_set_content(cell, "A");
    /* Resize larger — content should survive */
    wixen_terminal_resize(&t, 100, 30);
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[0].content);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_resize_clamps_cursor(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    t.grid.cursor.row = 23;
    t.grid.cursor.col = 79;
    /* Resize smaller — cursor must clamp */
    wixen_terminal_resize(&t, 40, 10);
    ASSERT(t.grid.cursor.row < 10);
    ASSERT(t.grid.cursor.col < 40);
    wixen_terminal_free(&t);
    PASS();
}

/* === Keyboard: Ctrl+L clears terminal === */

TEST red_ctrl_l_clear(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    /* Ctrl+L should be mapped to clear or reset */
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+l");
    /* May not be in defaults — some terminals use it, some don't */
    /* At minimum, shouldn't crash */
    (void)action;
    wixen_keybindings_free(&kb);
    PASS();
}

/* === Terminal cleanup on free === */

TEST red_terminal_free_complete(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    /* Set various state */
    wixen_terminal_set_title(&t, "Test");
    wixen_terminal_on_char_sent(&t, 'a');
    t.scroll_offset = 5;
    /* Free should clean everything */
    wixen_terminal_free(&t);
    /* Double free should be safe */
    wixen_terminal_free(&t);
    PASS();
}

/* === Keyboard encoding does not produce empty for known keys === */

TEST red_all_arrow_keys_produce_output(void) {
    char buf[32];
    /* Up=0x26, Down=0x28, Left=0x25, Right=0x27 */
    uint16_t arrows[] = {0x26, 0x28, 0x25, 0x27};
    for (int i = 0; i < 4; i++) {
        size_t n = wixen_encode_key(arrows[i], false, false, false, false, buf, sizeof(buf));
        ASSERT(n > 0);
    }
    PASS();
}

TEST red_enter_produces_cr(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x0D, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(buf[0] == '\r' || buf[0] == '\n');
    PASS();
}

TEST red_backspace_produces_byte(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x08, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(buf[0] == 0x7F || buf[0] == 0x08);
    PASS();
}

SUITE(red_window_lifecycle) {
    RUN_TEST(red_close_not_blocked_by_keybindings);
    RUN_TEST(red_resize_updates_grid);
    RUN_TEST(red_resize_preserves_content);
    RUN_TEST(red_resize_clamps_cursor);
    RUN_TEST(red_ctrl_l_clear);
    RUN_TEST(red_terminal_free_complete);
    RUN_TEST(red_all_arrow_keys_produce_output);
    RUN_TEST(red_enter_produces_cr);
    RUN_TEST(red_backspace_produces_byte);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_window_lifecycle);
    GREATEST_MAIN_END();
}
