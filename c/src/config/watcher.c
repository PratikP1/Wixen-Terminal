/* watcher.c — Config file watcher using ReadDirectoryChangesW */
#ifdef _WIN32

#include "wixen/config/watcher.h"
#include <stdint.h>
#include <process.h>
#include <string.h>

static unsigned __stdcall watcher_thread(void *arg) {
    WixenConfigWatcher *w = (WixenConfigWatcher *)arg;
    uint8_t buf[4096];
    DWORD bytes;

    while (w->running) {
        if (ReadDirectoryChangesW(
                w->dir_handle, buf, sizeof(buf), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                &bytes, NULL, NULL)) {
            /* Check if our config file was modified */
            FILE_NOTIFY_INFORMATION *info = (FILE_NOTIFY_INFORMATION *)buf;
            while (info) {
                if (info->Action == FILE_ACTION_MODIFIED) {
                    /* Extract filename and compare */
                    PostMessageW(w->notify_hwnd, WM_CONFIG_CHANGED, 0, 0);
                    break;
                }
                if (info->NextEntryOffset == 0) break;
                info = (FILE_NOTIFY_INFORMATION *)((uint8_t *)info + info->NextEntryOffset);
            }
        }
    }

    return 0;
}

bool wixen_watcher_start(WixenConfigWatcher *w, const char *config_path, HWND notify_hwnd) {
    memset(w, 0, sizeof(*w));
    w->notify_hwnd = notify_hwnd;
    w->running = true;
    strncpy_s(w->path, sizeof(w->path), config_path, _TRUNCATE);

    /* Extract directory from config path */
    char dir[MAX_PATH];
    strncpy_s(dir, sizeof(dir), config_path, _TRUNCATE);
    char *last_sep = strrchr(dir, '\\');
    if (!last_sep) last_sep = strrchr(dir, '/');
    if (last_sep) *last_sep = '\0';
    else strncpy_s(dir, sizeof(dir), ".", _TRUNCATE);

    wchar_t dir_w[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, dir_w, MAX_PATH);

    w->dir_handle = CreateFileW(dir_w,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (w->dir_handle == INVALID_HANDLE_VALUE) return false;

    w->thread = (HANDLE)_beginthreadex(NULL, 0, watcher_thread, w, 0, NULL);
    return w->thread != NULL;
}

void wixen_watcher_stop(WixenConfigWatcher *w) {
    w->running = false;
    if (w->dir_handle != INVALID_HANDLE_VALUE) {
        CancelIo(w->dir_handle);
        CloseHandle(w->dir_handle);
        w->dir_handle = INVALID_HANDLE_VALUE;
    }
    if (w->thread) {
        WaitForSingleObject(w->thread, 2000);
        CloseHandle(w->thread);
        w->thread = NULL;
    }
}

#endif /* _WIN32 */
