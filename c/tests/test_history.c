/* test_history.c — Tests for command history */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/history.h"

TEST history_init_empty(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    ASSERT_EQ(0, (int)wixen_history_count(&h));
    ASSERT(wixen_history_get(&h, 0) == NULL);
    wixen_history_free(&h);
    PASS();
}

TEST history_push_and_get(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "ls -la", 0, true, "/home", 1);
    ASSERT_EQ(1, (int)wixen_history_count(&h));
    const WixenHistoryEntry *e = wixen_history_get(&h, 0);
    ASSERT(e != NULL);
    ASSERT_STR_EQ("ls -la", e->command);
    ASSERT_EQ(0, e->exit_code);
    ASSERT(e->has_exit_code);
    ASSERT_STR_EQ("/home", e->cwd);
    ASSERT_EQ(1, (int)e->block_id);
    wixen_history_free(&h);
    PASS();
}

TEST history_newest_first(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "first", 0, true, NULL, 1);
    wixen_history_push(&h, "second", 0, true, NULL, 2);
    wixen_history_push(&h, "third", 0, true, NULL, 3);
    ASSERT_EQ(3, (int)wixen_history_count(&h));
    ASSERT_STR_EQ("third", wixen_history_get(&h, 0)->command);
    ASSERT_STR_EQ("second", wixen_history_get(&h, 1)->command);
    ASSERT_STR_EQ("first", wixen_history_get(&h, 2)->command);
    wixen_history_free(&h);
    PASS();
}

TEST history_dedup_consecutive(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "ls", 0, true, NULL, 1);
    wixen_history_push(&h, "ls", 0, true, NULL, 2);
    ASSERT_EQ(1, (int)wixen_history_count(&h)); /* Deduped */
    wixen_history_push(&h, "pwd", 0, true, NULL, 3);
    wixen_history_push(&h, "ls", 0, true, NULL, 4); /* Different from prev */
    ASSERT_EQ(3, (int)wixen_history_count(&h));
    wixen_history_free(&h);
    PASS();
}

TEST history_push_empty_ignored(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "", 0, true, NULL, 1);
    wixen_history_push(&h, NULL, 0, true, NULL, 2);
    ASSERT_EQ(0, (int)wixen_history_count(&h));
    wixen_history_free(&h);
    PASS();
}

TEST history_clear(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "a", 0, true, NULL, 1);
    wixen_history_push(&h, "b", 0, true, NULL, 2);
    wixen_history_clear(&h);
    ASSERT_EQ(0, (int)wixen_history_count(&h));
    wixen_history_free(&h);
    PASS();
}

TEST history_search_found(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "git status", 0, true, NULL, 1);
    wixen_history_push(&h, "cargo build", 0, true, NULL, 2);
    wixen_history_push(&h, "git log", 0, true, NULL, 3);
    size_t *indices;
    size_t count = wixen_history_search(&h, "git", &indices);
    ASSERT_EQ(2, (int)count);
    free(indices);
    wixen_history_free(&h);
    PASS();
}

TEST history_search_case_insensitive(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "Git Status", 0, true, NULL, 1);
    size_t *indices;
    size_t count = wixen_history_search(&h, "git", &indices);
    ASSERT_EQ(1, (int)count);
    free(indices);
    wixen_history_free(&h);
    PASS();
}

TEST history_search_not_found(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "ls", 0, true, NULL, 1);
    size_t *indices;
    size_t count = wixen_history_search(&h, "xyz", &indices);
    ASSERT_EQ(0, (int)count);
    ASSERT(indices == NULL);
    wixen_history_free(&h);
    PASS();
}

TEST history_recent(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    for (int i = 0; i < 10; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        wixen_history_push(&h, cmd, 0, true, NULL, (uint64_t)i);
    }
    size_t count;
    const WixenHistoryEntry *recent = wixen_history_recent(&h, 3, &count);
    ASSERT_EQ(3, (int)count);
    ASSERT_STR_EQ("cmd9", recent[0].command);
    ASSERT_STR_EQ("cmd8", recent[1].command);
    wixen_history_free(&h);
    PASS();
}

TEST history_get_out_of_range(void) {
    WixenCommandHistory h;
    wixen_history_init(&h);
    wixen_history_push(&h, "x", 0, true, NULL, 1);
    ASSERT(wixen_history_get(&h, 1) == NULL);
    ASSERT(wixen_history_get(&h, 999) == NULL);
    wixen_history_free(&h);
    PASS();
}

SUITE(history_tests) {
    RUN_TEST(history_init_empty);
    RUN_TEST(history_push_and_get);
    RUN_TEST(history_newest_first);
    RUN_TEST(history_dedup_consecutive);
    RUN_TEST(history_push_empty_ignored);
    RUN_TEST(history_clear);
    RUN_TEST(history_search_found);
    RUN_TEST(history_search_case_insensitive);
    RUN_TEST(history_search_not_found);
    RUN_TEST(history_recent);
    RUN_TEST(history_get_out_of_range);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(history_tests);
    GREATEST_MAIN_END();
}
