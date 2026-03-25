/* test_keyboard.c — Tests for key and mouse encoding */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"

static char kbuf[64];

/* === Key encoding === */

TEST key_ctrl_c(void) {
    size_t n = wixen_encode_key('C', false, true, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(3, kbuf[0]); /* Ctrl+C = ETX */
    PASS();
}

TEST key_ctrl_a(void) {
    size_t n = wixen_encode_key('A', false, true, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(1, kbuf[0]); /* Ctrl+A = SOH */
    PASS();
}

TEST key_ctrl_z(void) {
    size_t n = wixen_encode_key('Z', false, true, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(26, kbuf[0]); /* Ctrl+Z = SUB */
    PASS();
}

TEST key_alt_a(void) {
    size_t n = wixen_encode_key('A', false, false, true, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(2, (int)n);
    ASSERT_EQ('\x1b', kbuf[0]);
    ASSERT_EQ('a', kbuf[1]);
    PASS();
}

TEST key_backspace(void) {
    size_t n = wixen_encode_key(0x08, false, false, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\x7f', kbuf[0]);
    PASS();
}

TEST key_tab(void) {
    size_t n = wixen_encode_key(0x09, false, false, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\t', kbuf[0]);
    PASS();
}

TEST key_shift_tab(void) {
    size_t n = wixen_encode_key(0x09, true, false, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(3, (int)n);
    ASSERT_MEM_EQ("\x1b[Z", kbuf, 3);
    PASS();
}

TEST key_enter(void) {
    size_t n = wixen_encode_key(0x0D, false, false, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\r', kbuf[0]);
    PASS();
}

TEST key_escape(void) {
    size_t n = wixen_encode_key(0x1B, false, false, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\x1b', kbuf[0]);
    PASS();
}

TEST key_arrow_up_normal(void) {
    size_t n = wixen_encode_key(0x26, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[A", kbuf);
    PASS();
}

TEST key_arrow_up_app_cursor(void) {
    size_t n = wixen_encode_key(0x26, false, false, false, true, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1bOA", kbuf);
    PASS();
}

TEST key_arrow_with_modifier(void) {
    size_t n = wixen_encode_key(0x26, true, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[1;2A", kbuf); /* Shift+Up: modifier 2 */
    PASS();
}

TEST key_ctrl_shift_arrow(void) {
    size_t n = wixen_encode_key(0x26, true, true, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[1;6A", kbuf); /* Ctrl+Shift+Up: 1+1+4=6 */
    PASS();
}

TEST key_home(void) {
    size_t n = wixen_encode_key(0x24, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[H", kbuf);
    PASS();
}

TEST key_delete(void) {
    size_t n = wixen_encode_key(0x2E, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[3~", kbuf);
    PASS();
}

TEST key_f1(void) {
    size_t n = wixen_encode_key(0x70, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1bOP", kbuf);
    PASS();
}

TEST key_f5(void) {
    size_t n = wixen_encode_key(0x74, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[15~", kbuf);
    PASS();
}

TEST key_f12(void) {
    size_t n = wixen_encode_key(0x7B, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[24~", kbuf);
    PASS();
}

TEST key_unhandled_returns_zero(void) {
    size_t n = wixen_encode_key(0xFF, false, false, false, false, kbuf, sizeof(kbuf));
    ASSERT_EQ(0, (int)n);
    PASS();
}

TEST key_pageup(void) {
    size_t n = wixen_encode_key(0x21, false, false, false, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[5~", kbuf);
    PASS();
}

/* === Mouse encoding === */

TEST mouse_sgr_left_press(void) {
    size_t n = wixen_encode_mouse_sgr(0, false, false, false, 5, 10, true, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[<0;6;11M", kbuf); /* 1-based coords */
    PASS();
}

TEST mouse_sgr_left_release(void) {
    size_t n = wixen_encode_mouse_sgr(0, false, false, false, 5, 10, false, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[<0;6;11m", kbuf); /* lowercase m for release */
    PASS();
}

TEST mouse_sgr_right_press(void) {
    size_t n = wixen_encode_mouse_sgr(2, false, false, false, 0, 0, true, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[<2;1;1M", kbuf);
    PASS();
}

TEST mouse_sgr_with_modifiers(void) {
    size_t n = wixen_encode_mouse_sgr(0, true, true, false, 3, 7, true, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    /* button=0, shift=4, ctrl=16 → cb=20 */
    ASSERT_STR_EQ("\x1b[<20;4;8M", kbuf);
    PASS();
}

TEST mouse_sgr_wheel_up(void) {
    size_t n = wixen_encode_mouse_sgr(64, false, false, false, 10, 5, true, kbuf, sizeof(kbuf));
    kbuf[n] = 0;
    ASSERT_STR_EQ("\x1b[<64;11;6M", kbuf);
    PASS();
}

TEST mouse_legacy_basic(void) {
    size_t n = wixen_encode_mouse_legacy(0, false, false, false, 5, 10, kbuf, sizeof(kbuf));
    ASSERT_EQ(6, (int)n);
    ASSERT_EQ('\x1b', kbuf[0]);
    ASSERT_EQ('[', kbuf[1]);
    ASSERT_EQ('M', kbuf[2]);
    ASSERT_EQ(32, kbuf[3]);       /* button 0 + 32 */
    ASSERT_EQ(32 + 6, kbuf[4]);  /* col 5+1+32 = 38 */
    ASSERT_EQ(32 + 11, kbuf[5]); /* row 10+1+32 = 43 */
    PASS();
}

TEST mouse_legacy_overflow(void) {
    /* Coordinates > 222 should fail */
    size_t n = wixen_encode_mouse_legacy(0, false, false, false, 223, 0, kbuf, sizeof(kbuf));
    ASSERT_EQ(0, (int)n);
    PASS();
}

SUITE(key_encoding) {
    RUN_TEST(key_ctrl_c);
    RUN_TEST(key_ctrl_a);
    RUN_TEST(key_ctrl_z);
    RUN_TEST(key_alt_a);
    RUN_TEST(key_backspace);
    RUN_TEST(key_tab);
    RUN_TEST(key_shift_tab);
    RUN_TEST(key_enter);
    RUN_TEST(key_escape);
    RUN_TEST(key_arrow_up_normal);
    RUN_TEST(key_arrow_up_app_cursor);
    RUN_TEST(key_arrow_with_modifier);
    RUN_TEST(key_ctrl_shift_arrow);
    RUN_TEST(key_home);
    RUN_TEST(key_delete);
    RUN_TEST(key_f1);
    RUN_TEST(key_f5);
    RUN_TEST(key_f12);
    RUN_TEST(key_unhandled_returns_zero);
    RUN_TEST(key_pageup);
}

SUITE(mouse_sgr_encoding) {
    RUN_TEST(mouse_sgr_left_press);
    RUN_TEST(mouse_sgr_left_release);
    RUN_TEST(mouse_sgr_right_press);
    RUN_TEST(mouse_sgr_with_modifiers);
    RUN_TEST(mouse_sgr_wheel_up);
}

SUITE(mouse_legacy_encoding) {
    RUN_TEST(mouse_legacy_basic);
    RUN_TEST(mouse_legacy_overflow);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(key_encoding);
    RUN_SUITE(mouse_sgr_encoding);
    RUN_SUITE(mouse_legacy_encoding);
    GREATEST_MAIN_END();
}
