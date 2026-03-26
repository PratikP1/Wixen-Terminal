/* explorer_menu.c — Explorer context menu via Registry */
#ifdef _WIN32

#include "wixen/ui/explorer_menu.h"
#include <windows.h>
#include <stdio.h>

#define WIXEN_REG_KEY L"Software\\Classes\\Directory\\Background\\shell\\WixenTerminal"
#define WIXEN_REG_CMD L"Software\\Classes\\Directory\\Background\\shell\\WixenTerminal\\command"
#define WIXEN_DIR_KEY L"Software\\Classes\\Directory\\shell\\WixenTerminal"
#define WIXEN_DIR_CMD L"Software\\Classes\\Directory\\shell\\WixenTerminal\\command"

/* Helper: set a registry key's default value + icon in one shot.
 * Returns true on success, false on any failure. Closes hkey on failure. */
static bool reg_set_label_and_icon(HKEY hkey, const wchar_t *label,
                                    const wchar_t *icon_path) {
    LONG r;
    r = RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)label,
                        (DWORD)((wcslen(label) + 1) * sizeof(wchar_t)));
    if (r != ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return false;
    }
    r = RegSetValueExW(hkey, L"Icon", 0, REG_SZ, (const BYTE *)icon_path,
                        (DWORD)((wcslen(icon_path) + 1) * sizeof(wchar_t)));
    if (r != ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return false;
    }
    RegCloseKey(hkey);
    return true;
}

/* Helper: create a command subkey with the given command line.
 * Returns true on success, false on any failure. */
static bool reg_set_command(const wchar_t *key_path, const wchar_t *cmd) {
    HKEY hkey;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, key_path,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (r != ERROR_SUCCESS) return false;

    r = RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)cmd,
                        (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);
    return r == ERROR_SUCCESS;
}

bool wixen_explorer_menu_register(const wchar_t *exe_path, const wchar_t *label) {
    /* Validate inputs */
    if (!exe_path || !label) return false;
    if (exe_path[0] == L'\0' || label[0] == L'\0') return false;

    HKEY hkey;
    LONG result;

    /* Build command string */
    wchar_t cmd[MAX_PATH + 32];
    _snwprintf_s(cmd, MAX_PATH + 32, _TRUNCATE, L"\"%s\" --dir \"%%V\"", exe_path);

    /* Background context menu (right-click empty area in folder) */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, WIXEN_REG_KEY,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) return false;
    if (!reg_set_label_and_icon(hkey, label, exe_path)) {
        /* Clean up partially created key */
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_REG_KEY);
        return false;
    }

    if (!reg_set_command(WIXEN_REG_CMD, cmd)) {
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_REG_KEY);
        return false;
    }

    /* Directory context menu (right-click on folder) */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, WIXEN_DIR_KEY,
                              0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) {
        /* Roll back background keys */
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_REG_KEY);
        return false;
    }
    if (!reg_set_label_and_icon(hkey, label, exe_path)) {
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_REG_KEY);
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_DIR_KEY);
        return false;
    }

    if (!reg_set_command(WIXEN_DIR_CMD, cmd)) {
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_REG_KEY);
        RegDeleteTreeW(HKEY_CURRENT_USER, WIXEN_DIR_KEY);
        return false;
    }

    return true;
}

bool wixen_explorer_menu_unregister(void) {
    /* Both calls may fail if keys don't exist — that's fine, not an error */
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
