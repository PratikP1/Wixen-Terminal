/* bg_task.c — Background task controller */
#ifdef _WIN32

#include "wixen/core/bg_task.h"
#include <stdbool.h>

void wixen_bg_task_init(WixenBgTask *t) {
    t->thread = NULL;
    t->done = 0;
    t->consumed = 0;
}

bool wixen_bg_task_start(WixenBgTask *t, LPTHREAD_START_ROUTINE fn, void *arg) {
    t->done = 0;
    t->consumed = 0;
    t->thread = CreateThread(NULL, 0, fn, arg, 0, NULL);
    return t->thread != NULL;
}

bool wixen_bg_task_is_done(const WixenBgTask *t) {
    return InterlockedCompareExchange((volatile LONG *)&t->done, 0, 0) != 0;
}

void wixen_bg_task_mark_done(WixenBgTask *t) {
    InterlockedExchange(&t->done, 1);
}

void wixen_bg_task_finalize(WixenBgTask *t) {
    if (InterlockedCompareExchange(&t->consumed, 1, 0) != 0) return;
    if (t->thread) {
        WaitForSingleObject(t->thread, 2000);
        CloseHandle(t->thread);
        t->thread = NULL;
    }
}

void wixen_bg_task_cleanup(WixenBgTask *t) {
    if (InterlockedCompareExchange(&t->consumed, 1, 0) != 0) return;
    if (t->thread) {
        CloseHandle(t->thread);
        t->thread = NULL;
    }
}

#endif /* _WIN32 */
