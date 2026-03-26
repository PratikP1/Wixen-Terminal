/* test_red_word_delete.c — RED tests for Ctrl+Backspace word deletion
 *
 * Ctrl+Backspace should send the "delete previous word" sequence to the shell.
 * For most shells this is ESC + DEL (0x1b 0x7f) or Ctrl+W (0x17).
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"

/* Ctrl+Backspace should produce ESC DEL (word delete) */
TEST red_ctrl_backspace_produces_word_delete(void) {
    char buf[32];
    /* VK_BACK=0x08, shift=false, ctrl=true, alt=false */
    size_t n = wixen_encode_key(0x08, false, true, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Should produce either ESC+DEL (0x1b 0x7f) or Ctrl+W (0x17) */
    bool is_esc_del = (n == 2 && buf[0] == '\x1b' && buf[1] == '\x7f');
    bool is_ctrl_w = (n == 1 && buf[0] == '\x17');
    ASSERT(is_esc_del || is_ctrl_w);
    PASS();
}

/* Plain Backspace should produce DEL (0x7f) or BS (0x08) */
TEST red_plain_backspace(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x08, false, false, false, false, buf, sizeof(buf));
    ASSERT(n == 1);
    ASSERT(buf[0] == '\x7f' || buf[0] == '\x08');
    PASS();
}

/* Alt+Backspace should also produce ESC DEL (word delete in many shells) */
TEST red_alt_backspace_word_delete(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x08, false, false, true, false, buf, sizeof(buf));
    ASSERT(n > 0);
    /* ESC + DEL */
    bool is_esc_del = (n == 2 && buf[0] == '\x1b' && buf[1] == '\x7f');
    ASSERT(is_esc_del);
    PASS();
}

SUITE(red_word_delete) {
    RUN_TEST(red_ctrl_backspace_produces_word_delete);
    RUN_TEST(red_plain_backspace);
    RUN_TEST(red_alt_backspace_word_delete);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_word_delete);
    GREATEST_MAIN_END();
}
