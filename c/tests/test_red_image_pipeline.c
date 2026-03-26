/* test_red_image_pipeline.c — RED-GREEN TDD: Sixel decode -> image store -> renderer pipeline
 *
 * Tests the full data path:
 *   1. sixel_decode produces non-NULL image data for valid input
 *   2. image_store_add returns a valid image ID
 *   3. image_store_get retrieves the stored image
 *   4. image_store_remove clears a specific image
 *   5. Kitty graphics APC returns "not implemented" / no-op
 *   6. DCS dispatch routes Sixel data through terminal into image store
 */
#include <stdlib.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/sixel.h"
#include "wixen/core/image.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/action.h"

/* ------------------------------------------------------------------ */
/*  1. sixel_decode produces non-NULL image data for valid input       */
/* ------------------------------------------------------------------ */
TEST sixel_decode_produces_pixels(void) {
    /* A minimal valid Sixel stream: define red, paint one column */
    const char *sixel = "#0;2;100;0;0~";
    WixenSixelImage img;
    bool ok = wixen_sixel_decode(sixel, strlen(sixel), &img);
    ASSERT(ok);
    ASSERT(img.pixels != NULL);
    ASSERT(img.width >= 1);
    ASSERT(img.height >= 6);
    /* First pixel should be opaque red */
    ASSERT_EQ(255, img.pixels[0]); /* R */
    ASSERT_EQ(0,   img.pixels[1]); /* G */
    ASSERT_EQ(0,   img.pixels[2]); /* B */
    ASSERT_EQ(255, img.pixels[3]); /* A */
    wixen_sixel_free(&img);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  2. image_store_add returns a valid image ID                       */
/* ------------------------------------------------------------------ */
TEST image_store_add_returns_valid_id(void) {
    WixenImageStore store;
    wixen_images_init(&store);
    uint8_t *pixels = calloc(4 * 4, 4); /* 4x4 RGBA */
    uint64_t id = wixen_images_add(&store, 4, 4, pixels, 0, 0, 1, 1);
    ASSERT(id > 0);
    ASSERT_EQ(1, (int)wixen_images_count(&store));
    wixen_images_free(&store);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  3. image_store_get retrieves the stored image                     */
/* ------------------------------------------------------------------ */
TEST image_store_get_retrieves(void) {
    WixenImageStore store;
    wixen_images_init(&store);

    uint8_t *px = calloc(2 * 2, 4);
    px[0] = 0xAA; /* Tag byte so we can verify identity */
    uint64_t id = wixen_images_add(&store, 2, 2, px, 5, 10, 3, 4);
    ASSERT(id > 0);

    const WixenTerminalImage *got = wixen_images_get(&store, id);
    ASSERT(got != NULL);
    ASSERT_EQ(id, got->id);
    ASSERT_EQ(2u, got->width);
    ASSERT_EQ(2u, got->height);
    ASSERT_EQ(5, (int)got->col);
    ASSERT_EQ(10, (int)got->row);
    ASSERT_EQ(0xAA, got->pixels[0]);

    /* Non-existent ID returns NULL */
    ASSERT_EQ(NULL, wixen_images_get(&store, 9999));

    wixen_images_free(&store);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  4. image_store_remove clears a specific image                     */
/* ------------------------------------------------------------------ */
TEST image_store_remove_clears(void) {
    WixenImageStore store;
    wixen_images_init(&store);

    uint64_t id1 = wixen_images_add(&store, 1, 1, calloc(4, 1), 0, 0, 1, 1);
    uint64_t id2 = wixen_images_add(&store, 1, 1, calloc(4, 1), 0, 0, 1, 1);
    ASSERT_EQ(2, (int)wixen_images_count(&store));

    bool removed = wixen_images_remove(&store, id1);
    ASSERT(removed);
    ASSERT_EQ(1, (int)wixen_images_count(&store));

    /* id1 should be gone */
    ASSERT_EQ(NULL, wixen_images_get(&store, id1));
    /* id2 should still be there */
    ASSERT(wixen_images_get(&store, id2) != NULL);

    /* Removing non-existent ID returns false */
    ASSERT_FALSE(wixen_images_remove(&store, 9999));

    wixen_images_free(&store);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  5. Kitty graphics APC returns no-op / "not implemented"           */
/* ------------------------------------------------------------------ */
TEST kitty_apc_not_implemented(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);

    /* Fabricate a Kitty APC payload: "_Ga=t,..." */
    const char *payload = "_Ga=t,f=32,s=1,v=1;AAAA";
    WixenAction action;
    memset(&action, 0, sizeof(action));
    action.type = WIXEN_ACTION_APC_DISPATCH;
    action.apc.data = (uint8_t *)payload;
    action.apc.data_len = strlen(payload);

    /* Dispatch should not crash and should queue a "not implemented" response */
    wixen_terminal_dispatch(&t, &action);

    const char *resp = wixen_terminal_pop_response(&t);
    /* We expect either NULL (silent no-op) or a string containing "not" */
    if (resp != NULL) {
        ASSERT(strstr(resp, "not") != NULL || strstr(resp, "NOT") != NULL);
    }
    /* Either way, no crash = success */

    wixen_terminal_free(&t);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  6. DCS Sixel dispatch routes through terminal into image store    */
/* ------------------------------------------------------------------ */
TEST dcs_sixel_routes_to_image_store(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);

    /* Step 1: DCS_HOOK with final_byte 'q' (Sixel start) */
    WixenAction hook = {0};
    hook.type = WIXEN_ACTION_DCS_HOOK;
    hook.dcs_hook.final_byte = 'q';
    hook.dcs_hook.param_count = 0;
    hook.dcs_hook.intermediate_count = 0;
    wixen_terminal_dispatch(&t, &hook);
    ASSERT(t.in_sixel);

    /* Step 2: Feed DCS_PUT bytes — a minimal sixel stream:
     * "#0;2;100;0;0~" = define red + paint one column */
    const char *sixel_data = "#0;2;100;0;0~";
    for (size_t i = 0; i < strlen(sixel_data); i++) {
        WixenAction put = {0};
        put.type = WIXEN_ACTION_DCS_PUT;
        put.dcs_byte = (uint8_t)sixel_data[i];
        wixen_terminal_dispatch(&t, &put);
    }

    /* Step 3: DCS_UNHOOK (Sixel end) — should decode and add to store */
    WixenAction unhook = {0};
    unhook.type = WIXEN_ACTION_DCS_UNHOOK;
    wixen_terminal_dispatch(&t, &unhook);

    ASSERT_FALSE(t.in_sixel);

    /* The terminal's image store should now contain one image */
    ASSERT(wixen_images_count(&t.images) >= 1);

    /* Retrieve the image and verify it has pixels */
    const WixenTerminalImage *img = wixen_images_get(&t.images, 1);
    ASSERT(img != NULL);
    ASSERT(img->width >= 1);
    ASSERT(img->height >= 6);
    ASSERT(img->pixels != NULL);

    wixen_terminal_free(&t);
    PASS();
}

SUITE(image_pipeline_tests) {
    RUN_TEST(sixel_decode_produces_pixels);
    RUN_TEST(image_store_add_returns_valid_id);
    RUN_TEST(image_store_get_retrieves);
    RUN_TEST(image_store_remove_clears);
    RUN_TEST(kitty_apc_not_implemented);
    RUN_TEST(dcs_sixel_routes_to_image_store);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(image_pipeline_tests);
    GREATEST_MAIN_END();
}
