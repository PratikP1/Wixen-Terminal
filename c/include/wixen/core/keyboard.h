/* keyboard.h — Key encoding for terminal input (xterm + Kitty protocol) */
#ifndef WIXEN_CORE_KEYBOARD_H
#define WIXEN_CORE_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Encode a key press to xterm escape sequence.
 * vk: Windows virtual key code (VK_*)
 * Returns length written to buf, 0 if key not handled. */
size_t wixen_encode_key(uint16_t vk, bool shift, bool ctrl, bool alt,
                         bool app_cursor, char *buf, size_t buf_size);

/* Encode mouse event in SGR format (CSI < ...).
 * Returns length written to buf. */
size_t wixen_encode_mouse_sgr(uint8_t button, bool shift, bool ctrl, bool alt,
                               uint16_t col, uint16_t row, bool pressed,
                               char *buf, size_t buf_size);

/* Encode mouse event in legacy format (ESC [ M ...).
 * Returns length written to buf, 0 if coordinates overflow. */
size_t wixen_encode_mouse_legacy(uint8_t button, bool shift, bool ctrl, bool alt,
                                  uint16_t col, uint16_t row,
                                  char *buf, size_t buf_size);

/* Mouse button constants */
#define WIXEN_MOUSE_BTN_LEFT   0
#define WIXEN_MOUSE_BTN_MIDDLE 1
#define WIXEN_MOUSE_BTN_RIGHT  2
#define WIXEN_MOUSE_BTN_WHEEL_UP   64
#define WIXEN_MOUSE_BTN_WHEEL_DOWN 65

#endif /* WIXEN_CORE_KEYBOARD_H */
