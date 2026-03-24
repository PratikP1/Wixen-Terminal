/* pty.c — ConPTY wrapper for Windows */
#ifdef _WIN32

#include "wixen/pty/pty.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <process.h>

/* Reader thread — reads from pipe_out_read and posts WM_PTY_OUTPUT */
static unsigned __stdcall reader_thread_proc(void *arg) {
    WixenPty *pty = (WixenPty *)arg;
    uint8_t buf[8192];
    DWORD bytes_read;

    while (pty->running) {
        if (!ReadFile(pty->pipe_out_read, buf, sizeof(buf), &bytes_read, NULL)
            || bytes_read == 0) {
            break;
        }

        /* Allocate output event and post to window */
        WixenPtyOutputEvent *evt = malloc(sizeof(WixenPtyOutputEvent));
        if (evt) {
            evt->data = malloc(bytes_read);
            if (evt->data) {
                memcpy(evt->data, buf, bytes_read);
                evt->len = bytes_read;
                PostMessageW(pty->notify_hwnd, WM_PTY_OUTPUT, 0, (LPARAM)evt);
            } else {
                free(evt);
            }
        }
    }

    /* Notify window that process exited */
    DWORD exit_code = 0;
    GetExitCodeProcess(pty->process, &exit_code);
    PostMessageW(pty->notify_hwnd, WM_PTY_EXITED, (WPARAM)exit_code, 0);

    return 0;
}

/* --- Shell detection --- */

wchar_t *wixen_pty_detect_shell(void) {
    /* Try pwsh.exe first (PowerShell 7+) */
    wchar_t path_buf[MAX_PATH];
    if (SearchPathW(NULL, L"pwsh.exe", NULL, MAX_PATH, path_buf, NULL)) {
        size_t len = wcslen(path_buf);
        wchar_t *result = malloc((len + 1) * sizeof(wchar_t));
        if (result) { wcscpy_s(result, len + 1, path_buf); return result; }
    }

    /* Try powershell.exe (Windows PowerShell 5.1) */
    if (SearchPathW(NULL, L"powershell.exe", NULL, MAX_PATH, path_buf, NULL)) {
        size_t len = wcslen(path_buf);
        wchar_t *result = malloc((len + 1) * sizeof(wchar_t));
        if (result) { wcscpy_s(result, len + 1, path_buf); return result; }
    }

    /* Fallback to COMSPEC (cmd.exe) */
    DWORD env_len = GetEnvironmentVariableW(L"COMSPEC", path_buf, MAX_PATH);
    if (env_len > 0 && env_len < MAX_PATH) {
        size_t len = wcslen(path_buf);
        wchar_t *result = malloc((len + 1) * sizeof(wchar_t));
        if (result) { wcscpy_s(result, len + 1, path_buf); return result; }
    }

    /* Last resort */
    const wchar_t *fallback = L"cmd.exe";
    size_t len = wcslen(fallback);
    wchar_t *result = malloc((len + 1) * sizeof(wchar_t));
    if (result) wcscpy_s(result, len + 1, fallback);
    return result;
}

/* --- Spawn --- */

bool wixen_pty_spawn(WixenPty *pty, uint16_t cols, uint16_t rows,
                     const wchar_t *program, const wchar_t *args,
                     const wchar_t *cwd, HWND notify_hwnd) {
    memset(pty, 0, sizeof(*pty));
    pty->cols = cols;
    pty->rows = rows;
    pty->notify_hwnd = notify_hwnd;
    pty->running = true;

    /* Create pipes */
    if (!CreatePipe(&pty->pipe_in_read, &pty->pipe_in_write, NULL, 0)) return false;
    if (!CreatePipe(&pty->pipe_out_read, &pty->pipe_out_write, NULL, 0)) {
        CloseHandle(pty->pipe_in_read);
        CloseHandle(pty->pipe_in_write);
        return false;
    }

    /* Create pseudo console */
    COORD size = { (SHORT)cols, (SHORT)rows };
    HRESULT hr = CreatePseudoConsole(size, pty->pipe_in_read, pty->pipe_out_write, 0, &pty->hpc);
    if (FAILED(hr)) {
        CloseHandle(pty->pipe_in_read); CloseHandle(pty->pipe_in_write);
        CloseHandle(pty->pipe_out_read); CloseHandle(pty->pipe_out_write);
        return false;
    }

    /* Build startup info with pseudo console attribute */
    STARTUPINFOEXW si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!si.lpAttributeList) goto fail;

    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size)) goto fail;
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                    pty->hpc, sizeof(HPCON), NULL, NULL)) goto fail;

    /* Build command line */
    wchar_t cmdline[4096];
    if (args && args[0]) {
        _snwprintf_s(cmdline, 4096, _TRUNCATE, L"\"%s\" %s", program, args);
    } else {
        _snwprintf_s(cmdline, 4096, _TRUNCATE, L"\"%s\"", program);
    }

    /* Create process */
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                         EXTENDED_STARTUPINFO_PRESENT, NULL,
                         cwd && cwd[0] ? cwd : NULL,
                         &si.StartupInfo, &pi)) goto fail;

    pty->process = pi.hProcess;
    pty->thread = pi.hThread;

    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);

    /* Close pipe ends that belong to the child */
    CloseHandle(pty->pipe_out_write);
    pty->pipe_out_write = NULL;
    CloseHandle(pty->pipe_in_read);
    pty->pipe_in_read = NULL;

    /* Start reader thread */
    pty->reader_thread = (HANDLE)_beginthreadex(NULL, 0, reader_thread_proc, pty, 0, NULL);
    if (!pty->reader_thread) goto fail;

    return true;

fail:
    if (si.lpAttributeList) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
    }
    wixen_pty_close(pty);
    return false;
}

/* --- Close --- */

void wixen_pty_close(WixenPty *pty) {
    pty->running = false;

    if (pty->hpc) { ClosePseudoConsole(pty->hpc); pty->hpc = NULL; }
    if (pty->pipe_in_write) { CloseHandle(pty->pipe_in_write); pty->pipe_in_write = NULL; }
    if (pty->pipe_in_read) { CloseHandle(pty->pipe_in_read); pty->pipe_in_read = NULL; }
    if (pty->pipe_out_write) { CloseHandle(pty->pipe_out_write); pty->pipe_out_write = NULL; }
    if (pty->pipe_out_read) { CloseHandle(pty->pipe_out_read); pty->pipe_out_read = NULL; }

    if (pty->reader_thread) {
        WaitForSingleObject(pty->reader_thread, 2000);
        CloseHandle(pty->reader_thread);
        pty->reader_thread = NULL;
    }

    if (pty->process) {
        TerminateProcess(pty->process, 1);
        WaitForSingleObject(pty->process, 1000);
        CloseHandle(pty->process);
        pty->process = NULL;
    }
    if (pty->thread) { CloseHandle(pty->thread); pty->thread = NULL; }
}

/* --- I/O --- */

bool wixen_pty_write(WixenPty *pty, const void *data, size_t len) {
    if (!pty->pipe_in_write || len == 0) return false;
    DWORD written;
    return WriteFile(pty->pipe_in_write, data, (DWORD)len, &written, NULL) != 0;
}

bool wixen_pty_resize(WixenPty *pty, uint16_t cols, uint16_t rows) {
    if (!pty->hpc) return false;
    COORD size = { (SHORT)cols, (SHORT)rows };
    HRESULT hr = ResizePseudoConsole(pty->hpc, size);
    if (SUCCEEDED(hr)) {
        pty->cols = cols;
        pty->rows = rows;
        return true;
    }
    return false;
}

#endif /* _WIN32 */
