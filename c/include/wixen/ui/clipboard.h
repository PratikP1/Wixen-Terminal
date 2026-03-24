/* clipboard.h — Win32 clipboard operations */
#ifndef WIXEN_UI_CLIPBOARD_H
#define WIXEN_UI_CLIPBOARD_H

#ifdef _WIN32

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

/* Copy UTF-8 text to clipboard. Returns true on success. */
bool wixen_clipboard_set_text(HWND hwnd, const char *utf8_text);

/* Get UTF-8 text from clipboard. Returns heap-allocated string or NULL. Caller frees. */
char *wixen_clipboard_get_text(HWND hwnd);

/* Check if clipboard has text content. */
bool wixen_clipboard_has_text(void);

#endif /* _WIN32 */
#endif /* WIXEN_UI_CLIPBOARD_H */
