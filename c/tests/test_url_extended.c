/* test_url_extended.c — More URL detection edge cases */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/url.h"

TEST url_with_port(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("See http://localhost:8080/api", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("http://localhost:8080/api", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_with_auth(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("https://user:pass@host.com/path", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://user:pass@host.com/path", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_strip_comma(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("See https://example.com, and more", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://example.com", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_strip_semicolon(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("URL: https://example.com;", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://example.com", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_mailto(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("Email mailto:user@example.com", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("mailto:user@example.com", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_ssh_scheme(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("Connect to ssh://server.com", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_FALSE(wixen_is_safe_url_scheme(m[0].url));
    wixen_url_matches_free(m, count);
    PASS();
}

SUITE(url_extended) {
    RUN_TEST(url_with_port);
    RUN_TEST(url_with_auth);
    RUN_TEST(url_strip_comma);
    RUN_TEST(url_strip_semicolon);
    RUN_TEST(url_mailto);
    RUN_TEST(url_ssh_scheme);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(url_extended);
    GREATEST_MAIN_END();
}
