/* renderer.h — D3D11 GPU renderer for terminal */
#ifndef WIXEN_RENDER_RENDERER_H
#define WIXEN_RENDER_RENDERER_H

#ifdef _WIN32

#include "wixen/render/colors.h"
#include "wixen/core/grid.h"
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

/* Opaque renderer handle — D3D11 internals hidden */
typedef struct WixenRenderer WixenRenderer;

/* Font metrics from DirectWrite */
typedef struct {
    float cell_width;
    float cell_height;
    float baseline;
    float underline_pos;
    float underline_thickness;
} WixenFontMetrics;

/* Per-pane render info */
typedef struct {
    const WixenGrid *grid;
    float x, y, width, height;   /* Pixel rect in window */
    bool has_focus;
    size_t scroll_offset;         /* Viewport offset into scrollback */
} WixenPaneRenderInfo;

/* Renderer lifecycle */
WixenRenderer *wixen_renderer_create(HWND hwnd, uint32_t width, uint32_t height,
                                      const char *font_family, float font_size,
                                      const WixenColorScheme *colors);
void wixen_renderer_destroy(WixenRenderer *r);

/* Phased creation: each step takes <100ms. Call one per frame.
 * Returns the step number completed (0-based). When done, returns -1.
 * Usage: step=0 → D3D11 device, step=1 → shaders, step=2 → atlas, step=3 → done */
WixenRenderer *wixen_renderer_create_begin(HWND hwnd, uint32_t width, uint32_t height,
                                             const WixenColorScheme *colors);
bool wixen_renderer_create_step(WixenRenderer *r, int step,
                                  const char *font_family, float font_size);
#define WIXEN_RENDERER_INIT_STEPS 3

/* Clear screen and present (used during init before full rendering is ready) */
void wixen_renderer_clear_present(WixenRenderer *r, const WixenColorScheme *colors);

/* Resize the render target */
void wixen_renderer_resize(WixenRenderer *r, uint32_t width, uint32_t height);

/* Render a frame */
void wixen_renderer_render_frame(WixenRenderer *r,
                                  const WixenPaneRenderInfo *panes, size_t pane_count);

/* Get font metrics */
WixenFontMetrics wixen_renderer_font_metrics(const WixenRenderer *r);

/* Update color scheme */
void wixen_renderer_set_colors(WixenRenderer *r, const WixenColorScheme *colors);

/* Get dimensions */
uint32_t wixen_renderer_width(const WixenRenderer *r);
uint32_t wixen_renderer_height(const WixenRenderer *r);

/* DPI helpers (usable without a renderer instance) */
float wixen_dpi_scale_factor(uint32_t dpi);
void wixen_dpi_grid_dimensions(uint32_t window_width, uint32_t window_height,
                                float base_cell_width, float base_cell_height,
                                uint32_t dpi,
                                uint32_t *out_cols, uint32_t *out_rows);

#endif /* _WIN32 */
#endif /* WIXEN_RENDER_RENDERER_H */
