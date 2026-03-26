/* test_red_copilot_p5_text_provider.c — RED tests for Copilot finding P5
 *
 * P5: Text provider methods are stubs/approximate.
 * Move, MoveEndpoint, FindText, RangeFromPoint need real implementations.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* P5a: Word boundaries — find start and end of word */
TEST red_p5_word_boundary_start(void) {
    const char *text = "hello world foo";
    /* At offset 6 (in "world"), word start should be 6 */
    size_t start = wixen_text_word_start(text, strlen(text), 6);
    ASSERT_EQ(6, (int)start);
    PASS();
}

TEST red_p5_word_boundary_end(void) {
    const char *text = "hello world foo";
    /* At offset 6 (in "world"), word end should be 11 */
    size_t end = wixen_text_word_end(text, strlen(text), 6);
    ASSERT_EQ(11, (int)end);
    PASS();
}

TEST red_p5_word_boundary_at_space(void) {
    const char *text = "hello world";
    /* At offset 5 (the space), word start is 5, end is 6 */
    size_t start = wixen_text_word_start(text, strlen(text), 5);
    size_t end = wixen_text_word_end(text, strlen(text), 5);
    ASSERT_EQ(5, (int)start);
    ASSERT_EQ(6, (int)end);
    PASS();
}

/* P5b: Line boundaries */
TEST red_p5_line_boundary_start(void) {
    const char *text = "line0\nline1\nline2";
    /* At offset 8 (in "line1"), line start should be 6 */
    size_t start = wixen_text_line_start(text, strlen(text), 8);
    ASSERT_EQ(6, (int)start);
    PASS();
}

TEST red_p5_line_boundary_end(void) {
    const char *text = "line0\nline1\nline2";
    /* At offset 8 (in "line1"), line end should be 11 */
    size_t end = wixen_text_line_end(text, strlen(text), 8);
    ASSERT_EQ(11, (int)end);
    PASS();
}

/* P5c: Move by word */
TEST red_p5_move_by_word_forward(void) {
    const char *text = "hello world foo bar";
    /* From offset 0, move 2 words forward → should land at "foo" (12) */
    size_t pos = 0;
    int moved = wixen_text_move_by_word(text, strlen(text), &pos, 2);
    ASSERT_EQ(2, moved);
    ASSERT_EQ(12, (int)pos);
    PASS();
}

TEST red_p5_move_by_word_backward(void) {
    const char *text = "hello world foo bar";
    /* From offset 12 ("foo"), move 1 word backward → should land at "world" (6) */
    size_t pos = 12;
    int moved = wixen_text_move_by_word(text, strlen(text), &pos, -1);
    ASSERT_EQ(-1, moved);
    ASSERT_EQ(6, (int)pos);
    PASS();
}

/* P5d: Move by line */
TEST red_p5_move_by_line_forward(void) {
    const char *text = "line0\nline1\nline2\nline3";
    /* From offset 0, move 2 lines forward → should land at line2 (12) */
    size_t pos = 0;
    int moved = wixen_text_move_by_line(text, strlen(text), &pos, 2);
    ASSERT_EQ(2, moved);
    ASSERT_EQ(12, (int)pos);
    PASS();
}

TEST red_p5_move_by_line_backward(void) {
    const char *text = "line0\nline1\nline2\nline3";
    /* From offset 12 (line2), move 1 line backward → line1 (6) */
    size_t pos = 12;
    int moved = wixen_text_move_by_line(text, strlen(text), &pos, -1);
    ASSERT_EQ(-1, moved);
    ASSERT_EQ(6, (int)pos);
    PASS();
}

/* P5e: Move by character */
TEST red_p5_move_by_char_forward(void) {
    const char *text = "ABCDEF";
    size_t pos = 2;
    int moved = wixen_text_move_by_char(text, strlen(text), &pos, 3);
    ASSERT_EQ(3, moved);
    ASSERT_EQ(5, (int)pos);
    PASS();
}

TEST red_p5_move_by_char_at_end(void) {
    const char *text = "ABC";
    size_t pos = 2;
    /* Try to move 5 forward, but only 1 position available */
    int moved = wixen_text_move_by_char(text, strlen(text), &pos, 5);
    ASSERT_EQ(1, moved);
    ASSERT_EQ(3, (int)pos);
    PASS();
}

/* P5f: FindText (substring search in text) */
TEST red_p5_find_text_forward(void) {
    const char *text = "hello beautiful world";
    size_t start, end;
    bool found = wixen_text_find(text, strlen(text), "beautiful", 0, false, false, &start, &end);
    ASSERT(found);
    ASSERT_EQ(6, (int)start);
    ASSERT_EQ(15, (int)end);
    PASS();
}

TEST red_p5_find_text_case_insensitive(void) {
    const char *text = "Hello Beautiful World";
    size_t start, end;
    bool found = wixen_text_find(text, strlen(text), "beautiful", 0, false, true, &start, &end);
    ASSERT(found);
    ASSERT_EQ(6, (int)start);
    ASSERT_EQ(15, (int)end);
    PASS();
}

TEST red_p5_find_text_not_found(void) {
    const char *text = "hello world";
    size_t start, end;
    bool found = wixen_text_find(text, strlen(text), "xyz", 0, false, false, &start, &end);
    ASSERT_FALSE(found);
    PASS();
}

SUITE(red_copilot_p5_text_provider) {
    RUN_TEST(red_p5_word_boundary_start);
    RUN_TEST(red_p5_word_boundary_end);
    RUN_TEST(red_p5_word_boundary_at_space);
    RUN_TEST(red_p5_line_boundary_start);
    RUN_TEST(red_p5_line_boundary_end);
    RUN_TEST(red_p5_move_by_word_forward);
    RUN_TEST(red_p5_move_by_word_backward);
    RUN_TEST(red_p5_move_by_line_forward);
    RUN_TEST(red_p5_move_by_line_backward);
    RUN_TEST(red_p5_move_by_char_forward);
    RUN_TEST(red_p5_move_by_char_at_end);
    RUN_TEST(red_p5_find_text_forward);
    RUN_TEST(red_p5_find_text_case_insensitive);
    RUN_TEST(red_p5_find_text_not_found);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_copilot_p5_text_provider);
    GREATEST_MAIN_END();
}
