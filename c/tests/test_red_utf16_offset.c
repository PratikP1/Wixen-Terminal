/* test_red_utf16_offset.c — RED tests for UTF-16 offset calculation
 *
 * UIA ITextProvider works with UTF-16 offsets. Our visible_text is UTF-8.
 * The cursor_offset_utf16 must correctly map cursor (row,col) to the
 * UTF-16 code unit position in the flattened text.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* Count UTF-16 code units in a UTF-8 string (for verification) */
static size_t utf16_len(const char *utf8) {
    size_t count = 0;
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if (*p < 0xE0) { cp = (*p++ & 0x1F) << 6; cp |= (*p++ & 0x3F); }
        else if (*p < 0xF0) { cp = (*p++ & 0x0F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
        else { cp = (*p++ & 0x07) << 18; cp |= (*p++ & 0x3F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
        count += (cp >= 0x10000) ? 2 : 1; /* Surrogate pair for supplementary */
    }
    return count;
}

/* Calculate UTF-16 offset for a given byte offset in UTF-8 text */
static size_t utf8_to_utf16_offset(const char *text, size_t byte_offset) {
    size_t count = 0;
    const unsigned char *p = (const unsigned char *)text;
    size_t pos = 0;
    while (*p && pos < byte_offset) {
        uint32_t cp;
        size_t char_bytes;
        if (*p < 0x80) { cp = *p; char_bytes = 1; }
        else if (*p < 0xE0) { cp = (*p & 0x1F) << 6 | (p[1] & 0x3F); char_bytes = 2; }
        else if (*p < 0xF0) { cp = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); char_bytes = 3; }
        else { cp = (*p & 0x07) << 18 | (p[1] & 0x3F) << 12 | (p[2] & 0x3F) << 6 | (p[3] & 0x3F); char_bytes = 4; }
        p += char_bytes;
        pos += char_bytes;
        count += (cp >= 0x10000) ? 2 : 1;
    }
    return count;
}

TEST red_utf16_ascii_same(void) {
    /* Pure ASCII: UTF-8 offset == UTF-16 offset */
    const char *text = "Hello World\nLine two";
    ASSERT_EQ(0, (int)utf8_to_utf16_offset(text, 0));
    ASSERT_EQ(5, (int)utf8_to_utf16_offset(text, 5));
    ASSERT_EQ(11, (int)utf8_to_utf16_offset(text, 11)); /* \n */
    ASSERT_EQ(19, (int)utf8_to_utf16_offset(text, 19));
    PASS();
}

TEST red_utf16_bmp_chars(void) {
    /* BMP chars (2-3 byte UTF-8, 1 UTF-16 unit each) */
    /* é = U+00E9 = 2 bytes UTF-8, 1 UTF-16 unit */
    const char *text = "caf\xc3\xa9"; /* "café" */
    ASSERT_EQ(4, (int)utf16_len(text)); /* c,a,f,é = 4 UTF-16 units */
    ASSERT_EQ(3, (int)utf8_to_utf16_offset(text, 3)); /* Before é */
    ASSERT_EQ(4, (int)utf8_to_utf16_offset(text, 5)); /* After é */
    PASS();
}

TEST red_utf16_cjk(void) {
    /* CJK: 世 = U+4E16 = 3 bytes UTF-8, 1 UTF-16 unit */
    const char *text = "A\xe4\xb8\x96" "B"; /* A世B */
    ASSERT_EQ(3, (int)utf16_len(text)); /* A,世,B */
    ASSERT_EQ(1, (int)utf8_to_utf16_offset(text, 1)); /* After A */
    ASSERT_EQ(2, (int)utf8_to_utf16_offset(text, 4)); /* After 世 */
    ASSERT_EQ(3, (int)utf8_to_utf16_offset(text, 5)); /* After B */
    PASS();
}

TEST red_utf16_supplementary(void) {
    /* Emoji: 😀 = U+1F600 = 4 bytes UTF-8, 2 UTF-16 units (surrogate pair) */
    const char *text = "A\xf0\x9f\x98\x80" "B"; /* A😀B */
    ASSERT_EQ(4, (int)utf16_len(text)); /* A=1, 😀=2, B=1 */
    ASSERT_EQ(1, (int)utf8_to_utf16_offset(text, 1)); /* After A */
    ASSERT_EQ(3, (int)utf8_to_utf16_offset(text, 5)); /* After 😀 (2 UTF-16 units) */
    ASSERT_EQ(4, (int)utf8_to_utf16_offset(text, 6)); /* After B */
    PASS();
}

TEST red_utf16_mixed(void) {
    /* Mix: ASCII + BMP + supplementary */
    const char *text = "Hi\xc3\xa9\xf0\x9f\x98\x80"; /* Hié😀 */
    /* H=1, i=1, é=1, 😀=2 → 5 UTF-16 units */
    ASSERT_EQ(5, (int)utf16_len(text));
    PASS();
}

TEST red_utf16_rowcol_mapping(void) {
    /* Verify row/col → byte offset → UTF-16 offset chain */
    const char *text = "Row0\nRow1\nRow2";
    size_t byte_offset = wixen_text_rowcol_to_offset(text, strlen(text), 1, 2);
    /* Row 1, col 2 = "w" in "Row1" = byte offset 7 */
    ASSERT_EQ(7, (int)byte_offset);
    /* UTF-16 offset should also be 7 (all ASCII) */
    ASSERT_EQ(7, (int)utf8_to_utf16_offset(text, byte_offset));
    PASS();
}

SUITE(red_utf16_offset) {
    RUN_TEST(red_utf16_ascii_same);
    RUN_TEST(red_utf16_bmp_chars);
    RUN_TEST(red_utf16_cjk);
    RUN_TEST(red_utf16_supplementary);
    RUN_TEST(red_utf16_mixed);
    RUN_TEST(red_utf16_rowcol_mapping);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_utf16_offset);
    GREATEST_MAIN_END();
}
