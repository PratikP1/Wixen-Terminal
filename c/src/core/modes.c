/* modes.c — Terminal mode flags */
#include "wixen/core/modes.h"
#include <string.h>

void wixen_modes_init(WixenTerminalModes *m) {
    memset(m, 0, sizeof(*m));
    m->auto_wrap = true;
    m->cursor_visible = true;
    m->ansi_mode = true;
}

void wixen_modes_reset(WixenTerminalModes *m) {
    wixen_modes_init(m);
}
