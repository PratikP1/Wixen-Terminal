/* test_red_mouse_encoding.c — RED tests for SGR mouse encoding
 *
 * Terminal apps receive mouse events as escape sequences.
 * SGR format: CSI < button;col;row M (press) or m (release)
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"

TEST red_mouse_sgr_left_press(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_sgr(0, false, false, false, 10, 5, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    /* CSI < 0;11;6 M  (1-based col/row) */
    ASSERT(strstr(buf, "\x1b[<0;11;6M") != NULL);
    PASS();
}

TEST red_mouse_sgr_left_release(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_sgr(0, false, false, false, 10, 5, false, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "\x1b[<0;11;6m") != NULL); /* lowercase m = release */
    PASS();
}

TEST red_mouse_sgr_right_press(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_sgr(2, false, false, false, 0, 0, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "\x1b[<2;1;1M") != NULL);
    PASS();
}

TEST red_mouse_sgr_middle_press(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_sgr(1, false, false, false, 5, 3, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "\x1b[<1;6;4M") != NULL);
    PASS();
}

TEST red_mouse_sgr_shift(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_sgr(0, true, false, false, 1, 1, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    /* Shift adds 4 to button code */
    ASSERT(strstr(buf, "\x1b[<4;2;2M") != NULL);
    PASS();
}

TEST red_mouse_sgr_ctrl(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_sgr(0, false, true, false, 1, 1, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    /* Ctrl adds 16 to button code */
    ASSERT(strstr(buf, "\x1b[<16;2;2M") != NULL);
    PASS();
}

TEST red_mouse_sgr_scroll_up(void) {
    char buf[64];
    /* Scroll up = button 64 */
    size_t n = wixen_encode_mouse_sgr(64, false, false, false, 5, 5, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "\x1b[<64;6;6M") != NULL);
    PASS();
}

TEST red_mouse_sgr_scroll_down(void) {
    char buf[64];
    /* Scroll down = button 65 */
    size_t n = wixen_encode_mouse_sgr(65, false, false, false, 5, 5, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "\x1b[<65;6;6M") != NULL);
    PASS();
}

TEST red_mouse_sgr_large_coordinates(void) {
    char buf[64];
    /* SGR supports coordinates > 223 (unlike legacy X10) */
    size_t n = wixen_encode_mouse_sgr(0, false, false, false, 300, 100, true, buf, sizeof(buf));
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, ";301;101M") != NULL);
    PASS();
}

TEST red_mouse_legacy_left_press(void) {
    char buf[64];
    size_t n = wixen_encode_mouse_legacy(0, false, false, false, 10, 5, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Legacy: CSI M <button+32> <col+33> <row+33> */
    ASSERT_EQ(6, (int)n);
    ASSERT_EQ('\x1b', buf[0]);
    ASSERT_EQ('[', buf[1]);
    ASSERT_EQ('M', buf[2]);
    ASSERT_EQ(32, (unsigned char)buf[3]); /* button 0 + 32 */
    ASSERT_EQ(10 + 33, (unsigned char)buf[4]); /* col + 33 */
    ASSERT_EQ(5 + 33, (unsigned char)buf[5]);  /* row + 33 */
    PASS();
}

SUITE(red_mouse_encoding) {
    RUN_TEST(red_mouse_sgr_left_press);
    RUN_TEST(red_mouse_sgr_left_release);
    RUN_TEST(red_mouse_sgr_right_press);
    RUN_TEST(red_mouse_sgr_middle_press);
    RUN_TEST(red_mouse_sgr_shift);
    RUN_TEST(red_mouse_sgr_ctrl);
    RUN_TEST(red_mouse_sgr_scroll_up);
    RUN_TEST(red_mouse_sgr_scroll_down);
    RUN_TEST(red_mouse_sgr_large_coordinates);
    RUN_TEST(red_mouse_legacy_left_press);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_mouse_encoding);
    GREATEST_MAIN_END();
}
