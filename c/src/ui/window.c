/* window.c — Win32 window creation and event handling */
#ifdef _WIN32

#include "wixen/ui/window.h"
#include "wixen/ui/tray.h"
#include "wixen/ui/focus.h"
#include "wixen/a11y/provider.h"
#include <stdlib.h>
#include <stdio.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

/* Do NOT include <uiautomation.h> here — it causes duplicate symbol
 * errors when linking with wixen_a11y. All UIA calls go through
 * wixen_a11y_handle_getobject() in provider.c instead. */

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#define WIXEN_CLASS_NAME L"WixenTerminalWindow"
#define MAX_EVENTS 64

/* Event queue stored in window extra data */
typedef struct {
    WixenWindowEvent events[MAX_EVENTS];
    size_t head;
    size_t tail;
    size_t count;
} WixenEventQueue;

static void eq_push(WixenEventQueue *q, const WixenWindowEvent *evt) {
    if (q->count >= MAX_EVENTS) return; /* Drop if full */
    q->events[q->tail] = *evt;
    q->tail = (q->tail + 1) % MAX_EVENTS;
    q->count++;
}

static bool eq_pop(WixenEventQueue *q, WixenWindowEvent *evt) {
    if (q->count == 0) return false;
    *evt = q->events[q->head];
    q->head = (q->head + 1) % MAX_EVENTS;
    q->count--;
    return true;
}

/* WndProc */
static LRESULT CALLBACK wixen_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WixenEventQueue *q = (WixenEventQueue *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        DragAcceptFiles(hwnd, TRUE);
        /* Register a minimal UIA provider immediately so NVDA doesn't
         * cache this window as non-UIA. BUG #23 follow-up: even with
         * deferred ShowWindow, NVDA discovers HWNDs from CreateWindowExW.
         * The provider will be updated with terminal data later. */
        wixen_a11y_provider_init_minimal(hwnd);
        return 0;
    }

    case WM_SIZE:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_RESIZED };
            evt.resize.width = LOWORD(lparam);
            evt.resize.height = HIWORD(lparam);
            eq_push(q, &evt);
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* Alt+F4 must pass through to DefWindowProc for WM_CLOSE.
         * BUG #28: returning 0 consumed the keystroke, preventing close. */
        if (wparam == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) {
            break; /* Fall through to DefWindowProcW */
        }
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_KEY_INPUT };
            evt.key.vk = (uint16_t)wparam;
            evt.key.scan = (uint16_t)((lparam >> 16) & 0xFF);
            evt.key.down = true;
            evt.key.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            evt.key.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            evt.key.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            eq_push(q, &evt);
        }
        return 0;

    case WM_CHAR:
        if (q && wparam >= 32) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_CHAR_INPUT };
            evt.char_input = (uint32_t)wparam;
            eq_push(q, &evt);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_MOUSE_WHEEL };
            evt.wheel.delta = (int16_t)(GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA);
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(hwnd, &pt);
            evt.wheel.x = (int16_t)pt.x;
            evt.wheel.y = (int16_t)pt.y;
            eq_push(q, &evt);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_MOUSE_DOWN };
            evt.mouse.button = WIXEN_MB_LEFT;
            evt.mouse.x = GET_X_LPARAM(lparam);
            evt.mouse.y = GET_Y_LPARAM(lparam);
            eq_push(q, &evt);
        }
        SetCapture(hwnd);
        return 0;

    case WM_LBUTTONUP:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_MOUSE_UP };
            evt.mouse.button = WIXEN_MB_LEFT;
            evt.mouse.x = GET_X_LPARAM(lparam);
            evt.mouse.y = GET_Y_LPARAM(lparam);
            eq_push(q, &evt);
        }
        ReleaseCapture();
        return 0;

    case WM_RBUTTONDOWN:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_MOUSE_DOWN };
            evt.mouse.button = WIXEN_MB_RIGHT;
            evt.mouse.x = GET_X_LPARAM(lparam);
            evt.mouse.y = GET_Y_LPARAM(lparam);
            eq_push(q, &evt);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_MOUSE_MOVE };
            evt.mouse_move.x = GET_X_LPARAM(lparam);
            evt.mouse_move.y = GET_Y_LPARAM(lparam);
            eq_push(q, &evt);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_DOUBLE_CLICK };
            evt.dbl_click.x = GET_X_LPARAM(lparam);
            evt.dbl_click.y = GET_Y_LPARAM(lparam);
            eq_push(q, &evt);
        }
        return 0;

    case WM_CLOSE:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_CLOSE_REQUESTED };
            eq_push(q, &evt);
        }
        return 0; /* Don't call DefWindowProc — we handle close ourselves */

    case WM_SETFOCUS:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_FOCUS_GAINED };
            eq_push(q, &evt);
        }
        return 0;

    case WM_KILLFOCUS:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_FOCUS_LOST };
            eq_push(q, &evt);
        }
        return 0;

    case WM_DPICHANGED:
        if (q) {
            WixenWindowEvent evt = { .type = WIXEN_EVT_DPI_CHANGED };
            evt.dpi = LOWORD(wparam);
            eq_push(q, &evt);
            /* Apply suggested rect */
            RECT *rc = (RECT *)lparam;
            SetWindowPos(hwnd, NULL, rc->left, rc->top,
                         rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_SYSCOMMAND:
        /* Custom system menu items (Alt+Space → Settings) */
        /* System menu custom IDs use SC_* range. Our settings = 0x0100 */
        #define SC_WIXEN_SETTINGS 0x0100
        if ((wparam & 0xFFF0) == SC_WIXEN_SETTINGS) {
            if (q) {
                WixenWindowEvent evt = { .type = WIXEN_EVT_CONTEXT_MENU };
                evt.context_action = WIXEN_CTX_SETTINGS;
                eq_push(q, &evt);
            }
            return 0;
        }
        break; /* Let DefWindowProc handle standard items including Alt+F4 */

    case WM_COMMAND:
        if (q) {
            WORD cmd_id = LOWORD(wparam);
            if (cmd_id >= WIXEN_TRAY_SHOW_HIDE && cmd_id <= WIXEN_TRAY_EXIT) {
                /* Tray popup menu command */
                WixenWindowEvent evt = { .type = WIXEN_EVT_TRAY_COMMAND };
                evt.tray_action = (int)cmd_id;
                eq_push(q, &evt);
            } else {
                /* Context menu command */
                WixenWindowEvent evt = { .type = WIXEN_EVT_CONTEXT_MENU };
                evt.context_action = (WixenContextAction)cmd_id;
                eq_push(q, &evt);
            }
        }
        return 0;

    case WM_GETOBJECT:
        /* Delegate to a11y provider — uses correct UiaRootObjectId constant
         * and calls UiaReturnRawElementProvider. Provider is stored as a
         * window property (set in WM_CREATE via init_minimal). */
        return wixen_a11y_handle_wm_getobject(hwnd, wparam, lparam);

    case WM_TRAY_CALLBACK:
        if (LOWORD(lparam) == WM_RBUTTONUP) {
            /* Right-click on tray icon — show popup menu.
             * We push the menu via wixen_tray_show_menu; the resulting
             * WM_COMMAND is already handled above and enqueues a
             * WIXEN_EVT_TRAY_COMMAND event. */
            if (q) {
                /* Post a synthetic marker so main.c can call
                 * wixen_tray_show_menu() with the tray pointer.
                 * We use tray_action = -1 as "show menu" sentinel. */
                WixenWindowEvent evt = { .type = WIXEN_EVT_TRAY_COMMAND };
                evt.tray_action = -1; /* Sentinel: show tray popup menu */
                eq_push(q, &evt);
            }
            return 0;
        }
        if (LOWORD(lparam) == WM_LBUTTONDBLCLK) {
            /* Double-click on tray icon — show/restore window */
            ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW);
            if (IsWindowVisible(hwnd)) {
                SetForegroundWindow(hwnd);
            }
            return 0;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* --- Lifecycle --- */

bool wixen_window_create(WixenWindow *w, const wchar_t *title,
                          uint32_t width, uint32_t height) {
    memset(w, 0, sizeof(*w));

    /* Per-monitor DPI awareness */
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    /* Register window class */
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wixen_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM);
    wc.lpszClassName = WIXEN_CLASS_NAME;
    RegisterClassExW(&wc);

    /* Allocate event queue */
    WixenEventQueue *q = calloc(1, sizeof(WixenEventQueue));
    if (!q) return false;

    /* Create window HIDDEN. All init (renderer, PTY, tray, config watcher)
     * happens before showing. Then wixen_window_show does a single clean
     * ShowWindow + force_foreground. This avoids the "not responding"
     * state caused by a visible window not pumping messages during init.
     * The UIA provider is registered in WM_CREATE so NVDA doesn't cache
     * us as non-UIA even though the window isn't visible yet. */
    w->hwnd = CreateWindowExW(
        0,
        WIXEN_CLASS_NAME,
        title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)width, (int)height,
        NULL, NULL, GetModuleHandleW(NULL), q);

    if (!w->hwnd) {
        free(q);
        return false;
    }

    w->dpi = GetDpiForWindow(w->hwnd);

    /* Add Settings to system menu (Alt+Space) — Issue #5 */
    {
        HMENU sys_menu = GetSystemMenu(w->hwnd, FALSE);
        if (sys_menu) {
            AppendMenuW(sys_menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(sys_menu, MF_STRING, SC_WIXEN_SETTINGS, L"S&ettings\tCtrl+,");
        }
    }

    /* Don't show yet — caller must init a11y provider first,
     * then call wixen_window_show(). Otherwise NVDA's WM_GETOBJECT
     * arrives before the provider is registered and caches the
     * window as non-UIA permanently. (BUG #23) */
    w->visible = false;

    return true;
}

void wixen_window_show(WixenWindow *w) {
    if (!w->visible && w->hwnd) {
        /* Show the window first — it was created hidden */
        ShowWindow(w->hwnd, SW_SHOWNORMAL);
        UpdateWindow(w->hwnd);

        /* Use the instrumented focus acquisition strategy */
        WixenFocusResult fr = wixen_focus_acquire(w->hwnd);

        /* Log to debug file so we can diagnose focus failures */
        {
            char path[MAX_PATH];
            if (GetTempPathA(MAX_PATH, path)) {
                strncat(path, "wixen_debug.log", MAX_PATH - strlen(path) - 1);
                FILE *f = fopen(path, "a");
                if (f) {
                    fprintf(f, "[focus] method=%s success=%d attempts=%d\n",
                            wixen_focus_method_name(fr.method_used),
                            fr.success, fr.attempts);
                    fclose(f);
                }
            }
        }

        w->visible = true;
    }
}

void wixen_window_destroy(WixenWindow *w) {
    if (w->hwnd) {
        WixenEventQueue *q = (WixenEventQueue *)GetWindowLongPtrW(w->hwnd, GWLP_USERDATA);
        free(q);
        DestroyWindow(w->hwnd);
        w->hwnd = NULL;
    }
}

/* --- Events --- */

bool wixen_window_poll_event(WixenWindow *w, WixenWindowEvent *evt) {
    WixenEventQueue *q = (WixenEventQueue *)GetWindowLongPtrW(w->hwnd, GWLP_USERDATA);
    if (!q) return false;
    return eq_pop(q, evt);
}

/* --- Properties --- */

void wixen_window_set_title(WixenWindow *w, const wchar_t *title) {
    SetWindowTextW(w->hwnd, title);
}

void wixen_window_set_fullscreen(WixenWindow *w, bool fullscreen) {
    if (fullscreen == w->fullscreen) return;
    w->fullscreen = fullscreen;

    if (fullscreen) {
        GetWindowPlacement(w->hwnd, &w->saved_placement);
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(w->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(w->hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(w->hwnd, GWL_STYLE,
                       WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE);
        SetWindowPlacement(w->hwnd, &w->saved_placement);
        SetWindowPos(w->hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

uint32_t wixen_window_get_dpi(const WixenWindow *w) {
    return w->dpi;
}

void wixen_window_request_redraw(WixenWindow *w) {
    InvalidateRect(w->hwnd, NULL, FALSE);
}

void wixen_window_pump_messages(WixenWindow *w) {
    (void)w;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* --- Context menu --- */

void wixen_window_show_context_menu(WixenWindow *w, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_COPY, L"&Copy\tCtrl+Shift+C");
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_PASTE, L"&Paste\tCtrl+Shift+V");
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_SELECT_ALL, L"Select &All");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_SEARCH, L"&Find\tCtrl+Shift+F");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_SPLIT_H, L"Split &Horizontal");
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_SPLIT_V, L"Split &Vertical");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_NEW_TAB, L"&New Tab\tCtrl+Shift+T");
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_CLOSE_TAB, L"C&lose Tab\tCtrl+Shift+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, WIXEN_CTX_SETTINGS, L"S&ettings\tCtrl+,");

    POINT pt = { x, y };
    ClientToScreen(w->hwnd, &pt);
    /* TPM_TOPALIGN ensures first item is at cursor position.
     * Screen readers focus the first item when the menu opens. */
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_TOPALIGN, pt.x, pt.y, 0, w->hwnd, NULL);
    DestroyMenu(menu);
}

#endif /* _WIN32 */

/* --- Platform-independent UI data --- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define strdup _strdup
#endif

static const WixenContextMenuItem ctx_items[] = {
    { "Copy",           "copy",           false },
    { "Paste",          "paste",          false },
    { "Select All",     "select_all",     false },
    { NULL,             NULL,             true  }, /* separator */
    { "Search...",      "search",         false },
    { NULL,             NULL,             true  }, /* separator */
    { "Split Right",    "split_horizontal", false },
    { "Split Down",     "split_vertical",   false },
    { NULL,             NULL,             true  }, /* separator */
    { "Settings",       "settings",       false },
};

const WixenContextMenuItem *wixen_context_menu_items(size_t *out_count) {
    *out_count = sizeof(ctx_items) / sizeof(ctx_items[0]);
    return ctx_items;
}

bool wixen_context_menu_focus_first(void) {
    return true; /* TrackPopupMenu with TPM_TOPALIGN focuses first item */
}

static const WixenSystemMenuItem sys_menu_items[] = {
    { "Settings...", "settings" },
    { "About Wixen Terminal", "about" },
};

const WixenSystemMenuItem *wixen_system_menu_items(size_t *out_count) {
    *out_count = sizeof(sys_menu_items) / sizeof(sys_menu_items[0]);
    return sys_menu_items;
}

const char *wixen_window_default_title(void) {
    return "Wixen Terminal";
}

char *wixen_window_format_title(const char *tab_name, const char *cwd) {
    char buf[512];
    if (cwd && cwd[0]) {
        snprintf(buf, sizeof(buf), "%s \xe2\x80\x94 %s \xe2\x80\x94 Wixen Terminal",
                 tab_name ? tab_name : "Shell", cwd);
    } else if (tab_name && tab_name[0]) {
        snprintf(buf, sizeof(buf), "%s \xe2\x80\x94 Wixen Terminal", tab_name);
    } else {
        snprintf(buf, sizeof(buf), "Wixen Terminal");
    }
    return strdup(buf);
}
