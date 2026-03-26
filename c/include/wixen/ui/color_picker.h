/* color_picker.h — Hex color parsing/formatting + Windows ChooseColor wrapper
 *
 * Provides:
 *   - wixen_parse_hex_color()   — "#RRGGBB" / "#RGB" / "RRGGBB" to uint32_t
 *   - wixen_format_hex_color()  — uint32_t to "#RRGGBB"
 *   - wixen_show_color_picker() — native ChooseColor dialog (Win32 only)
 */
#ifndef WIXEN_UI_COLOR_PICKER_H
#define WIXEN_UI_COLOR_PICKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parse a hex color string to a packed RGB value (0x00RRGGBB).
 * Accepts: "#RRGGBB", "RRGGBB", "#RGB", "RGB" (case-insensitive).
 * Returns 0 on invalid input. */
uint32_t wixen_parse_hex_color(const char *hex);

/* Format a packed RGB value (0x00RRGGBB) as "#RRGGBB" into buf.
 * buf must be at least 8 bytes. */
void wixen_format_hex_color(uint32_t rgb, char *buf, size_t len);

#ifdef _WIN32
#include <windows.h>

/* Show the Windows ChooseColor dialog.
 * initial: starting color as 0x00RRGGBB.
 * out_rgb: receives the chosen color on success.
 * Returns true if user picked a color, false on cancel. */
bool wixen_show_color_picker(HWND parent, uint32_t initial, uint32_t *out_rgb);

#endif /* _WIN32 */
#endif /* WIXEN_UI_COLOR_PICKER_H */
