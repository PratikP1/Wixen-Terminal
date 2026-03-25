/* window.h — Win32 window creation and event handling */
#ifndef WIXEN_UI_WINDOW_H
#define WIXEN_UI_WINDOW_H

#ifdef _WIN32

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

/* Window events */
typedef enum {
    WIXEN_EVT_NONE = 0,
    WIXEN_EVT_RESIZED,
    WIXEN_EVT_KEY_INPUT,
    WIXEN_EVT_CHAR_INPUT,
    WIXEN_EVT_MOUSE_WHEEL,
    WIXEN_EVT_MOUSE_DOWN,
    WIXEN_EVT_MOUSE_UP,
    WIXEN_EVT_MOUSE_MOVE,
    WIXEN_EVT_DOUBLE_CLICK,
    WIXEN_EVT_CLOSE_REQUESTED,
    WIXEN_EVT_FOCUS_GAINED,
    WIXEN_EVT_FOCUS_LOST,
    WIXEN_EVT_DPI_CHANGED,
    WIXEN_EVT_FILES_DROPPED,
    WIXEN_EVT_CONTEXT_MENU,
} WixenEventType;

typedef enum {
    WIXEN_MB_LEFT = 0,
    WIXEN_MB_RIGHT,
    WIXEN_MB_MIDDLE,
} WixenMouseButton;

typedef enum {
    WIXEN_CTX_COPY = 1,
    WIXEN_CTX_PASTE,
    WIXEN_CTX_SELECT_ALL,
    WIXEN_CTX_SEARCH,
    WIXEN_CTX_SPLIT_H,
    WIXEN_CTX_SPLIT_V,
    WIXEN_CTX_NEW_TAB,
    WIXEN_CTX_CLOSE_TAB,
    WIXEN_CTX_SETTINGS,
} WixenContextAction;

typedef struct {
    WixenEventType type;
    union {
        struct { uint32_t width; uint32_t height; } resize;
        struct {
            uint16_t vk; uint16_t scan;
            bool down; bool shift; bool ctrl; bool alt;
        } key;
        uint32_t char_input;
        struct { int16_t delta; int16_t x; int16_t y; } wheel;
        struct { WixenMouseButton button; int16_t x; int16_t y; } mouse;
        struct { int16_t x; int16_t y; } mouse_move;
        struct { int16_t x; int16_t y; } dbl_click;
        uint32_t dpi;
        WixenContextAction context_action;
    };
} WixenWindowEvent;

typedef struct {
    HWND hwnd;
    bool fullscreen;
    WINDOWPLACEMENT saved_placement;
    bool visible;
    uint32_t dpi;
} WixenWindow;

/* Lifecycle */
bool wixen_window_create(WixenWindow *w, const wchar_t *title,
                          uint32_t width, uint32_t height);
void wixen_window_destroy(WixenWindow *w);

/* Event polling */
bool wixen_window_poll_event(WixenWindow *w, WixenWindowEvent *evt);

/* Properties */
void wixen_window_set_title(WixenWindow *w, const wchar_t *title);
void wixen_window_set_fullscreen(WixenWindow *w, bool fullscreen);
uint32_t wixen_window_get_dpi(const WixenWindow *w);
void wixen_window_request_redraw(WixenWindow *w);

/* Message pump — pump Win32 messages for COM/UIA dispatch */
void wixen_window_pump_messages(WixenWindow *w);

/* Show context menu at screen coordinates */
void wixen_window_show_context_menu(WixenWindow *w, int x, int y);

#endif /* _WIN32 */

/* Platform-independent UI data (testable) */
#include <stddef.h>

typedef struct {
    const char *label;   /* Menu item text (NULL for separator) */
    const char *action;  /* Action ID */
    bool is_separator;
} WixenContextMenuItem;

const WixenContextMenuItem *wixen_context_menu_items(size_t *out_count);
const char *wixen_window_default_title(void);
char *wixen_window_format_title(const char *tab_name, const char *cwd);

#endif /* WIXEN_UI_WINDOW_H */
