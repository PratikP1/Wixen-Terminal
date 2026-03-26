/* test_red_findtext_large.c -- RED tests for P0.5: FindText truncation
 *
 * range_FindText() uses a fixed char full[8192] buffer which truncates
 * accessible text for large terminals, breaking screen reader search.
 *
 * These tests exercise wixen_text_find() with buffers larger than 8192
 * bytes to prove the pure-logic layer handles them correctly -- the
 * COM-level fix must pass the full dynamic buffer to wixen_text_find().
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* Helper: build a text buffer of exactly `size` bytes (filled with 'A',
 * with a sentinel needle placed at `needle_offset`). */
static char *make_large_text(size_t size, const char *needle, size_t needle_offset) {
    char *buf = (char *)malloc(size + 1);
    if (!buf) return NULL;
    memset(buf, 'A', size);
    if (needle && needle_offset + strlen(needle) <= size) {
        memcpy(buf + needle_offset, needle, strlen(needle));
    }
    buf[size] = '\0';
    return buf;
}

/* --- Test: find text past the 8192 byte boundary --- */

TEST red_findtext_large_past_8192(void) {
    /* Create a 16384-byte buffer with "NEEDLE" placed at byte 10000 */
    const size_t total = 16384;
    const char *needle = "NEEDLE";
    const size_t needle_off = 10000;
    char *text = make_large_text(total, needle, needle_off);
    ASSERT(text != NULL);

    size_t found_start, found_end;
    bool ok = wixen_text_find(text, total, needle,
        0, false, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ((int)needle_off, (int)found_start);
    ASSERT_EQ((int)(needle_off + strlen(needle)), (int)found_end);

    free(text);
    PASS();
}

/* --- Test: find text right at the 8192 boundary --- */

TEST red_findtext_large_at_boundary(void) {
    /* Place "BOUNDARY" straddling offset 8190..8198 */
    const size_t total = 12000;
    const char *needle = "BOUNDARY";
    const size_t needle_off = 8190;
    char *text = make_large_text(total, needle, needle_off);
    ASSERT(text != NULL);

    size_t found_start, found_end;
    bool ok = wixen_text_find(text, total, needle,
        0, false, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ((int)needle_off, (int)found_start);
    ASSERT_EQ((int)(needle_off + strlen(needle)), (int)found_end);

    free(text);
    PASS();
}

/* --- Test: search near end of large snapshot succeeds --- */

TEST red_findtext_large_near_end(void) {
    const size_t total = 20000;
    const char *needle = "ENDMARKER";
    const size_t needle_off = total - strlen("ENDMARKER"); /* last 9 bytes */
    char *text = make_large_text(total, needle, needle_off);
    ASSERT(text != NULL);

    size_t found_start, found_end;
    bool ok = wixen_text_find(text, total, needle,
        0, false, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ((int)needle_off, (int)found_start);
    ASSERT_EQ((int)total, (int)found_end);

    free(text);
    PASS();
}

/* --- Test: empty text snapshot --- */

TEST red_findtext_empty_text(void) {
    size_t found_start, found_end;
    bool ok = wixen_text_find("", 0, "anything",
        0, false, false, &found_start, &found_end);
    ASSERT_FALSE(ok);
    PASS();
}

/* --- Test: multibyte UTF-8 content across the 8192 boundary --- */

TEST red_findtext_utf8_across_boundary(void) {
    /* Build a buffer where multibyte chars straddle byte 8192.
     * Use 2-byte UTF-8 chars (e.g. U+00E9 = 0xC3 0xA9 = "e with acute").
     * Fill 8190 bytes of 'A', then place the 2-byte char at 8190..8191,
     * then the needle "FOUND" at 8192. Total > 8192. */
    const size_t prefix = 8190;
    const size_t needle_off = prefix + 2; /* after the multibyte char */
    const char *needle = "FOUND";
    const size_t total = needle_off + strlen(needle) + 100;
    char *text = (char *)malloc(total + 1);
    ASSERT(text != NULL);
    memset(text, 'A', total);
    /* Place a 2-byte UTF-8 character (U+00E9) at 8190 */
    unsigned char *utext = (unsigned char *)text;
    utext[prefix]     = 0xC3;
    utext[prefix + 1] = 0xA9;
    /* Place needle right after */
    memcpy(text + needle_off, needle, strlen(needle));
    text[total] = '\0';

    size_t found_start, found_end;
    bool ok = wixen_text_find(text, total, needle,
        0, false, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ((int)needle_off, (int)found_start);
    ASSERT_EQ((int)(needle_off + strlen(needle)), (int)found_end);

    free(text);
    PASS();
}

/* --- Test: search miss in large buffer --- */

TEST red_findtext_large_miss(void) {
    const size_t total = 16384;
    char *text = make_large_text(total, NULL, 0); /* All 'A's, no needle */
    ASSERT(text != NULL);

    size_t found_start, found_end;
    bool ok = wixen_text_find(text, total, "NOTHERE",
        0, false, false, &found_start, &found_end);
    ASSERT_FALSE(ok);

    free(text);
    PASS();
}

/* --- Test: backward search in large buffer past 8192 --- */

TEST red_findtext_large_backward(void) {
    const size_t total = 16384;
    const char *needle = "REVERSE";
    const size_t needle_off = 12000;
    char *text = make_large_text(total, needle, needle_off);
    ASSERT(text != NULL);

    size_t found_start, found_end;
    /* Search backward from the end */
    bool ok = wixen_text_find(text, total, needle,
        total, true, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ((int)needle_off, (int)found_start);
    ASSERT_EQ((int)(needle_off + strlen(needle)), (int)found_end);

    free(text);
    PASS();
}

/* --- Test: case-insensitive search past 8192 --- */

TEST red_findtext_large_case_insensitive(void) {
    const size_t total = 16384;
    const char *placed = "CaSeFoLd";
    const size_t needle_off = 9500;
    char *text = make_large_text(total, placed, needle_off);
    ASSERT(text != NULL);

    size_t found_start, found_end;
    bool ok = wixen_text_find(text, total, "casefold",
        0, false, true, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ((int)needle_off, (int)found_start);
    ASSERT_EQ((int)(needle_off + strlen(placed)), (int)found_end);

    free(text);
    PASS();
}

SUITE(red_findtext_large) {
    RUN_TEST(red_findtext_large_past_8192);
    RUN_TEST(red_findtext_large_at_boundary);
    RUN_TEST(red_findtext_large_near_end);
    RUN_TEST(red_findtext_empty_text);
    RUN_TEST(red_findtext_utf8_across_boundary);
    RUN_TEST(red_findtext_large_miss);
    RUN_TEST(red_findtext_large_backward);
    RUN_TEST(red_findtext_large_case_insensitive);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_findtext_large);
    GREATEST_MAIN_END();
}
