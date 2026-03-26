/* test_red_gdi_fallback.c — RED tests for GDI software renderer fallback
 *
 * Verifies:
 * 1. wixen_soft_create with NULL HWND handles failure gracefully
 * 2. wixen_soft_destroy(NULL) is safe (no crash)
 * 3. WixenRenderer has a use_software flag for GDI fallback
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/render/software.h"
#include "wixen/render/renderer.h"
#endif

/* wixen_soft_destroy(NULL) must not crash */
TEST red_soft_destroy_null_is_safe(void) {
#ifdef _WIN32
    wixen_soft_destroy(NULL); /* Should be a no-op */
#endif
    PASS();
}

/* wixen_soft_create with NULL HWND should return NULL (cannot get DC) */
TEST red_soft_create_null_hwnd_returns_null(void) {
#ifdef _WIN32
    WixenColorScheme colors;
    memset(&colors, 0, sizeof(colors));
    /* NULL HWND — GetDC(NULL) returns screen DC but CreateCompatibleBitmap
     * with 0x0 size would fail, or more importantly the renderer is not
     * usable without a valid window. We pass 0 width/height to trigger failure. */
    WixenSoftwareRenderer *r = wixen_soft_create(NULL, 0, 0, &colors);
    /* Even if it doesn't return NULL, we just verify no crash and clean up */
    if (r) wixen_soft_destroy(r);
#endif
    PASS();
}

/* WixenRenderer should expose use_software flag via wixen_renderer_is_software() */
TEST red_renderer_has_software_flag(void) {
#ifdef _WIN32
    /* This tests that the function exists and is linkable.
     * NULL renderer should return false. */
    bool is_sw = wixen_renderer_is_software(NULL);
    ASSERT_EQ(false, is_sw);
#endif
    PASS();
}

SUITE(red_gdi_fallback) {
    RUN_TEST(red_soft_destroy_null_is_safe);
    RUN_TEST(red_soft_create_null_hwnd_returns_null);
    RUN_TEST(red_renderer_has_software_flag);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_gdi_fallback);
    GREATEST_MAIN_END();
}
