/* test_search_extended.c — Additional search edge cases */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/search/search.h"

static WixenSearchOptions default_opts(void) {
    WixenSearchOptions o = { false, false, true };
    return o;
}

TEST search_overlapping_matches(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "aaa" };
    wixen_search_execute(&se, "aa", default_opts(), rows, 1, 0, 0);
    /* "aa" in "aaa" should find 2 overlapping matches: pos 0-2 and 1-3 */
    ASSERT_EQ(2, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_single_char(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abcabc" };
    wixen_search_execute(&se, "a", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(2, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_entire_line(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "exact" };
    wixen_search_execute(&se, "exact", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    ASSERT_EQ(0, (int)se.matches[0].col_start);
    ASSERT_EQ(5, (int)se.matches[0].col_end);
    wixen_search_free(&se);
    PASS();
}

TEST search_utf8_content(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "caf\xc3\xa9 latt\xc3\xa9" };
    wixen_search_execute(&se, "caf", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_many_rows(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[100];
    char buf[100][32];
    for (int i = 0; i < 100; i++) {
        sprintf(buf[i], "line %d match here", i);
        rows[i] = buf[i];
    }
    wixen_search_execute(&se, "match", default_opts(), rows, 100, 0, 50);
    ASSERT_EQ(100, (int)wixen_search_match_count(&se));
    /* Active should be at or after row 50 */
    const WixenSearchMatch *m = wixen_search_active_match(&se);
    ASSERT(m != NULL);
    ASSERT(m->row >= 50);
    wixen_search_free(&se);
    PASS();
}

TEST search_status_empty_query(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    char *s = wixen_search_status_text(&se);
    ASSERT_STR_EQ("", s);
    free(s);
    wixen_search_free(&se);
    PASS();
}

TEST search_wrap_backward_from_start(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abc", "def", "abc" };
    wixen_search_execute(&se, "abc", default_opts(), rows, 3, 0, 0);
    /* Active at row 0, go backward should wrap to row 2 */
    wixen_search_next(&se, WIXEN_SEARCH_BACKWARD);
    ASSERT_EQ(2, (int)wixen_search_active_match(&se)->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_no_wrap_stays(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    WixenSearchOptions opts = { false, false, false }; /* No wrap */
    const char *rows[] = { "abc" };
    wixen_search_execute(&se, "abc", opts, rows, 1, 0, 0);
    /* Can't go backward from first match without wrap */
    wixen_search_next(&se, WIXEN_SEARCH_BACKWARD);
    ASSERT_EQ(0, (int)wixen_search_active_match(&se)->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_regex_caseless(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    WixenSearchOptions opts = { false, true, true }; /* Case-insensitive regex */
    const char *rows[] = { "Hello WORLD" };
    wixen_search_execute(&se, "hello", opts, rows, 1, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_cell_state_no_matches(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    ASSERT_EQ(WIXEN_MATCH_NONE, wixen_search_cell_state(&se, 0, 0));
    wixen_search_free(&se);
    PASS();
}

TEST search_reexecute_clears_old(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows1[] = { "aaa" };
    wixen_search_execute(&se, "aaa", default_opts(), rows1, 1, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    /* Re-execute with query that doesn't match */
    const char *rows2[] = { "bbb" };
    wixen_search_execute(&se, "xyz", default_opts(), rows2, 1, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    /* Old "aaa" match should be gone */
    ASSERT_EQ(WIXEN_MATCH_NONE, wixen_search_cell_state(&se, 0, 0));
    wixen_search_free(&se);
    PASS();
}

SUITE(search_extended) {
    RUN_TEST(search_overlapping_matches);
    RUN_TEST(search_single_char);
    RUN_TEST(search_entire_line);
    RUN_TEST(search_utf8_content);
    RUN_TEST(search_many_rows);
    RUN_TEST(search_status_empty_query);
    RUN_TEST(search_wrap_backward_from_start);
    RUN_TEST(search_no_wrap_stays);
    RUN_TEST(search_regex_caseless);
    RUN_TEST(search_cell_state_no_matches);
    RUN_TEST(search_reexecute_clears_old);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(search_extended);
    GREATEST_MAIN_END();
}
