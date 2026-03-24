/* test_pipeline.c — Tests for vertex building pipeline */
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "greatest.h"
#include "wixen/render/pipeline.h"

#define APPROX(a, b) (fabs((double)(a) - (double)(b)) < 0.01)

TEST resolve_color_default_fg(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenColor c = { .type = WIXEN_COLOR_DEFAULT };
    WixenRgb rgb = wixen_resolve_color(&c, &cs, true);
    ASSERT_EQ(cs.default_fg.r, rgb.r);
    ASSERT_EQ(cs.default_fg.g, rgb.g);
    PASS();
}

TEST resolve_color_default_bg(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenColor c = { .type = WIXEN_COLOR_DEFAULT };
    WixenRgb rgb = wixen_resolve_color(&c, &cs, false);
    ASSERT_EQ(cs.default_bg.r, rgb.r);
    PASS();
}

TEST resolve_color_indexed(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenColor c = wixen_color_indexed(1); /* Red */
    WixenRgb rgb = wixen_resolve_color(&c, &cs, true);
    ASSERT_EQ(cs.palette[1].r, rgb.r);
    PASS();
}

TEST resolve_color_rgb(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenColor c = wixen_color_rgb(100, 200, 50);
    WixenRgb rgb = wixen_resolve_color(&c, &cs, true);
    ASSERT_EQ(100, rgb.r);
    ASSERT_EQ(200, rgb.g);
    ASSERT_EQ(50, rgb.b);
    PASS();
}

TEST build_vertices_empty_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 0, 0);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    size_t count = wixen_build_cell_vertices(&g, 8, 16, 0, 0, &cs,
                                              NULL, NULL, false, 0, 0,
                                              verts, 64);
    ASSERT_EQ(0, (int)count);
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_single_cell(void) {
    WixenGrid g;
    wixen_grid_init(&g, 1, 1);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    size_t count = wixen_build_cell_vertices(&g, 8.0f, 16.0f, 0, 0, &cs,
                                              NULL, NULL, false, 0, 0,
                                              verts, 64);
    /* 1 cell = 6 vertices (2 triangles) */
    ASSERT_EQ(6, (int)count);
    /* First vertex at (0,0) */
    ASSERT(APPROX(0, verts[0].position[0]));
    ASSERT(APPROX(0, verts[0].position[1]));
    /* Second vertex at (8, 0) */
    ASSERT(APPROX(8, verts[1].position[0]));
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_3x2_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 2);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[256];
    size_t count = wixen_build_cell_vertices(&g, 8.0f, 16.0f, 0, 0, &cs,
                                              NULL, NULL, false, 0, 0,
                                              verts, 256);
    /* 6 cells * 6 vertices = 36 */
    ASSERT_EQ(36, (int)count);
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_with_cursor(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 2);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[256];
    size_t count = wixen_build_cell_vertices(&g, 8.0f, 16.0f, 0, 0, &cs,
                                              NULL, NULL, true, 1, 0,
                                              verts, 256);
    /* 6 cells + 1 cursor = 42 vertices */
    ASSERT_EQ(42, (int)count);
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_colored_cell(void) {
    WixenGrid g;
    wixen_grid_init(&g, 1, 1);
    g.rows[0].cells[0].attrs.fg = wixen_color_indexed(1); /* Red */
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    wixen_build_cell_vertices(&g, 8, 16, 0, 0, &cs,
                               NULL, NULL, false, 0, 0, verts, 64);
    /* fg_color should be red (palette[1]) */
    float expected_r = cs.palette[1].r / 255.0f;
    ASSERT(APPROX(expected_r, verts[0].fg_color[0]));
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_inverse(void) {
    WixenGrid g;
    wixen_grid_init(&g, 1, 1);
    g.rows[0].cells[0].attrs.inverse = true;
    g.rows[0].cells[0].attrs.fg = wixen_color_indexed(1); /* Red fg */
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    wixen_build_cell_vertices(&g, 8, 16, 0, 0, &cs,
                               NULL, NULL, false, 0, 0, verts, 64);
    /* Inverse: fg becomes bg and vice versa. bg_color should be red. */
    float expected_r = cs.palette[1].r / 255.0f;
    ASSERT(APPROX(expected_r, verts[0].bg_color[0]));
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_wide_char(void) {
    WixenGrid g;
    wixen_grid_init(&g, 4, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "\xe4\xb8\xad"); /* 中 */
    g.rows[0].cells[0].width = 2;
    wixen_cell_set_content(&g.rows[0].cells[1], "");
    g.rows[0].cells[1].width = 0; /* Continuation */
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[256];
    size_t count = wixen_build_cell_vertices(&g, 8, 16, 0, 0, &cs,
                                              NULL, NULL, false, 0, 0,
                                              verts, 256);
    /* 3 visible cells (wide char skips continuation) + 2 normal = 3 cells * 6 = 18 */
    ASSERT_EQ(18, (int)count);
    /* Wide char quad should be 2*cell_width = 16 pixels wide */
    ASSERT(APPROX(16, verts[1].position[0] - verts[0].position[0]));
    wixen_grid_free(&g);
    PASS();
}

static WixenHighlightType always_selected(size_t col, size_t row, void *ctx) {
    (void)col; (void)row; (void)ctx;
    return WIXEN_HIGHLIGHT_SELECTION;
}

TEST build_vertices_with_highlight(void) {
    WixenGrid g;
    wixen_grid_init(&g, 2, 1);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    wixen_build_cell_vertices(&g, 8, 16, 0, 0, &cs,
                               always_selected, NULL, false, 0, 0,
                               verts, 64);
    /* bg should be selection color */
    float sel_r = cs.selection_bg.r / 255.0f;
    ASSERT(APPROX(sel_r, verts[0].bg_color[0]));
    wixen_grid_free(&g);
    PASS();
}

TEST build_vertices_null_safety(void) {
    ASSERT_EQ(0, (int)wixen_build_cell_vertices(NULL, 8, 16, 0, 0, NULL,
                                                  NULL, NULL, false, 0, 0, NULL, 0));
    PASS();
}

TEST build_vertices_origin_offset(void) {
    WixenGrid g;
    wixen_grid_init(&g, 1, 1);
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenVertex verts[64];
    wixen_build_cell_vertices(&g, 8, 16, 100, 50, &cs,
                               NULL, NULL, false, 0, 0, verts, 64);
    ASSERT(APPROX(100, verts[0].position[0]));
    ASSERT(APPROX(50, verts[0].position[1]));
    wixen_grid_free(&g);
    PASS();
}

SUITE(color_resolution) {
    RUN_TEST(resolve_color_default_fg);
    RUN_TEST(resolve_color_default_bg);
    RUN_TEST(resolve_color_indexed);
    RUN_TEST(resolve_color_rgb);
}

SUITE(vertex_building) {
    RUN_TEST(build_vertices_empty_grid);
    RUN_TEST(build_vertices_single_cell);
    RUN_TEST(build_vertices_3x2_grid);
    RUN_TEST(build_vertices_with_cursor);
    RUN_TEST(build_vertices_colored_cell);
    RUN_TEST(build_vertices_inverse);
    RUN_TEST(build_vertices_wide_char);
    RUN_TEST(build_vertices_with_highlight);
    RUN_TEST(build_vertices_null_safety);
    RUN_TEST(build_vertices_origin_offset);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(color_resolution);
    RUN_SUITE(vertex_building);
    GREATEST_MAIN_END();
}
