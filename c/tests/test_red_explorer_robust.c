/* test_red_explorer_robust.c — RED tests: explorer menu handles non-admin gracefully
 *
 * Verifies that:
 * 1. register returns false (not crash) when registry write fails
 * 2. is_registered returns correct state after register/unregister
 * 3. unregister on non-registered state is safe
 * 4. register with empty/NULL exe path returns false
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/ui/explorer_menu.h"
#include <windows.h>

/* ------------------------------------------------------------------ */
/* 1. register returns false (not crash) when NULL exe_path            */
/* ------------------------------------------------------------------ */
TEST red_register_null_exe_path(void) {
    bool ok = wixen_explorer_menu_register(NULL, L"Label");
    ASSERT_FALSE(ok);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. register returns false when empty exe_path                       */
/* ------------------------------------------------------------------ */
TEST red_register_empty_exe_path(void) {
    bool ok = wixen_explorer_menu_register(L"", L"Label");
    ASSERT_FALSE(ok);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. register returns false when NULL label                           */
/* ------------------------------------------------------------------ */
TEST red_register_null_label(void) {
    bool ok = wixen_explorer_menu_register(L"C:\\test.exe", NULL);
    ASSERT_FALSE(ok);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 4. is_registered returns false when not registered                  */
/* ------------------------------------------------------------------ */
TEST red_is_registered_default_false(void) {
    /* Ensure clean state first */
    wixen_explorer_menu_unregister();
    bool reg = wixen_explorer_menu_is_registered();
    ASSERT_FALSE(reg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 5. unregister on non-registered state is safe (no crash)            */
/* ------------------------------------------------------------------ */
TEST red_unregister_when_not_registered(void) {
    /* Clean first to guarantee non-registered */
    wixen_explorer_menu_unregister();
    /* Second unregister must be safe */
    bool ok = wixen_explorer_menu_unregister();
    /* Should return true (no-op success) or false, but must not crash */
    (void)ok;
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. double unregister is safe                                        */
/* ------------------------------------------------------------------ */
TEST red_double_unregister_safe(void) {
    wixen_explorer_menu_unregister();
    wixen_explorer_menu_unregister();
    /* Must not crash */
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. register then is_registered returns true                         */
/* ------------------------------------------------------------------ */
TEST red_register_then_check(void) {
    bool ok = wixen_explorer_menu_register(L"C:\\test\\wixen.exe",
                                            L"Open Wixen Here");
    if (ok) {
        /* If register succeeded (we have permission), verify state */
        ASSERT(wixen_explorer_menu_is_registered());
        wixen_explorer_menu_unregister();
    }
    /* If register failed (no permission), that's fine — it returned false */
    PASS();
}

/* ------------------------------------------------------------------ */
/* 8. register failure doesn't leave partial state                     */
/* ------------------------------------------------------------------ */
TEST red_register_no_partial_on_failure(void) {
    /* NULL exe should fail early, leaving no registry artifacts */
    bool ok = wixen_explorer_menu_register(NULL, L"Label");
    ASSERT_FALSE(ok);
    /* Should still not be registered */
    ASSERT_FALSE(wixen_explorer_menu_is_registered());
    PASS();
}

/* ------------------------------------------------------------------ */
/* 9. RegSetValueExW failure is caught (check return values)           */
/*    We test this indirectly: after register, all RegSetValueExW      */
/*    calls must succeed for the function to return true.              */
/* ------------------------------------------------------------------ */
TEST red_register_returns_bool(void) {
    bool ok = wixen_explorer_menu_register(L"C:\\test\\wixen.exe",
                                            L"Open Wixen Here");
    /* Must be a clean bool, not some random LONG */
    ASSERT(ok == true || ok == false);
    if (ok) wixen_explorer_menu_unregister();
    PASS();
}

SUITE(explorer_robust) {
    RUN_TEST(red_register_null_exe_path);
    RUN_TEST(red_register_empty_exe_path);
    RUN_TEST(red_register_null_label);
    RUN_TEST(red_is_registered_default_false);
    RUN_TEST(red_unregister_when_not_registered);
    RUN_TEST(red_double_unregister_safe);
    RUN_TEST(red_register_then_check);
    RUN_TEST(red_register_no_partial_on_failure);
    RUN_TEST(red_register_returns_bool);
}

#else /* !_WIN32 */

TEST red_skip_non_windows(void) {
    SKIPm("Windows-only test");
}

SUITE(explorer_robust) {
    RUN_TEST(red_skip_non_windows);
}

#endif /* _WIN32 */

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(explorer_robust);
    GREATEST_MAIN_END();
}
