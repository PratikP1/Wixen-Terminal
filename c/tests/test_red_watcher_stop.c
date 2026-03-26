/* test_red_watcher_stop.c — RED tests for watcher stop/shutdown correctness
 *
 * P1.1: wixen_watcher_stop() must reliably cancel the blocking
 * ReadDirectoryChangesW call issued by the watcher thread, even though
 * the call originates on a different thread. This requires CancelIoEx
 * (not CancelIo, which only cancels I/O from the calling thread).
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/watcher.h"
#include <windows.h>

/* Helper: create a temp directory and config file, returning paths */
static bool setup_temp_config(char *dir_out, size_t dir_sz,
                              char *file_out, size_t file_sz) {
    char tmp[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmp)) return false;
    /* Create a unique subdirectory */
    char subdir[MAX_PATH];
    snprintf(subdir, sizeof(subdir), "%swixen_test_%lu", tmp, GetCurrentProcessId());
    CreateDirectoryA(subdir, NULL);
    strncpy_s(dir_out, dir_sz, subdir, _TRUNCATE);

    snprintf(file_out, file_sz, "%s\\test_config.toml", subdir);
    FILE *f = fopen(file_out, "w");
    if (!f) return false;
    fputs("[font]\nsize = 14\n", f);
    fclose(f);
    return true;
}

static void cleanup_temp(const char *dir, const char *file) {
    if (file) remove(file);
    if (dir) RemoveDirectoryA(dir);
}

/* ------------------------------------------------------------------ */
/* TEST 1: watcher_stop after successful start returns promptly       */
/* ------------------------------------------------------------------ */
TEST red_stop_after_start_returns_promptly(void) {
    char dir[MAX_PATH], file[MAX_PATH];
    ASSERT(setup_temp_config(dir, sizeof(dir), file, sizeof(file)));

    WixenConfigWatcher w;
    bool started = wixen_watcher_start(&w, file, NULL);
    ASSERT(started);

    /* Give the watcher thread a moment to enter ReadDirectoryChangesW */
    Sleep(100);

    /* Stop should return within 2 seconds, not hang forever */
    DWORD t0 = GetTickCount();
    wixen_watcher_stop(&w);
    DWORD elapsed = GetTickCount() - t0;

    /* Should complete well under the 2-second WaitForSingleObject timeout */
    ASSERT(elapsed < 2000);

    cleanup_temp(dir, file);
    PASS();
}

/* ------------------------------------------------------------------ */
/* TEST 2: Double stop is safe (no crash, no double-free)             */
/* ------------------------------------------------------------------ */
TEST red_double_stop_is_safe(void) {
    char dir[MAX_PATH], file[MAX_PATH];
    ASSERT(setup_temp_config(dir, sizeof(dir), file, sizeof(file)));

    WixenConfigWatcher w;
    bool started = wixen_watcher_start(&w, file, NULL);
    ASSERT(started);
    Sleep(50);

    wixen_watcher_stop(&w);
    /* Second stop should not crash or hang */
    wixen_watcher_stop(&w);

    cleanup_temp(dir, file);
    PASS();
}

/* ------------------------------------------------------------------ */
/* TEST 3: Stop on zero-initialized (never started) watcher is safe   */
/* ------------------------------------------------------------------ */
TEST red_stop_without_start_is_safe(void) {
    WixenConfigWatcher w;
    memset(&w, 0, sizeof(w));
    /* dir_handle is 0 (not INVALID_HANDLE_VALUE) after memset,
     * and thread is NULL — stop must handle both gracefully */
    wixen_watcher_stop(&w);
    PASS();
}

/* ------------------------------------------------------------------ */
/* TEST 4: Watcher thread exits within 2 seconds of stop              */
/* ------------------------------------------------------------------ */
TEST red_thread_exits_within_timeout(void) {
    char dir[MAX_PATH], file[MAX_PATH];
    ASSERT(setup_temp_config(dir, sizeof(dir), file, sizeof(file)));

    WixenConfigWatcher w;
    bool started = wixen_watcher_start(&w, file, NULL);
    ASSERT(started);

    /* Let the thread settle into ReadDirectoryChangesW */
    Sleep(200);

    /* Capture thread handle before stop closes it */
    HANDLE thread_copy;
    ASSERT(DuplicateHandle(GetCurrentProcess(), w.thread,
                           GetCurrentProcess(), &thread_copy,
                           SYNCHRONIZE, FALSE, 0));

    wixen_watcher_stop(&w);

    /* The thread must have already exited (stop waits up to 2s internally).
     * Verify it's actually terminated. */
    DWORD wait = WaitForSingleObject(thread_copy, 0);
    ASSERT_EQ(WAIT_OBJECT_0, wait);

    CloseHandle(thread_copy);
    cleanup_temp(dir, file);
    PASS();
}

SUITE(red_watcher_stop) {
    RUN_TEST(red_stop_after_start_returns_promptly);
    RUN_TEST(red_double_stop_is_safe);
    RUN_TEST(red_stop_without_start_is_safe);
    RUN_TEST(red_thread_exits_within_timeout);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_watcher_stop);
    GREATEST_MAIN_END();
}

#else
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) { GREATEST_MAIN_BEGIN(); GREATEST_MAIN_END(); }
#endif
