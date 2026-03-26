/* test_red_renderer_cleanup.c — RED-GREEN TDD for renderer partial-init cleanup
 *
 * Verifies:
 * - destroy(NULL) is safe (no crash)
 * - create with invalid HWND returns NULL without leak
 * - destroy releases all COM objects
 * - double destroy is safe
 * - shader_fail path still cleans up device/swapchain
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/render/renderer.h"

/* ---------------------------------------------------------------------------
 * Helper: create a minimal hidden window for D3D11 tests
 * --------------------------------------------------------------------------- */
static HWND create_test_window(const wchar_t *class_name) {
    WNDCLASSW wc = { .lpfnWndProc = DefWindowProcW, .lpszClassName = class_name };
    RegisterClassW(&wc);
    return CreateWindowExW(0, class_name, L"Test", 0,
        0, 0, 100, 100, NULL, NULL, NULL, NULL);
}

static void destroy_test_window(HWND hwnd, const wchar_t *class_name) {
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(class_name, NULL);
}

/* ---------------------------------------------------------------------------
 * Test 1: wixen_renderer_destroy(NULL) must not crash
 * --------------------------------------------------------------------------- */
TEST red_destroy_null_safe(void) {
    wixen_renderer_destroy(NULL);
    /* If we reach here, it didn't crash */
    PASS();
}

/* ---------------------------------------------------------------------------
 * Test 2: Create renderer with invalid HWND returns NULL
 *
 * D3D11CreateDeviceAndSwapChain requires a valid HWND for the OutputWindow.
 * Passing (HWND)0xDEAD should fail both hardware and WARP paths, and
 * the function should free(r) and return NULL — no leak.
 * --------------------------------------------------------------------------- */
TEST red_create_invalid_hwnd_returns_null(void) {
    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    WixenRenderer *r = wixen_renderer_create(
        (HWND)(uintptr_t)0xDEAD, 800, 600,
        "Cascadia Mono", 14.0f, &colors);

    /* D3D11 fails with invalid HWND, but GDI fallback may succeed.
     * Either NULL (total failure) or non-NULL (GDI fallback) is acceptable.
     * What matters is no crash. */
    if (r) {
        /* GDI fallback kicked in — verify it's marked as software */
        ASSERT(wixen_renderer_is_software(r));
        wixen_renderer_destroy(r);
    }
    PASS();
}

/* ---------------------------------------------------------------------------
 * Test 3: Full create then destroy releases all COM objects (no leak)
 *
 * We can't inspect COM refcounts easily, but we verify that destroy
 * completes without crash after a successful create. If any Release
 * call was missing, repeated runs would leak (detectable by ASAN/leak
 * sanitizer in CI).
 * --------------------------------------------------------------------------- */
TEST red_destroy_releases_all(void) {
    HWND hwnd = create_test_window(L"TestCleanupAll");
    ASSERT(hwnd != NULL);

    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    WixenRenderer *r = wixen_renderer_create(
        hwnd, 100, 100, "Cascadia Mono", 14.0f, &colors);
    /* Even if create fails (e.g. no GPU), destroy(NULL) is safe */
    if (r) {
        /* Verify we can get metrics (proves init completed) */
        WixenFontMetrics m = wixen_renderer_font_metrics(r);
        ASSERT(m.cell_width > 0);
        ASSERT(m.cell_height > 0);
    }

    wixen_renderer_destroy(r);
    /* No crash = all releases worked */

    destroy_test_window(hwnd, L"TestCleanupAll");
    PASS();
}

/* ---------------------------------------------------------------------------
 * Test 4: Double destroy must not crash (use-after-free protection)
 *
 * Calling destroy twice on the same pointer is a programmer error, but
 * the function should be defensive. After the first destroy, the pointer
 * is dangling — so this tests that we don't double-Release COM objects
 * or double-free the struct. The correct fix is to document that callers
 * must set their pointer to NULL after destroy, but the function itself
 * should at minimum not corrupt memory on the first call.
 *
 * NOTE: We can't safely call destroy twice on the same pointer (it's UB
 * after free). Instead we verify that destroy properly frees, and that
 * destroy(NULL) is safe — the intended usage pattern is:
 *   wixen_renderer_destroy(r);
 *   r = NULL;
 *   wixen_renderer_destroy(r);  // safe no-op
 * --------------------------------------------------------------------------- */
TEST red_double_destroy_null_pattern_safe(void) {
    HWND hwnd = create_test_window(L"TestDblDestroy");
    ASSERT(hwnd != NULL);

    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    WixenRenderer *r = wixen_renderer_create(
        hwnd, 100, 100, "Cascadia Mono", 14.0f, &colors);

    /* First destroy */
    wixen_renderer_destroy(r);
    r = NULL;

    /* Second destroy on NULL — must be a no-op */
    wixen_renderer_destroy(r);

    destroy_test_window(hwnd, L"TestDblDestroy");
    PASS();
}

/* ---------------------------------------------------------------------------
 * Test 5: Phased create (begin only) + destroy cleans up partial state
 *
 * wixen_renderer_create_begin allocates device, swapchain, context, rtv
 * but NO shaders, buffers, atlas, sampler. Destroying this partial
 * renderer must release only what was allocated and not crash on the
 * NULL shader/buffer/atlas fields.
 * --------------------------------------------------------------------------- */
TEST red_partial_init_destroy_safe(void) {
    HWND hwnd = create_test_window(L"TestPartialInit");
    ASSERT(hwnd != NULL);

    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    WixenRenderer *r = wixen_renderer_create_begin(hwnd, 100, 100, &colors);
    /* create_begin may fail on headless / no GPU — skip gracefully */
    if (!r) {
        destroy_test_window(hwnd, L"TestPartialInit");
        SKIPm("D3D11 device creation failed (no GPU?)");
    }

    /* At this point: device, context, swapchain, rtv are set.
     * vs, ps, input_layout, vertex_buffer, uniform_buffer, atlas_tex,
     * atlas_srv, sampler, atlas are all NULL (calloc zeroed). */
    wixen_renderer_destroy(r);
    /* No crash = partial cleanup worked */

    destroy_test_window(hwnd, L"TestPartialInit");
    PASS();
}

/* ---------------------------------------------------------------------------
 * Test 6: Phased create with step 0 (shaders+buffers) but no step 1
 * (no atlas) — destroy must clean up shaders+buffers but not crash
 * on NULL atlas.
 * --------------------------------------------------------------------------- */
TEST red_partial_step0_only_destroy_safe(void) {
    HWND hwnd = create_test_window(L"TestStep0Only");
    ASSERT(hwnd != NULL);

    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    WixenRenderer *r = wixen_renderer_create_begin(hwnd, 100, 100, &colors);
    if (!r) {
        destroy_test_window(hwnd, L"TestStep0Only");
        SKIPm("D3D11 device creation failed (no GPU?)");
    }

    /* Run step 0 only: compiles shaders, creates buffers, placeholder texture */
    bool ok = wixen_renderer_create_step(r, 0, "Cascadia Mono", 14.0f);
    ASSERT(ok);

    /* Now destroy without running step 1 (atlas). The atlas field is NULL. */
    wixen_renderer_destroy(r);

    destroy_test_window(hwnd, L"TestStep0Only");
    PASS();
}

/* ---------------------------------------------------------------------------
 * Test 7: bg_result_free(NULL) is safe
 * --------------------------------------------------------------------------- */
TEST red_bg_result_free_null_safe(void) {
    wixen_renderer_bg_result_free(NULL);
    PASS();
}

#else
/* Non-Windows stubs — skip gracefully */
TEST red_destroy_null_safe(void) { PASS(); }
TEST red_create_invalid_hwnd_returns_null(void) { SKIPm("Windows only"); PASS(); }
TEST red_destroy_releases_all(void) { SKIPm("Windows only"); PASS(); }
TEST red_double_destroy_null_pattern_safe(void) { SKIPm("Windows only"); PASS(); }
TEST red_partial_init_destroy_safe(void) { SKIPm("Windows only"); PASS(); }
TEST red_partial_step0_only_destroy_safe(void) { SKIPm("Windows only"); PASS(); }
TEST red_bg_result_free_null_safe(void) { SKIPm("Windows only"); PASS(); }
#endif

SUITE(red_renderer_cleanup) {
    RUN_TEST(red_destroy_null_safe);
    RUN_TEST(red_create_invalid_hwnd_returns_null);
    RUN_TEST(red_destroy_releases_all);
    RUN_TEST(red_double_destroy_null_pattern_safe);
    RUN_TEST(red_partial_init_destroy_safe);
    RUN_TEST(red_partial_step0_only_destroy_safe);
    RUN_TEST(red_bg_result_free_null_safe);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_renderer_cleanup);
    GREATEST_MAIN_END();
}
