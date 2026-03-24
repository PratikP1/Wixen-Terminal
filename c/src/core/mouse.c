/* mouse.c — Mouse event encoding for terminal applications */
#include "wixen/core/mouse.h"
#include <stdio.h>
#include <string.h>

size_t wixen_encode_mouse(WixenMouseMode mode, bool sgr_mode,
                           WixenMouseButton button, uint16_t col, uint16_t row,
                           bool shift, bool ctrl, bool pressed,
                           char *buf, size_t buf_size) {
    if (mode == WIXEN_MOUSE_NONE || buf_size < 16) return 0;

    /* X10 mode only reports button presses */
    if (mode == WIXEN_MOUSE_X10 && !pressed) return 0;

    /* Build button byte */
    uint8_t cb = (uint8_t)button;
    if (shift) cb |= 4;
    if (ctrl) cb |= 16;

    if (sgr_mode) {
        /* SGR encoding: CSI < Cb ; Cx ; Cy M/m */
        return (size_t)snprintf(buf, buf_size, "\x1b[<%u;%u;%u%c",
            (unsigned)cb, (unsigned)(col + 1), (unsigned)(row + 1),
            pressed ? 'M' : 'm');
    }

    /* Legacy encoding: CSI M Cb+32 Cx+33 Cy+33 */
    if (col > 222 || row > 222) return 0;  /* Legacy can't encode beyond 223 */
    buf[0] = '\x1b';
    buf[1] = '[';
    buf[2] = 'M';
    buf[3] = (char)(cb + 32);
    buf[4] = (char)(col + 33);
    buf[5] = (char)(row + 33);
    buf[6] = '\0';
    return 6;
}
