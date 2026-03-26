/* test_red_utf16_deep_audit.c — Deep audit: ALL UIA-facing offset/length paths
 *                                must use UTF-16 code units, not UTF-8 byte counts.
 *
 * RED-GREEN TDD: Each test computes the EXPECTED UTF-16 value by hand.
 *
 * This file exercises every conversion path that feeds into UIA:
 *   DocumentRange, GetSelection, GetCaretRange, ExpandToEnclosingUnit(Line),
 *   GetVisibleRanges, FindText, RangeFromPoint, Move, MoveEndpointByUnit.
 *
 * Encoding reference:
 *   "cafe" with e-acute: c(1) a(1) f(1) e(CC81=2 bytes,1 UTF-16) = 5 bytes, 4 UTF-16
 *   BUT we use the precomposed form: "caf\xc3\xa9" = 5 bytes, 4 UTF-16 units
 *   "日本語": each char is 3 UTF-8 bytes, 1 UTF-16 unit = 9 bytes, 3 UTF-16
 *   "🙂" U+1F642: 4 UTF-8 bytes, 2 UTF-16 units (surrogate pair)
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* ================================================================
 * Test 1: DocumentRange length for "cafe" (precomposed e-acute)
 *   UTF-8: c(1) a(1) f(1) e-acute(C3 A9 = 2 bytes) = 5 bytes
 *   UTF-16: c(1) a(1) f(1) e-acute(1) = 4 code units
 * ================================================================ */
TEST doc_range_cafe(void) {
    const char *text = "caf\xc3\xa9"; /* cafe with precomposed e-acute */
    size_t byte_len = strlen(text);
    ASSERT_EQ(5, (int)byte_len); /* Verify our UTF-8 encoding */
    size_t utf16_len = wixen_utf8_to_utf16_offset(text, byte_len);
    ASSERT_EQ(4, (int)utf16_len); /* DocumentRange.end should be 4, not 5 */
    PASS();
}

/* ================================================================
 * Test 2: DocumentRange length for "日本語"
 *   UTF-8: 日(E6 97 A5=3) 本(E6 9C AC=3) 語(E8 AA 9E=3) = 9 bytes
 *   UTF-16: 日(1) 本(1) 語(1) = 3 code units
 * ================================================================ */
TEST doc_range_cjk(void) {
    const char *text = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"; /* 日本語 */
    size_t byte_len = strlen(text);
    ASSERT_EQ(9, (int)byte_len);
    size_t utf16_len = wixen_utf8_to_utf16_offset(text, byte_len);
    ASSERT_EQ(3, (int)utf16_len); /* Not 9 */
    PASS();
}

/* ================================================================
 * Test 3: DocumentRange length for "🙂" (U+1F642)
 *   UTF-8: F0 9F 99 82 = 4 bytes
 *   UTF-16: surrogate pair = 2 code units
 * ================================================================ */
TEST doc_range_emoji(void) {
    const char *text = "\xf0\x9f\x99\x82"; /* 🙂 U+1F642 */
    size_t byte_len = strlen(text);
    ASSERT_EQ(4, (int)byte_len);
    size_t utf16_len = wixen_utf8_to_utf16_offset(text, byte_len);
    ASSERT_EQ(2, (int)utf16_len); /* Surrogate pair, not 4 */
    PASS();
}

/* ================================================================
 * Test 4: GetSelection offset when cursor is after "cafe " on a line
 *   Text: "caf\xc3\xa9 hello"
 *   "cafe " = c(1) a(1) f(1) e-acute(2 bytes) space(1) = 6 bytes
 *   UTF-16 offset after "cafe ": c(1) a(1) f(1) e-acute(1) space(1) = 5
 *   Cursor at byte offset 6 → UTF-16 offset should be 5, not 6
 * ================================================================ */
TEST selection_offset_after_cafe(void) {
    const char *text = "caf\xc3\xa9 hello";
    /* Byte offset of cursor after "cafe " */
    size_t byte_off = 6; /* c(1)+a(1)+f(1)+e-acute(2)+space(1) = 6 */
    size_t utf16_off = wixen_utf8_to_utf16_offset(text, byte_off);
    ASSERT_EQ(5, (int)utf16_off); /* cafe(4) + space(1) = 5, not 6 */
    PASS();
}

/* ================================================================
 * Test 5: GetCaretRange for cursor after CJK characters
 *   Text: "日本語XY"
 *   Cursor after "日本": byte offset = 6, UTF-16 offset = 2
 * ================================================================ */
TEST caret_after_cjk(void) {
    const char *text = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9eXY"; /* 日本語XY */
    /* After 日本: byte 6, UTF-16: 2 */
    size_t utf16_off = wixen_utf8_to_utf16_offset(text, 6);
    ASSERT_EQ(2, (int)utf16_off);
    /* After 日本語: byte 9, UTF-16: 3 */
    utf16_off = wixen_utf8_to_utf16_offset(text, 9);
    ASSERT_EQ(3, (int)utf16_off);
    /* After 日本語X: byte 10, UTF-16: 4 */
    utf16_off = wixen_utf8_to_utf16_offset(text, 10);
    ASSERT_EQ(4, (int)utf16_off);
    PASS();
}

/* ================================================================
 * Test 6: ExpandToEnclosingUnit(Line) on "PS C:\> echo cafe 日本語"
 *   Full text: "PS C:\\> echo caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\nNext line"
 *   Line 0 bytes: "PS C:\> echo cafe 日本語\n"
 *     = P(1) S(1) space(1) C(1) :(1) \(1) >(1) space(1) e(1) c(1) h(1) o(1) space(1)
 *       c(1) a(1) f(1) e-acute(2) space(1) 日(3) 本(3) 語(3) \n(1)
 *     = 13 + 4+1 + 1 + 9 + 1 = 13 ASCII + 2-byte char + space + 9 CJK + newline
 *     byte count = 28, UTF-16 count = 22
 *
 *   Verify that line boundaries found from a mid-line UTF-16 offset
 *   produce correct UTF-16 start/end.
 * ================================================================ */
TEST expand_line_with_nonascii(void) {
    /* "PS C:\> echo cafe 日本語\nNext line" */
    const char *text = "PS C:\\> echo caf\xc3\xa9 "
                        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n"
                        "Next line";
    size_t text_len = strlen(text);

    /* Line 0 byte boundaries */
    size_t line_start, line_end;
    /* Pick a byte offset in line 0 — say offset 5 (the '>') */
    wixen_text_line_at(text, text_len, 5, &line_start, &line_end);
    ASSERT_EQ(0, (int)line_start);
    /* line_end should be at the \n position (not including it) */

    /* Now convert line boundaries to UTF-16 */
    size_t utf16_start = wixen_utf8_to_utf16_offset(text, line_start);
    size_t utf16_end = wixen_utf8_to_utf16_offset(text, line_end);
    ASSERT_EQ(0, (int)utf16_start);

    /* Count expected UTF-16 units for line 0 (excluding \n):
     * "PS C:\> echo " = 13 ASCII = 13 UTF-16
     * "cafe" = c(1)+a(1)+f(1)+e-acute(1) = 4 UTF-16
     * " " = 1 UTF-16
     * "日本語" = 3 UTF-16
     * Total without \n = 13 + 4 + 1 + 3 = 21 */
    ASSERT_EQ(21, (int)utf16_end);

    /* Include newline: line_end + 1 byte = line_end + 1 UTF-16 */
    size_t line_end_with_nl = line_end + 1; /* include \n */
    size_t utf16_end_nl = wixen_utf8_to_utf16_offset(text, line_end_with_nl);
    ASSERT_EQ(22, (int)utf16_end_nl);

    PASS();
}

/* ================================================================
 * Test 7: GetVisibleRanges length matches DocumentRange length
 *   Both should return the same UTF-16 length for the full text.
 *   Text: "caf\xc3\xa9\n\xe6\x97\xa5\n\xf0\x9f\x99\x82"
 *   = "cafe\n日\n🙂"
 *   byte len = 5 + 1 + 3 + 1 + 4 = 14
 *   UTF-16: cafe(4) + \n(1) + 日(1) + \n(1) + 🙂(2) = 9
 * ================================================================ */
TEST visible_range_matches_doc_range(void) {
    const char *text = "caf\xc3\xa9\n\xe6\x97\xa5\n\xf0\x9f\x99\x82";
    size_t byte_len = strlen(text);
    ASSERT_EQ(14, (int)byte_len);
    size_t utf16_len = wixen_utf8_to_utf16_offset(text, byte_len);
    /* Both GetVisibleRanges and DocumentRange use this same conversion */
    ASSERT_EQ(9, (int)utf16_len);
    PASS();
}

/* ================================================================
 * Test 8: Multi-line "cafe\n日本語\n🙂"
 *   Verify total document length in UTF-16 units.
 *   Line 0: "cafe" = c(1)+a(1)+f(1)+e-acute(2 bytes) = 5 bytes, 4 UTF-16
 *   \n = 1 byte, 1 UTF-16
 *   Line 1: "日本語" = 9 bytes, 3 UTF-16
 *   \n = 1 byte, 1 UTF-16
 *   Line 2: "🙂" = 4 bytes, 2 UTF-16
 *   Total: 20 bytes, 11 UTF-16 units
 * ================================================================ */
TEST multiline_total_utf16(void) {
    const char *text = "caf\xc3\xa9\n"
                        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n"
                        "\xf0\x9f\x99\x82";
    size_t byte_len = strlen(text);
    ASSERT_EQ(20, (int)byte_len); /* 5 + 1 + 9 + 1 + 4 */
    size_t utf16_len = wixen_utf8_to_utf16_offset(text, byte_len);
    ASSERT_EQ(11, (int)utf16_len); /* 4 + 1 + 3 + 1 + 2 */
    PASS();
}

/* ================================================================
 * Test 9: RangeFromPoint — verify wixen_text_rowcol_to_offset then
 *         wixen_utf8_to_utf16_offset produces correct UTF-16 offset.
 *   Text: "caf\xc3\xa9\nABC"
 *   Row 0 col 4 → byte offset of character after e-acute
 *   rowcol_to_offset(text, len, 0, 4) should find past the e-acute
 *   Byte offset 5 → UTF-16 offset 4
 *
 *   Row 1 col 2 → byte offset 8 (5 for "cafe" + 1 \n + 2 for "AB")
 *   UTF-16: 4 + 1 + 2 = 7
 * ================================================================ */
TEST range_from_point_utf16(void) {
    const char *text = "caf\xc3\xa9\nABC";
    size_t text_len = strlen(text);

    /* Row 0, col 4: the column counting in rowcol_to_offset counts bytes,
     * not characters. col=4 means 4 bytes from line start.
     * In "caf\xc3\xa9", col 4 = byte offset 4 (middle of e-acute). */
    size_t byte_off = wixen_text_rowcol_to_offset(text, text_len, 0, 4);
    /* byte_off = 4 (within e-acute two-byte sequence) */
    ASSERT_EQ(4, (int)byte_off);

    /* The UTF-16 offset for byte 4 = caf(3 UTF-16) + partial e-acute...
     * wixen_utf8_to_utf16_offset only counts COMPLETE characters.
     * At byte 4 we've consumed c(1)+a(1)+f(1)+first byte of e-acute
     * but the e-acute started at byte 3, so only 3 complete chars → 3 UTF-16.
     * Actually: let's trace: pos=0→c(1byte), pos=1→a(1byte), pos=2→f(1byte),
     * pos=3→e-acute starts at 0xC3 which is 2-byte, so pos=3+2=5.
     * But byte_offset is 4, and 4 < 5, so the loop stops with utf16_count=3.
     * Wait, the loop condition is pos < byte_offset. At pos=3, b=0xC3, char_bytes=2.
     * pos = 3+2 = 5, which is > 4. But the loop already advanced.
     * Actually: pos=3, byte_offset=4: pos(3) < 4, so we process.
     * char_bytes=2, pos = 3+2 = 5. utf16_count = 4. Then pos=5 >= 4, loop exits.
     * So utf16_count = 4. */
    size_t utf16_off = wixen_utf8_to_utf16_offset(text, byte_off);
    /* utf8_to_utf16 counts whole chars whose start is < byte_offset.
     * c starts at 0 (<4), a starts at 1 (<4), f starts at 2 (<4),
     * e-acute starts at 3 (<4), so all 4 chars are counted → 4 UTF-16 units */
    ASSERT_EQ(4, (int)utf16_off);

    /* Row 1, col 2 → byte offset */
    byte_off = wixen_text_rowcol_to_offset(text, text_len, 1, 2);
    ASSERT_EQ(8, (int)byte_off); /* 5("cafe") + 1(\n) + 2("AB") = 8 */
    utf16_off = wixen_utf8_to_utf16_offset(text, byte_off);
    ASSERT_EQ(7, (int)utf16_off); /* 4(cafe) + 1(\n) + 2(AB) = 7 */

    PASS();
}

/* ================================================================
 * Test 10: FindText with non-ASCII needle returns correct UTF-16 range
 *   Text: "hello caf\xc3\xa9 world"
 *   Search for "cafe" (the UTF-8 bytes of "caf\xc3\xa9")
 *   Byte result: start=6, end=11 (5 bytes for "cafe")
 *   UTF-16: start=6, end=10 (4 UTF-16 units for "cafe")
 * ================================================================ */
TEST find_text_nonascii_needle(void) {
    const char *text = "hello caf\xc3\xa9 world";
    size_t text_len = strlen(text);
    const char *needle = "caf\xc3\xa9"; /* "cafe" */

    size_t byte_start, byte_end;
    bool found = wixen_text_find(text, text_len, needle, 0, false, false,
                                  &byte_start, &byte_end);
    ASSERT(found);
    ASSERT_EQ(6, (int)byte_start);  /* "hello " = 6 bytes */
    ASSERT_EQ(11, (int)byte_end);   /* 6 + 5 = 11 */

    /* Convert to UTF-16 */
    size_t utf16_start = wixen_utf8_to_utf16_offset(text, byte_start);
    size_t utf16_end = wixen_utf8_to_utf16_offset(text, byte_end);
    ASSERT_EQ(6, (int)utf16_start);  /* "hello " = 6 UTF-16 units */
    ASSERT_EQ(10, (int)utf16_end);   /* 6 + 4 ("cafe") = 10 */
    PASS();
}

/* ================================================================
 * Test 11: wixen_utf16_to_utf8_offset reverse conversion
 *   Verify the new reverse function works correctly.
 * ================================================================ */
TEST utf16_to_utf8_reverse(void) {
    /* "cafe" = c(1)+a(1)+f(1)+e-acute(2) = 5 bytes, 4 UTF-16 */
    const char *text = "caf\xc3\xa9";
    size_t text_len = strlen(text);
    ASSERT_EQ(0, (int)wixen_utf16_to_utf8_offset(text, text_len, 0));
    ASSERT_EQ(1, (int)wixen_utf16_to_utf8_offset(text, text_len, 1)); /* after c */
    ASSERT_EQ(2, (int)wixen_utf16_to_utf8_offset(text, text_len, 2)); /* after a */
    ASSERT_EQ(3, (int)wixen_utf16_to_utf8_offset(text, text_len, 3)); /* after f */
    ASSERT_EQ(5, (int)wixen_utf16_to_utf8_offset(text, text_len, 4)); /* after e-acute */
    PASS();
}

TEST utf16_to_utf8_reverse_cjk(void) {
    /* "日本語" = 9 bytes, 3 UTF-16 */
    const char *text = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    size_t text_len = strlen(text);
    ASSERT_EQ(0, (int)wixen_utf16_to_utf8_offset(text, text_len, 0));
    ASSERT_EQ(3, (int)wixen_utf16_to_utf8_offset(text, text_len, 1));
    ASSERT_EQ(6, (int)wixen_utf16_to_utf8_offset(text, text_len, 2));
    ASSERT_EQ(9, (int)wixen_utf16_to_utf8_offset(text, text_len, 3));
    PASS();
}

TEST utf16_to_utf8_reverse_emoji(void) {
    /* "A🙂B" = A(1) + 🙂(4) + B(1) = 6 bytes, 4 UTF-16 (1+2+1) */
    const char *text = "A\xf0\x9f\x99\x82" "B";
    size_t text_len = strlen(text);
    ASSERT_EQ(0, (int)wixen_utf16_to_utf8_offset(text, text_len, 0)); /* before A */
    ASSERT_EQ(1, (int)wixen_utf16_to_utf8_offset(text, text_len, 1)); /* after A */
    /* UTF-16 offset 2 is in the middle of surrogate pair — should advance past it */
    /* After the surrogate pair (2 UTF-16 units) we're at byte 5 */
    ASSERT_EQ(5, (int)wixen_utf16_to_utf8_offset(text, text_len, 3)); /* after 🙂 */
    ASSERT_EQ(6, (int)wixen_utf16_to_utf8_offset(text, text_len, 4)); /* after B */
    PASS();
}

/* ================================================================
 * Test 12: Roundtrip: utf8→utf16→utf8 for mixed text
 * ================================================================ */
TEST roundtrip_utf8_utf16(void) {
    const char *text = "caf\xc3\xa9\n\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n\xf0\x9f\x99\x82";
    size_t byte_len = strlen(text);

    /* For each character boundary, verify roundtrip */
    /* Byte offsets of character starts: 0,1,2,3,5,6,9,12,15,16 (20 is end) */
    size_t byte_boundaries[] = {0, 1, 2, 3, 5, 6, 9, 12, 15, 16, 20};
    size_t utf16_expected[] =  {0, 1, 2, 3, 4, 5, 6, 7,  8,  9,  11};
    size_t num = sizeof(byte_boundaries) / sizeof(byte_boundaries[0]);

    for (size_t i = 0; i < num; i++) {
        size_t utf16 = wixen_utf8_to_utf16_offset(text, byte_boundaries[i]);
        ASSERT_EQ((int)utf16_expected[i], (int)utf16);
        /* Reverse: UTF-16 → byte */
        size_t byte_back = wixen_utf16_to_utf8_offset(text, byte_len, utf16);
        ASSERT_EQ((int)byte_boundaries[i], (int)byte_back);
    }
    PASS();
}

/* ================================================================
 * Test 13: Move by line with non-ASCII content
 *   Text: "caf\xc3\xa9\n\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\nend"
 *   Starting at byte 0, move forward 1 line → should land at byte 6 (after "cafe\n")
 *   In UTF-16: start=0, after move=5 (4 for "cafe" + 1 for \n)
 * ================================================================ */
TEST move_line_nonascii(void) {
    const char *text = "caf\xc3\xa9\n"
                        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n"
                        "end";
    size_t text_len = strlen(text);

    /* Simulate what fixed range_Move does: convert UTF-16→byte, move, convert back */
    int utf16_start = 0;
    size_t byte_pos = wixen_utf16_to_utf8_offset(text, text_len, (size_t)utf16_start);
    ASSERT_EQ(0, (int)byte_pos);

    int moved = wixen_text_move_by_line(text, text_len, &byte_pos, 1);
    ASSERT_EQ(1, moved);
    ASSERT_EQ(6, (int)byte_pos); /* Past "cafe\n" */

    int utf16_result = (int)wixen_utf8_to_utf16_offset(text, byte_pos);
    ASSERT_EQ(5, utf16_result); /* 4(cafe) + 1(\n) = 5 */

    /* Move one more line */
    moved = wixen_text_move_by_line(text, text_len, &byte_pos, 1);
    ASSERT_EQ(1, moved);
    ASSERT_EQ(16, (int)byte_pos); /* Past "日本語\n" */
    utf16_result = (int)wixen_utf8_to_utf16_offset(text, byte_pos);
    ASSERT_EQ(9, utf16_result); /* 5 + 3(日本語) + 1(\n) = 9 */

    PASS();
}

/* ================================================================
 * Test 14: FindText with CJK needle
 *   Text: "abc\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9exyz"
 *         = "abc日本語xyz"
 *   Search for "日本語"
 *   Byte: start=3, end=12
 *   UTF-16: start=3, end=6
 * ================================================================ */
TEST find_text_cjk_needle(void) {
    const char *text = "abc\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9exyz";
    size_t text_len = strlen(text);
    const char *needle = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"; /* 日本語 */

    size_t byte_start, byte_end;
    bool found = wixen_text_find(text, text_len, needle, 0, false, false,
                                  &byte_start, &byte_end);
    ASSERT(found);
    ASSERT_EQ(3, (int)byte_start);
    ASSERT_EQ(12, (int)byte_end);

    /* UIA must report UTF-16 offsets */
    size_t utf16_start = wixen_utf8_to_utf16_offset(text, byte_start);
    size_t utf16_end = wixen_utf8_to_utf16_offset(text, byte_end);
    ASSERT_EQ(3, (int)utf16_start);
    ASSERT_EQ(6, (int)utf16_end); /* Not 12 */
    PASS();
}

/* ================================================================
 * Test 15: FindText with emoji needle
 *   Text: "hi\xf0\x9f\x99\x82bye"
 *         = "hi🙂bye"
 *   Search for "🙂"
 *   Byte: start=2, end=6
 *   UTF-16: start=2, end=4 (surrogate pair = 2 units)
 * ================================================================ */
TEST find_text_emoji_needle(void) {
    const char *text = "hi\xf0\x9f\x99\x82" "bye";
    size_t text_len = strlen(text);
    const char *needle = "\xf0\x9f\x99\x82"; /* 🙂 */

    size_t byte_start, byte_end;
    bool found = wixen_text_find(text, text_len, needle, 0, false, false,
                                  &byte_start, &byte_end);
    ASSERT(found);
    ASSERT_EQ(2, (int)byte_start);
    ASSERT_EQ(6, (int)byte_end);

    size_t utf16_start = wixen_utf8_to_utf16_offset(text, byte_start);
    size_t utf16_end = wixen_utf8_to_utf16_offset(text, byte_end);
    ASSERT_EQ(2, (int)utf16_start);
    ASSERT_EQ(4, (int)utf16_end); /* 2 + 2(surrogate pair) = 4 */
    PASS();
}

/* ================================================================
 * Test 16: ExpandToEnclosingUnit(Line) boundary conversion roundtrip
 *   Text: "abc\n\xe6\x97\xa5\xe6\x9c\xac\n\xf0\x9f\x99\x82"
 *   = "abc\n日本\n🙂"
 *   Start at UTF-16 offset 5 (in line 1, which is "日本")
 *   After expand(Line), should get line 1: start=4, end=7
 *   (Line 0 = "abc\n" = 4 UTF-16 units)
 *   (Line 1 = "日本\n" = 3 UTF-16 units → end = 4+3 = 7)
 * ================================================================ */
TEST expand_line_roundtrip(void) {
    const char *text = "abc\n\xe6\x97\xa5\xe6\x9c\xac\n\xf0\x9f\x99\x82";
    size_t text_len = strlen(text);

    /* Simulate ExpandToEnclosingUnit(Line) with UTF-16 start = 5 */
    int utf16_pos = 5; /* In "日本" line, after 日 */
    size_t byte_pos = wixen_utf16_to_utf8_offset(text, text_len, (size_t)utf16_pos);
    /* byte_pos should be 4(\n end of line 0) + 3(日) = 7 */
    ASSERT_EQ(7, (int)byte_pos);

    /* Find line boundaries in byte space */
    size_t line_start, line_end;
    wixen_text_line_at(text, text_len, byte_pos, &line_start, &line_end);
    ASSERT_EQ(4, (int)line_start); /* after "abc\n" */
    ASSERT_EQ(10, (int)line_end);  /* end of "日本" before \n */

    /* Include newline */
    if (line_end < text_len && text[line_end] == '\n') line_end++;
    ASSERT_EQ(11, (int)line_end); /* include \n */

    /* Convert back to UTF-16 */
    int utf16_start = (int)wixen_utf8_to_utf16_offset(text, line_start);
    int utf16_end = (int)wixen_utf8_to_utf16_offset(text, line_end);
    ASSERT_EQ(4, utf16_start);  /* "abc\n" = 4 UTF-16 */
    ASSERT_EQ(7, utf16_end);    /* 4 + 2(日本) + 1(\n) = 7 */

    PASS();
}

/* ================================================================
 * Test 17: MoveEndpointByUnit with non-ASCII
 *   Text: "caf\xc3\xa9 \xe6\x97\xa5"
 *   = "cafe 日"
 *   Start at UTF-16 offset 0, move end by 1 word forward
 *   Should land at end of "cafe" (byte 5, UTF-16 4)
 *   Actually wixen_text_move_endpoint for word skips whitespace then word end.
 *   From byte 0: skip word "caf\xc3\xa9" → byte 5, then skip space → byte 6,
 *   NO wait: move_endpoint for word count>0: skip ws, then skip non-ws
 *   From 0: text[0]='c' not space, so skip non-space: c,a,f,\xc3,\xa9, (space at 5)
 *   Actually \xc3 is not a space char, so it continues. It stops at byte 5 (space).
 *   Wait: the move_endpoint for word > 0 skips whitespace first, then word.
 *   From byte 0: text[0]='c', not space → skip ws does nothing.
 *   Then skip non-space: 'c','a','f',0xc3,0xa9,' ' → stops at byte 5 (the space).
 *   So endpoint = 5 bytes. UTF-16: 4 (cafe).
 *   Hmm, isspace(0xC3) and isspace(0xA9) — both are non-space, so the
 *   byte-level word movement works correctly even with multi-byte chars.
 * ================================================================ */
TEST move_endpoint_word_nonascii(void) {
    const char *text = "caf\xc3\xa9 \xe6\x97\xa5"; /* "cafe 日" */
    size_t text_len = strlen(text);

    /* Simulate: start at UTF-16 0, move endpoint by 1 word */
    size_t byte_pos = wixen_utf16_to_utf8_offset(text, text_len, 0);
    ASSERT_EQ(0, (int)byte_pos);

    int moved = wixen_text_move_endpoint(text, text_len, &byte_pos,
                                          WIXEN_TEXT_UNIT_WORD, 1);
    ASSERT_EQ(1, moved);
    /* Should be at byte 5 (past "cafe", at space) */
    ASSERT_EQ(5, (int)byte_pos);

    /* Convert to UTF-16 */
    int utf16_result = (int)wixen_utf8_to_utf16_offset(text, byte_pos);
    ASSERT_EQ(4, utf16_result); /* cafe = 4 UTF-16 units */

    PASS();
}

/* ================================================================
 * Test 18: Cursor offset computation matching main.c logic
 *   Simulates the cursor offset computation from main.c lines 1162-1173.
 *   Grid row 0: "caf\xc3\xa9" (cafe, 5 bytes, 4 UTF-16)
 *   Grid row 1: "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" (日本語, 9 bytes, 3 UTF-16)
 *   Cursor at row 1, col 3 (after 日)
 *   Expected: row0_utf16(4) + newline(1) + col_utf16 = ?
 *   col 3 means 3 bytes into row 1. In "日本語", byte 3 = after 日.
 *   utf8_to_utf16_offset("日本語", 3) = 1
 *   Total: 4 + 1 + 1 = 6
 * ================================================================ */
TEST cursor_offset_main_logic(void) {
    const char *row0 = "caf\xc3\xa9";     /* "cafe" */
    const char *row1 = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"; /* "日本語" */

    /* Simulate main.c cursor offset logic */
    int32_t utf16_off = 0;

    /* Sum rows before cursor row (row 0) */
    size_t byte_len0 = strlen(row0);
    utf16_off += (int32_t)wixen_utf8_to_utf16_offset(row0, byte_len0) + 1; /* +1 newline */
    ASSERT_EQ(5, utf16_off); /* 4(cafe) + 1(\n) = 5 */

    /* Add cursor column offset in current row */
    size_t cur_col = 3; /* 3 bytes into row 1 = after 日 */
    utf16_off += (int32_t)wixen_utf8_to_utf16_offset(row1, cur_col);
    ASSERT_EQ(6, utf16_off); /* 5 + 1(日) = 6 */

    PASS();
}

/* ================================================================
 * Test 19: Verify wixen_utf8_to_utf16_offset handles NULL and empty
 * ================================================================ */
TEST utf16_null_and_empty(void) {
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset(NULL, 0));
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset(NULL, 5));
    ASSERT_EQ(0, (int)wixen_utf8_to_utf16_offset("", 0));
    ASSERT_EQ(0, (int)wixen_utf16_to_utf8_offset(NULL, 0, 0));
    ASSERT_EQ(0, (int)wixen_utf16_to_utf8_offset(NULL, 0, 5));
    ASSERT_EQ(0, (int)wixen_utf16_to_utf8_offset("", 0, 0));
    PASS();
}

/* ================================================================
 * Test 20: Line boundaries with CJK produce correct byte→UTF16 mapping
 *   Text: "\xe6\x97\xa5\xe6\x9c\xac\n\xe8\xaa\x9e"
 *   = "日本\n語"
 *   Line 0: bytes 0-5 (日本), excluding \n at byte 6
 *   Line 0 UTF-16: 0 to 2
 *   Line 0 with \n: 0 to 3
 * ================================================================ */
TEST line_boundaries_cjk(void) {
    const char *text = "\xe6\x97\xa5\xe6\x9c\xac\n\xe8\xaa\x9e"; /* 日本\n語 */
    size_t text_len = strlen(text);
    ASSERT_EQ(10, (int)text_len);

    size_t line_start, line_end;
    wixen_text_line_at(text, text_len, 0, &line_start, &line_end);
    ASSERT_EQ(0, (int)line_start);
    ASSERT_EQ(6, (int)line_end); /* Before \n */

    size_t utf16_start = wixen_utf8_to_utf16_offset(text, line_start);
    size_t utf16_end = wixen_utf8_to_utf16_offset(text, line_end);
    ASSERT_EQ(0, (int)utf16_start);
    ASSERT_EQ(2, (int)utf16_end); /* 日(1) + 本(1) = 2 */

    /* With newline included */
    size_t utf16_end_nl = wixen_utf8_to_utf16_offset(text, line_end + 1);
    ASSERT_EQ(3, (int)utf16_end_nl); /* 2 + \n(1) = 3 */

    PASS();
}

/* Suites */
SUITE(doc_range_tests) {
    RUN_TEST(doc_range_cafe);
    RUN_TEST(doc_range_cjk);
    RUN_TEST(doc_range_emoji);
}

SUITE(selection_caret_tests) {
    RUN_TEST(selection_offset_after_cafe);
    RUN_TEST(caret_after_cjk);
}

SUITE(expand_line_tests) {
    RUN_TEST(expand_line_with_nonascii);
    RUN_TEST(expand_line_roundtrip);
    RUN_TEST(line_boundaries_cjk);
}

SUITE(visible_range_tests) {
    RUN_TEST(visible_range_matches_doc_range);
    RUN_TEST(multiline_total_utf16);
}

SUITE(find_text_tests) {
    RUN_TEST(find_text_nonascii_needle);
    RUN_TEST(find_text_cjk_needle);
    RUN_TEST(find_text_emoji_needle);
}

SUITE(move_tests) {
    RUN_TEST(move_line_nonascii);
    RUN_TEST(move_endpoint_word_nonascii);
}

SUITE(reverse_conversion_tests) {
    RUN_TEST(utf16_to_utf8_reverse);
    RUN_TEST(utf16_to_utf8_reverse_cjk);
    RUN_TEST(utf16_to_utf8_reverse_emoji);
    RUN_TEST(roundtrip_utf8_utf16);
}

SUITE(cursor_and_edge_tests) {
    RUN_TEST(cursor_offset_main_logic);
    RUN_TEST(utf16_null_and_empty);
    RUN_TEST(range_from_point_utf16);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(doc_range_tests);
    RUN_SUITE(selection_caret_tests);
    RUN_SUITE(expand_line_tests);
    RUN_SUITE(visible_range_tests);
    RUN_SUITE(find_text_tests);
    RUN_SUITE(move_tests);
    RUN_SUITE(reverse_conversion_tests);
    RUN_SUITE(cursor_and_edge_tests);
    GREATEST_MAIN_END();
}
