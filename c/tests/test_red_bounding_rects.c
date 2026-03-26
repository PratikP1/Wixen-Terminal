/* test_red_bounding_rects.c — RED tests for text range bounding rectangles
 *
 * GetBoundingRectangles should return screen-space rectangles for a text range.
 * Used by screen readers to highlight text and by Narrator for reading position.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/text_boundaries.h"

/* Bounding rect calculation: given a text offset range and cell dimensions,
 * compute the screen rectangles. This is pure math — no Win32 needed. */

typedef struct {
    float x, y, width, height;
} WixenTextRect;

/* Compute bounding rect for a single-line range */
static WixenTextRect compute_single_line_rect(
    size_t col_start, size_t col_end, size_t row,
    float cell_width, float cell_height,
    float origin_x, float origin_y) {
    WixenTextRect r;
    r.x = origin_x + col_start * cell_width;
    r.y = origin_y + row * cell_height;
    r.width = (col_end - col_start) * cell_width;
    r.height = cell_height;
    return r;
}

TEST red_rect_single_line(void) {
    /* Range covers cols 5-10 on row 3, cell_width=8, cell_height=16 */
    WixenTextRect r = compute_single_line_rect(5, 10, 3, 8.0f, 16.0f, 0, 0);
    ASSERT(r.x > 39.0f && r.x < 41.0f);   /* 5*8 = 40 */
    ASSERT(r.y > 47.0f && r.y < 49.0f);   /* 3*16 = 48 */
    ASSERT(r.width > 39.0f && r.width < 41.0f); /* 5*8 = 40 */
    ASSERT(r.height > 15.0f && r.height < 17.0f); /* 16 */
    PASS();
}

TEST red_rect_with_origin(void) {
    /* Same but with window origin at (100, 200) */
    WixenTextRect r = compute_single_line_rect(0, 5, 0, 8.0f, 16.0f, 100.0f, 200.0f);
    ASSERT(r.x > 99.0f && r.x < 101.0f);  /* 100 + 0 */
    ASSERT(r.y > 199.0f && r.y < 201.0f);  /* 200 + 0 */
    PASS();
}

TEST red_rect_zero_width(void) {
    /* Degenerate range (caret position) */
    WixenTextRect r = compute_single_line_rect(5, 5, 0, 8.0f, 16.0f, 0, 0);
    ASSERT(r.width < 0.1f); /* Zero width */
    ASSERT(r.height > 15.0f); /* Still has height */
    PASS();
}

/* Multi-line: offset to row/col using text_boundaries */
TEST red_rect_offset_to_rowcol(void) {
    const char *text = "Hello\nWorld\nFoo";
    size_t row, col;
    wixen_text_offset_to_rowcol(text, strlen(text), 8, &row, &col);
    /* Offset 8 = "Wo" in "World" → row 1, col 2 */
    ASSERT_EQ(1, (int)row);
    ASSERT_EQ(2, (int)col);
    PASS();
}

TEST red_rect_rowcol_roundtrip(void) {
    const char *text = "Line0\nLine1\nLine2";
    /* Row 2, col 3 → should be at "e" in "Line2" */
    size_t offset = wixen_text_rowcol_to_offset(text, strlen(text), 2, 3);
    /* "Line0\nLine1\n" = 12 chars, then "Lin" = 3 more → offset 15 */
    ASSERT_EQ(15, (int)offset);
    /* Round-trip back */
    size_t row, col;
    wixen_text_offset_to_rowcol(text, strlen(text), offset, &row, &col);
    ASSERT_EQ(2, (int)row);
    ASSERT_EQ(3, (int)col);
    PASS();
}

SUITE(red_bounding_rects) {
    RUN_TEST(red_rect_single_line);
    RUN_TEST(red_rect_with_origin);
    RUN_TEST(red_rect_zero_width);
    RUN_TEST(red_rect_offset_to_rowcol);
    RUN_TEST(red_rect_rowcol_roundtrip);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_bounding_rects);
    GREATEST_MAIN_END();
}
