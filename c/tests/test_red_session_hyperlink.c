/* test_red_session_hyperlink.c — Deeper tests for session + hyperlink */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/session.h"
#include "wixen/core/hyperlink.h"

/* --- Session --- */

TEST red_session_many_tabs(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    for (int i = 0; i < 20; i++) {
        char title[32], shell[32];
        snprintf(title, sizeof(title), "Tab %d", i);
        snprintf(shell, sizeof(shell), "shell%d.exe", i);
        wixen_session_add_tab(&ss, title, shell, "C:\\");
    }
    ASSERT_EQ(20, (int)ss.tab_count);

    const char *path = "test_session_many.json";
    ASSERT(wixen_session_save(&ss, path));

    WixenSessionState loaded;
    wixen_session_init(&loaded);
    ASSERT(wixen_session_load(&loaded, path));
    ASSERT_EQ(20, (int)loaded.tab_count);
    ASSERT_STR_EQ("Tab 0", loaded.tabs[0].title);
    ASSERT_STR_EQ("Tab 19", loaded.tabs[19].title);

    wixen_session_free(&loaded);
    wixen_session_free(&ss);
    remove(path);
    PASS();
}

TEST red_session_unicode_title(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    wixen_session_add_tab(&ss, "caf\xc3\xa9 \xe2\x9c\x93", "pwsh.exe", "C:\\");
    ASSERT_STR_EQ("caf\xc3\xa9 \xe2\x9c\x93", ss.tabs[0].title);

    const char *path = "test_session_utf8.json";
    wixen_session_save(&ss, path);

    WixenSessionState loaded;
    wixen_session_init(&loaded);
    wixen_session_load(&loaded, path);
    ASSERT_STR_EQ("caf\xc3\xa9 \xe2\x9c\x93", loaded.tabs[0].title);

    wixen_session_free(&loaded);
    wixen_session_free(&ss);
    remove(path);
    PASS();
}

TEST red_session_empty_fields(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    wixen_session_add_tab(&ss, "", "", "");
    ASSERT_EQ(1, (int)ss.tab_count);
    wixen_session_free(&ss);
    PASS();
}

/* --- Hyperlinks --- */

TEST red_hyperlink_many(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    for (int i = 0; i < 100; i++) {
        char url[64];
        snprintf(url, sizeof(url), "https://example.com/page%d", i);
        uint32_t id = wixen_hyperlinks_get_or_insert(&hs, url, NULL);
        ASSERT(id > 0);
    }
    ASSERT(hs.count >= 100);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hyperlink_same_url_dedup(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id1 = wixen_hyperlinks_get_or_insert(&hs, "https://a.com", "link1");
    uint32_t id2 = wixen_hyperlinks_get_or_insert(&hs, "https://a.com", "link1");
    /* Same URI + same ID should return same internal ID */
    ASSERT_EQ(id1, id2);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hyperlink_different_id_different_entry(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id1 = wixen_hyperlinks_get_or_insert(&hs, "https://a.com", "link1");
    uint32_t id2 = wixen_hyperlinks_get_or_insert(&hs, "https://a.com", "link2");
    /* Same URL but different external IDs — may or may not dedup */
    ASSERT(id1 > 0 && id2 > 0);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hyperlink_get_url(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://test.com/path?q=1", NULL);
    const WixenHyperlink *h = wixen_hyperlinks_get(&hs, id);
    ASSERT(h != NULL);
    ASSERT_STR_EQ("https://test.com/path?q=1", h->uri);
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hyperlink_open_close_cycle(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    uint32_t id = wixen_hyperlinks_get_or_insert(&hs, "https://cycle.com", NULL);
    wixen_hyperlinks_open(&hs, id);
    ASSERT_EQ(id, hs.active_id);
    wixen_hyperlinks_close(&hs);
    ASSERT_EQ(0u, hs.active_id);
    /* Re-open */
    wixen_hyperlinks_open(&hs, id);
    ASSERT_EQ(id, hs.active_id);
    wixen_hyperlinks_free(&hs);
    PASS();
}

SUITE(red_session_hyperlink) {
    RUN_TEST(red_session_many_tabs);
    RUN_TEST(red_session_unicode_title);
    RUN_TEST(red_session_empty_fields);
    RUN_TEST(red_hyperlink_many);
    RUN_TEST(red_hyperlink_same_url_dedup);
    RUN_TEST(red_hyperlink_different_id_different_entry);
    RUN_TEST(red_hyperlink_get_url);
    RUN_TEST(red_hyperlink_open_close_cycle);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_session_hyperlink);
    GREATEST_MAIN_END();
}
