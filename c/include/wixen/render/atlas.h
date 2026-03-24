/* atlas.h — Glyph atlas with DirectWrite rasterization */
#ifndef WIXEN_RENDER_ATLAS_H
#define WIXEN_RENDER_ATLAS_H

#ifdef _WIN32

#include "wixen/render/colors.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Glyph cache entry */
typedef struct {
    uint32_t atlas_x, atlas_y;   /* Position in atlas texture */
    uint32_t width, height;      /* Glyph dimensions in pixels */
    int32_t bearing_x;           /* Horizontal bearing */
    int32_t bearing_y;           /* Vertical bearing (baseline to top) */
} WixenGlyphEntry;

/* Glyph cache key */
typedef struct {
    uint32_t codepoint;
    bool bold;
    bool italic;
} WixenGlyphKey;

/* Font metrics computed from DirectWrite */
typedef struct {
    float cell_width;
    float cell_height;
    float baseline;
    float underline_pos;
    float underline_thickness;
    float strikethrough_pos;
    float dpi;
} WixenDWriteMetrics;

/* Glyph atlas — manages a texture filled with rasterized glyphs */
typedef struct WixenGlyphAtlas WixenGlyphAtlas;

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle */
WixenGlyphAtlas *wixen_atlas_create(const char *font_family, float font_size_pt,
                                     float dpi);
void wixen_atlas_destroy(WixenGlyphAtlas *atlas);

/* Lookup or rasterize a glyph. Returns entry, rasterizes on cache miss. */
const WixenGlyphEntry *wixen_atlas_get_glyph(WixenGlyphAtlas *atlas,
                                              uint32_t codepoint,
                                              bool bold, bool italic);

/* Get font metrics */
WixenDWriteMetrics wixen_atlas_metrics(const WixenGlyphAtlas *atlas);

/* Get atlas texture data (R8 alpha, row-major). Returns NULL if empty. */
const uint8_t *wixen_atlas_texture_data(const WixenGlyphAtlas *atlas,
                                         uint32_t *out_width, uint32_t *out_height);

/* Check if atlas texture needs re-upload to GPU */
bool wixen_atlas_is_dirty(const WixenGlyphAtlas *atlas);
void wixen_atlas_clear_dirty(WixenGlyphAtlas *atlas);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* WIXEN_RENDER_ATLAS_H */
