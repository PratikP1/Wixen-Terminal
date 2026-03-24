/* mouse.h — Mouse event encoding for terminal applications */
#ifndef WIXEN_CORE_MOUSE_H
#define WIXEN_CORE_MOUSE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "wixen/core/modes.h"

typedef enum WixenMouseButton {
    WIXEN_MBTN_LEFT = 0,
    WIXEN_MBTN_MIDDLE = 1,
    WIXEN_MBTN_RIGHT = 2,
    WIXEN_MBTN_RELEASE = 3,
    WIXEN_MBTN_WHEEL_UP = 64,
    WIXEN_MBTN_WHEEL_DOWN = 65,
} WixenMouseButton;

/* Encode a mouse event into VT escape sequence.
 * col/row are 0-based terminal coordinates.
 * Returns number of bytes written to buf (0 if mode doesn't report this event). */
size_t wixen_encode_mouse(WixenMouseMode mode, bool sgr_mode,
                           WixenMouseButton button, uint16_t col, uint16_t row,
                           bool shift, bool ctrl, bool pressed,
                           char *buf, size_t buf_size);

#endif /* WIXEN_CORE_MOUSE_H */
