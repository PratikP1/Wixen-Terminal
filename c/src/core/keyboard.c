/* keyboard.c — Key and mouse encoding for terminal input */
#include "wixen/core/keyboard.h"
#include <stdio.h>
#include <string.h>

/* Windows VK codes we handle */
#define VK_BACK    0x08
#define VK_TAB     0x09
#define VK_RETURN  0x0D
#define VK_ESCAPE  0x1B
#define VK_SPACE   0x20
#define VK_PRIOR   0x21  /* Page Up */
#define VK_NEXT    0x22  /* Page Down */
#define VK_END     0x23
#define VK_HOME    0x24
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_INSERT  0x2D
#define VK_DELETE  0x2E
#define VK_F1      0x70
#define VK_F12     0x7B

/* xterm modifier parameter: 1 + shift*1 + alt*2 + ctrl*4 */
static int xterm_modifier(bool shift, bool ctrl, bool alt) {
    int m = 1;
    if (shift) m += 1;
    if (alt) m += 2;
    if (ctrl) m += 4;
    return m;
}

size_t wixen_encode_key(uint16_t vk, bool shift, bool ctrl, bool alt,
                         bool app_cursor, char *buf, size_t buf_size) {
    if (buf_size < 16) return 0;

    int mod = xterm_modifier(shift, ctrl, alt);
    bool has_mod = (mod > 1);

    /* Ctrl+letter → C0 control character */
    if (ctrl && !alt && !shift && vk >= 'A' && vk <= 'Z') {
        buf[0] = (char)(vk - 'A' + 1);
        return 1;
    }

    /* Alt+letter → ESC + lowercase */
    if (alt && !ctrl && !shift && vk >= 'A' && vk <= 'Z') {
        buf[0] = '\x1b';
        buf[1] = (char)(vk - 'A' + 'a');
        return 2;
    }

    /* Special keys */
    switch (vk) {
    case VK_BACK:
        if (ctrl || alt) { buf[0] = '\x1b'; buf[1] = '\x7f'; return 2; } /* Word delete */
        buf[0] = '\x7f'; return 1;
    case VK_TAB:
        if (shift) { buf[0] = '\x1b'; buf[1] = '['; buf[2] = 'Z'; return 3; }
        buf[0] = '\t'; return 1;
    case VK_RETURN:
        buf[0] = '\r'; return 1;
    case VK_ESCAPE:
        buf[0] = '\x1b'; return 1;

    /* Arrow keys */
    case VK_UP:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%dA", mod);
        return (size_t)snprintf(buf, buf_size, app_cursor ? "\x1bOA" : "\x1b[A");
    case VK_DOWN:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%dB", mod);
        return (size_t)snprintf(buf, buf_size, app_cursor ? "\x1bOB" : "\x1b[B");
    case VK_RIGHT:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%dC", mod);
        return (size_t)snprintf(buf, buf_size, app_cursor ? "\x1bOC" : "\x1b[C");
    case VK_LEFT:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%dD", mod);
        return (size_t)snprintf(buf, buf_size, app_cursor ? "\x1bOD" : "\x1b[D");

    /* Navigation */
    case VK_HOME:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%dH", mod);
        return (size_t)snprintf(buf, buf_size, app_cursor ? "\x1bOH" : "\x1b[H");
    case VK_END:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%dF", mod);
        return (size_t)snprintf(buf, buf_size, app_cursor ? "\x1bOF" : "\x1b[F");
    case VK_INSERT:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[2;%d~", mod);
        return (size_t)snprintf(buf, buf_size, "\x1b[2~");
    case VK_DELETE:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[3;%d~", mod);
        return (size_t)snprintf(buf, buf_size, "\x1b[3~");
    case VK_PRIOR:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[5;%d~", mod);
        return (size_t)snprintf(buf, buf_size, "\x1b[5~");
    case VK_NEXT:
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[6;%d~", mod);
        return (size_t)snprintf(buf, buf_size, "\x1b[6~");
    }

    /* Function keys F1-F4 use SS3 (ESC O), F5-F12 use CSI ~ */
    if (vk >= VK_F1 && vk <= VK_F1 + 3) {
        /* F1=P, F2=Q, F3=R, F4=S */
        char final_ch = (char)('P' + (vk - VK_F1));
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[1;%d%c", mod, final_ch);
        return (size_t)snprintf(buf, buf_size, "\x1bO%c", final_ch);
    }
    if (vk >= VK_F1 + 4 && vk <= VK_F12) {
        static const int fkey_codes[] = {
            /* F5=15, F6=17, F7=18, F8=19, F9=20, F10=21, F11=23, F12=24 */
            15, 17, 18, 19, 20, 21, 23, 24
        };
        int code = fkey_codes[vk - VK_F1 - 4];
        if (has_mod) return (size_t)snprintf(buf, buf_size, "\x1b[%d;%d~", code, mod);
        return (size_t)snprintf(buf, buf_size, "\x1b[%d~", code);
    }

    return 0; /* Not handled */
}

/* --- Mouse encoding --- */

size_t wixen_encode_mouse_sgr(uint8_t button, bool shift, bool ctrl, bool alt,
                               uint16_t col, uint16_t row, bool pressed,
                               char *buf, size_t buf_size) {
    int cb = button;
    if (shift) cb |= 4;
    if (alt) cb |= 8;
    if (ctrl) cb |= 16;
    /* SGR: CSI < Cb ; Cx ; Cy M/m (1-based coordinates) */
    return (size_t)snprintf(buf, buf_size, "\x1b[<%d;%d;%d%c",
                             cb, col + 1, row + 1, pressed ? 'M' : 'm');
}

size_t wixen_encode_mouse_legacy(uint8_t button, bool shift, bool ctrl, bool alt,
                                  uint16_t col, uint16_t row,
                                  char *buf, size_t buf_size) {
    if (col > 222 || row > 222) return 0; /* Legacy caps at 223 */
    int cb = button;
    if (shift) cb |= 4;
    if (alt) cb |= 8;
    if (ctrl) cb |= 16;
    if (buf_size < 6) return 0;
    buf[0] = '\x1b';
    buf[1] = '[';
    buf[2] = 'M';
    buf[3] = (char)(32 + cb);
    buf[4] = (char)(32 + col + 1);
    buf[5] = (char)(32 + row + 1);
    return 6;
}
