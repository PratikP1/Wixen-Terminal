/* session.h — Session persistence (save/restore tabs) */
#ifndef WIXEN_CONFIG_SESSION_H
#define WIXEN_CONFIG_SESSION_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *title;
    char *profile_name;
    char *working_directory;
} WixenSavedTab;

typedef struct {
    WixenSavedTab *tabs;
    size_t tab_count;
    size_t active_tab;
} WixenSessionState;

void wixen_session_init(WixenSessionState *ss);
void wixen_session_free(WixenSessionState *ss);
void wixen_session_add_tab(WixenSessionState *ss, const char *title,
                            const char *profile, const char *cwd);
bool wixen_session_save(const WixenSessionState *ss, const char *path);
bool wixen_session_load(WixenSessionState *ss, const char *path);

#endif /* WIXEN_CONFIG_SESSION_H */
