/* startup_timer.c --- Phase-based startup timing measurement */
#include "wixen/core/startup_timer.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

static int64_t now_us(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (int64_t)((double)counter.QuadPart / (double)freq.QuadPart * 1000000.0);
}

#else
#include <time.h>

static int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#endif

void wixen_timer_init(WixenStartupTimer *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->start_us = now_us();
}

void wixen_timer_mark(WixenStartupTimer *t, const char *name) {
    if (!t || t->count >= WIXEN_TIMER_MAX_MARKS) return;
    WixenTimerMark *m = &t->marks[t->count];
    strncpy(m->name, name ? name : "?", WIXEN_TIMER_NAME_MAX - 1);
    m->name[WIXEN_TIMER_NAME_MAX - 1] = '\0';
    m->timestamp_us = now_us() - t->start_us;
    t->count++;
}

double wixen_timer_total_ms(const WixenStartupTimer *t) {
    if (!t || t->count == 0) return 0.0;
    return (double)t->marks[t->count - 1].timestamp_us / 1000.0;
}

void wixen_timer_log(const WixenStartupTimer *t, const char *log_path) {
    if (!t || !log_path) return;
    FILE *f = fopen(log_path, "w");
    if (!f) return;

    fprintf(f, "=== Wixen Startup Timing ===\n");

    for (size_t i = 0; i < t->count; i++) {
        double elapsed_ms = (double)t->marks[i].timestamp_us / 1000.0;
        double delta_ms = 0.0;
        if (i > 0) {
            delta_ms = (double)(t->marks[i].timestamp_us -
                                t->marks[i - 1].timestamp_us) / 1000.0;
        } else {
            delta_ms = elapsed_ms;
        }
        fprintf(f, "  [%6.1f ms] +%6.1f ms  %s\n", elapsed_ms, delta_ms,
                t->marks[i].name);
    }

    fprintf(f, "  -------------------------\n");
    fprintf(f, "  Total: %.1f ms\n", wixen_timer_total_ms(t));
    fclose(f);
}
