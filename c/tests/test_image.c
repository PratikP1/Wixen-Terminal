/* test_image.c — Tests for image store and Sixel decoder */
#include <stdbool.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/image.h"

TEST image_store_init(void) {
    WixenImageStore store;
    wixen_images_init(&store);
    ASSERT_EQ(0, (int)wixen_images_count(&store));
    wixen_images_free(&store);
    PASS();
}

TEST image_store_add(void) {
    WixenImageStore store;
    wixen_images_init(&store);
    uint8_t *pixels = calloc(4 * 4, 4);
    uint64_t id = wixen_images_add(&store, 4, 4, pixels, 0, 0, 1, 1);
    ASSERT(id > 0);
    ASSERT_EQ(1, (int)wixen_images_count(&store));
    wixen_images_free(&store);
    PASS();
}

TEST image_store_clear(void) {
    WixenImageStore store;
    wixen_images_init(&store);
    wixen_images_add(&store, 2, 2, calloc(16, 1), 0, 0, 1, 1);
    wixen_images_add(&store, 2, 2, calloc(16, 1), 0, 0, 1, 1);
    ASSERT_EQ(2, (int)wixen_images_count(&store));
    wixen_images_clear(&store);
    ASSERT_EQ(0, (int)wixen_images_count(&store));
    wixen_images_free(&store);
    PASS();
}

TEST sixel_decode_simple(void) {
    /* Simple sixel: one red pixel column (6 pixels high) */
    /* #0;2;100;0;0 defines color 0 as red
     * ~ = 0x7E = all 6 bits set */
    const char *sixel = "#0;2;100;0;0~";
    uint32_t w, h;
    uint8_t *pixels;
    bool ok = wixen_decode_sixel((const uint8_t *)sixel, strlen(sixel), &w, &h, &pixels);
    ASSERT(ok);
    ASSERT(w >= 1);
    ASSERT(h >= 6);
    /* First pixel should be red */
    ASSERT_EQ(255, pixels[0]); /* R */
    ASSERT_EQ(0, pixels[1]);   /* G */
    ASSERT_EQ(0, pixels[2]);   /* B */
    ASSERT_EQ(255, pixels[3]); /* A */
    free(pixels);
    PASS();
}

TEST sixel_decode_empty(void) {
    uint32_t w, h;
    uint8_t *pixels;
    ASSERT_FALSE(wixen_decode_sixel(NULL, 0, &w, &h, &pixels));
    ASSERT_FALSE(wixen_decode_sixel((const uint8_t *)"", 0, &w, &h, &pixels));
    PASS();
}

TEST sixel_decode_newline(void) {
    /* Two rows of sixel data separated by - */
    const char *sixel = "#0;2;0;100;0?-?";
    uint32_t w, h;
    uint8_t *pixels;
    bool ok = wixen_decode_sixel((const uint8_t *)sixel, strlen(sixel), &w, &h, &pixels);
    ASSERT(ok);
    ASSERT(h >= 12); /* Two 6-pixel rows */
    free(pixels);
    PASS();
}

TEST sixel_decode_repeat(void) {
    /* !5~ = repeat ~ five times */
    const char *sixel = "#0;2;50;50;50!5~";
    uint32_t w, h;
    uint8_t *pixels;
    bool ok = wixen_decode_sixel((const uint8_t *)sixel, strlen(sixel), &w, &h, &pixels);
    ASSERT(ok);
    ASSERT(w >= 5);
    free(pixels);
    PASS();
}

SUITE(image_store_tests) {
    RUN_TEST(image_store_init);
    RUN_TEST(image_store_add);
    RUN_TEST(image_store_clear);
}

SUITE(sixel_tests) {
    RUN_TEST(sixel_decode_simple);
    RUN_TEST(sixel_decode_empty);
    RUN_TEST(sixel_decode_newline);
    RUN_TEST(sixel_decode_repeat);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(image_store_tests);
    RUN_SUITE(sixel_tests);
    GREATEST_MAIN_END();
}
