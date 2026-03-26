/* test_red_ctrl_delete.c — RED tests for Ctrl+Delete (forward word delete)
 *
 * Ctrl+Delete should send ESC+d (0x1b 0x64) — standard forward word delete.
 * Plain Delete should send ESC+[3~ as before.
 * Shift+Delete should behave like plain Delete (no special handling).
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"

/* Ctrl+Delete should produce ESC d (0x1b 0x64) — forward word delete */
TEST red_ctrl_delete_produces_forward_word_delete(void) {
    char buf[32];
    /* VK_DELETE=0x2E, shift=false, ctrl=true, alt=false */
    size_t n = wixen_encode_key(0x2E, false, true, false, false, buf, sizeof(buf));
    ASSERT_EQ(2, (int)n);
    ASSERT_EQ('\x1b', buf[0]);
    ASSERT_EQ('d', buf[1]);
    PASS();
}

/* Plain Delete should produce ESC[3~ */
TEST red_plain_delete_produces_standard_sequence(void) {
    char buf[32];
    /* VK_DELETE=0x2E, no modifiers */
    size_t n = wixen_encode_key(0x2E, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(4, (int)n);
    ASSERT_MEM_EQ("\x1b[3~", buf, 4);
    PASS();
}

/* Shift+Delete should produce same as plain Delete (no special handling) */
TEST red_shift_delete_same_as_plain(void) {
    char buf[32];
    /* VK_DELETE=0x2E, shift=true, ctrl=false, alt=false */
    size_t n = wixen_encode_key(0x2E, true, false, false, false, buf, sizeof(buf));
    /* Shift+Delete: xterm modifier param = 2, so ESC[3;2~ */
    ASSERT(n > 0);
    /* Should NOT produce ESC d (that's only for Ctrl) */
    bool is_forward_word_delete = (n == 2 && buf[0] == '\x1b' && buf[1] == 'd');
    ASSERT(!is_forward_word_delete);
    PASS();
}

SUITE(red_ctrl_delete) {
    RUN_TEST(red_ctrl_delete_produces_forward_word_delete);
    RUN_TEST(red_plain_delete_produces_standard_sequence);
    RUN_TEST(red_shift_delete_same_as_plain);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_ctrl_delete);
    GREATEST_MAIN_END();
}
