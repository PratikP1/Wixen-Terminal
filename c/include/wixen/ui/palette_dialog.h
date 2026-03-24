/* palette_dialog.h — Native Win32 command palette (modeless dialog) */
#ifndef WIXEN_UI_PALETTE_DIALOG_H
#define WIXEN_UI_PALETTE_DIALOG_H

#ifdef _WIN32

#include <stdbool.h>
#include <windows.h>

/* Command palette result */
typedef struct {
    char *action;       /* Action ID (heap-allocated, caller frees) */
    char *args;         /* Optional args (heap-allocated, caller frees) */
} WixenPaletteResult;

/* Show the command palette. Returns false if cancelled, true if action selected. */
bool wixen_palette_dialog_show(HWND parent, WixenPaletteResult *out_result);

#endif /* _WIN32 */
#endif /* WIXEN_UI_PALETTE_DIALOG_H */
