/* settings_dialog.h — Native Win32 settings dialog (PropertySheet)
 *
 * 4-tab layout:
 *   Appearance   — Font + Colors + Window chrome
 *   Terminal     — Cursor + Bell + Scrollback + Profiles + Renderer
 *   Accessibility — SR verbosity, audio, prompts, motion
 *   Keybindings  — Standalone listbox
 */
#ifndef WIXEN_UI_SETTINGS_DIALOG_H
#define WIXEN_UI_SETTINGS_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

/* Tab configuration (testable without Win32) */
int wixen_settings_tab_count(void);
const char *wixen_settings_tab_name(int index);
const char **wixen_settings_tab_fields(int tab_index, size_t *out_count);

/* Visual layout parameters */
const char *wixen_settings_dialog_font_name(void);
int wixen_settings_dialog_font_size(void);
int wixen_settings_dialog_width(void);
int wixen_settings_dialog_height(void);
int wixen_settings_dialog_margin(void);
int wixen_settings_dialog_control_spacing(void);
const char **wixen_settings_appearance_groups(size_t *out_count);

#ifdef _WIN32
#include <windows.h>

/* Show the settings dialog (modal). Returns true if settings were changed. */
bool wixen_settings_dialog_show(HWND parent);

#endif /* _WIN32 */
#endif /* WIXEN_UI_SETTINGS_DIALOG_H */
