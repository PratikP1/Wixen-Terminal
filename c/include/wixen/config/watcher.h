/* watcher.h — Config file watcher for hot-reload */
#ifndef WIXEN_CONFIG_WATCHER_H
#define WIXEN_CONFIG_WATCHER_H

#ifdef _WIN32

#include <stdbool.h>
#include <windows.h>

typedef struct {
    HANDLE dir_handle;
    HANDLE thread;
    HWND notify_hwnd;
    volatile bool running;
    volatile bool changed;  /* Set by watcher thread, cleared by main loop */
    char path[MAX_PATH];
} WixenConfigWatcher;

#define WM_CONFIG_CHANGED (WM_APP + 20)

bool wixen_watcher_start(WixenConfigWatcher *w, const char *config_path, HWND notify_hwnd);
void wixen_watcher_stop(WixenConfigWatcher *w);

/* Lightweight poll-based checker (for tests and simple use) */
typedef struct {
    char path[MAX_PATH];
    FILETIME last_write;
    bool valid;
} WixenConfigPollWatcher;

bool wixen_config_watcher_init(WixenConfigPollWatcher *w, const char *path);
bool wixen_config_watcher_check(WixenConfigPollWatcher *w);
void wixen_config_watcher_destroy(WixenConfigPollWatcher *w);

#endif /* _WIN32 */
#endif /* WIXEN_CONFIG_WATCHER_H */
