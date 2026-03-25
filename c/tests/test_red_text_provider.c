/* test_red_text_provider.c — RED tests for a11y text provider logic
 *
 * The UIA ITextProvider needs to find line boundaries, word boundaries,
 * and character positions within the terminal text. These are used by
 * NVDA for ExpandToEnclosingUnit(Line/Word/Character).
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* === Line boundary detection === */

TEST red_line_boundaries_simple(void) {
    const char *text = "Line one\nLine two\nLine three";
    size_t start, end;
    /* At offset 0 — should find line "Line one" */
    wixen_text_line_at(text, strlen(text), 0, &start, &end);
    ASSERT_EQ(0, (int)start);
    ASSERT_EQ(8, (int)end); /* "Line one" = 8 chars */
    PASS();
}

TEST red_line_boundaries_middle(void) {
    const char *text = "AAA\nBBBB\nCC";
    size_t start, end;
    /* At offset 5 (inside "BBBB") */
    wixen_text_line_at(text, strlen(text), 5, &start, &end);
    ASSERT_EQ(4, (int)start); /* After first \n */
    ASSERT_EQ(8, (int)end);   /* "BBBB" = 4 chars at offset 4-7 */
    PASS();
}

TEST red_line_boundaries_last_line(void) {
    const char *text = "AAA\nBBB";
    size_t start, end;
    /* At offset 6 (inside "BBB") */
    wixen_text_line_at(text, strlen(text), 6, &start, &end);
    ASSERT_EQ(4, (int)start);
    ASSERT_EQ(7, (int)end);
    PASS();
}

TEST red_line_boundaries_at_newline(void) {
    const char *text = "AAA\nBBB";
    size_t start, end;
    /* At the \n itself (offset 3) — belongs to first line */
    wixen_text_line_at(text, strlen(text), 3, &start, &end);
    ASSERT_EQ(0, (int)start);
    /* End could be 3 or 4 depending on whether \n is included */
    ASSERT(end >= 3 && end <= 4);
    PASS();
}

TEST red_line_boundaries_single_line(void) {
    const char *text = "No newlines here";
    size_t start, end;
    wixen_text_line_at(text, strlen(text), 5, &start, &end);
    ASSERT_EQ(0, (int)start);
    ASSERT_EQ(strlen(text), end);
    PASS();
}

TEST red_line_boundaries_empty(void) {
    size_t start, end;
    wixen_text_line_at("", 0, 0, &start, &end);
    ASSERT_EQ(0, (int)start);
    ASSERT_EQ(0, (int)end);
    PASS();
}

/* === Word boundary detection === */

TEST red_word_boundaries_simple(void) {
    const char *text = "hello world foo";
    size_t start, end;
    /* At offset 0 — inside "hello" */
    wixen_text_word_at(text, strlen(text), 0, &start, &end);
    ASSERT_EQ(0, (int)start);
    ASSERT_EQ(5, (int)end);
    PASS();
}

TEST red_word_boundaries_middle(void) {
    const char *text = "hello world foo";
    size_t start, end;
    /* At offset 7 — inside "world" */
    wixen_text_word_at(text, strlen(text), 7, &start, &end);
    ASSERT_EQ(6, (int)start);
    ASSERT_EQ(11, (int)end);
    PASS();
}

TEST red_word_at_space(void) {
    const char *text = "hello world";
    size_t start, end;
    /* At offset 5 — the space between words */
    wixen_text_word_at(text, strlen(text), 5, &start, &end);
    /* Space is its own "word" or belongs to previous word */
    ASSERT(start <= 5);
    ASSERT(end >= 6);
    PASS();
}

/* === Character offset to row/col mapping === */

TEST red_offset_to_position(void) {
    const char *text = "ABCDE\nFGHIJ\nKLMNO";
    size_t row, col;
    /* Offset 0 → row 0, col 0 */
    wixen_text_offset_to_rowcol(text, strlen(text), 0, &row, &col);
    ASSERT_EQ(0, (int)row);
    ASSERT_EQ(0, (int)col);
    /* Offset 7 → row 1, col 1 (F=6, G=7) */
    wixen_text_offset_to_rowcol(text, strlen(text), 7, &row, &col);
    ASSERT_EQ(1, (int)row);
    ASSERT_EQ(1, (int)col);
    /* Offset 14 → row 2, col 2 */
    wixen_text_offset_to_rowcol(text, strlen(text), 14, &row, &col);
    ASSERT_EQ(2, (int)row);
    ASSERT_EQ(2, (int)col);
    PASS();
}

TEST red_rowcol_to_offset(void) {
    const char *text = "ABCDE\nFGHIJ\nKLMNO";
    size_t offset;
    offset = wixen_text_rowcol_to_offset(text, strlen(text), 0, 0);
    ASSERT_EQ(0, (int)offset);
    offset = wixen_text_rowcol_to_offset(text, strlen(text), 1, 1);
    ASSERT_EQ(7, (int)offset);
    offset = wixen_text_rowcol_to_offset(text, strlen(text), 2, 2);
    ASSERT_EQ(14, (int)offset);
    PASS();
}

SUITE(red_text_provider) {
    RUN_TEST(red_line_boundaries_simple);
    RUN_TEST(red_line_boundaries_middle);
    RUN_TEST(red_line_boundaries_last_line);
    RUN_TEST(red_line_boundaries_at_newline);
    RUN_TEST(red_line_boundaries_single_line);
    RUN_TEST(red_line_boundaries_empty);
    RUN_TEST(red_word_boundaries_simple);
    RUN_TEST(red_word_boundaries_middle);
    RUN_TEST(red_word_at_space);
    RUN_TEST(red_offset_to_position);
    RUN_TEST(red_rowcol_to_offset);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_text_provider);
    GREATEST_MAIN_END();
}
