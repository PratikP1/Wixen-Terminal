/* test_red_wm_getobject.c — RED tests for WM_GETOBJECT UIA provider dispatch
 *
 * The WM_GETOBJECT handler must compare lparam against UiaRootObjectId (-25)
 * to return the UIA provider. A wrong constant means NVDA never receives
 * the provider and falls back to MSAA — making the entire a11y layer invisible.
 *
 * BUG: window.c used (DWORD)0xFFFFFFF0 (-16) instead of UiaRootObjectId (-25).
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"

/* Platform-independent functions declared here to avoid
 * pulling in the full window.h / uiautomation.h chain. */
char *wixen_window_format_title(const char *tab_name, const char *cwd);
const char *wixen_window_default_title(void);

#ifdef _WIN32
#include <windows.h>

/* === UiaRootObjectId constant correctness === */

/* The correct value from the Windows SDK. We define it here rather
 * than including uiautomation.h to avoid duplicate symbol link errors. */
#define EXPECTED_UIA_ROOT_OBJECT_ID (-25)

TEST red_wm_getobject_constant_is_not_negative_16(void) {
    /* 0xFFFFFFF0 is -16 as a DWORD. This was the bug in window.c.
     * UiaRootObjectId is -25 (0xFFFFFFE7). They are NOT equal. */
    ASSERT_EQ(-25, EXPECTED_UIA_ROOT_OBJECT_ID);
    ASSERT((DWORD)EXPECTED_UIA_ROOT_OBJECT_ID != (DWORD)0xFFFFFFF0);
    /* Document the actual values for posterity */
    ASSERT_EQ(0xFFFFFFE7u, (DWORD)EXPECTED_UIA_ROOT_OBJECT_ID);
    ASSERT_EQ(0xFFFFFFF0u, (DWORD)-16);
    PASS();
}

/* wixen_wm_getobject_matches returns true when lparam matches
 * UiaRootObjectId. This is the logic extracted from window.c WndProc. */
bool wixen_wm_getobject_matches(LPARAM lparam);

TEST red_getobject_matches_correct_lparam(void) {
    /* Must match when lparam == UiaRootObjectId (-25) */
    ASSERT(wixen_wm_getobject_matches((LPARAM)-25));
    PASS();
}

TEST red_getobject_rejects_wrong_lparam(void) {
    /* Must NOT match -16 (the old bug), 0, or random values */
    ASSERT_FALSE(wixen_wm_getobject_matches((LPARAM)-16));
    ASSERT_FALSE(wixen_wm_getobject_matches((LPARAM)0));
    ASSERT_FALSE(wixen_wm_getobject_matches((LPARAM)-4)); /* OBJID_CLIENT */
    PASS();
}

#endif /* _WIN32 */

/* === Title formatting (platform independent) === */

TEST red_title_always_includes_app_name(void) {
    char *title = wixen_window_format_title("PowerShell", "C:\\Users\\prati");
    ASSERT(title != NULL);
    ASSERT(strstr(title, "Wixen Terminal") != NULL);
    free(title);

    title = wixen_window_format_title(NULL, NULL);
    ASSERT(title != NULL);
    ASSERT(strstr(title, "Wixen Terminal") != NULL);
    free(title);

    title = wixen_window_format_title("Administrator: Windows PowerShell", NULL);
    ASSERT(title != NULL);
    ASSERT(strstr(title, "Wixen Terminal") != NULL);
    free(title);

    PASS();
}

SUITE(wm_getobject_suite) {
#ifdef _WIN32
    RUN_TEST(red_wm_getobject_constant_is_not_negative_16);
    RUN_TEST(red_getobject_matches_correct_lparam);
    RUN_TEST(red_getobject_rejects_wrong_lparam);
#endif
    RUN_TEST(red_title_always_includes_app_name);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(wm_getobject_suite);
    GREATEST_MAIN_END();
}
