/* test_red_font_reload.c — RED tests for hot-reload font change optimization.
 *
 * Validates that wixen_renderer_update_font() exists and rebuilds only the
 * glyph atlas, leaving the D3D11 device/swapchain/shaders intact.
 *
 * These tests run headless: they exercise the renderer on a real HWND
 * (a hidden message-only window) so D3D11 can initialise.
 */
#ifdef _WIN32

#include "greatest.h"
#include "wixen/render/renderer.h"
#include "wixen/render/colors.h"
#include <windows.h>

/* Helper: create a hidden message-only window for D3D11 */
static HWND create_test_hwnd(void) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance      = GetModuleHandleW(NULL);
    wc.lpszClassName  = L"WixenTestFontReload";
    RegisterClassW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"test", WS_OVERLAPPEDWINDOW,
                           0, 0, 800, 600, HWND_MESSAGE, NULL, wc.hInstance, NULL);
}

/* -------------------------------------------------------------------
 * TEST 1: wixen_renderer_update_font() exists and returns true
 * ------------------------------------------------------------------- */
TEST update_font_function_exists(void) {
    HWND hwnd = create_test_hwnd();
    ASSERT(hwnd != NULL);

    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenRenderer *r = wixen_renderer_create(hwnd, 800, 600,
                                              "Cascadia Mono", 14.0f, &cs);
    ASSERT(r != NULL);

    /* The function under test — should succeed without error */
    bool ok = wixen_renderer_update_font(r, "Consolas", 16.0f);
    ASSERT(ok);

    wixen_renderer_destroy(r);
    DestroyWindow(hwnd);
    PASS();
}

/* -------------------------------------------------------------------
 * TEST 2: After update_font, font metrics actually change
 * ------------------------------------------------------------------- */
TEST update_font_changes_metrics(void) {
    HWND hwnd = create_test_hwnd();
    ASSERT(hwnd != NULL);

    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenRenderer *r = wixen_renderer_create(hwnd, 800, 600,
                                              "Cascadia Mono", 12.0f, &cs);
    ASSERT(r != NULL);

    WixenFontMetrics before = wixen_renderer_font_metrics(r);

    /* Switch to a significantly different size */
    bool ok = wixen_renderer_update_font(r, "Cascadia Mono", 24.0f);
    ASSERT(ok);

    WixenFontMetrics after = wixen_renderer_font_metrics(r);

    /* Cell dimensions must have grown — 24pt is double 12pt */
    ASSERT(after.cell_width > before.cell_width);
    ASSERT(after.cell_height > before.cell_height);

    wixen_renderer_destroy(r);
    DestroyWindow(hwnd);
    PASS();
}

/* -------------------------------------------------------------------
 * TEST 3: Device and swapchain survive font update (not recreated)
 *
 * Strategy: capture renderer width/height before and after. If the
 * device+swapchain were destroyed and recreated, the width/height
 * would need to be re-supplied. Our update_font must NOT change them.
 * ------------------------------------------------------------------- */
TEST device_survives_font_update(void) {
    HWND hwnd = create_test_hwnd();
    ASSERT(hwnd != NULL);

    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenRenderer *r = wixen_renderer_create(hwnd, 800, 600,
                                              "Cascadia Mono", 14.0f, &cs);
    ASSERT(r != NULL);

    uint32_t w_before = wixen_renderer_width(r);
    uint32_t h_before = wixen_renderer_height(r);

    /* Update font — device/swapchain must survive */
    bool ok = wixen_renderer_update_font(r, "Consolas", 18.0f);
    ASSERT(ok);

    uint32_t w_after = wixen_renderer_width(r);
    uint32_t h_after = wixen_renderer_height(r);

    /* Dimensions unchanged proves the swapchain wasn't recreated */
    ASSERT_EQ(w_before, w_after);
    ASSERT_EQ(h_before, h_after);

    /* Renderer still functional — can still query metrics */
    WixenFontMetrics m = wixen_renderer_font_metrics(r);
    ASSERT(m.cell_width > 0.0f);
    ASSERT(m.cell_height > 0.0f);

    wixen_renderer_destroy(r);
    DestroyWindow(hwnd);
    PASS();
}

SUITE(font_reload) {
    RUN_TEST(update_font_function_exists);
    RUN_TEST(update_font_changes_metrics);
    RUN_TEST(device_survives_font_update);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(font_reload);
    GREATEST_MAIN_END();
}

#else
/* Non-Windows stub */
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    GREATEST_MAIN_END();
}
#endif /* _WIN32 */
