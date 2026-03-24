/* test_mouse.c — Mouse encoding tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/mouse.h"

TEST mouse_none_mode_no_output(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NONE, false,
        WIXEN_MBTN_LEFT, 10, 5, false, false, true, buf, sizeof(buf));
    ASSERT_EQ(0, (int)n);
    PASS();
}

TEST mouse_x10_press_only(void) {
    char buf[32] = {0};
    /* Press reports */
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_X10, false,
        WIXEN_MBTN_LEFT, 0, 0, false, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Release doesn't report in X10 mode */
    n = wixen_encode_mouse(WIXEN_MOUSE_X10, false,
        WIXEN_MBTN_RELEASE, 0, 0, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(0, (int)n);
    PASS();
}

TEST mouse_legacy_left_press(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, false,
        WIXEN_MBTN_LEFT, 0, 0, false, false, true, buf, sizeof(buf));
    ASSERT_EQ(6, (int)n);
    ASSERT_EQ('\x1b', buf[0]);
    ASSERT_EQ('[', buf[1]);
    ASSERT_EQ('M', buf[2]);
    ASSERT_EQ(32, (int)(unsigned char)buf[3]);  /* button 0 + 32 */
    ASSERT_EQ(33, (int)(unsigned char)buf[4]);  /* col 0 + 33 */
    ASSERT_EQ(33, (int)(unsigned char)buf[5]);  /* row 0 + 33 */
    PASS();
}

TEST mouse_legacy_right_press(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, false,
        WIXEN_MBTN_RIGHT, 5, 10, false, false, true, buf, sizeof(buf));
    ASSERT_EQ(6, (int)n);
    ASSERT_EQ(34, (int)(unsigned char)buf[3]);  /* button 2 + 32 */
    ASSERT_EQ(38, (int)(unsigned char)buf[4]);  /* col 5 + 33 */
    ASSERT_EQ(43, (int)(unsigned char)buf[5]);  /* row 10 + 33 */
    PASS();
}

TEST mouse_sgr_left_press(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_LEFT, 10, 20, false, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[<0;11;21M", buf);
    PASS();
}

TEST mouse_sgr_left_release(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_LEFT, 10, 20, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[<0;11;21m", buf);  /* lowercase m for release */
    PASS();
}

TEST mouse_sgr_wheel_up(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_WHEEL_UP, 5, 5, false, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[<64;6;6M", buf);
    PASS();
}

TEST mouse_sgr_with_shift(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_LEFT, 0, 0, true, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[<4;1;1M", buf);  /* shift adds 4 */
    PASS();
}

TEST mouse_sgr_with_ctrl(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_LEFT, 0, 0, false, true, true, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[<16;1;1M", buf);  /* ctrl adds 16 */
    PASS();
}

TEST mouse_legacy_overflow(void) {
    char buf[32] = {0};
    /* Legacy can't encode beyond col/row 222 */
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, false,
        WIXEN_MBTN_LEFT, 250, 250, false, false, true, buf, sizeof(buf));
    ASSERT_EQ(0, (int)n);
    PASS();
}

TEST mouse_sgr_large_coords(void) {
    char buf[32] = {0};
    /* SGR can handle large coords */
    size_t n = wixen_encode_mouse(WIXEN_MOUSE_NORMAL, true,
        WIXEN_MBTN_LEFT, 250, 250, false, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[<0;251;251M", buf);
    PASS();
}

SUITE(mouse_tests) {
    RUN_TEST(mouse_none_mode_no_output);
    RUN_TEST(mouse_x10_press_only);
    RUN_TEST(mouse_legacy_left_press);
    RUN_TEST(mouse_legacy_right_press);
    RUN_TEST(mouse_sgr_left_press);
    RUN_TEST(mouse_sgr_left_release);
    RUN_TEST(mouse_sgr_wheel_up);
    RUN_TEST(mouse_sgr_with_shift);
    RUN_TEST(mouse_sgr_with_ctrl);
    RUN_TEST(mouse_legacy_overflow);
    RUN_TEST(mouse_sgr_large_coords);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(mouse_tests);
    GREATEST_MAIN_END();
}
