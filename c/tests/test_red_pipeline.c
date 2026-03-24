/* test_red_pipeline.c — RED tests for vertex pipeline + color resolution */
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "greatest.h"
#include "wixen/render/pipeline.h"
#include "wixen/render/colors.h"
#include "wixen/core/grid.h"

TEST red_build_vertices_empty_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[256 * 6];
    size_t n = wixen_build_cell_vertices(&g, 8.0f, 16.0f, 0, 0, &cs,
        NULL, NULL, true, 0, 0, verts, 256 * 6);
    /* Empty grid should still produce background quads */
    ASSERT(n > 0);
    wixen_grid_free(&g);
    PASS();
}

TEST red_build_vertices_with_text(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[256 * 6];
    size_t n = wixen_build_cell_vertices(&g, 8.0f, 16.0f, 0, 0, &cs,
        NULL, NULL, true, 0, 0, verts, 256 * 6);
    ASSERT(n > 0);
    /* Should have 6 verts per cell (2 triangles) for bg, plus fg for non-empty */
    wixen_grid_free(&g);
    PASS();
}

TEST red_build_vertices_with_selection(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 3);
    for (int c = 0; c < 10; c++)
        wixen_cell_set_content(&g.rows[0].cells[c], "X");
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[256 * 6];
    /* Build with highlight callback would modify colors for selected cells */
    size_t n = wixen_build_cell_vertices(&g, 8.0f, 16.0f, 0, 0, &cs,
        NULL, NULL, true, 5, 0, verts, 256 * 6);
    ASSERT(n > 0);
    wixen_grid_free(&g);
    PASS();
}

TEST red_vertex_positions_correct(void) {
    WixenGrid g;
    wixen_grid_init(&g, 2, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    size_t n = wixen_build_cell_vertices(&g, 10.0f, 20.0f, 0, 0, &cs,
        NULL, NULL, false, 0, 0, verts, 64);
    ASSERT(n >= 6); /* At least one cell */
    /* First vertex should be near (0,0) */
    ASSERT(fabsf(verts[0].position[0]) < 11.0f);
    ASSERT(fabsf(verts[0].position[1]) < 21.0f);
    wixen_grid_free(&g);
    PASS();
}

TEST red_color_resolve_indexed_0(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 0 = "black" (Windows Terminal uses near-black {12,12,12}) */
    WixenRgb c = cs.palette[0];
    ASSERT(c.r < 20);
    ASSERT(c.g < 20);
    ASSERT(c.b < 20);
    PASS();
}

TEST red_color_resolve_indexed_15(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 15 = bright white */
    WixenRgb c = cs.palette[15];
    ASSERT(c.r > 200);
    ASSERT(c.g > 200);
    ASSERT(c.b > 200);
    PASS();
}

TEST red_color_cube_index_16(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 16 = rgb(0,0,0) in 6x6x6 cube */
    ASSERT_EQ(0, cs.palette[16].r);
    ASSERT_EQ(0, cs.palette[16].g);
    ASSERT_EQ(0, cs.palette[16].b);
    PASS();
}

TEST red_color_cube_index_231(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 231 = rgb(255,255,255) in cube */
    ASSERT_EQ(255, cs.palette[231].r);
    ASSERT_EQ(255, cs.palette[231].g);
    ASSERT_EQ(255, cs.palette[231].b);
    PASS();
}

TEST red_color_grayscale_monotonic(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Grayscale 232-255 should be monotonically increasing */
    for (int i = 232; i < 255; i++) {
        ASSERT(cs.palette[i].r <= cs.palette[i + 1].r);
    }
    PASS();
}

SUITE(red_pipeline) {
    RUN_TEST(red_build_vertices_empty_grid);
    RUN_TEST(red_build_vertices_with_text);
    RUN_TEST(red_build_vertices_with_selection);
    RUN_TEST(red_vertex_positions_correct);
    RUN_TEST(red_color_resolve_indexed_0);
    RUN_TEST(red_color_resolve_indexed_15);
    RUN_TEST(red_color_cube_index_16);
    RUN_TEST(red_color_cube_index_231);
    RUN_TEST(red_color_grayscale_monotonic);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_pipeline);
    GREATEST_MAIN_END();
}
