/* palette_dialog.h — Native Win32 command palette */
#ifndef WIXEN_UI_PALETTE_DIALOG_H
#define WIXEN_UI_PALETTE_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

/* Palette entry (testable without Win32) */
typedef struct {
    const char *label;     /* Display text */
    const char *action;    /* Action ID */
    const char *shortcut;  /* Keyboard shortcut hint (or NULL) */
} WixenPaletteEntry;

/* Default palette entries */
const WixenPaletteEntry *wixen_palette_default_entries(size_t *out_count);

/* Visual parameters */
const char *wixen_palette_font_name(void);
int wixen_palette_font_size(void);

#ifdef _WIN32
#include <windows.h>

typedef struct {
    char *action;
    char *args;
} WixenPaletteResult;

bool wixen_palette_dialog_show(HWND parent, WixenPaletteResult *out_result);

#endif /* _WIN32 */
#endif /* WIXEN_UI_PALETTE_DIALOG_H */
