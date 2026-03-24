/* settings_dialog.h — Native Win32 settings dialog (PropertySheet) */
#ifndef WIXEN_UI_SETTINGS_DIALOG_H
#define WIXEN_UI_SETTINGS_DIALOG_H

#ifdef _WIN32

#include <stdbool.h>
#include <windows.h>

/* Show the settings dialog (modal). Returns true if settings were changed. */
bool wixen_settings_dialog_show(HWND parent);

#endif /* _WIN32 */
#endif /* WIXEN_UI_SETTINGS_DIALOG_H */
