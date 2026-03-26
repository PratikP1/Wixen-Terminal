/* shutdown.c — Shutdown lifecycle state machine implementation */
#include "wixen/core/shutdown.h"

#ifndef _WIN32
#include <time.h>
static uint64_t portable_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
#endif

void wixen_shutdown_init(WixenShutdownState *st) {
    if (!st) return;
    st->phase = WIXEN_SHUTDOWN_RUNNING;
    st->begin_tick = 0;
}

void wixen_shutdown_begin(WixenShutdownState *st) {
    if (!st) return;
    st->phase = WIXEN_SHUTDOWN_SHUTTING_DOWN;
#ifdef _WIN32
    st->begin_tick = GetTickCount();
#else
    st->begin_tick = portable_tick_ms();
#endif
}

void wixen_shutdown_mark_complete(WixenShutdownState *st) {
    if (!st) return;
    st->phase = WIXEN_SHUTDOWN_COMPLETE;
}

bool wixen_shutdown_is_complete(const WixenShutdownState *st) {
    if (!st) return false;
    return st->phase == WIXEN_SHUTDOWN_COMPLETE;
}

bool wixen_shutdown_should_force_kill(const WixenShutdownState *st, DWORD timeout_ms) {
    if (!st) return false;
    /* Only force-kill when actively shutting down but not yet complete. */
    if (st->phase != WIXEN_SHUTDOWN_SHUTTING_DOWN) return false;
    return wixen_shutdown_elapsed_ms(st) > timeout_ms;
}

DWORD wixen_shutdown_elapsed_ms(const WixenShutdownState *st) {
    if (!st || st->phase == WIXEN_SHUTDOWN_RUNNING) return 0;
#ifdef _WIN32
    return GetTickCount() - st->begin_tick;
#else
    return (DWORD)(portable_tick_ms() - st->begin_tick);
#endif
}
