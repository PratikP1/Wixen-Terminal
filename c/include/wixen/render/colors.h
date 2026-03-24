/* colors.h — Color scheme for terminal rendering */
#ifndef WIXEN_RENDER_COLORS_H
#define WIXEN_RENDER_COLORS_H

#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} WixenRgb;

typedef struct {
    WixenRgb palette[256];    /* Full 256-color palette */
    WixenRgb default_fg;
    WixenRgb default_bg;
    WixenRgb cursor_color;
    WixenRgb selection_bg;
    WixenRgb selection_fg;
    WixenRgb search_match_bg;
    WixenRgb search_active_bg;
} WixenColorScheme;

/* Initialize with default dark theme */
void wixen_colors_init_default(WixenColorScheme *cs);

/* Resolve an indexed color (0-255) from the palette */
WixenRgb wixen_colors_resolve_indexed(const WixenColorScheme *cs, uint8_t index);

/* Convert to float [0,1] for GPU */
void wixen_rgb_to_float(WixenRgb c, float out[3]);

#endif /* WIXEN_RENDER_COLORS_H */
