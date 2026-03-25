/* terminal.h — Terminal emulator state machine */
#ifndef WIXEN_CORE_TERMINAL_H
#define WIXEN_CORE_TERMINAL_H

#include "wixen/core/grid.h"
#include "wixen/core/modes.h"
#include "wixen/core/selection.h"
#include "wixen/core/hyperlink.h"
#include "wixen/shell_integ/shell_integ.h"
#include "wixen/vt/action.h"
#include <stdbool.h>
#include <stdint.h>

/* Scroll region (DECSTBM) */
typedef struct {
    size_t top;    /* First scrolling row (0-based) */
    size_t bottom; /* One past last scrolling row */
} WixenScrollRegion;

/* Saved cursor (DECSC/DECRC) */
typedef struct {
    size_t col;
    size_t row;
    WixenCellAttributes attrs;
    bool origin_mode;
    bool auto_wrap;
    uint8_t charsets[4];
    uint8_t active_charset;
} WixenSavedCursor;

/* Tab stops */
typedef struct {
    bool *stops;   /* stops[col] = true if tab stop set */
    size_t count;  /* == grid cols */
} WixenTabStops;

/* Terminal state */
typedef struct {
    WixenGrid grid;
    WixenGrid *alt_grid;         /* NULL when not in alt screen */
    WixenTerminalModes modes;
    WixenScrollRegion scroll_region;
    WixenSavedCursor *saved_cursor;
    WixenSelection selection;
    WixenTabStops tab_stops;

    char *title;
    bool title_dirty;
    bool dirty;
    bool bell_pending;
    bool pending_wrap;           /* Cursor at right margin, next char wraps */

    /* Alt screen saved state */
    WixenScrollRegion saved_scroll_region;
    bool saved_origin_mode;

    /* Shell integration */
    WixenShellIntegration shell_integ;

    /* Hyperlink store (OSC 8) */
    WixenHyperlinkStore hyperlinks;
    uint32_t current_hyperlink_id;  /* 0 = no active link */
    uint8_t active_charset;          /* 0=G0, 1=G1 */
    bool in_sixel;                   /* Inside Sixel DCS sequence */
    uint8_t charsets[4];             /* G0-G3 charset (0=ASCII, 1=DEC Special, 2=UK) */
    int32_t scroll_offset;           /* Viewport: 0=live, >0=scrolled back into history */
    uint32_t last_printed_char;      /* For REP (CSI b) */

    /* Echo timeout detection (password prompts) */
    uint64_t last_char_sent_ms;      /* When user last typed a char */
    bool char_sent_pending;          /* Waiting for echo */
    char last_char_sent;             /* The char we're waiting to see echoed */
    bool last_char_was_echoed;       /* Terminal echoed it back */

    /* Clipboard (OSC 52) */
    char *clipboard_write_pending;   /* Text to write to clipboard (NULL if none) */
    char *clipboard_injected;        /* Injected clipboard content for query response */

    /* Pending responses (DSR etc.) — simple queue */
    char **responses;
    size_t response_count;
    size_t response_cap;
} WixenTerminal;

/* Lifecycle */
void wixen_terminal_init(WixenTerminal *t, size_t cols, size_t rows);
void wixen_terminal_free(WixenTerminal *t);
void wixen_terminal_reset(WixenTerminal *t);
void wixen_terminal_resize(WixenTerminal *t, size_t new_cols, size_t new_rows);
void wixen_terminal_scroll_viewport(WixenTerminal *t, int32_t delta);

/* Action dispatch — main entry point for VT parser output */
void wixen_terminal_dispatch(WixenTerminal *t, const WixenAction *action);

/* Cursor movement */
void wixen_terminal_move_cursor_up(WixenTerminal *t, size_t count);
void wixen_terminal_move_cursor_down(WixenTerminal *t, size_t count);
void wixen_terminal_move_cursor_forward(WixenTerminal *t, size_t count);
void wixen_terminal_move_cursor_backward(WixenTerminal *t, size_t count);
void wixen_terminal_set_cursor_pos(WixenTerminal *t, size_t row, size_t col);
void wixen_terminal_cursor_home(WixenTerminal *t);
void wixen_terminal_carriage_return(WixenTerminal *t);
void wixen_terminal_linefeed(WixenTerminal *t);
void wixen_terminal_reverse_index(WixenTerminal *t);
void wixen_terminal_index(WixenTerminal *t);

/* Tab stops */
void wixen_terminal_tab(WixenTerminal *t);
void wixen_terminal_set_tab_stop(WixenTerminal *t);
void wixen_terminal_clear_tab_stop(WixenTerminal *t, int mode);

/* Erase */
void wixen_terminal_erase_in_display(WixenTerminal *t, int mode);
void wixen_terminal_erase_in_line(WixenTerminal *t, int mode);
void wixen_terminal_erase_chars(WixenTerminal *t, size_t count);

/* Line/cell operations */
void wixen_terminal_insert_lines(WixenTerminal *t, size_t count);
void wixen_terminal_delete_lines(WixenTerminal *t, size_t count);
void wixen_terminal_insert_blank_chars(WixenTerminal *t, size_t count);
void wixen_terminal_delete_chars(WixenTerminal *t, size_t count);

/* Scroll region */
void wixen_terminal_set_scroll_region(WixenTerminal *t, size_t top, size_t bottom);

/* Modes */
void wixen_terminal_set_mode(WixenTerminal *t, uint16_t mode, bool private_mode, bool set);

/* Alt screen */
void wixen_terminal_enter_alt_screen(WixenTerminal *t);
void wixen_terminal_exit_alt_screen(WixenTerminal *t);

/* Save/restore cursor */
void wixen_terminal_save_cursor(WixenTerminal *t);
void wixen_terminal_restore_cursor(WixenTerminal *t);

/* SGR (Select Graphic Rendition) */
void wixen_terminal_apply_sgr(WixenTerminal *t, const WixenAction *action);

/* Title */
void wixen_terminal_set_title(WixenTerminal *t, const char *title);

/* Text extraction */
size_t wixen_terminal_visible_text_buf(const WixenTerminal *t, char *buf, size_t buf_size);
char *wixen_terminal_visible_text(const WixenTerminal *t);
char *wixen_terminal_extract_row_text(const WixenTerminal *t, size_t row);

/* Selection text extraction */
char *wixen_terminal_selected_text(const WixenTerminal *t, const WixenSelection *sel);

/* Echo timeout detection (password prompts) */
void wixen_terminal_on_char_sent(WixenTerminal *t, char ch);
bool wixen_terminal_check_echo_timeout(WixenTerminal *t);

/* Prompt jumping (requires shell integration) */
bool wixen_terminal_jump_to_next_prompt(WixenTerminal *t);
bool wixen_terminal_jump_to_previous_prompt(WixenTerminal *t);

/* Resize with text reflow */
void wixen_terminal_resize_reflow(WixenTerminal *t, size_t new_cols, size_t new_rows);

/* Hyperlink at grid position. Returns URL or NULL. Do not free. */
const char *wixen_terminal_hyperlink_at(const WixenTerminal *t, size_t col, size_t row);

/* Selection helpers */
void wixen_terminal_select_all(WixenTerminal *t);
void wixen_terminal_clear_selection(WixenTerminal *t);

/* Clipboard (OSC 52) */
char *wixen_terminal_drain_clipboard_write(WixenTerminal *t);
void wixen_terminal_inject_clipboard(WixenTerminal *t, const char *text);

/* Base64 */
char *wixen_base64_encode(const uint8_t *data, size_t len);
uint8_t *wixen_base64_decode(const char *b64, size_t *out_len);

/* Queued responses */
const char *wixen_terminal_pop_response(WixenTerminal *t);

#endif /* WIXEN_CORE_TERMINAL_H */
