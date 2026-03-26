/* focus.c -- Focus acquisition strategy with diagnostics
 *
 * Refactored from the monolithic force_foreground() in window.c.
 * Each strategy is tried in order and the result struct records
 * which method actually succeeded, enabling debug logging and
 * testability. */

#include "wixen/ui/focus.h"

const char *wixen_focus_method_name(WixenFocusMethod m) {
    switch (m) {
    case FOCUS_NONE:           return "none";
    case FOCUS_DIRECT:         return "direct";
    case FOCUS_ALLOC_CONSOLE:  return "alloc_console";
    case FOCUS_ATTACH_THREAD:  return "attach_thread";
    case FOCUS_FLASH_ONLY:     return "flash_only";
    }
    return "unknown";
}

#ifdef _WIN32

bool wixen_focus_is_foreground(HWND hwnd) {
    if (!hwnd) return false;
    if (!IsWindow(hwnd)) return false;
    return GetForegroundWindow() == hwnd;
}

WixenFocusResult wixen_focus_acquire(HWND hwnd) {
    WixenFocusResult result = { FOCUS_NONE, false, 0 };

    if (!hwnd || !IsWindow(hwnd)) {
        return result;
    }

    /* Strategy 1: Direct SetForegroundWindow */
    result.attempts++;
    if (SetForegroundWindow(hwnd)) {
        SetFocus(hwnd);
        if (wixen_focus_is_foreground(hwnd)) {
            result.method_used = FOCUS_DIRECT;
            result.success = true;
            return result;
        }
    }

    /* Strategy 2: AllocConsole trick -- console creation carries
     * foreground rights. Safe for ConPTY since pseudo consoles
     * are separate from the process console. */
    result.attempts++;
    FreeConsole();
    if (AllocConsole()) {
        HWND console = GetConsoleWindow();
        if (console) ShowWindow(console, SW_HIDE);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        FreeConsole();
        if (wixen_focus_is_foreground(hwnd)) {
            result.method_used = FOCUS_ALLOC_CONSOLE;
            result.success = true;
            return result;
        }
    }

    /* Strategy 3: AttachThreadInput trick */
    result.attempts++;
    {
        HWND fg = GetForegroundWindow();
        if (fg) {
            DWORD fg_tid = GetWindowThreadProcessId(fg, NULL);
            DWORD our_tid = GetCurrentThreadId();
            if (fg_tid != our_tid) {
                AttachThreadInput(our_tid, fg_tid, TRUE);
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
                AttachThreadInput(our_tid, fg_tid, FALSE);
                if (wixen_focus_is_foreground(hwnd)) {
                    result.method_used = FOCUS_ATTACH_THREAD;
                    result.success = true;
                    return result;
                }
            }
        }
    }

    /* Strategy 4: Flash taskbar as last resort */
    result.attempts++;
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetFocus(hwnd);
    {
        FLASHWINFO fi = { sizeof(fi), hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0 };
        FlashWindowEx(&fi);
    }
    result.method_used = FOCUS_FLASH_ONLY;
    result.success = wixen_focus_is_foreground(hwnd);
    return result;
}

#endif /* _WIN32 */
