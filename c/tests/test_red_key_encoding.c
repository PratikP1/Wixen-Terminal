/* test_red_key_encoding.c — RED tests for keyboard encoding to PTY
 *
 * Arrow keys, function keys, and modifiers must encode to the correct
 * escape sequences for the PTY. Wrong encoding = broken terminal.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"

TEST red_encode_up_arrow(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x26, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[A", buf);
    PASS();
}

TEST red_encode_down_arrow(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x28, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[B", buf);
    PASS();
}

TEST red_encode_right_arrow(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x27, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[C", buf);
    PASS();
}

TEST red_encode_left_arrow(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x25, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[D", buf);
    PASS();
}

TEST red_encode_up_arrow_app_mode(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x26, false, false, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Application cursor keys: ESC O A */
    ASSERT_STR_EQ("\x1bOA", buf);
    PASS();
}

TEST red_encode_home(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x24, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[H", buf);
    PASS();
}

TEST red_encode_end(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x23, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[F", buf);
    PASS();
}

TEST red_encode_delete(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x2E, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[3~", buf);
    PASS();
}

TEST red_encode_f1(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x70, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1bOP", buf);
    PASS();
}

TEST red_encode_f5(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x74, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[15~", buf);
    PASS();
}

TEST red_encode_f12(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x7B, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[24~", buf);
    PASS();
}

TEST red_encode_ctrl_c(void) {
    char buf[32];
    /* Ctrl+C = VK 0x43 with ctrl flag */
    size_t n = wixen_encode_key(0x43, false, true, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Ctrl+C = 0x03 (ETX) */
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(0x03, (unsigned char)buf[0]);
    PASS();
}

TEST red_encode_ctrl_z(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x5A, false, true, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(0x1A, (unsigned char)buf[0]);
    PASS();
}

TEST red_encode_shift_arrow(void) {
    char buf[32];
    /* Shift+Up = CSI 1;2 A */
    size_t n = wixen_encode_key(0x26, true, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "1;2A") != NULL);
    PASS();
}

TEST red_encode_alt_arrow(void) {
    char buf[32];
    /* Alt+Up = CSI 1;3 A */
    size_t n = wixen_encode_key(0x26, false, false, true, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "1;3A") != NULL);
    PASS();
}

TEST red_encode_enter(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x0D, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_EQ('\r', buf[0]);
    PASS();
}

TEST red_encode_tab(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x09, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_EQ('\t', buf[0]);
    PASS();
}

TEST red_encode_escape(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x1B, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_EQ(0x1B, (unsigned char)buf[0]);
    PASS();
}

TEST red_encode_backspace(void) {
    char buf[32];
    size_t n = wixen_encode_key(0x08, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Backspace = 0x7F or 0x08 depending on config */
    ASSERT(buf[0] == 0x7F || buf[0] == 0x08);
    PASS();
}

TEST red_encode_unknown_key(void) {
    char buf[32];
    /* Unknown VK code should return 0 (nothing to send) */
    size_t n = wixen_encode_key(0xFF, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(0, (int)n);
    PASS();
}

SUITE(red_key_encoding) {
    RUN_TEST(red_encode_up_arrow);
    RUN_TEST(red_encode_down_arrow);
    RUN_TEST(red_encode_right_arrow);
    RUN_TEST(red_encode_left_arrow);
    RUN_TEST(red_encode_up_arrow_app_mode);
    RUN_TEST(red_encode_home);
    RUN_TEST(red_encode_end);
    RUN_TEST(red_encode_delete);
    RUN_TEST(red_encode_f1);
    RUN_TEST(red_encode_f5);
    RUN_TEST(red_encode_f12);
    RUN_TEST(red_encode_ctrl_c);
    RUN_TEST(red_encode_ctrl_z);
    RUN_TEST(red_encode_shift_arrow);
    RUN_TEST(red_encode_alt_arrow);
    RUN_TEST(red_encode_enter);
    RUN_TEST(red_encode_tab);
    RUN_TEST(red_encode_escape);
    RUN_TEST(red_encode_backspace);
    RUN_TEST(red_encode_unknown_key);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_key_encoding);
    GREATEST_MAIN_END();
}
