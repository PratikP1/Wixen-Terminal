/* startup_timer.h --- Phase-based startup timing measurement */
#ifndef WIXEN_CORE_STARTUP_TIMER_H
#define WIXEN_CORE_STARTUP_TIMER_H

#include <stddef.h>
#include <stdint.h>

#define WIXEN_TIMER_MAX_MARKS 32
#define WIXEN_TIMER_NAME_MAX  32

typedef struct {
    char name[WIXEN_TIMER_NAME_MAX];
    int64_t timestamp_us;  /* Microseconds since timer init */
} WixenTimerMark;

typedef struct {
    int64_t start_us;                          /* Absolute start (QPC-based) */
    WixenTimerMark marks[WIXEN_TIMER_MAX_MARKS];
    size_t count;
} WixenStartupTimer;

/* Initialize timer and record baseline timestamp */
void wixen_timer_init(WixenStartupTimer *t);

/* Record a named phase timestamp */
void wixen_timer_mark(WixenStartupTimer *t, const char *name);

/* Total elapsed time from init to last mark, in milliseconds */
double wixen_timer_total_ms(const WixenStartupTimer *t);

/* Write timing breakdown to a log file */
void wixen_timer_log(const WixenStartupTimer *t, const char *log_path);

#endif /* WIXEN_CORE_STARTUP_TIMER_H */
