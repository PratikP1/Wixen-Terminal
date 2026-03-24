/* test_session.c — Tests for session persistence */
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/session.h"

TEST session_init_empty(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    ASSERT_EQ(0, (int)ss.tab_count);
    ASSERT_EQ(0, (int)ss.active_tab);
    wixen_session_free(&ss);
    PASS();
}

TEST session_add_tabs(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    wixen_session_add_tab(&ss, "Shell 1", "PowerShell", "C:\\Users");
    wixen_session_add_tab(&ss, "Shell 2", "CMD", "C:\\");
    ASSERT_EQ(2, (int)ss.tab_count);
    ASSERT_STR_EQ("Shell 1", ss.tabs[0].title);
    ASSERT_STR_EQ("CMD", ss.tabs[1].profile_name);
    wixen_session_free(&ss);
    PASS();
}

TEST session_save_and_load(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    ss.active_tab = 1;
    wixen_session_add_tab(&ss, "Tab1", "pwsh", "/home/user");
    wixen_session_add_tab(&ss, "Tab2", "cmd", "C:\\Windows");

    const char *path = "test_session_tmp.json";
    ASSERT(wixen_session_save(&ss, path));
    wixen_session_free(&ss);

    /* Reload */
    WixenSessionState loaded;
    ASSERT(wixen_session_load(&loaded, path));
    ASSERT_EQ(2, (int)loaded.tab_count);
    ASSERT_EQ(1, (int)loaded.active_tab);
    ASSERT_STR_EQ("Tab1", loaded.tabs[0].title);
    ASSERT_STR_EQ("pwsh", loaded.tabs[0].profile_name);
    ASSERT_STR_EQ("C:\\Windows", loaded.tabs[1].working_directory);
    wixen_session_free(&loaded);
    remove(path);
    PASS();
}

TEST session_load_nonexistent(void) {
    WixenSessionState ss;
    ASSERT_FALSE(wixen_session_load(&ss, "nonexistent_session.json"));
    PASS();
}

TEST session_empty_save(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    const char *path = "test_session_empty.json";
    ASSERT(wixen_session_save(&ss, path));

    WixenSessionState loaded;
    ASSERT(wixen_session_load(&loaded, path));
    ASSERT_EQ(0, (int)loaded.tab_count);
    wixen_session_free(&loaded);
    wixen_session_free(&ss);
    remove(path);
    PASS();
}

SUITE(session_tests) {
    RUN_TEST(session_init_empty);
    RUN_TEST(session_add_tabs);
    RUN_TEST(session_save_and_load);
    RUN_TEST(session_load_nonexistent);
    RUN_TEST(session_empty_save);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(session_tests);
    GREATEST_MAIN_END();
}
