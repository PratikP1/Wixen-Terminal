/* test_red_sixel_render.c — RED tests for Sixel image rendering pipeline
 *
 * Verifies that:
 * 1. WixenPaneRenderInfo carries an image_store pointer
 * 2. WixenImagePlacement contains x, y, width, height, and texture data
 * 3. wixen_renderer_draw_image() and wixen_renderer_upload_image() exist
 * 4. The image store is accessible from pane render info
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/image.h"
#include "wixen/render/renderer.h"

/* Test 1: WixenPaneRenderInfo has image_store pointer */
TEST red_pane_render_info_has_image_store(void) {
    WixenPaneRenderInfo info;
    memset(&info, 0, sizeof(info));

    WixenImageStore store;
    wixen_images_init(&store);

    info.image_store = &store;
    ASSERT_EQ(&store, info.image_store);
    ASSERT_EQ(0u, info.image_store->count);

    /* Add an image and verify it's visible through the pointer */
    uint8_t *pixels = calloc(4 * 4, 4); /* 4x4 RGBA */
    uint64_t id = wixen_images_add(&store, 4, 4, pixels, 0, 0, 1, 1);
    ASSERT(id != 0);
    ASSERT_EQ(1u, info.image_store->count);

    wixen_images_free(&store);
    PASS();
}

/* Test 2: WixenImagePlacement struct has required fields */
TEST red_image_placement_has_fields(void) {
    WixenImagePlacement placement;
    memset(&placement, 0, sizeof(placement));

    placement.x = 10.0f;
    placement.y = 20.0f;
    placement.width = 100.0f;
    placement.height = 80.0f;

    /* Verify pixel data fields exist */
    uint8_t dummy_pixels[16] = {0};
    placement.pixels = dummy_pixels;
    placement.pixel_width = 4;
    placement.pixel_height = 4;

    ASSERT_EQ(10.0f, placement.x);
    ASSERT_EQ(20.0f, placement.y);
    ASSERT_EQ(100.0f, placement.width);
    ASSERT_EQ(80.0f, placement.height);
    ASSERT_EQ(dummy_pixels, placement.pixels);
    ASSERT_EQ(4u, placement.pixel_width);
    ASSERT_EQ(4u, placement.pixel_height);
    PASS();
}

/* Test 3: wixen_renderer_draw_image exists as a symbol (link test)
 * We can't call it without a real D3D11 device, but we verify the
 * function pointer is non-NULL — i.e. the symbol links. */
TEST red_renderer_draw_image_exists(void) {
    /* Take address of the function to verify it links */
    void (*fn)(WixenRenderer *, const WixenImagePlacement *, void *) =
        wixen_renderer_draw_image;
    ASSERT(fn != NULL);
    PASS();
}

/* Test 4: wixen_renderer_upload_image exists as a symbol (link test) */
TEST red_renderer_upload_image_exists(void) {
    void *(*fn)(WixenRenderer *, const uint8_t *, uint32_t, uint32_t) =
        wixen_renderer_upload_image;
    ASSERT(fn != NULL);
    PASS();
}

/* Test 5: wixen_renderer_release_image exists as a symbol (link test) */
TEST red_renderer_release_image_exists(void) {
    void (*fn)(void *) = wixen_renderer_release_image;
    ASSERT(fn != NULL);
    /* Calling with NULL should be safe (no-op) */
    wixen_renderer_release_image(NULL);
    PASS();
}

/* Test 6: Image store images have grid position and size fields */
TEST red_terminal_image_has_grid_fields(void) {
    WixenTerminalImage img;
    memset(&img, 0, sizeof(img));

    img.col = 5;
    img.row = 10;
    img.cell_cols = 20;
    img.cell_rows = 15;
    img.width = 160;
    img.height = 240;
    img.pixels = NULL;

    ASSERT_EQ(5u, img.col);
    ASSERT_EQ(10u, img.row);
    ASSERT_EQ(20u, img.cell_cols);
    ASSERT_EQ(15u, img.cell_rows);
    ASSERT_EQ(160u, img.width);
    ASSERT_EQ(240u, img.height);
    PASS();
}

/* Test 7: NULL image_store in pane info doesn't break (defensive) */
TEST red_null_image_store_safe(void) {
    WixenPaneRenderInfo info;
    memset(&info, 0, sizeof(info));
    ASSERT(info.image_store == NULL);

    /* Calling render_frame with NULL renderer is safe (tested by existence of guard) */
    wixen_renderer_render_frame(NULL, &info, 1);
    PASS();
}

SUITE(red_sixel_render) {
    RUN_TEST(red_pane_render_info_has_image_store);
    RUN_TEST(red_image_placement_has_fields);
    RUN_TEST(red_renderer_draw_image_exists);
    RUN_TEST(red_renderer_upload_image_exists);
    RUN_TEST(red_renderer_release_image_exists);
    RUN_TEST(red_terminal_image_has_grid_fields);
    RUN_TEST(red_null_image_store_safe);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_sixel_render);
    GREATEST_MAIN_END();
}
