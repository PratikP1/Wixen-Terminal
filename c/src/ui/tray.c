/* tray.c — System tray icon with popup menu */
#ifdef _WIN32

#include "wixen/ui/tray.h"
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

bool wixen_tray_create(WixenTrayIcon *tray, HWND hwnd, const wchar_t *tooltip) {
    memset(tray, 0, sizeof(*tray));
    tray->hwnd = hwnd;

    tray->nid.cbSize = sizeof(NOTIFYICONDATAW);
    tray->nid.hWnd = hwnd;
    tray->nid.uID = 1;
    tray->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray->nid.uCallbackMessage = WM_TRAY_CALLBACK;
    tray->nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);

    if (tooltip) {
        wcsncpy_s(tray->nid.szTip, sizeof(tray->nid.szTip) / sizeof(wchar_t),
                   tooltip, _TRUNCATE);
    }

    tray->active = Shell_NotifyIconW(NIM_ADD, &tray->nid) != FALSE;
    return tray->active;
}

void wixen_tray_destroy(WixenTrayIcon *tray) {
    if (tray->active) {
        Shell_NotifyIconW(NIM_DELETE, &tray->nid);
        tray->active = false;
    }
}

void wixen_tray_show_menu(WixenTrayIcon *tray) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, WIXEN_TRAY_SHOW_HIDE, L"&Show/Hide");
    AppendMenuW(menu, MF_STRING, WIXEN_TRAY_NEW_TAB, L"&New Tab");
    AppendMenuW(menu, MF_STRING, WIXEN_TRAY_SETTINGS, L"S&ettings");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, WIXEN_TRAY_EXIT, L"E&xit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(tray->hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, tray->hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(tray->hwnd, WM_NULL, 0, 0);
}

#endif /* _WIN32 */

/* --- Platform-independent tray menu data --- */
#include "wixen/ui/tray.h"

static const WixenTrayMenuItem tray_items[] = {
    { "Show/Hide Window", 1 },
    { "New Tab",          2 },
    { "Settings",         3 },
    { "Quit Wixen",       100 },
};

const WixenTrayMenuItem *wixen_tray_menu_items(size_t *out_count) {
    *out_count = sizeof(tray_items) / sizeof(tray_items[0]);
    return tray_items;
}
