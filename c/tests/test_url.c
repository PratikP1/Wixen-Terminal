/* test_url.c — Tests for URL detection */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/url.h"

TEST url_detect_https(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("Visit https://example.com for info", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://example.com", m[0].url);
    ASSERT_EQ(6, (int)m[0].col_start);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_detect_http(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("Go to http://foo.bar/path", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("http://foo.bar/path", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_detect_ftp(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("Download from ftp://files.example.com/pub", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("ftp://files.example.com/pub", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_detect_multiple(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("See https://a.com and http://b.com", &m);
    ASSERT_EQ(2, (int)count);
    ASSERT_STR_EQ("https://a.com", m[0].url);
    ASSERT_STR_EQ("http://b.com", m[1].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_strip_trailing_punct(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("See https://example.com.", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://example.com", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_strip_trailing_paren(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("(https://example.com)", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://example.com", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_no_urls(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("No urls here", &m);
    ASSERT_EQ(0, (int)count);
    ASSERT(m == NULL);
    PASS();
}

TEST url_empty_input(void) {
    WixenUrlMatch *m;
    ASSERT_EQ(0, (int)wixen_detect_urls("", &m));
    ASSERT_EQ(0, (int)wixen_detect_urls(NULL, &m));
    PASS();
}

TEST url_safe_only(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_safe_urls(
        "https://safe.com ssh://not.safe file://also.not", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://safe.com", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_is_safe_scheme(void) {
    ASSERT(wixen_is_safe_url_scheme("http://example.com"));
    ASSERT(wixen_is_safe_url_scheme("https://example.com"));
    ASSERT(wixen_is_safe_url_scheme("ftp://files.com"));
    ASSERT_FALSE(wixen_is_safe_url_scheme("ssh://server.com"));
    ASSERT_FALSE(wixen_is_safe_url_scheme("file:///tmp/foo"));
    ASSERT_FALSE(wixen_is_safe_url_scheme("javascript:alert(1)"));
    PASS();
}

TEST url_at_col_found(void) {
    char *url = wixen_url_at_col("Visit https://example.com here", 10);
    ASSERT(url != NULL);
    ASSERT_STR_EQ("https://example.com", url);
    free(url);
    PASS();
}

TEST url_at_col_not_found(void) {
    char *url = wixen_url_at_col("Visit https://example.com here", 0);
    ASSERT(url == NULL);
    PASS();
}

TEST url_with_path_and_query(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("https://example.com/path?q=1&b=2#frag", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("https://example.com/path?q=1&b=2#frag", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

TEST url_file_scheme(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("file:///tmp/log.txt", &m);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("file:///tmp/log.txt", m[0].url);
    wixen_url_matches_free(m, count);
    PASS();
}

SUITE(url_detection) {
    RUN_TEST(url_detect_https);
    RUN_TEST(url_detect_http);
    RUN_TEST(url_detect_ftp);
    RUN_TEST(url_detect_multiple);
    RUN_TEST(url_strip_trailing_punct);
    RUN_TEST(url_strip_trailing_paren);
    RUN_TEST(url_no_urls);
    RUN_TEST(url_empty_input);
    RUN_TEST(url_with_path_and_query);
    RUN_TEST(url_file_scheme);
}

SUITE(url_safety) {
    RUN_TEST(url_safe_only);
    RUN_TEST(url_is_safe_scheme);
}

SUITE(url_at_position) {
    RUN_TEST(url_at_col_found);
    RUN_TEST(url_at_col_not_found);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(url_detection);
    RUN_SUITE(url_safety);
    RUN_SUITE(url_at_position);
    GREATEST_MAIN_END();
}
