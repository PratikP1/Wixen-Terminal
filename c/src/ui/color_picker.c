/* color_picker.c — Hex color parsing/formatting + Windows ChooseColor wrapper */

#include "wixen/ui/color_picker.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* --- Hex parsing helpers --- */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool is_hex(char c) {
    return hex_digit(c) >= 0;
}

uint32_t wixen_parse_hex_color(const char *hex) {
    if (!hex) return 0;

    /* Skip leading '#' */
    if (*hex == '#') hex++;

    size_t len = strlen(hex);

    /* Validate all chars are hex digits */
    for (size_t i = 0; i < len; i++) {
        if (!is_hex(hex[i])) return 0;
    }

    if (len == 6) {
        /* RRGGBB */
        uint32_t r = (uint32_t)(hex_digit(hex[0]) << 4 | hex_digit(hex[1]));
        uint32_t g = (uint32_t)(hex_digit(hex[2]) << 4 | hex_digit(hex[3]));
        uint32_t b = (uint32_t)(hex_digit(hex[4]) << 4 | hex_digit(hex[5]));
        return (r << 16) | (g << 8) | b;
    }

    if (len == 3) {
        /* RGB shorthand: each digit is doubled (e.g. "F0A" -> "FF00AA") */
        uint32_t r = (uint32_t)hex_digit(hex[0]);
        uint32_t g = (uint32_t)hex_digit(hex[1]);
        uint32_t b = (uint32_t)hex_digit(hex[2]);
        r = (r << 4) | r;
        g = (g << 4) | g;
        b = (b << 4) | b;
        return (r << 16) | (g << 8) | b;
    }

    return 0; /* Invalid length */
}

void wixen_format_hex_color(uint32_t rgb, char *buf, size_t len) {
    if (!buf || len < 8) return;
    unsigned r = (rgb >> 16) & 0xFF;
    unsigned g = (rgb >> 8) & 0xFF;
    unsigned b = rgb & 0xFF;
    snprintf(buf, len, "#%02X%02X%02X", r, g, b);
}

/* --- Win32 ChooseColor dialog --- */

#ifdef _WIN32

#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

/* Persistent custom colors across dialog invocations (per process). */
static COLORREF s_custom_colors[16] = {0};

bool wixen_show_color_picker(HWND parent, uint32_t initial, uint32_t *out_rgb) {
    if (!out_rgb) return false;

    /* Convert our 0x00RRGGBB to Win32 COLORREF (0x00BBGGRR) */
    unsigned r = (initial >> 16) & 0xFF;
    unsigned g = (initial >> 8) & 0xFF;
    unsigned b = initial & 0xFF;
    COLORREF init_cr = RGB(r, g, b);

    CHOOSECOLORW cc = {0};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = parent;
    cc.rgbResult = init_cr;
    cc.lpCustColors = s_custom_colors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (ChooseColorW(&cc)) {
        /* Convert COLORREF (BBGGRR) back to 0x00RRGGBB */
        unsigned or_ = GetRValue(cc.rgbResult);
        unsigned og = GetGValue(cc.rgbResult);
        unsigned ob = GetBValue(cc.rgbResult);
        *out_rgb = (or_ << 16) | (og << 8) | ob;
        return true;
    }
    return false;
}

#endif /* _WIN32 */
