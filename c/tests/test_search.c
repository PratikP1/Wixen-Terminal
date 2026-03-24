/* test_search.c — Tests for search engine */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/search/search.h"

static WixenSearchOptions default_opts(void) {
    WixenSearchOptions o = { false, false, true };
    return o;
}

static WixenSearchOptions case_sensitive_opts(void) {
    WixenSearchOptions o = { true, false, true };
    return o;
}

TEST search_init_empty(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    ASSERT(wixen_search_active_match(&se) == NULL);
    wixen_search_free(&se);
    PASS();
}

TEST search_basic_found(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello World", "Goodbye" };
    wixen_search_execute(&se, "World", default_opts(), rows, 2, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    const WixenSearchMatch *m = wixen_search_active_match(&se);
    ASSERT(m != NULL);
    ASSERT_EQ(0, (int)m->row);
    ASSERT_EQ(6, (int)m->col_start);
    ASSERT_EQ(11, (int)m->col_end);
    wixen_search_free(&se);
    PASS();
}

TEST search_not_found(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello World" };
    wixen_search_execute(&se, "xyz", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    ASSERT(wixen_search_active_match(&se) == NULL);
    wixen_search_free(&se);
    PASS();
}

TEST search_case_insensitive(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello WORLD", "hello world" };
    wixen_search_execute(&se, "hello", default_opts(), rows, 2, 0, 0);
    ASSERT_EQ(2, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_case_sensitive(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello WORLD", "hello world" };
    wixen_search_execute(&se, "hello", case_sensitive_opts(), rows, 2, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    ASSERT_EQ(1, (int)wixen_search_active_match(&se)->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_multiple_matches_same_row(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abcabc" };
    wixen_search_execute(&se, "abc", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(2, (int)wixen_search_match_count(&se));
    ASSERT_EQ(0, (int)se.matches[0].col_start);
    ASSERT_EQ(3, (int)se.matches[1].col_start);
    wixen_search_free(&se);
    PASS();
}

TEST search_with_row_offset(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "match here" };
    wixen_search_execute(&se, "match", default_opts(), rows, 1, 100, 100);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    ASSERT_EQ(100, (int)se.matches[0].row); /* Absolute row */
    wixen_search_free(&se);
    PASS();
}

TEST search_active_starts_at_cursor(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "match1", "match2", "match3" };
    wixen_search_execute(&se, "match", default_opts(), rows, 3, 0, 1);
    /* Active should be at row 1 (first match at/after cursor) */
    const WixenSearchMatch *m = wixen_search_active_match(&se);
    ASSERT(m != NULL);
    ASSERT_EQ(1, (int)m->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_navigate_forward(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "aaa", "aaa", "aaa" };
    wixen_search_execute(&se, "aaa", default_opts(), rows, 3, 0, 0);
    ASSERT_EQ(3, (int)wixen_search_match_count(&se));
    ASSERT_EQ(0, (int)wixen_search_active_match(&se)->row);
    wixen_search_next(&se, WIXEN_SEARCH_FORWARD);
    ASSERT_EQ(1, (int)wixen_search_active_match(&se)->row);
    wixen_search_next(&se, WIXEN_SEARCH_FORWARD);
    ASSERT_EQ(2, (int)wixen_search_active_match(&se)->row);
    /* Wrap around */
    wixen_search_next(&se, WIXEN_SEARCH_FORWARD);
    ASSERT_EQ(0, (int)wixen_search_active_match(&se)->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_navigate_backward(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "aaa", "aaa", "aaa" };
    wixen_search_execute(&se, "aaa", default_opts(), rows, 3, 0, 0);
    /* Wrap backward from first */
    wixen_search_next(&se, WIXEN_SEARCH_BACKWARD);
    ASSERT_EQ(2, (int)wixen_search_active_match(&se)->row);
    wixen_search_next(&se, WIXEN_SEARCH_BACKWARD);
    ASSERT_EQ(1, (int)wixen_search_active_match(&se)->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_no_wrap(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    WixenSearchOptions opts = { false, false, false }; /* No wrap */
    const char *rows[] = { "aaa", "aaa" };
    wixen_search_execute(&se, "aaa", opts, rows, 2, 0, 0);
    wixen_search_next(&se, WIXEN_SEARCH_FORWARD);
    ASSERT_EQ(1, (int)wixen_search_active_match(&se)->row);
    /* At end, forward should stay */
    wixen_search_next(&se, WIXEN_SEARCH_FORWARD);
    ASSERT_EQ(1, (int)wixen_search_active_match(&se)->row);
    wixen_search_free(&se);
    PASS();
}

TEST search_status_text(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abc", "def", "abc" };
    wixen_search_execute(&se, "abc", default_opts(), rows, 3, 0, 0);
    char *status = wixen_search_status_text(&se);
    ASSERT_STR_EQ("1/2", status);
    free(status);
    wixen_search_next(&se, WIXEN_SEARCH_FORWARD);
    status = wixen_search_status_text(&se);
    ASSERT_STR_EQ("2/2", status);
    free(status);
    wixen_search_free(&se);
    PASS();
}

TEST search_status_no_results(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abc" };
    wixen_search_execute(&se, "xyz", default_opts(), rows, 1, 0, 0);
    char *status = wixen_search_status_text(&se);
    ASSERT_STR_EQ("No results", status);
    free(status);
    wixen_search_free(&se);
    PASS();
}

TEST search_cell_state(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello World" };
    wixen_search_execute(&se, "World", default_opts(), rows, 1, 0, 0);
    /* Active match at cols 6-10 */
    ASSERT_EQ(WIXEN_MATCH_NONE, wixen_search_cell_state(&se, 0, 5));
    ASSERT_EQ(WIXEN_MATCH_ACTIVE, wixen_search_cell_state(&se, 0, 6));
    ASSERT_EQ(WIXEN_MATCH_ACTIVE, wixen_search_cell_state(&se, 0, 10));
    ASSERT_EQ(WIXEN_MATCH_NONE, wixen_search_cell_state(&se, 0, 11));
    wixen_search_free(&se);
    PASS();
}

TEST search_cell_state_highlighted_vs_active(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abc abc" };
    wixen_search_execute(&se, "abc", default_opts(), rows, 1, 0, 0);
    /* First match is active, second is highlighted */
    ASSERT_EQ(WIXEN_MATCH_ACTIVE, wixen_search_cell_state(&se, 0, 0));
    ASSERT_EQ(WIXEN_MATCH_HIGHLIGHTED, wixen_search_cell_state(&se, 0, 4));
    wixen_search_free(&se);
    PASS();
}

TEST search_empty_query(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello" };
    wixen_search_execute(&se, "", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_null_query(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "Hello" };
    wixen_search_execute(&se, NULL, default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_clear_resets(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abc" };
    wixen_search_execute(&se, "abc", default_opts(), rows, 1, 0, 0);
    ASSERT_EQ(1, (int)wixen_search_match_count(&se));
    wixen_search_clear(&se);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    ASSERT(wixen_search_active_match(&se) == NULL);
    wixen_search_free(&se);
    PASS();
}

TEST search_navigate_empty(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    ASSERT(wixen_search_next(&se, WIXEN_SEARCH_FORWARD) == NULL);
    ASSERT(wixen_search_next(&se, WIXEN_SEARCH_BACKWARD) == NULL);
    wixen_search_free(&se);
    PASS();
}

SUITE(search_basic) {
    RUN_TEST(search_init_empty);
    RUN_TEST(search_basic_found);
    RUN_TEST(search_not_found);
    RUN_TEST(search_case_insensitive);
    RUN_TEST(search_case_sensitive);
    RUN_TEST(search_multiple_matches_same_row);
    RUN_TEST(search_with_row_offset);
    RUN_TEST(search_active_starts_at_cursor);
}

SUITE(search_navigation) {
    RUN_TEST(search_navigate_forward);
    RUN_TEST(search_navigate_backward);
    RUN_TEST(search_no_wrap);
    RUN_TEST(search_navigate_empty);
}

SUITE(search_status) {
    RUN_TEST(search_status_text);
    RUN_TEST(search_status_no_results);
}

SUITE(search_rendering) {
    RUN_TEST(search_cell_state);
    RUN_TEST(search_cell_state_highlighted_vs_active);
}

/* === Regex === */

static WixenSearchOptions regex_opts(void) {
    WixenSearchOptions o = { false, true, true };
    return o;
}

TEST search_regex_basic(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "error: foo", "warning: bar", "error: baz" };
    wixen_search_execute(&se, "error:", regex_opts(), rows, 3, 0, 0);
    ASSERT_EQ(2, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_regex_pattern(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "file.txt", "image.png", "data.csv" };
    wixen_search_execute(&se, "\\.(txt|csv)$", regex_opts(), rows, 3, 0, 0);
    ASSERT_EQ(2, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST search_regex_invalid(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[] = { "abc" };
    /* Invalid regex — unclosed bracket */
    wixen_search_execute(&se, "[abc", regex_opts(), rows, 1, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se)); /* No crash, no matches */
    wixen_search_free(&se);
    PASS();
}

SUITE(search_regex) {
    RUN_TEST(search_regex_basic);
    RUN_TEST(search_regex_pattern);
    RUN_TEST(search_regex_invalid);
}

SUITE(search_edge_cases) {
    RUN_TEST(search_empty_query);
    RUN_TEST(search_null_query);
    RUN_TEST(search_clear_resets);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(search_basic);
    RUN_SUITE(search_navigation);
    RUN_SUITE(search_status);
    RUN_SUITE(search_rendering);
    RUN_SUITE(search_regex);
    RUN_SUITE(search_edge_cases);
    GREATEST_MAIN_END();
}
