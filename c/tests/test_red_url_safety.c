/* test_red_url_safety.c — RED tests for wixen_is_safe_url_scheme() */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/url.h"

/* --- Safe schemes should return true --- */

TEST safe_scheme_https(void) {
    ASSERT(wixen_is_safe_url_scheme("https://example.com"));
    PASS();
}

TEST safe_scheme_http(void) {
    ASSERT(wixen_is_safe_url_scheme("http://example.com"));
    PASS();
}

TEST safe_scheme_ftp(void) {
    ASSERT(wixen_is_safe_url_scheme("ftp://files.com"));
    PASS();
}

/* --- Unsafe schemes should return false --- */

TEST unsafe_scheme_file(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme("file:///etc/passwd"));
    PASS();
}

TEST unsafe_scheme_ssh(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme("ssh://server"));
    PASS();
}

TEST unsafe_scheme_javascript(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme("javascript:alert(1)"));
    PASS();
}

TEST unsafe_scheme_data(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme("data:text/html,..."));
    PASS();
}

/* --- Edge cases --- */

TEST unsafe_null_url(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme(NULL));
    PASS();
}

TEST unsafe_empty_url(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme(""));
    PASS();
}

SUITE(url_safety) {
    RUN_TEST(safe_scheme_https);
    RUN_TEST(safe_scheme_http);
    RUN_TEST(safe_scheme_ftp);
    RUN_TEST(unsafe_scheme_file);
    RUN_TEST(unsafe_scheme_ssh);
    RUN_TEST(unsafe_scheme_javascript);
    RUN_TEST(unsafe_scheme_data);
    RUN_TEST(unsafe_null_url);
    RUN_TEST(unsafe_empty_url);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(url_safety);
    GREATEST_MAIN_END();
}
