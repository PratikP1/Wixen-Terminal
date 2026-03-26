/* shutdown.h — Shutdown lifecycle state machine
 *
 * Tracks shutdown phases so the watchdog only force-kills when cleanup
 * genuinely stalls, not on every normal exit.
 *
 * Phases: RUNNING -> SHUTTING_DOWN -> COMPLETE
 */
#ifndef WIXEN_CORE_SHUTDOWN_H
#define WIXEN_CORE_SHUTDOWN_H

#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdint.h>
#include <time.h>
typedef unsigned long DWORD;
#endif

typedef enum {
    WIXEN_SHUTDOWN_RUNNING,
    WIXEN_SHUTDOWN_SHUTTING_DOWN,
    WIXEN_SHUTDOWN_COMPLETE
} WixenShutdownPhase;

typedef struct {
    WixenShutdownPhase phase;
#ifdef _WIN32
    DWORD begin_tick;  /* GetTickCount() at begin */
#else
    uint64_t begin_tick;
#endif
} WixenShutdownState;

/* Initialize to RUNNING phase. */
void wixen_shutdown_init(WixenShutdownState *st);

/* Transition to SHUTTING_DOWN and record timestamp. */
void wixen_shutdown_begin(WixenShutdownState *st);

/* Mark shutdown as COMPLETE. Safe to call multiple times. */
void wixen_shutdown_mark_complete(WixenShutdownState *st);

/* Returns true if phase is COMPLETE. */
bool wixen_shutdown_is_complete(const WixenShutdownState *st);

/* Returns true only if SHUTTING_DOWN and elapsed time > timeout_ms.
 * Returns false if RUNNING or COMPLETE. */
bool wixen_shutdown_should_force_kill(const WixenShutdownState *st, DWORD timeout_ms);

/* Milliseconds since begin() was called. Returns 0 if not yet begun. */
DWORD wixen_shutdown_elapsed_ms(const WixenShutdownState *st);

#endif /* WIXEN_CORE_SHUTDOWN_H */
