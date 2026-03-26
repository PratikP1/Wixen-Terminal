/* test_red_product_wiring.c — RED tests for explorer_menu + jumplist wiring
 *
 * Verifies that:
 * 1. wixen_explorer_menu_register exists and is callable
 * 2. wixen_explorer_menu_unregister exists and is callable
 * 3. wixen_jumplist_update exists and is callable
 * 4. wixen_jumplist_clear exists and is callable
 * 5. Settings dialog exposes context menu fields
 * 6. Config reload triggers jumplist update (helper callable)
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/ui/explorer_menu.h"
#include "wixen/ui/jumplist.h"
#endif

#include "wixen/ui/settings_dialog.h"
#include "wixen/config/config.h"

/* ------------------------------------------------------------------ */
/* 1. explorer_menu_register function exists and is callable           */
/* ------------------------------------------------------------------ */
TEST red_explorer_menu_register_callable(void) {
#ifdef _WIN32
    /* Just verify the symbol links and the function can be called.
     * We pass a dummy exe path — actual registry side-effects are
     * acceptable in CI (HKCU, non-admin, easily cleaned up). */
    bool ok = wixen_explorer_menu_register(L"C:\\test\\wixen.exe",
                                            L"Open Wixen Terminal Here");
    /* On CI the registry write might succeed or fail depending on
     * permissions, but the function MUST be callable without crash. */
    (void)ok;
    /* Clean up any test registry entries */
    wixen_explorer_menu_unregister();
    PASS();
#else
    SKIPm("Windows-only test");
#endif
}

/* ------------------------------------------------------------------ */
/* 2. explorer_menu_unregister function exists and is callable         */
/* ------------------------------------------------------------------ */
TEST red_explorer_menu_unregister_callable(void) {
#ifdef _WIN32
    /* Must not crash even if nothing is registered */
    bool ok = wixen_explorer_menu_unregister();
    ASSERT(ok);
    PASS();
#else
    SKIPm("Windows-only test");
#endif
}

/* ------------------------------------------------------------------ */
/* 3. jumplist_update function exists and is callable                  */
/* ------------------------------------------------------------------ */
TEST red_jumplist_update_callable(void) {
#ifdef _WIN32
    /* COM must be initialized for jumplist operations */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    const wchar_t *profiles[] = { L"PowerShell", L"CMD", L"WSL" };
    bool ok = wixen_jumplist_update(L"C:\\test\\wixen.exe", profiles, 3);
    /* May fail if COM subsystem isn't fully available in CI,
     * but the function MUST link and be callable without crash. */
    (void)ok;

    CoUninitialize();
    PASS();
#else
    SKIPm("Windows-only test");
#endif
}

/* ------------------------------------------------------------------ */
/* 4. jumplist_clear function exists and is callable                   */
/* ------------------------------------------------------------------ */
TEST red_jumplist_clear_callable(void) {
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    bool ok = wixen_jumplist_clear();
    /* Same as above — may fail in CI but must not crash */
    (void)ok;

    CoUninitialize();
    PASS();
#else
    SKIPm("Windows-only test");
#endif
}

/* ------------------------------------------------------------------ */
/* 5. explorer_menu_is_registered exists and is callable               */
/* ------------------------------------------------------------------ */
TEST red_explorer_menu_is_registered_callable(void) {
#ifdef _WIN32
    /* Ensure no stale registration */
    wixen_explorer_menu_unregister();
    bool before = wixen_explorer_menu_is_registered();
    ASSERT_FALSE(before);

    wixen_explorer_menu_register(L"C:\\test\\wixen.exe",
                                  L"Open Wixen Terminal Here");
    bool after = wixen_explorer_menu_is_registered();
    ASSERT(after);

    /* Clean up */
    wixen_explorer_menu_unregister();
    bool cleaned = wixen_explorer_menu_is_registered();
    ASSERT_FALSE(cleaned);
    PASS();
#else
    SKIPm("Windows-only test");
#endif
}

/* ------------------------------------------------------------------ */
/* 6. Settings dialog has Terminal tab with context menu fields        */
/* ------------------------------------------------------------------ */
TEST red_settings_terminal_tab_has_context_menu(void) {
    /* The Terminal tab (index 1) should contain context menu fields */
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(1, &count);
    ASSERT(fields != NULL);
    ASSERT(count > 0);

    /* Check for the context menu register/unregister field */
    bool found_context_menu = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(fields[i], "Explorer context menu") == 0) {
            found_context_menu = true;
            break;
        }
    }
    ASSERT(found_context_menu);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. Config profile names can be extracted for jumplist               */
/* ------------------------------------------------------------------ */
TEST red_config_profiles_for_jumplist(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);

    /* Default config should have at least one profile */
    ASSERT(cfg.profile_count > 0);

    /* Each profile should have a name usable for jumplist */
    for (size_t i = 0; i < cfg.profile_count; i++) {
        ASSERT(cfg.profiles[i].name != NULL);
        ASSERT(strlen(cfg.profiles[i].name) > 0);
    }

    wixen_config_free(&cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Suite                                                               */
/* ------------------------------------------------------------------ */
SUITE(product_wiring_suite) {
    RUN_TEST(red_explorer_menu_register_callable);
    RUN_TEST(red_explorer_menu_unregister_callable);
    RUN_TEST(red_jumplist_update_callable);
    RUN_TEST(red_jumplist_clear_callable);
    RUN_TEST(red_explorer_menu_is_registered_callable);
    RUN_TEST(red_settings_terminal_tab_has_context_menu);
    RUN_TEST(red_config_profiles_for_jumplist);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(product_wiring_suite);
    GREATEST_MAIN_END();
}
