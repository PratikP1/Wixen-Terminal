/* explorer_menu.h — Windows Explorer context menu integration */
#ifndef WIXEN_UI_EXPLORER_MENU_H
#define WIXEN_UI_EXPLORER_MENU_H

#ifdef _WIN32

#include <stdbool.h>
#include <wchar.h>

/* Register "Open Wixen Terminal here" in Explorer context menu */
bool wixen_explorer_menu_register(const wchar_t *exe_path, const wchar_t *label);

/* Unregister the Explorer context menu entry */
bool wixen_explorer_menu_unregister(void);

/* Check if already registered */
bool wixen_explorer_menu_is_registered(void);

#endif /* _WIN32 */
#endif /* WIXEN_UI_EXPLORER_MENU_H */
