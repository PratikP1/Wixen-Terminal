/* software.c — GDI-based software renderer fallback
 *
 * Used when D3D11 is unavailable (e.g., Remote Desktop, VM without GPU).
 * Renders terminal cells using GDI text drawing on a DIB section.
 */
#ifdef _WIN32

#include "wixen/render/software.h"
#include <stdlib.h>
#include <string.h>

struct WixenSoftwareRenderer {
    HWND hwnd;
    HDC mem_dc;
    HBITMAP bitmap;
    HFONT font;
    uint32_t width, height;
    WixenColorScheme colors;
    float cell_width, cell_height;
};

WixenSoftwareRenderer *wixen_soft_create(HWND hwnd, uint32_t width, uint32_t height,
                                          const WixenColorScheme *colors) {
    WixenSoftwareRenderer *r = (WixenSoftwareRenderer *)calloc(1, sizeof(*r));
    if (!r) return NULL;

    r->hwnd = hwnd;
    r->width = width;
    r->height = height;
    r->colors = *colors;

    HDC screen_dc = GetDC(hwnd);
    r->mem_dc = CreateCompatibleDC(screen_dc);
    r->bitmap = CreateCompatibleBitmap(screen_dc, (int)width, (int)height);
    SelectObject(r->mem_dc, r->bitmap);
    ReleaseDC(hwnd, screen_dc);

    /* Create monospace font */
    r->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                            L"Cascadia Mono");
    if (!r->font) {
        r->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                                L"Consolas");
    }
    SelectObject(r->mem_dc, r->font);

    /* Measure cell size */
    TEXTMETRICW tm;
    GetTextMetricsW(r->mem_dc, &tm);
    r->cell_width = (float)tm.tmAveCharWidth;
    r->cell_height = (float)tm.tmHeight;

    return r;
}

void wixen_soft_destroy(WixenSoftwareRenderer *r) {
    if (!r) return;
    if (r->font) DeleteObject(r->font);
    if (r->bitmap) DeleteObject(r->bitmap);
    if (r->mem_dc) DeleteDC(r->mem_dc);
    free(r);
}

void wixen_soft_resize(WixenSoftwareRenderer *r, uint32_t width, uint32_t height) {
    if (!r || (width == r->width && height == r->height)) return;
    r->width = width;
    r->height = height;

    if (r->bitmap) DeleteObject(r->bitmap);
    HDC screen_dc = GetDC(r->hwnd);
    r->bitmap = CreateCompatibleBitmap(screen_dc, (int)width, (int)height);
    SelectObject(r->mem_dc, r->bitmap);
    ReleaseDC(r->hwnd, screen_dc);
}

static COLORREF rgb_to_colorref(WixenRgb c) {
    return RGB(c.r, c.g, c.b);
}

void wixen_soft_render(WixenSoftwareRenderer *r, const WixenGrid *grid) {
    if (!r || !grid) return;

    /* Clear background */
    RECT rc = { 0, 0, (LONG)r->width, (LONG)r->height };
    HBRUSH bg_brush = CreateSolidBrush(rgb_to_colorref(r->colors.default_bg));
    FillRect(r->mem_dc, &rc, bg_brush);
    DeleteObject(bg_brush);

    SetBkMode(r->mem_dc, TRANSPARENT);
    SelectObject(r->mem_dc, r->font);

    /* Render each cell */
    for (size_t row = 0; row < grid->num_rows; row++) {
        int y = (int)(row * r->cell_height);
        if (y >= (int)r->height) break;

        for (size_t col = 0; col < grid->cols; col++) {
            int x = (int)(col * r->cell_width);
            if (x >= (int)r->width) break;

            const WixenCell *cell = wixen_grid_cell_const(grid, col, row);
            if (!cell || cell->width == 0) continue; /* Skip continuation cells */

            /* Draw background if not default */
            if (cell->attrs.bg.type != WIXEN_COLOR_DEFAULT) {
                WixenRgb bg = cell->attrs.bg.type == WIXEN_COLOR_INDEXED
                    ? r->colors.palette[cell->attrs.bg.index]
                    : (WixenRgb){ cell->attrs.bg.rgb.r, cell->attrs.bg.rgb.g, cell->attrs.bg.rgb.b };
                RECT cell_rc = { x, y, x + (int)(r->cell_width * cell->width), y + (int)r->cell_height };
                HBRUSH cell_brush = CreateSolidBrush(rgb_to_colorref(bg));
                FillRect(r->mem_dc, &cell_rc, cell_brush);
                DeleteObject(cell_brush);
            }

            /* Draw text */
            const char *content = cell->content;
            if (content && strcmp(content, " ") != 0 && content[0] != '\0') {
                /* Resolve fg color */
                WixenRgb fg = r->colors.default_fg;
                if (cell->attrs.fg.type == WIXEN_COLOR_INDEXED)
                    fg = r->colors.palette[cell->attrs.fg.index];
                else if (cell->attrs.fg.type == WIXEN_COLOR_RGB)
                    fg = (WixenRgb){ cell->attrs.fg.rgb.r, cell->attrs.fg.rgb.g, cell->attrs.fg.rgb.b };

                SetTextColor(r->mem_dc, rgb_to_colorref(fg));

                /* Convert UTF-8 to UTF-16 */
                wchar_t wtext[4] = {0};
                MultiByteToWideChar(CP_UTF8, 0, content, -1, wtext, 4);
                TextOutW(r->mem_dc, x, y, wtext, (int)wcslen(wtext));
            }
        }
    }

    /* Blit to window */
    HDC hdc = GetDC(r->hwnd);
    BitBlt(hdc, 0, 0, (int)r->width, (int)r->height, r->mem_dc, 0, 0, SRCCOPY);
    ReleaseDC(r->hwnd, hdc);
}

#endif /* _WIN32 */
