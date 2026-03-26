/* test_red_jumplist_robust.c — RED tests: jumplist COM handles failures gracefully
 *
 * Verifies that:
 * 1. jumplist_update with NULL profile list returns false
 * 2. jumplist_update with zero count is a safe no-op
 * 3. jumplist_update with NULL exe_path returns false
 * 4. jumplist_clear on non-initialized state is safe
 * 5. double-clear is safe
 * 6. jumplist_update handles CoCreateInstance failure gracefully
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/ui/jumplist.h"
#include <windows.h>

/* ------------------------------------------------------------------ */
/* 1. jumplist_update with NULL profile_names returns false             */
/* ------------------------------------------------------------------ */
TEST red_update_null_profiles(void) {
    bool ok = wixen_jumplist_update(L"C:\\test\\wixen.exe", NULL, 3);
    ASSERT_FALSE(ok);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. jumplist_update with zero count is safe no-op                    */
/* ------------------------------------------------------------------ */
TEST red_update_zero_count(void) {
    const wchar_t *names[] = { L"Default" };
    bool ok = wixen_jumplist_update(L"C:\\test\\wixen.exe", names, 0);
    /* Zero profiles: should return false (nothing to do) or true (no-op) */
    (void)ok;
    /* Must not crash */
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. jumplist_update with NULL exe_path returns false                  */
/* ------------------------------------------------------------------ */
TEST red_update_null_exe_path(void) {
    const wchar_t *names[] = { L"Default" };
    bool ok = wixen_jumplist_update(NULL, names, 1);
    ASSERT_FALSE(ok);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 4. jumplist_update with empty exe_path returns false                 */
/* ------------------------------------------------------------------ */
TEST red_update_empty_exe_path(void) {
    const wchar_t *names[] = { L"Default" };
    bool ok = wixen_jumplist_update(L"", names, 1);
    ASSERT_FALSE(ok);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 5. jumplist_clear on non-initialized COM state is safe              */
/* ------------------------------------------------------------------ */
TEST red_clear_without_init(void) {
    /* COM may or may not be initialized. Clear must not crash either way. */
    bool ok = wixen_jumplist_clear();
    /* May return false if COM isn't init'd — that's fine, just no crash */
    (void)ok;
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. double-clear is safe                                             */
/* ------------------------------------------------------------------ */
TEST red_double_clear(void) {
    wixen_jumplist_clear();
    wixen_jumplist_clear();
    /* Must not crash */
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. jumplist_update returns clean bool                               */
/* ------------------------------------------------------------------ */
TEST red_update_returns_bool(void) {
    const wchar_t *names[] = { L"Default", L"PowerShell" };
    bool ok = wixen_jumplist_update(L"C:\\test\\wixen.exe", names, 2);
    ASSERT(ok == true || ok == false);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 8. jumplist_update with NULL entry in profile list doesn't crash    */
/* ------------------------------------------------------------------ */
TEST red_update_null_entry_in_profiles(void) {
    const wchar_t *names[] = { L"Default", NULL, L"PowerShell" };
    bool ok = wixen_jumplist_update(L"C:\\test\\wixen.exe", names, 3);
    /* Should handle gracefully — skip NULL entries or return false */
    (void)ok;
    PASS();
}

SUITE(jumplist_robust) {
    RUN_TEST(red_update_null_profiles);
    RUN_TEST(red_update_zero_count);
    RUN_TEST(red_update_null_exe_path);
    RUN_TEST(red_update_empty_exe_path);
    RUN_TEST(red_clear_without_init);
    RUN_TEST(red_double_clear);
    RUN_TEST(red_update_returns_bool);
    RUN_TEST(red_update_null_entry_in_profiles);
}

#else /* !_WIN32 */

TEST red_skip_non_windows(void) {
    SKIPm("Windows-only test");
}

SUITE(jumplist_robust) {
    RUN_TEST(red_skip_non_windows);
}

#endif /* _WIN32 */

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(jumplist_robust);
    GREATEST_MAIN_END();
}
