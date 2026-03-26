/* test_red_utf16_real.c — RED-GREEN TDD for wixen_utf8_to_utf16_offset
 *
 * This tests the PRODUCTION helper that converts UTF-8 byte offsets
 * to UTF-16 code unit counts. The function lives in text_boundaries.c
 * and is used by state.c, main.c, and text_provider.c to report
 * correct offsets to UIA screen readers.
 *
 * RED phase: wixen_utf8_to_utf16_offset does not exist yet — compile error.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* ================================================================
 * Test 1: ASCII — byte offset == UTF-16 offset
 * ================================================================ */
TEST utf16_real_ascii(void) {
    const char *text = "Hello World";
    /* Every ASCII byte is 1 UTF-16 code unit */
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset(text, 0));
    ASSERT_EQ(5, (int)wixen_utf8_to_utf16_offset(text, 5));
    ASSERT_EQ(11, (int)wixen_utf8_to_utf16_offset(text, 11));
    PASS();
}

/* ================================================================
 * Test 2: Accented chars — "cafe\xcc\x81" ("cafe" + combining accent)
 *         but simpler: "caf\xc3\xa9" = "cafe" where e-acute is 2 bytes
 *         byte offset 3 → UTF-16 offset 3 (before e-acute)
 *         byte offset 5 → UTF-16 offset 4 (after e-acute, 2 UTF-8 bytes = 1 UTF-16 unit)
 * ================================================================ */
TEST utf16_real_accented(void) {
    /* "cafe" with e-acute: c(1) a(1) f(1) e-acute(2 bytes, 1 UTF-16 unit) */
    const char *text = "caf\xc3\xa9";
    ASSERT_EQ(3, (int)wixen_utf8_to_utf16_offset(text, 3)); /* before e-acute */
    ASSERT_EQ(4, (int)wixen_utf8_to_utf16_offset(text, 5)); /* after e-acute */
    PASS();
}

/* ================================================================
 * Test 3: CJK — each char is 3 UTF-8 bytes, 1 UTF-16 unit
 *         "\xe6\xbc\xa2\xe5\xad\x97" = "漢字" (2 CJK chars)
 * ================================================================ */
TEST utf16_real_cjk(void) {
    const char *text = "\xe6\xbc\xa2\xe5\xad\x97"; /* 漢字 */
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset(text, 0));
    ASSERT_EQ(1, (int)wixen_utf8_to_utf16_offset(text, 3)); /* after 漢 (3 bytes → 1 unit) */
    ASSERT_EQ(2, (int)wixen_utf8_to_utf16_offset(text, 6)); /* after 字 (3 bytes → 1 unit) */
    PASS();
}

/* ================================================================
 * Test 4: Emoji — U+1F600 = 4 UTF-8 bytes, 2 UTF-16 units (surrogate pair)
 * ================================================================ */
TEST utf16_real_emoji(void) {
    const char *text = "A\xf0\x9f\x98\x80" "B"; /* A😀B */
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset(text, 0)); /* before A */
    ASSERT_EQ(1, (int)wixen_utf8_to_utf16_offset(text, 1)); /* after A */
    ASSERT_EQ(3, (int)wixen_utf8_to_utf16_offset(text, 5)); /* after 😀 (4 bytes → 2 units) */
    ASSERT_EQ(4, (int)wixen_utf8_to_utf16_offset(text, 6)); /* after B */
    PASS();
}

/* ================================================================
 * Test 5: Mixed ASCII + CJK + emoji
 *         "Hi\xe4\xb8\x96\xf0\x9f\x98\x80" = "Hi世😀"
 *         H(1→1) i(1→1) 世(3→1) 😀(4→2) = 9 bytes, 5 UTF-16 units
 * ================================================================ */
TEST utf16_real_mixed(void) {
    const char *text = "Hi\xe4\xb8\x96\xf0\x9f\x98\x80"; /* Hi世😀 */
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset(text, 0)); /* start */
    ASSERT_EQ(2, (int)wixen_utf8_to_utf16_offset(text, 2)); /* after "Hi" */
    ASSERT_EQ(3, (int)wixen_utf8_to_utf16_offset(text, 5)); /* after 世 */
    ASSERT_EQ(5, (int)wixen_utf8_to_utf16_offset(text, 9)); /* after 😀 (end) */
    PASS();
}

/* ================================================================
 * Test 6: Full cursor offset with non-ASCII rows
 *         Simulates what main.c/state.c must do: compute the UTF-16
 *         offset of a cursor position in multi-line UTF-8 text.
 *
 *         Text: "caf\xc3\xa9\nA\xf0\x9f\x98\x80" = "cafe\nA😀"
 *         Row 0: "cafe" = 5 bytes, 4 UTF-16 units (+1 for \n = 5)
 *         Row 1: "A😀" cursor at col 1 (after A)
 *         Expected: row0 UTF-16 len (4) + newline (1) + col offset (1) = 6
 *
 *         Use wixen_text_rowcol_to_offset to get byte offset, then convert.
 * ================================================================ */
TEST utf16_real_cursor_multiline(void) {
    const char *text = "caf\xc3\xa9\n" "A\xf0\x9f\x98\x80"; /* "cafe\nA😀" */
    /* Row 1, col 1 → byte offset of 'A' + 1 */
    size_t byte_off = wixen_text_rowcol_to_offset(text, strlen(text), 1, 1);
    /* byte_off should be 7 (5 bytes for "cafe" + 1 newline + 1 for 'A') */
    ASSERT_EQ(7, (int)byte_off);
    /* UTF-16 offset: "cafe" = 4 units, \n = 1 unit, A = 1 unit → 6 */
    ASSERT_EQ(6, (int)wixen_utf8_to_utf16_offset(text, byte_off));
    PASS();
}

SUITE(red_utf16_real) {
    RUN_TEST(utf16_real_ascii);
    RUN_TEST(utf16_real_accented);
    RUN_TEST(utf16_real_cjk);
    RUN_TEST(utf16_real_emoji);
    RUN_TEST(utf16_real_mixed);
    RUN_TEST(utf16_real_cursor_multiline);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_utf16_real);
    GREATEST_MAIN_END();
}
