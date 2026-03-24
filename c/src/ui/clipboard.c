/* clipboard.c — Win32 clipboard operations */
#ifdef _WIN32

#include "wixen/ui/clipboard.h"
#include <stdlib.h>
#include <string.h>

bool wixen_clipboard_set_text(HWND hwnd, const char *utf8_text) {
    if (!utf8_text) return false;

    /* Convert UTF-8 to UTF-16 */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, NULL, 0);
    if (wlen <= 0) return false;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(wchar_t));
    if (!hMem) return false;

    wchar_t *pMem = (wchar_t *)GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return false; }
    MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, pMem, wlen);
    GlobalUnlock(hMem);

    if (!OpenClipboard(hwnd)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

char *wixen_clipboard_get_text(HWND hwnd) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return NULL;
    if (!OpenClipboard(hwnd)) return NULL;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return NULL; }

    const wchar_t *pData = (const wchar_t *)GlobalLock(hData);
    if (!pData) { CloseClipboard(); return NULL; }

    /* Convert UTF-16 to UTF-8 */
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, pData, -1, NULL, 0, NULL, NULL);
    char *result = NULL;
    if (utf8_len > 0) {
        result = (char *)malloc((size_t)utf8_len);
        if (result) {
            WideCharToMultiByte(CP_UTF8, 0, pData, -1, result, utf8_len, NULL, NULL);
        }
    }

    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

bool wixen_clipboard_has_text(void) {
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

#endif /* _WIN32 */
