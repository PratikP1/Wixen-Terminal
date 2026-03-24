/* atlas.c — Glyph atlas with DirectWrite rasterization
 *
 * Uses DirectWrite to:
 * 1. Load font by family name
 * 2. Compute cell metrics (advance width, line height, baseline)
 * 3. Rasterize individual glyphs to 8-bit alpha bitmaps
 * 4. Pack glyphs into a 4096x4096 texture atlas
 *
 * Atlas layout: guillotine packing (row-based, grow downward)
 */
#ifdef _WIN32

/* Compiled as C++ because dwrite.h requires C++ class syntax.
 * All public functions use extern "C" for C linkage. */
extern "C" {
#include "wixen/render/atlas.h"
}
#include <dwrite.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "dwrite.lib")

/* C++ — use method calls directly, release helper */
#define DW_RELEASE(obj) do { if (obj) { (obj)->Release(); (obj) = NULL; } } while(0)

#define ATLAS_WIDTH  4096
#define ATLAS_HEIGHT 4096
#define MAX_GLYPHS   8192

/* Hash map entry for glyph cache */
typedef struct {
    WixenGlyphKey key;
    WixenGlyphEntry entry;
    bool occupied;
} GlyphCacheSlot;

struct WixenGlyphAtlas {
    /* DirectWrite state */
    IDWriteFactory *dwrite_factory;
    IDWriteTextFormat *format_normal;
    IDWriteTextFormat *format_bold;
    IDWriteTextFormat *format_italic;
    IDWriteTextFormat *format_bold_italic;
    IDWriteFontFace *font_face;

    /* Bitmap render target (for glyph rasterization) */
    IDWriteGdiInterop *gdi_interop;
    IDWriteBitmapRenderTarget *render_target;
    IDWriteRenderingParams *rendering_params;

    /* Atlas texture */
    uint8_t *texture;            /* R8 alpha, ATLAS_WIDTH * ATLAS_HEIGHT */
    uint32_t tex_width;
    uint32_t tex_height;
    bool dirty;

    /* Packing state */
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t row_height;         /* Height of tallest glyph in current row */

    /* Glyph cache (simple hash map) */
    GlyphCacheSlot cache[MAX_GLYPHS];

    /* Metrics */
    WixenDWriteMetrics metrics;
};

/* --- Hash function --- */

static uint32_t glyph_hash(uint32_t cp, bool bold, bool italic) {
    uint32_t h = cp;
    h ^= (bold ? 0x80000000u : 0) | (italic ? 0x40000000u : 0);
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    return h % MAX_GLYPHS;
}

/* --- DirectWrite initialization --- */

static wchar_t *utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *ws = static_cast<wchar_t *>(malloc(len * sizeof(wchar_t)));
    if (ws) MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, len);
    return ws;
}

extern "C" {

WixenGlyphAtlas *wixen_atlas_create(const char *font_family, float font_size_pt,
                                     float dpi) {
    WixenGlyphAtlas *a = static_cast<WixenGlyphAtlas *>(calloc(1, sizeof(WixenGlyphAtlas)));
    if (!a) return NULL;

    a->tex_width = ATLAS_WIDTH;
    a->tex_height = ATLAS_HEIGHT;
    a->texture = static_cast<uint8_t *>(calloc(ATLAS_WIDTH * ATLAS_HEIGHT, 1));
    if (!a->texture) { free(a); return NULL; }
    a->dirty = true;

    HRESULT hr;

    /* Create DirectWrite factory */
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown **>(&a->dwrite_factory));
    if (FAILED(hr)) goto fail;

    /* Create text formats (normal, bold, italic, bold+italic) */
    wchar_t *family_w = utf8_to_wide(font_family ? font_family : "Cascadia Mono");
    float font_size_dip = font_size_pt * dpi / 72.0f;

    hr = a->dwrite_factory->CreateTextFormat(
        family_w, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        font_size_dip, L"en-us", &a->format_normal);
    if (FAILED(hr)) { free(family_w); goto fail; }

    a->dwrite_factory->CreateTextFormat(
        family_w, NULL,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        font_size_dip, L"en-us", &a->format_bold);

    a->dwrite_factory->CreateTextFormat(
        family_w, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        font_size_dip, L"en-us", &a->format_italic);

    a->dwrite_factory->CreateTextFormat(
        family_w, NULL,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        font_size_dip, L"en-us", &a->format_bold_italic);

    free(family_w);

    /* GDI interop for bitmap rendering */
    hr = a->dwrite_factory->GetGdiInterop( &a->gdi_interop);
    if (FAILED(hr)) goto fail;

    /* Create bitmap render target (large enough for any glyph) */
    HDC screen_dc = GetDC(NULL);
    hr = a->gdi_interop->CreateBitmapRenderTarget(
        screen_dc, 256, 256, &a->render_target);
    ReleaseDC(NULL, screen_dc);
    if (FAILED(hr)) goto fail;

    /* Rendering params: ClearType, natural symmetric */
    hr = a->dwrite_factory->CreateCustomRenderingParams(
        1.8f,   /* gamma */
        0.5f,   /* enhanced contrast */
        1.0f,   /* clear type level */
        DWRITE_PIXEL_GEOMETRY_RGB,
        DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
        &a->rendering_params);

    /* Compute cell metrics using a reference character */
    {
        IDWriteTextLayout *layout = NULL;
        hr = a->dwrite_factory->CreateTextLayout(
            L"M", 1, a->format_normal, 1000, 1000, &layout);
        if (SUCCEEDED(hr) && layout) {
            DWRITE_TEXT_METRICS tm;
            layout->GetMetrics( &tm);

            DWRITE_LINE_METRICS lm;
            UINT32 line_count = 0;
            layout->GetLineMetrics( &lm, 1, &line_count);

            a->metrics.cell_width = tm.widthIncludingTrailingWhitespace;
            a->metrics.cell_height = lm.height;
            a->metrics.baseline = lm.baseline;
            a->metrics.dpi = dpi;

            /* Underline/strikethrough from font metrics */
            DWRITE_FONT_METRICS fm;
            (void)a->format_normal->GetFontSize();
            /* Simple approximation */
            a->metrics.underline_pos = a->metrics.baseline + 2;
            a->metrics.underline_thickness = 1.0f;
            a->metrics.strikethrough_pos = a->metrics.baseline * 0.65f;
            (void)fm;

            DW_RELEASE(layout);
        }
    }

    return a;

fail:
    wixen_atlas_destroy(a);
    return NULL;
}

void wixen_atlas_destroy(WixenGlyphAtlas *a) {
    if (!a) return;
    DW_RELEASE(a->rendering_params);
    DW_RELEASE(a->render_target);
    DW_RELEASE(a->gdi_interop);
    DW_RELEASE(a->font_face);
    DW_RELEASE(a->format_bold_italic);
    DW_RELEASE(a->format_italic);
    DW_RELEASE(a->format_bold);
    DW_RELEASE(a->format_normal);
    DW_RELEASE(a->dwrite_factory);
    free(a->texture);
    free(a);
}

/* --- Glyph rasterization --- */

static bool rasterize_glyph(WixenGlyphAtlas *a, uint32_t codepoint,
                              bool bold, bool italic, WixenGlyphEntry *out) {
    if (!a->render_target) return false;

    /* Select text format */
    IDWriteTextFormat *fmt = a->format_normal;
    if (bold && italic) fmt = a->format_bold_italic;
    else if (bold) fmt = a->format_bold;
    else if (italic) fmt = a->format_italic;
    if (!fmt) fmt = a->format_normal;

    /* Encode codepoint to UTF-16 */
    wchar_t text[3] = {0};
    if (codepoint < 0x10000) {
        text[0] = (wchar_t)codepoint;
    } else {
        /* Surrogate pair */
        codepoint -= 0x10000;
        text[0] = (wchar_t)(0xD800 + (codepoint >> 10));
        text[1] = (wchar_t)(0xDC00 + (codepoint & 0x3FF));
    }
    UINT32 text_len = text[1] ? 2 : 1;

    /* Create text layout to get metrics */
    IDWriteTextLayout *layout = NULL;
    HRESULT hr = a->dwrite_factory->CreateTextLayout(
        text, text_len, fmt, 256, 256, &layout);
    if (FAILED(hr) || !layout) return false;

    DWRITE_TEXT_METRICS tm;
    layout->GetMetrics( &tm);

    uint32_t gw = (uint32_t)(tm.widthIncludingTrailingWhitespace + 1.5f);
    uint32_t gh = (uint32_t)(tm.height + 1.5f);
    if (gw == 0) gw = 1;
    if (gh == 0) gh = 1;

    /* Check atlas space */
    if (a->cursor_x + gw >= a->tex_width) {
        a->cursor_x = 0;
        a->cursor_y += a->row_height + 1;
        a->row_height = 0;
    }
    if (a->cursor_y + gh >= a->tex_height) {
        /* Atlas full — can't rasterize */
        DW_RELEASE(layout);
        return false;
    }

    /* Clear render target area */
    HDC hdc = a->render_target->GetMemoryDC();
    RECT rc = { 0, 0, (LONG)gw + 2, (LONG)gh + 2 };
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    /* Draw glyph */
    DWRITE_LINE_METRICS lm;
    UINT32 lc = 0;
    layout->GetLineMetrics( &lm, 1, &lc);

    /* Draw text using GDI on the bitmap render target's DC */
    COLORREF old_color = SetTextColor(hdc, RGB(255, 255, 255));
    int old_mode = SetBkMode(hdc, TRANSPARENT);
    RECT text_rc = { 0, 0, (LONG)(gw + 2), (LONG)(gh + 2) };
    DrawTextW(hdc, text, (int)text_len, &text_rc, DT_LEFT | DT_TOP | DT_NOCLIP);
    SetTextColor(hdc, old_color);
    SetBkMode(hdc, old_mode);
    hr = S_OK;

    DW_RELEASE(layout);
    if (FAILED(hr)) return false;

    /* Extract pixels from DIB and copy to atlas */
    DIBSECTION dib;
    HBITMAP hbm = (HBITMAP)GetCurrentObject(hdc, OBJ_BITMAP);
    GetObject(hbm, sizeof(dib), &dib);

    uint8_t *src = (uint8_t *)dib.dsBm.bmBits;
    int src_stride = dib.dsBm.bmWidthBytes;
    int bpp = dib.dsBm.bmBitsPixel / 8;

    for (uint32_t y = 0; y < gh; y++) {
        for (uint32_t x = 0; x < gw; x++) {
            /* DIB is bottom-up, BGR format */
            int src_y = (int)(dib.dsBm.bmHeight - 1 - y);
            if (src_y < 0) continue;
            uint8_t *pixel = src + src_y * src_stride + x * bpp;
            /* Use max channel as alpha (ClearType subpixel → grayscale) */
            uint8_t alpha = pixel[0];
            if (bpp >= 2 && pixel[1] > alpha) alpha = pixel[1];
            if (bpp >= 3 && pixel[2] > alpha) alpha = pixel[2];

            uint32_t dx = a->cursor_x + x;
            uint32_t dy = a->cursor_y + y;
            a->texture[dy * a->tex_width + dx] = alpha;
        }
    }

    out->atlas_x = a->cursor_x;
    out->atlas_y = a->cursor_y;
    out->width = gw;
    out->height = gh;
    out->bearing_x = 0;
    out->bearing_y = (int32_t)lm.baseline;

    /* Advance packing cursor */
    a->cursor_x += gw + 1;
    if (gh > a->row_height) a->row_height = gh;
    a->dirty = true;

    return true;
}

/* --- Public API --- */

const WixenGlyphEntry *wixen_atlas_get_glyph(WixenGlyphAtlas *a,
                                              uint32_t codepoint,
                                              bool bold, bool italic) {
    uint32_t idx = glyph_hash(codepoint, bold, italic);
    /* Linear probe */
    for (uint32_t i = 0; i < MAX_GLYPHS; i++) {
        uint32_t slot = (idx + i) % MAX_GLYPHS;
        GlyphCacheSlot *s = &a->cache[slot];
        if (!s->occupied) {
            /* Cache miss — rasterize */
            s->key.codepoint = codepoint;
            s->key.bold = bold;
            s->key.italic = italic;
            if (rasterize_glyph(a, codepoint, bold, italic, &s->entry)) {
                s->occupied = true;
                return &s->entry;
            }
            return NULL; /* Rasterization failed */
        }
        if (s->key.codepoint == codepoint && s->key.bold == bold && s->key.italic == italic) {
            return &s->entry; /* Cache hit */
        }
    }
    return NULL; /* Cache full */
}

WixenDWriteMetrics wixen_atlas_metrics(const WixenGlyphAtlas *a) {
    return a->metrics;
}

const uint8_t *wixen_atlas_texture_data(const WixenGlyphAtlas *a,
                                         uint32_t *out_width, uint32_t *out_height) {
    if (out_width) *out_width = a->tex_width;
    if (out_height) *out_height = a->tex_height;
    return a->texture;
}

bool wixen_atlas_is_dirty(const WixenGlyphAtlas *a) {
    return a->dirty;
}

void wixen_atlas_clear_dirty(WixenGlyphAtlas *a) {
    a->dirty = false;
}

} /* extern "C" */

#endif /* _WIN32 */
