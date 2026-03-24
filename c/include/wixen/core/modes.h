/* modes.h — Terminal DEC/ANSI mode flags */
#ifndef WIXEN_CORE_MODES_H
#define WIXEN_CORE_MODES_H

#include <stdbool.h>

typedef enum {
    WIXEN_MOUSE_NONE = 0,
    WIXEN_MOUSE_X10,          /* Button press only */
    WIXEN_MOUSE_NORMAL,       /* Button press + release */
    WIXEN_MOUSE_BUTTON,       /* + motion while pressed */
    WIXEN_MOUSE_ANY,          /* + all motion */
} WixenMouseMode;

typedef struct {
    bool cursor_keys_application; /* DECCKM: ESC O vs ESC [ */
    bool ansi_mode;               /* DECANM */
    bool auto_wrap;               /* DECAWM */
    bool cursor_visible;          /* DECTCEM */
    bool alternate_screen;
    bool bracketed_paste;
    bool focus_reporting;
    WixenMouseMode mouse_tracking;
    bool mouse_sgr;               /* CSI ? 1006 */
    bool line_feed_new_line;      /* LNM */
    bool insert_mode;             /* IRM */
    bool origin_mode;             /* DECOM */
    bool reverse_video;           /* DECSCNM */
    bool synchronized_output;     /* Mode 2026 */
} WixenTerminalModes;

void wixen_modes_init(WixenTerminalModes *m);
void wixen_modes_reset(WixenTerminalModes *m);

#endif /* WIXEN_CORE_MODES_H */
