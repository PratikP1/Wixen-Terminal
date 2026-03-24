/* test_grid_visible_text.c — Grid visible text extraction */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/grid.h"

TEST vis_empty_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    char buf[256];
    size_t len = wixen_grid_visible_text(&g, buf, sizeof(buf));
    /* Empty grid = all blank lines */
    (void)len; /* Just no crash */
    wixen_grid_free(&g);
    PASS();
}

TEST vis_single_char(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    char buf[256];
    size_t len = wixen_grid_visible_text(&g, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "A") != NULL);
    wixen_grid_free(&g);
    PASS();
}

TEST vis_full_row(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "H");
    wixen_cell_set_content(&g.rows[0].cells[1], "e");
    wixen_cell_set_content(&g.rows[0].cells[2], "l");
    wixen_cell_set_content(&g.rows[0].cells[3], "l");
    wixen_cell_set_content(&g.rows[0].cells[4], "o");
    char buf[256];
    wixen_grid_visible_text(&g, buf, sizeof(buf));
    ASSERT(strstr(buf, "Hello") != NULL);
    wixen_grid_free(&g);
    PASS();
}

TEST vis_multiline(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 2);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[1].cells[0], "B");
    char buf[256];
    wixen_grid_visible_text(&g, buf, sizeof(buf));
    ASSERT(strstr(buf, "A") != NULL);
    ASSERT(strstr(buf, "B") != NULL);
    wixen_grid_free(&g);
    PASS();
}

TEST vis_trims_trailing_spaces(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "X");
    /* Cells 1-9 are spaces/empty */
    char buf[256];
    size_t len = wixen_grid_visible_text(&g, buf, sizeof(buf));
    /* Should be just "X" plus newline, not "X         " */
    ASSERT(len <= 3); /* "X\n" or just "X" */
    wixen_grid_free(&g);
    PASS();
}

TEST vis_utf8_content(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "\xc3\xa9"); /* é */
    char buf[256];
    wixen_grid_visible_text(&g, buf, sizeof(buf));
    ASSERT(strstr(buf, "\xc3\xa9") != NULL);
    wixen_grid_free(&g);
    PASS();
}

TEST vis_buffer_too_small(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    for (size_t r = 0; r < 24; r++)
        for (size_t c = 0; c < 80; c++)
            wixen_cell_set_content(&g.rows[r].cells[c], "X");
    char buf[16]; /* Tiny buffer */
    size_t len = wixen_grid_visible_text(&g, buf, sizeof(buf));
    /* Should not crash, truncates */
    ASSERT(len <= 15);
    ASSERT(buf[15] == '\0');
    wixen_grid_free(&g);
    PASS();
}

TEST row_text_empty(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 1);
    char buf[64];
    size_t len = wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT_EQ(0, (int)len);
    wixen_grid_free(&g);
    PASS();
}

TEST row_text_partial(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[0].cells[2], "C");
    char buf[64];
    size_t len = wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT_EQ(3, (int)len);
    ASSERT_STR_EQ("ABC", buf);
    wixen_grid_free(&g);
    PASS();
}

SUITE(grid_visible_text) {
    RUN_TEST(vis_empty_grid);
    RUN_TEST(vis_single_char);
    RUN_TEST(vis_full_row);
    RUN_TEST(vis_multiline);
    RUN_TEST(vis_trims_trailing_spaces);
    RUN_TEST(vis_utf8_content);
    RUN_TEST(vis_buffer_too_small);
    RUN_TEST(row_text_empty);
    RUN_TEST(row_text_partial);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(grid_visible_text);
    GREATEST_MAIN_END();
}
