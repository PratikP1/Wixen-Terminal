/* test_red_bg_init.c — RED tests for background renderer initialization
 *
 * D3D11CreateDevice (no swapchain) + shader compile + atlas creation
 * can all happen on a background thread without an HWND.
 * Only CreateSwapChainForHwnd needs the UI thread.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/render/renderer.h"

TEST red_bg_device_no_hwnd(void) {
    /* Create D3D11 device without HWND — must succeed on any GPU system */
    WixenRendererBgResult *bg = wixen_renderer_init_background(
        "Cascadia Mono", 14.0f, 96);
    ASSERT(bg != NULL);
    ASSERT(bg->device != NULL);
    ASSERT(bg->vs_blob != NULL);
    ASSERT(bg->ps_blob != NULL);
    wixen_renderer_bg_result_free(bg);
    PASS();
}

TEST red_bg_atlas_created(void) {
    WixenRendererBgResult *bg = wixen_renderer_init_background(
        "Cascadia Mono", 14.0f, 96);
    ASSERT(bg != NULL);
    ASSERT(bg->atlas != NULL);
    ASSERT(bg->metrics.cell_width > 0);
    ASSERT(bg->metrics.cell_height > 0);
    wixen_renderer_bg_result_free(bg);
    PASS();
}

TEST red_bg_finalize_with_hwnd(void) {
    /* Create a hidden test window */
    WNDCLASSW wc = { .lpfnWndProc = DefWindowProcW, .lpszClassName = L"TestBgInit" };
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"TestBgInit", L"Test", 0,
        0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ASSERT(hwnd != NULL);

    WixenRendererBgResult *bg = wixen_renderer_init_background(
        "Cascadia Mono", 14.0f, 96);
    ASSERT(bg != NULL);

    /* Finalize on UI thread — creates swapchain + binds everything */
    WixenRenderer *r = wixen_renderer_finalize(bg, hwnd, 100, 100);
    ASSERT(r != NULL);

    /* Should be able to render */
    WixenColorScheme colors;
    wixen_colors_init_default(&colors);
    wixen_renderer_clear_present(r, &colors);

    wixen_renderer_destroy(r);
    DestroyWindow(hwnd);
    UnregisterClassW(L"TestBgInit", NULL);
    PASS();
}

#else
TEST red_bg_device_no_hwnd(void) { SKIP(); PASS(); }
TEST red_bg_atlas_created(void) { SKIP(); PASS(); }
TEST red_bg_finalize_with_hwnd(void) { SKIP(); PASS(); }
#endif

SUITE(red_bg_init) {
    RUN_TEST(red_bg_device_no_hwnd);
    RUN_TEST(red_bg_atlas_created);
    RUN_TEST(red_bg_finalize_with_hwnd);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_bg_init);
    GREATEST_MAIN_END();
}
