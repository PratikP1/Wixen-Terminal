/* test_red_hyperlink_lifecycle.c — RED tests for hyperlink store lifecycle
 *
 * OSC 8 opens/closes hyperlinks. The store deduplicates by URI,
 * assigns IDs, and supports lookup.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/hyperlink.h"

TEST red_hl_empty(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    ASSERT_EQ(1, (int)hs.count); /* Sentinel at index 0 */
    /* Index 0 is sentinel — get() returns NULL for it (by design) */
    const WixenHyperlink *h = wixen_hyperlinks_get(&hs, 0);
    ASSERT(h == NULL);
    /* No valid links yet */
    h = wixen_hyperlinks_get(&hs, 1);
    ASSERT(h == NULL);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hl_insert(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://test.com", NULL);
    ASSERT(id > 0);
    const WixenHyperlink *h = wixen_hyperlinks_get(&hs, id);
    ASSERT(h != NULL);
    ASSERT_STR_EQ("https://test.com", h->uri);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hl_dedup(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id1 = wixen_hyperlinks_get_or_insert(&hs, "https://example.com", NULL);
    uint32_t id2 = wixen_hyperlinks_get_or_insert(&hs, "https://example.com", NULL);
    ASSERT_EQ(id1, id2); /* Same URI = same ID */
    ASSERT_EQ(2, (int)hs.count); /* Sentinel + 1 unique */
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hl_different_uris(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id1 = wixen_hyperlinks_get_or_insert(&hs, "https://a.com", NULL);
    uint32_t id2 = wixen_hyperlinks_get_or_insert(&hs, "https://b.com", NULL);
    ASSERT(id1 != id2);
    ASSERT_EQ(3, (int)hs.count); /* Sentinel + 2 */
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hl_with_id(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://test.com", "link-42");
    const WixenHyperlink *h = wixen_hyperlinks_get(&hs, id);
    ASSERT(h != NULL);
    ASSERT_STR_EQ("link-42", h->id);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hl_out_of_range(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    const WixenHyperlink *h = wixen_hyperlinks_get(&hs, 999);
    ASSERT(h == NULL);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hl_many_links(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    for (int i = 0; i < 100; i++) {
        char uri[64];
        snprintf(uri, sizeof(uri), "https://example.com/%d", i);
        uint32_t id = wixen_hyperlinks_get_or_insert(&hs, uri, NULL);
        ASSERT(id > 0);
    }
    ASSERT_EQ(101, (int)hs.count); /* Sentinel + 100 */
    /* Verify first and last */
    const WixenHyperlink *first = wixen_hyperlinks_get(&hs, 1);
    ASSERT(strstr(first->uri, "/0") != NULL);
    const WixenHyperlink *last = wixen_hyperlinks_get(&hs, 100);
    ASSERT(strstr(last->uri, "/99") != NULL);
    wixen_hyperlinks_free(&hs);
    PASS();
}

SUITE(red_hyperlink_lifecycle) {
    RUN_TEST(red_hl_empty);
    RUN_TEST(red_hl_insert);
    RUN_TEST(red_hl_dedup);
    RUN_TEST(red_hl_different_uris);
    RUN_TEST(red_hl_with_id);
    RUN_TEST(red_hl_out_of_range);
    RUN_TEST(red_hl_many_links);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_hyperlink_lifecycle);
    GREATEST_MAIN_END();
}
