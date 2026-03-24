/* jumplist.h — Windows taskbar jumplist */
#ifndef WIXEN_UI_JUMPLIST_H
#define WIXEN_UI_JUMPLIST_H

#ifdef _WIN32
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/* Update the taskbar jumplist with recent profiles */
bool wixen_jumplist_update(const wchar_t *exe_path,
                            const wchar_t **profile_names,
                            size_t profile_count);

/* Clear the jumplist */
bool wixen_jumplist_clear(void);

#endif /* _WIN32 */
#endif /* WIXEN_UI_JUMPLIST_H */
