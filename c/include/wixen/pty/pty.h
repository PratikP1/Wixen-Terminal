/* pty.h — ConPTY wrapper for spawning shell processes */
#ifndef WIXEN_PTY_H
#define WIXEN_PTY_H

#ifdef _WIN32

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

typedef struct {
    HPCON hpc;                /* Pseudo console handle */
    HANDLE pipe_in_read;      /* PTY reads from this */
    HANDLE pipe_in_write;     /* We write to this (child stdin) */
    HANDLE pipe_out_read;     /* We read from this (child stdout) */
    HANDLE pipe_out_write;    /* PTY writes to this */
    HANDLE process;           /* Child process handle */
    HANDLE thread;            /* Child main thread handle */
    HANDLE reader_thread;     /* Our reader thread */
    HWND notify_hwnd;         /* Window to post WM_APP messages */
    uint16_t cols;
    uint16_t rows;
    volatile bool running;    /* Reader thread exit flag */
} WixenPty;

/* Output event (posted to notify_hwnd via WM_APP+1) */
#define WM_PTY_OUTPUT (WM_APP + 1)
#define WM_PTY_EXITED (WM_APP + 2)

typedef struct {
    uint8_t *data;
    size_t len;
} WixenPtyOutputEvent;

/* Lifecycle */
bool wixen_pty_spawn(WixenPty *pty, uint16_t cols, uint16_t rows,
                     const wchar_t *program, const wchar_t *args,
                     const wchar_t *cwd, HWND notify_hwnd);
void wixen_pty_close(WixenPty *pty);

/* I/O */
bool wixen_pty_write(WixenPty *pty, const void *data, size_t len);
bool wixen_pty_resize(WixenPty *pty, uint16_t cols, uint16_t rows);

/* Shell auto-detection: returns heap-allocated wide string. Caller frees. */
wchar_t *wixen_pty_detect_shell(void);

#endif /* _WIN32 */
#endif /* WIXEN_PTY_H */
