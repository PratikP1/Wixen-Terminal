/* test_red_text_nav.c — RED tests for text provider navigation
 *
 * Tests the pure-logic functions that back ITextRangeProvider methods:
 * Move, MoveEndpointByUnit, FindText, GetBoundingRectangles
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* === Move by character === */

TEST red_move_char_forward(void) {
    const char *text = "Hello\nWorld";
    size_t offset = 0;
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_CHAR, 3);
    ASSERT_EQ(3, moved);
    ASSERT_EQ(3, (int)offset); /* Now at 'l' */
    PASS();
}

TEST red_move_char_backward(void) {
    const char *text = "Hello\nWorld";
    size_t offset = 5;
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_CHAR, -2);
    ASSERT_EQ(-2, moved);
    ASSERT_EQ(3, (int)offset);
    PASS();
}

TEST red_move_char_clamps(void) {
    const char *text = "Hi";
    size_t offset = 1;
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_CHAR, 10);
    ASSERT(moved < 10); /* Clamped */
    ASSERT_EQ(2, (int)offset); /* At end */
    PASS();
}

/* === Move by word === */

TEST red_move_word_forward(void) {
    const char *text = "one two three";
    size_t offset = 0;
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_WORD, 1);
    ASSERT_EQ(1, moved);
    /* Should be at start of "two" (offset 4) */
    ASSERT_EQ(4, (int)offset);
    PASS();
}

TEST red_move_word_backward(void) {
    const char *text = "one two three";
    size_t offset = 8; /* In "three" */
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_WORD, -1);
    ASSERT_EQ(-1, moved);
    ASSERT_EQ(4, (int)offset); /* Start of "two" */
    PASS();
}

/* === Move by line === */

TEST red_move_line_forward(void) {
    const char *text = "Line0\nLine1\nLine2";
    size_t offset = 0;
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_LINE, 2);
    ASSERT_EQ(2, moved);
    /* Should be at start of "Line2" (offset 12) */
    ASSERT_EQ(12, (int)offset);
    PASS();
}

TEST red_move_line_backward(void) {
    const char *text = "Line0\nLine1\nLine2";
    size_t offset = 14; /* In "Line2" */
    int moved = wixen_text_move_by_unit(text, strlen(text),
        &offset, WIXEN_TEXT_UNIT_LINE, -1);
    ASSERT_EQ(-1, moved);
    ASSERT_EQ(6, (int)offset); /* Start of "Line1" */
    PASS();
}

/* === MoveEndpoint === */

TEST red_move_endpoint_expand_word(void) {
    const char *text = "hello world";
    size_t end = 0;
    int moved = wixen_text_move_endpoint(text, strlen(text),
        &end, WIXEN_TEXT_UNIT_WORD, 1);
    ASSERT_EQ(1, moved);
    /* End should be at end of "hello" (offset 5) */
    ASSERT_EQ(5, (int)end);
    PASS();
}

/* === FindText === */

TEST red_find_text_forward(void) {
    const char *text = "Hello World Hello Again";
    size_t found_start, found_end;
    bool ok = wixen_text_find(text, strlen(text), "Hello",
        0, false, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ(0, (int)found_start);
    ASSERT_EQ(5, (int)found_end);
    PASS();
}

TEST red_find_text_second_occurrence(void) {
    const char *text = "Hello World Hello Again";
    size_t found_start, found_end;
    bool ok = wixen_text_find(text, strlen(text), "Hello",
        6, false, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ(12, (int)found_start);
    ASSERT_EQ(17, (int)found_end);
    PASS();
}

TEST red_find_text_backward(void) {
    const char *text = "Hello World Hello Again";
    size_t found_start, found_end;
    bool ok = wixen_text_find(text, strlen(text), "Hello",
        20, true, false, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ(12, (int)found_start);
    ASSERT_EQ(17, (int)found_end);
    PASS();
}

TEST red_find_text_case_insensitive(void) {
    const char *text = "Hello World";
    size_t found_start, found_end;
    bool ok = wixen_text_find(text, strlen(text), "hello",
        0, false, true, &found_start, &found_end);
    ASSERT(ok);
    ASSERT_EQ(0, (int)found_start);
    ASSERT_EQ(5, (int)found_end);
    PASS();
}

TEST red_find_text_not_found(void) {
    const char *text = "Hello World";
    size_t found_start, found_end;
    bool ok = wixen_text_find(text, strlen(text), "xyz",
        0, false, false, &found_start, &found_end);
    ASSERT_FALSE(ok);
    PASS();
}

SUITE(red_text_nav) {
    RUN_TEST(red_move_char_forward);
    RUN_TEST(red_move_char_backward);
    RUN_TEST(red_move_char_clamps);
    RUN_TEST(red_move_word_forward);
    RUN_TEST(red_move_word_backward);
    RUN_TEST(red_move_line_forward);
    RUN_TEST(red_move_line_backward);
    RUN_TEST(red_move_endpoint_expand_word);
    RUN_TEST(red_find_text_forward);
    RUN_TEST(red_find_text_second_occurrence);
    RUN_TEST(red_find_text_backward);
    RUN_TEST(red_find_text_case_insensitive);
    RUN_TEST(red_find_text_not_found);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_text_nav);
    GREATEST_MAIN_END();
}
