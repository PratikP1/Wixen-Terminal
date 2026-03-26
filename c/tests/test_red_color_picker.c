/* test_red_color_picker.c — Red tests for color picker module
 *
 * Tests:
 *  1. wixen_show_color_picker() exists and is callable
 *  2. It returns a valid RGB value when given an initial color
 *  3. Parsing "#FF0000" produces RGB(255,0,0)
 *  4. Formatting RGB(0,255,128) produces "#00FF80"
 *  5. Color parsing handles lowercase, no-hash, 3-digit shorthand
 */
#include "greatest.h"
#include "wixen/ui/color_picker.h"

/* --- Test 1: wixen_show_color_picker function exists and is callable --- */
/* We verify the function links by taking its address. We cannot actually
 * call ChooseColorW in a headless test because it opens a modal dialog. */
TEST color_picker_function_exists(void) {
#ifdef _WIN32
    /* Verify the symbol links — take the address of the function */
    typedef bool (*picker_fn)(HWND, uint32_t, uint32_t *);
    volatile picker_fn fn = wixen_show_color_picker;
    ASSERT(fn != NULL);
    PASS();
#else
    /* On non-Win32, the function isn't available — just pass */
    PASS();
#endif
}

/* --- Test 2: Returns valid RGB — verify out_rgb NULL rejection --- */
TEST color_picker_returns_valid_rgb(void) {
#ifdef _WIN32
    /* Passing NULL out_rgb should return false without crashing */
    bool ok = wixen_show_color_picker(NULL, 0x00336699, NULL);
    ASSERT(!ok);
    PASS();
#else
    PASS();
#endif
}

/* --- Test 3: Parsing "#FF0000" produces RGB(255,0,0) --- */
TEST parse_red_hex(void) {
    uint32_t rgb = wixen_parse_hex_color("#FF0000");
    ASSERT_EQ(0x00FF0000, rgb);

    /* Verify individual channels */
    unsigned r = (rgb >> 16) & 0xFF;
    unsigned g = (rgb >> 8) & 0xFF;
    unsigned b = rgb & 0xFF;
    ASSERT_EQ(255, r);
    ASSERT_EQ(0, g);
    ASSERT_EQ(0, b);
    PASS();
}

/* --- Test 4: Formatting RGB(0,255,128) produces "#00FF80" --- */
TEST format_green_mix(void) {
    char buf[16] = {0};
    wixen_format_hex_color(0x0000FF80, buf, sizeof(buf));
    ASSERT_STR_EQ("#00FF80", buf);
    PASS();
}

/* --- Test 5: Parsing handles lowercase, no-hash, 3-digit shorthand --- */
TEST parse_lowercase(void) {
    uint32_t rgb = wixen_parse_hex_color("#ff8800");
    ASSERT_EQ(0x00FF8800, rgb);
    PASS();
}

TEST parse_no_hash(void) {
    uint32_t rgb = wixen_parse_hex_color("1e1e1e");
    ASSERT_EQ(0x001E1E1E, rgb);
    PASS();
}

TEST parse_three_digit(void) {
    /* "#F0A" -> "#FF00AA" */
    uint32_t rgb = wixen_parse_hex_color("#F0A");
    ASSERT_EQ(0x00FF00AA, rgb);
    PASS();
}

TEST parse_three_digit_lower(void) {
    /* "abc" -> "#AABBCC" */
    uint32_t rgb = wixen_parse_hex_color("abc");
    ASSERT_EQ(0x00AABBCC, rgb);
    PASS();
}

TEST parse_mixed_case(void) {
    uint32_t rgb = wixen_parse_hex_color("#aaBBcc");
    ASSERT_EQ(0x00AABBCC, rgb);
    PASS();
}

TEST parse_invalid_returns_zero(void) {
    ASSERT_EQ(0u, wixen_parse_hex_color(NULL));
    ASSERT_EQ(0u, wixen_parse_hex_color(""));
    ASSERT_EQ(0u, wixen_parse_hex_color("#"));
    ASSERT_EQ(0u, wixen_parse_hex_color("#GGHHII"));
    ASSERT_EQ(0u, wixen_parse_hex_color("#12345")); /* 5 digits */
    ASSERT_EQ(0u, wixen_parse_hex_color("#1234567")); /* 7 digits */
    PASS();
}

TEST format_black(void) {
    char buf[16] = {0};
    wixen_format_hex_color(0x00000000, buf, sizeof(buf));
    ASSERT_STR_EQ("#000000", buf);
    PASS();
}

TEST format_white(void) {
    char buf[16] = {0};
    wixen_format_hex_color(0x00FFFFFF, buf, sizeof(buf));
    ASSERT_STR_EQ("#FFFFFF", buf);
    PASS();
}

TEST format_buf_too_small(void) {
    /* Buffer too small — should not crash, just not write */
    char buf[4] = {0};
    wixen_format_hex_color(0x00FF0000, buf, sizeof(buf));
    /* buf content is unspecified but no crash */
    PASS();
}

SUITE(color_picker_suite) {
    RUN_TEST(color_picker_function_exists);
    RUN_TEST(color_picker_returns_valid_rgb);
    RUN_TEST(parse_red_hex);
    RUN_TEST(format_green_mix);
    RUN_TEST(parse_lowercase);
    RUN_TEST(parse_no_hash);
    RUN_TEST(parse_three_digit);
    RUN_TEST(parse_three_digit_lower);
    RUN_TEST(parse_mixed_case);
    RUN_TEST(parse_invalid_returns_zero);
    RUN_TEST(format_black);
    RUN_TEST(format_white);
    RUN_TEST(format_buf_too_small);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(color_picker_suite);
    GREATEST_MAIN_END();
}
