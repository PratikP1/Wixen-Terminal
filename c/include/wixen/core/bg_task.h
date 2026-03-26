/* bg_task.h — Background task controller
 *
 * Wraps CreateThread so the handle is stored, enabling proper
 * lifecycle management: wait, close, and double-finalize safety.
 */
#ifndef WIXEN_CORE_BG_TASK_H
#define WIXEN_CORE_BG_TASK_H

#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>

typedef struct WixenBgTask {
    HANDLE          thread;
    volatile LONG   done;
    volatile LONG   consumed;
} WixenBgTask;

/* Zero-initialize all fields. */
void wixen_bg_task_init(WixenBgTask *t);

/* Start the background thread. Stores the handle in t->thread.
 * Returns true on success. */
bool wixen_bg_task_start(WixenBgTask *t, LPTHREAD_START_ROUTINE fn, void *arg);

/* Non-blocking check: has the task signalled completion? */
bool wixen_bg_task_is_done(const WixenBgTask *t);

/* Called by the worker to signal it is finished. */
void wixen_bg_task_mark_done(WixenBgTask *t);

/* Wait briefly for the thread, then close the handle.
 * Safe to call multiple times (idempotent). Sets consumed=1. */
void wixen_bg_task_finalize(WixenBgTask *t);

/* Emergency cleanup: if not yet finalized, close the handle.
 * Safe to call after finalize (no-op). */
void wixen_bg_task_cleanup(WixenBgTask *t);

#endif /* _WIN32 */
#endif /* WIXEN_CORE_BG_TASK_H */
