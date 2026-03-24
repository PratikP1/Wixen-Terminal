/* default_terminal.c — Default terminal registration via Registry */
#ifdef _WIN32

#include "wixen/ui/default_terminal.h"
#include <windows.h>
#include <string.h>

/* Windows 11+ stores default terminal in:
 * HKCU\Console\%%Startup\DelegationTerminal */
#define DEFAULT_TERMINAL_KEY L"Console\\%%Startup"
#define DEFAULT_TERMINAL_VALUE L"DelegationTerminal"

/* Wixen Terminal CLSID (generated) */
static const wchar_t WIXEN_CLSID[] = L"{E6B8D432-7C5A-4E1F-B8D3-F2A1C9E4D567}";

bool wixen_is_default_terminal(void) {
    HKEY hkey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, DEFAULT_TERMINAL_KEY,
                                 0, KEY_READ, &hkey);
    if (result != ERROR_SUCCESS) return false;

    wchar_t value[128] = {0};
    DWORD size = sizeof(value);
    result = RegQueryValueExW(hkey, DEFAULT_TERMINAL_VALUE, NULL, NULL,
                               (BYTE *)value, &size);
    RegCloseKey(hkey);

    if (result != ERROR_SUCCESS) return false;
    return wcscmp(value, WIXEN_CLSID) == 0;
}

bool wixen_set_default_terminal(const wchar_t *exe_path) {
    (void)exe_path;
    HKEY hkey;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, DEFAULT_TERMINAL_KEY,
                                   0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) return false;

    result = RegSetValueExW(hkey, DEFAULT_TERMINAL_VALUE, 0, REG_SZ,
                             (const BYTE *)WIXEN_CLSID,
                             (DWORD)((wcslen(WIXEN_CLSID) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);
    return result == ERROR_SUCCESS;
}

#endif /* _WIN32 */
