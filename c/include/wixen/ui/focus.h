/* focus.h -- Focus acquisition strategy with diagnostics */
#ifndef WIXEN_UI_FOCUS_H
#define WIXEN_UI_FOCUS_H

#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Which method succeeded in acquiring foreground focus */
typedef enum {
    FOCUS_NONE = 0,
    FOCUS_DIRECT,
    FOCUS_ALLOC_CONSOLE,
    FOCUS_ATTACH_THREAD,
    FOCUS_FLASH_ONLY
} WixenFocusMethod;

/* Result of a focus acquisition attempt */
typedef struct {
    WixenFocusMethod method_used;
    bool success;
    int attempts;
} WixenFocusResult;

#ifdef _WIN32
/* Try each focus method in order, return which one worked.
 * The window must already be shown (ShowWindow + UpdateWindow)
 * before calling this. */
WixenFocusResult wixen_focus_acquire(HWND hwnd);

/* Check whether hwnd is currently the foreground window */
bool wixen_focus_is_foreground(HWND hwnd);
#endif

/* Return a human-readable name for a focus method (for logging) */
const char *wixen_focus_method_name(WixenFocusMethod m);

#endif /* WIXEN_UI_FOCUS_H */
