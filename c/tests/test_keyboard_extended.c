/* test_keyboard_extended.c — More keyboard encoding edge cases */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"

TEST key_f1_normal(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x70, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1bOP", buf); /* F1 = SS3 P */
    PASS();
}

TEST key_f5_normal(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x74, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[15~", buf); /* F5 */
    PASS();
}

TEST key_home(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x24, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[H", buf);
    PASS();
}

TEST key_end(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x23, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[F", buf);
    PASS();
}

TEST key_insert(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x2D, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[2~", buf);
    PASS();
}

TEST key_delete(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x2E, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[3~", buf);
    PASS();
}

TEST key_pageup(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x21, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[5~", buf);
    PASS();
}

TEST key_pagedown(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x22, false, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[6~", buf);
    PASS();
}

TEST key_ctrl_a(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key('A', false, true, false, false, buf, sizeof(buf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(1, (int)(unsigned char)buf[0]); /* Ctrl+A = SOH */
    PASS();
}

TEST key_ctrl_z(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key('Z', false, true, false, false, buf, sizeof(buf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(26, (int)(unsigned char)buf[0]); /* Ctrl+Z = SUB */
    PASS();
}

TEST key_alt_a(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key('A', false, false, true, false, buf, sizeof(buf));
    ASSERT_EQ(2, (int)n);
    ASSERT_EQ('\x1b', buf[0]);
    ASSERT_EQ('a', buf[1]); /* Alt sends ESC + lowercase */
    PASS();
}

TEST key_escape(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x1B, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\x1b', buf[0]);
    PASS();
}

TEST key_tab(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x09, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\t', buf[0]);
    PASS();
}

TEST key_shift_tab(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x09, true, false, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1b[Z", buf); /* Backtab */
    PASS();
}

TEST key_enter(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x0D, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ('\r', buf[0]);
    PASS();
}

TEST key_backspace(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x08, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(0x7F, (int)(unsigned char)buf[0]); /* DEL */
    PASS();
}

TEST key_arrow_up_app_mode(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x26, false, false, false, true, buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_STR_EQ("\x1bOA", buf); /* Application cursor mode */
    PASS();
}

TEST key_unrecognized_vk(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0xFF, false, false, false, false, buf, sizeof(buf));
    ASSERT_EQ(0, (int)n); /* Unknown VK produces nothing */
    PASS();
}

TEST key_ctrl_shift_arrow(void) {
    char buf[32] = {0};
    size_t n = wixen_encode_key(0x26, true, true, false, false, buf, sizeof(buf));
    ASSERT(n > 0);
    /* Ctrl+Shift+Up = ESC[1;6A (modifier 6) */
    ASSERT_STR_EQ("\x1b[1;6A", buf);
    PASS();
}

SUITE(keyboard_extended) {
    RUN_TEST(key_f1_normal);
    RUN_TEST(key_f5_normal);
    RUN_TEST(key_home);
    RUN_TEST(key_end);
    RUN_TEST(key_insert);
    RUN_TEST(key_delete);
    RUN_TEST(key_pageup);
    RUN_TEST(key_pagedown);
    RUN_TEST(key_ctrl_a);
    RUN_TEST(key_ctrl_z);
    RUN_TEST(key_alt_a);
    RUN_TEST(key_escape);
    RUN_TEST(key_tab);
    RUN_TEST(key_shift_tab);
    RUN_TEST(key_enter);
    RUN_TEST(key_backspace);
    RUN_TEST(key_arrow_up_app_mode);
    RUN_TEST(key_unrecognized_vk);
    RUN_TEST(key_ctrl_shift_arrow);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(keyboard_extended);
    GREATEST_MAIN_END();
}
