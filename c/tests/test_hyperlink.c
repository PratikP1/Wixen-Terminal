/* test_hyperlink.c — Tests for hyperlink store */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/hyperlink.h"

TEST hyperlink_init(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    ASSERT_EQ(1, (int)hs.count); /* Sentinel at 0 */
    ASSERT_EQ(0, (int)hs.active_id);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST hyperlink_insert(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://example.com", NULL);
    ASSERT(id >= 1);
    const WixenHyperlink *link = wixen_hyperlinks_get(&hs, id);
    ASSERT(link != NULL);
    ASSERT_STR_EQ("https://example.com", link->uri);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST hyperlink_dedup(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id1 = wixen_hyperlinks_get_or_insert(&hs, "https://example.com", NULL);
    uint32_t id2 = wixen_hyperlinks_get_or_insert(&hs, "https://example.com", NULL);
    ASSERT_EQ(id1, id2);
    ASSERT_EQ(2, (int)hs.count); /* Sentinel + 1 unique link */
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST hyperlink_multiple(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id1 = wixen_hyperlinks_get_or_insert(&hs, "https://a.com", NULL);
    uint32_t id2 = wixen_hyperlinks_get_or_insert(&hs, "https://b.com", NULL);
    ASSERT(id1 != id2);
    ASSERT_STR_EQ("https://a.com", wixen_hyperlinks_get(&hs, id1)->uri);
    ASSERT_STR_EQ("https://b.com", wixen_hyperlinks_get(&hs, id2)->uri);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST hyperlink_open_close(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://test.com", NULL);
    ASSERT_EQ(0, (int)hs.active_id);
    wixen_hyperlinks_open(&hs, id);
    ASSERT_EQ(id, hs.active_id);
    wixen_hyperlinks_close(&hs);
    ASSERT_EQ(0, (int)hs.active_id);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST hyperlink_get_invalid(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    ASSERT(wixen_hyperlinks_get(&hs, 0) == NULL);  /* Sentinel */
    ASSERT(wixen_hyperlinks_get(&hs, 99) == NULL);  /* Out of range */
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST hyperlink_with_id(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://test.com", "link-42");
    const WixenHyperlink *link = wixen_hyperlinks_get(&hs, id);
    ASSERT(link->id != NULL);
    ASSERT_STR_EQ("link-42", link->id);
    wixen_hyperlinks_free(&hs);
    PASS();
}

SUITE(hyperlink_tests) {
    RUN_TEST(hyperlink_init);
    RUN_TEST(hyperlink_insert);
    RUN_TEST(hyperlink_dedup);
    RUN_TEST(hyperlink_multiple);
    RUN_TEST(hyperlink_open_close);
    RUN_TEST(hyperlink_get_invalid);
    RUN_TEST(hyperlink_with_id);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(hyperlink_tests);
    GREATEST_MAIN_END();
}
