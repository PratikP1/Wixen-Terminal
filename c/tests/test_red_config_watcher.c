/* test_red_config_watcher.c — RED tests for config file change detection
 *
 * The watcher monitors a config file and signals when it changes.
 * Tests use a temp file to simulate edits.
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/watcher.h"
#include <windows.h>

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(content, f); fclose(f); }
}

TEST red_watcher_init_destroy(void) {
    WixenConfigPollWatcher w;
    const char *path = "test_watch_init.toml";
    write_file(path, "[font]\nsize = 14\n");
    ASSERT(wixen_config_watcher_init(&w, path));
    wixen_config_watcher_destroy(&w);
    remove(path);
    PASS();
}

TEST red_watcher_no_change(void) {
    WixenConfigPollWatcher w;
    const char *path = "test_watch_nochange.toml";
    write_file(path, "[font]\nsize = 14\n");
    ASSERT(wixen_config_watcher_init(&w, path));
    /* No modification — should not detect change */
    ASSERT_FALSE(wixen_config_watcher_check(&w));
    wixen_config_watcher_destroy(&w);
    remove(path);
    PASS();
}

TEST red_watcher_detects_change(void) {
    WixenConfigPollWatcher w;
    const char *path = "test_watch_change.toml";
    write_file(path, "[font]\nsize = 14\n");
    ASSERT(wixen_config_watcher_init(&w, path));
    /* Ensure timestamp differs */
    Sleep(50);
    /* Modify the file */
    write_file(path, "[font]\nsize = 18\n");
    /* Should detect change */
    ASSERT(wixen_config_watcher_check(&w));
    /* After check, should reset (no repeat trigger) */
    ASSERT_FALSE(wixen_config_watcher_check(&w));
    wixen_config_watcher_destroy(&w);
    remove(path);
    PASS();
}

TEST red_watcher_deleted_file(void) {
    WixenConfigPollWatcher w;
    const char *path = "test_watch_delete.toml";
    write_file(path, "content\n");
    ASSERT(wixen_config_watcher_init(&w, path));
    remove(path);
    /* Should not crash on deleted file */
    ASSERT_FALSE(wixen_config_watcher_check(&w));
    wixen_config_watcher_destroy(&w);
    PASS();
}

TEST red_watcher_nonexistent(void) {
    WixenConfigPollWatcher w;
    /* Init with nonexistent file should fail gracefully */
    ASSERT_FALSE(wixen_config_watcher_init(&w, "does_not_exist_ever.toml"));
    PASS();
}

SUITE(red_config_watcher) {
    RUN_TEST(red_watcher_init_destroy);
    RUN_TEST(red_watcher_no_change);
    RUN_TEST(red_watcher_detects_change);
    RUN_TEST(red_watcher_deleted_file);
    RUN_TEST(red_watcher_nonexistent);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_config_watcher);
    GREATEST_MAIN_END();
}

#else
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) { GREATEST_MAIN_BEGIN(); GREATEST_MAIN_END(); }
#endif
