/* default_terminal.h — Windows default terminal integration */
#ifndef WIXEN_UI_DEFAULT_TERMINAL_H
#define WIXEN_UI_DEFAULT_TERMINAL_H

#ifdef _WIN32
#include <stdbool.h>
#include <wchar.h>

/* Check if Wixen is the default terminal */
bool wixen_is_default_terminal(void);

/* Register as the default terminal (requires elevation on some systems) */
bool wixen_set_default_terminal(const wchar_t *exe_path);

#endif /* _WIN32 */
#endif /* WIXEN_UI_DEFAULT_TERMINAL_H */
