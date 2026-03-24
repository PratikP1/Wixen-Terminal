/* explorer_menu.c — Explorer context menu via Registry */
#ifdef _WIN32

#include "wixen/ui/explorer_menu.h"
#include <windows.h>
#include <stdio.h>

#define WIXEN_REG_KEY L"Software\\Classes\\Directory\\Background\\shell\\WixenTerminal"
#define WIXEN_REG_CMD L"Software\\Classes\\Directory\\Background\\shell\\WixenTerminal\\command"
#define WIXEN_DIR_KEY L"Software\\Classes\\Directory\\shell\\WixenTerminal"
#define WIXEN_DIR_CMD L"Software\\Classes\\Directory\\shell\\WixenTerminal\\command"

bool wixen_explorer_menu_register(const wchar_t *exe_path, const wchar_t *label) {
    HKEY hkey;
    LONG result;

    /* Background context menu (right-click empty area in folder) */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, WIXEN_REG_KEY,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)label,
                    (DWORD)((wcslen(label) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hkey, L"Icon", 0, REG_SZ, (const BYTE *)exe_path,
                    (DWORD)((wcslen(exe_path) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);

    /* Command */
    wchar_t cmd[MAX_PATH + 32];
    _snwprintf_s(cmd, MAX_PATH + 32, _TRUNCATE, L"\"%s\" --dir \"%%V\"", exe_path);
    result = RegCreateKeyExW(HKEY_CURRENT_USER, WIXEN_REG_CMD,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)cmd,
                    (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);

    /* Directory context menu (right-click on folder) */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, WIXEN_DIR_KEY,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)label,
                    (DWORD)((wcslen(label) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hkey, L"Icon", 0, REG_SZ, (const BYTE *)exe_path,
                    (DWORD)((wcslen(exe_path) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);

    result = RegCreateKeyExW(HKEY_CURRENT_USER, WIXEN_DIR_CMD,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)cmd,
                    (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);

    return true;
}

bool wixen_explorer_menu_unregister(void) {
    RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_REG_KEY);
    RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_DIR_KEY);
    return true;
}

bool wixen_explorer_menu_is_registered(void) {
    HKEY hkey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, WIXEN_REG_KEY, 0, KEY_READ, &hkey);
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return true;
    }
    return false;
}

#endif /* _WIN32 */
