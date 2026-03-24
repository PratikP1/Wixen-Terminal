/* test_red_mouse_select.c — RED tests for mouse-driven text selection */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/grid.h"
#include "wixen/core/selection.h"

/* Simulate: user clicks at pixel (x,y), convert to grid (col,row) */
static void pixel_to_cell(float cell_w, float cell_h, int16_t px, int16_t py,
                           size_t *col, size_t *row) {
    *col = (size_t)(px / cell_w);
    *row = (size_t)(py / cell_h);
}

TEST red_mouse_click_selects_nothing(void) {
    /* Single click without drag = no selection */
    WixenSelection sel;
    wixen_selection_init(&sel);
    ASSERT_FALSE(sel.active);
    PASS();
}

TEST red_mouse_drag_selects_range(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    /* Mouse down at (col=2, row=1) */
    wixen_selection_start(&sel, 2, 1, WIXEN_SEL_NORMAL);
    ASSERT(sel.active);
    /* Mouse move to (col=8, row=1) */
    wixen_selection_update(&sel, 8, 1);
    /* Should contain cells 2-8 on row 1 */
    ASSERT(wixen_selection_contains(&sel, 5, 1, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 1, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 5, 0, 80));
    PASS();
}

TEST red_mouse_drag_multiline(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 2, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 10, 5);
    ASSERT_EQ(4, (int)wixen_selection_row_count(&sel));
    ASSERT(wixen_selection_contains(&sel, 0, 3, 80)); /* Middle rows fully selected */
    ASSERT(wixen_selection_contains(&sel, 79, 3, 80));
    PASS();
}

TEST red_double_click_word_select(void) {
    WixenGrid g;
    wixen_grid_init(&g, 20, 3);
    /* "Hello World" at row 0 */
    const char *text = "Hello World";
    for (size_t i = 0; i < strlen(text); i++) {
        char ch[2] = { text[i], 0 };
        wixen_cell_set_content(&g.rows[0].cells[i], ch);
    }
    /* Double-click on col 7 ("o" in "World") */
    /* Word boundaries: "World" = cols 6-10 */
    /* Find word start */
    size_t col = 7;
    size_t ws = col, we = col;
    while (ws > 0 && g.rows[0].cells[ws - 1].content[0] != ' '
           && g.rows[0].cells[ws - 1].content[0] != '\0') ws--;
    while (we < g.cols - 1 && g.rows[0].cells[we + 1].content[0] != ' '
           && g.rows[0].cells[we + 1].content[0] != '\0') we++;

    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, ws, 0, WIXEN_SEL_WORD);
    wixen_selection_update(&sel, we, 0);
    ASSERT_EQ(6, (int)ws);
    ASSERT_EQ(10, (int)we);
    ASSERT(wixen_selection_contains(&sel, 6, 0, 20));
    ASSERT(wixen_selection_contains(&sel, 10, 0, 20));
    ASSERT_FALSE(wixen_selection_contains(&sel, 5, 0, 20));
    wixen_grid_free(&g);
    PASS();
}

TEST red_triple_click_line_select(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 3, WIXEN_SEL_LINE);
    wixen_selection_update(&sel, 79, 3);
    /* Entire row 3 selected */
    ASSERT(wixen_selection_contains(&sel, 0, 3, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 3, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 2, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 4, 80));
    PASS();
}

TEST red_pixel_to_cell_conversion(void) {
    size_t col, row;
    pixel_to_cell(8.0f, 16.0f, 40, 48, &col, &row);
    ASSERT_EQ(5, (int)col); /* 40 / 8 = 5 */
    ASSERT_EQ(3, (int)row); /* 48 / 16 = 3 */
    PASS();
}

TEST red_selection_extract_text(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 2);
    const char *line1 = "Hello";
    for (size_t i = 0; i < strlen(line1); i++) {
        char ch[2] = { line1[i], 0 };
        wixen_cell_set_content(&g.rows[0].cells[i], ch);
    }
    const char *line2 = "World";
    for (size_t i = 0; i < strlen(line2); i++) {
        char ch[2] = { line2[i], 0 };
        wixen_cell_set_content(&g.rows[1].cells[i], ch);
    }

    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 4, 1);

    /* Extract selected text */
    WixenGridPoint start, end;
    wixen_selection_ordered(&sel, &start, &end);
    char buf[256] = {0};
    size_t pos = 0;
    for (size_t r = start.row; r <= end.row && r < g.num_rows; r++) {
        size_t c0 = (r == start.row) ? start.col : 0;
        size_t c1 = (r == end.row) ? end.col : g.cols - 1;
        for (size_t c = c0; c <= c1 && c < g.cols; c++) {
            const char *ch = g.rows[r].cells[c].content;
            if (ch[0] && ch[0] != ' ') {
                size_t l = strlen(ch);
                memcpy(buf + pos, ch, l); pos += l;
            }
        }
        if (r < end.row) buf[pos++] = '\n';
    }

    ASSERT(strstr(buf, "Hello") != NULL);
    ASSERT(strstr(buf, "World") != NULL);
    wixen_grid_free(&g);
    PASS();
}

SUITE(red_mouse_select) {
    RUN_TEST(red_mouse_click_selects_nothing);
    RUN_TEST(red_mouse_drag_selects_range);
    RUN_TEST(red_mouse_drag_multiline);
    RUN_TEST(red_double_click_word_select);
    RUN_TEST(red_triple_click_line_select);
    RUN_TEST(red_pixel_to_cell_conversion);
    RUN_TEST(red_selection_extract_text);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_mouse_select);
    GREATEST_MAIN_END();
}
