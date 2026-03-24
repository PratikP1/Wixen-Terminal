/* pipeline.h — Vertex building for terminal cell rendering */
#ifndef WIXEN_RENDER_PIPELINE_H
#define WIXEN_RENDER_PIPELINE_H

#include "wixen/render/colors.h"
#include "wixen/core/grid.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GPU vertex (matches HLSL input layout) */
typedef struct {
    float position[2];     /* Pixel x, y */
    float tex_coords[2];   /* Atlas UV */
    float fg_color[4];     /* RGBA [0,1] */
    float bg_color[4];     /* RGBA [0,1] */
} WixenVertex;

/* Uniform buffer (matches HLSL cbuffer) */
typedef struct {
    float screen_size[2];
    float _padding[2];
} WixenUniforms;

/* Cell highlight for search/selection overlay */
typedef enum {
    WIXEN_HIGHLIGHT_NONE = 0,
    WIXEN_HIGHLIGHT_SELECTION,
    WIXEN_HIGHLIGHT_SEARCH_MATCH,
    WIXEN_HIGHLIGHT_SEARCH_ACTIVE,
} WixenHighlightType;

/* Callback to determine cell highlight */
typedef WixenHighlightType (*WixenHighlightFn)(size_t col, size_t row, void *ctx);

/* Build vertices for a grid pane.
 * out_vertices must have room for at least grid_cols * grid_rows * 6 vertices.
 * Returns number of vertices written. */
size_t wixen_build_cell_vertices(
    const WixenGrid *grid,
    float cell_width, float cell_height,
    float origin_x, float origin_y,
    const WixenColorScheme *colors,
    WixenHighlightFn highlight_fn, void *highlight_ctx,
    bool show_cursor, size_t cursor_col, size_t cursor_row,
    WixenVertex *out_vertices, size_t out_cap);

/* Resolve a WixenColor to RGB using the color scheme */
WixenRgb wixen_resolve_color(const WixenColor *c, const WixenColorScheme *cs,
                              bool is_fg);

#endif /* WIXEN_RENDER_PIPELINE_H */
