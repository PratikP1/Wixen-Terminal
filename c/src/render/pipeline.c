/* pipeline.c — Vertex building for terminal cell rendering
 *
 * Converts each grid cell into two triangles (a quad):
 *   v0--v1     Indices: 0,1,2 and 2,3,0
 *   |  / |     Two triangles per cell
 *   | /  |
 *   v3--v2
 *
 * Background color fills the quad. Text glyph is composited via atlas UV + fg color.
 * Cursor is rendered as a colored overlay on the cursor cell.
 */
#include "wixen/render/pipeline.h"
#include <string.h>

/* Resolve WixenColor → WixenRgb */
WixenRgb wixen_resolve_color(const WixenColor *c, const WixenColorScheme *cs,
                              bool is_fg) {
    switch (c->type) {
    case WIXEN_COLOR_DEFAULT:
        return is_fg ? cs->default_fg : cs->default_bg;
    case WIXEN_COLOR_INDEXED:
        return cs->palette[c->index];
    case WIXEN_COLOR_RGB:
        return (WixenRgb){ c->rgb.r, c->rgb.g, c->rgb.b };
    }
    return is_fg ? cs->default_fg : cs->default_bg;
}

static void set_color4(float out[4], WixenRgb rgb) {
    out[0] = rgb.r / 255.0f;
    out[1] = rgb.g / 255.0f;
    out[2] = rgb.b / 255.0f;
    out[3] = 1.0f;
}

/* Emit 6 vertices for one cell quad (two triangles) */
static size_t emit_cell_quad(
    WixenVertex *out, size_t idx, size_t cap,
    float x, float y, float w, float h,
    float fg[4], float bg[4],
    float u0, float v0, float u1, float v1)
{
    if (idx + 6 > cap) return idx;

    /* v0 (top-left) */
    out[idx].position[0] = x;     out[idx].position[1] = y;
    out[idx].tex_coords[0] = u0;  out[idx].tex_coords[1] = v0;
    memcpy(out[idx].fg_color, fg, 16); memcpy(out[idx].bg_color, bg, 16);
    idx++;

    /* v1 (top-right) */
    out[idx].position[0] = x + w; out[idx].position[1] = y;
    out[idx].tex_coords[0] = u1;  out[idx].tex_coords[1] = v0;
    memcpy(out[idx].fg_color, fg, 16); memcpy(out[idx].bg_color, bg, 16);
    idx++;

    /* v2 (bottom-right) */
    out[idx].position[0] = x + w; out[idx].position[1] = y + h;
    out[idx].tex_coords[0] = u1;  out[idx].tex_coords[1] = v1;
    memcpy(out[idx].fg_color, fg, 16); memcpy(out[idx].bg_color, bg, 16);
    idx++;

    /* v2 again (second triangle) */
    out[idx] = out[idx - 1]; idx++;

    /* v3 (bottom-left) */
    out[idx].position[0] = x;     out[idx].position[1] = y + h;
    out[idx].tex_coords[0] = u0;  out[idx].tex_coords[1] = v1;
    memcpy(out[idx].fg_color, fg, 16); memcpy(out[idx].bg_color, bg, 16);
    idx++;

    /* v0 again */
    out[idx] = out[idx - 5]; idx++;

    return idx;
}

size_t wixen_build_cell_vertices(
    const WixenGrid *grid,
    float cell_width, float cell_height,
    float origin_x, float origin_y,
    const WixenColorScheme *colors,
    WixenHighlightFn highlight_fn, void *highlight_ctx,
    bool show_cursor, size_t cursor_col, size_t cursor_row,
    WixenVertex *out_vertices, size_t out_cap)
{
    if (!grid || !out_vertices || out_cap == 0) return 0;

    size_t idx = 0;

    for (size_t row = 0; row < grid->num_rows; row++) {
        float y = origin_y + row * cell_height;

        for (size_t col = 0; col < grid->cols; col++) {
            float x = origin_x + col * cell_width;

            const WixenCell *cell = wixen_grid_cell_const(grid, col, row);
            if (!cell) continue;

            /* Skip continuation cells (wide char second half) */
            if (cell->width == 0) continue;

            float w = cell_width * (cell->width > 1 ? 2 : 1);

            /* Resolve colors */
            WixenRgb fg_rgb = wixen_resolve_color(&cell->attrs.fg, colors, true);
            WixenRgb bg_rgb = wixen_resolve_color(&cell->attrs.bg, colors, false);

            /* Handle inverse */
            if (cell->attrs.inverse) {
                WixenRgb tmp = fg_rgb;
                fg_rgb = bg_rgb;
                bg_rgb = tmp;
            }

            /* Handle highlight (selection/search) */
            if (highlight_fn) {
                WixenHighlightType hl = highlight_fn(col, row, highlight_ctx);
                switch (hl) {
                case WIXEN_HIGHLIGHT_SELECTION:
                    bg_rgb = colors->selection_bg;
                    fg_rgb = colors->selection_fg;
                    break;
                case WIXEN_HIGHLIGHT_SEARCH_MATCH:
                    bg_rgb = colors->search_match_bg;
                    break;
                case WIXEN_HIGHLIGHT_SEARCH_ACTIVE:
                    bg_rgb = colors->search_active_bg;
                    break;
                case WIXEN_HIGHLIGHT_NONE:
                    break;
                }
            }

            float fg[4], bg[4];
            set_color4(fg, fg_rgb);
            set_color4(bg, bg_rgb);

            /* For now, use zero UV (no glyph texture mapped yet).
             * The pixel shader will show pure bg_color when atlas alpha=0. */
            float u0 = 0, v0_uv = 0, u1 = 0, v1_uv = 0;

            idx = emit_cell_quad(out_vertices, idx, out_cap,
                                  x, y, w, cell_height,
                                  fg, bg, u0, v0_uv, u1, v1_uv);
        }
    }

    /* Cursor overlay */
    if (show_cursor && cursor_row < grid->num_rows && cursor_col < grid->cols) {
        float x = origin_x + cursor_col * cell_width;
        float y = origin_y + cursor_row * cell_height;
        float fg[4], bg[4];
        set_color4(fg, colors->cursor_color);
        set_color4(bg, colors->cursor_color);
        bg[3] = 0.5f; /* Semi-transparent cursor */
        idx = emit_cell_quad(out_vertices, idx, out_cap,
                              x, y, cell_width, cell_height,
                              fg, bg, 0, 0, 0, 0);
    }

    return idx;
}
