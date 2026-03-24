/* tray.h — System tray icon */
#ifndef WIXEN_UI_TRAY_H
#define WIXEN_UI_TRAY_H

#ifdef _WIN32

#include <stdbool.h>
#include <windows.h>

#define WM_TRAY_CALLBACK (WM_APP + 10)

typedef enum {
    WIXEN_TRAY_SHOW_HIDE = 1,
    WIXEN_TRAY_NEW_TAB = 2,
    WIXEN_TRAY_SETTINGS = 3,
    WIXEN_TRAY_EXIT = 100,
} WixenTrayAction;

typedef struct {
    HWND hwnd;
    NOTIFYICONDATAW nid;
    bool active;
} WixenTrayIcon;

bool wixen_tray_create(WixenTrayIcon *tray, HWND hwnd, const wchar_t *tooltip);
void wixen_tray_destroy(WixenTrayIcon *tray);
void wixen_tray_show_menu(WixenTrayIcon *tray);

#endif /* _WIN32 */
#endif /* WIXEN_UI_TRAY_H */
