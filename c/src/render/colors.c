/* colors.c — Color scheme implementation */
#include "wixen/render/colors.h"
#include <string.h>

/* Standard 16 ANSI colors (dark theme — matches Windows Terminal defaults) */
static const WixenRgb ansi_16[16] = {
    { 12,  12,  12  },  /* 0  Black */
    { 197, 15,  31  },  /* 1  Red */
    { 19,  161, 14  },  /* 2  Green */
    { 193, 156, 0   },  /* 3  Yellow */
    { 0,   55,  218 },  /* 4  Blue */
    { 136, 23,  152 },  /* 5  Magenta */
    { 58,  150, 221 },  /* 6  Cyan */
    { 204, 204, 204 },  /* 7  White */
    { 118, 118, 118 },  /* 8  Bright Black */
    { 231, 72,  86  },  /* 9  Bright Red */
    { 22,  198, 12  },  /* 10 Bright Green */
    { 249, 241, 165 },  /* 11 Bright Yellow */
    { 59,  120, 255 },  /* 12 Bright Blue */
    { 180, 0,   158 },  /* 13 Bright Magenta */
    { 97,  214, 214 },  /* 14 Bright Cyan */
    { 242, 242, 242 },  /* 15 Bright White */
};

void wixen_colors_init_default(WixenColorScheme *cs) {
    memset(cs, 0, sizeof(*cs));

    /* Standard 16 colors */
    for (int i = 0; i < 16; i++) {
        cs->palette[i] = ansi_16[i];
    }

    /* 216 color cube (indices 16-231) */
    for (int i = 0; i < 216; i++) {
        int r = i / 36;
        int g = (i / 6) % 6;
        int b = i % 6;
        cs->palette[16 + i].r = (uint8_t)(r ? 55 + r * 40 : 0);
        cs->palette[16 + i].g = (uint8_t)(g ? 55 + g * 40 : 0);
        cs->palette[16 + i].b = (uint8_t)(b ? 55 + b * 40 : 0);
    }

    /* 24 grayscale (indices 232-255) */
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(8 + i * 10);
        cs->palette[232 + i] = (WixenRgb){ v, v, v };
    }

    /* Theme colors */
    cs->default_fg = (WixenRgb){ 204, 204, 204 };
    cs->default_bg = (WixenRgb){ 12, 12, 12 };
    cs->cursor_color = (WixenRgb){ 255, 255, 255 };
    cs->selection_bg = (WixenRgb){ 38, 79, 120 };
    cs->selection_fg = (WixenRgb){ 255, 255, 255 };
    cs->search_match_bg = (WixenRgb){ 100, 100, 0 };
    cs->search_active_bg = (WixenRgb){ 230, 150, 0 };
}

WixenRgb wixen_colors_resolve_indexed(const WixenColorScheme *cs, uint8_t index) {
    return cs->palette[index];
}

void wixen_rgb_to_float(WixenRgb c, float out[3]) {
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
}
