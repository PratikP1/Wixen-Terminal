/* test_red_search_modal.c — RED tests for search modal integration
 *
 * The search modal (Ctrl+F) needs:
 * - Open/close state tracking
 * - Query text accumulation
 * - Match highlighting via search engine
 * - Navigate next/prev match
 * - ESC closes and restores focus
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/search/search.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* Search state that main.c would track */
typedef struct {
    bool active;
    char query[256];
    size_t query_len;
    WixenSearchEngine engine;
} SearchModalState;

static void search_open(SearchModalState *s) {
    s->active = true;
    s->query[0] = '\0';
    s->query_len = 0;
}

static void search_close(SearchModalState *s) {
    s->active = false;
}

static void search_type_char(SearchModalState *s, char c) {
    if (s->query_len < sizeof(s->query) - 1) {
        s->query[s->query_len++] = c;
        s->query[s->query_len] = '\0';
    }
}

static void search_backspace(SearchModalState *s) {
    if (s->query_len > 0) {
        s->query_len--;
        s->query[s->query_len] = '\0';
    }
}

TEST red_search_open_close(void) {
    SearchModalState s = {0};
    wixen_search_init(&s.engine);
    ASSERT_FALSE(s.active);
    search_open(&s);
    ASSERT(s.active);
    search_close(&s);
    ASSERT_FALSE(s.active);
    wixen_search_free(&s.engine);
    PASS();
}

TEST red_search_type_query(void) {
    SearchModalState s = {0};
    wixen_search_init(&s.engine);
    search_open(&s);
    search_type_char(&s, 'h');
    search_type_char(&s, 'i');
    ASSERT_STR_EQ("hi", s.query);
    search_backspace(&s);
    ASSERT_STR_EQ("h", s.query);
    wixen_search_free(&s.engine);
    PASS();
}

TEST red_search_finds_in_terminal(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "Hello World\r\nFoo Bar\r\nHello Again");

    SearchModalState s = {0};
    wixen_search_init(&s.engine);
    search_open(&s);

    /* Build row text array for search */
    const char *rows[5];
    char row_bufs[5][64];
    for (int i = 0; i < 5; i++) {
        wixen_row_text(&t.grid.rows[i], row_bufs[i], sizeof(row_bufs[i]));
        rows[i] = row_bufs[i];
    }

    /* Type "Hello" and search */
    for (const char *c = "Hello"; *c; c++) search_type_char(&s, *c);

    WixenSearchOptions opts = { false, false, true };
    wixen_search_execute(&s.engine, s.query, opts, rows, 5, 0, 0);

    ASSERT_EQ(2, (int)wixen_search_match_count(&s.engine));

    wixen_search_free(&s.engine);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_search_navigate_matches(void) {
    SearchModalState s = {0};
    wixen_search_init(&s.engine);
    const char *rows[] = { "aaa", "bbb", "aaa" };
    WixenSearchOptions opts = { false, false, true };
    wixen_search_execute(&s.engine, "aaa", opts, rows, 3, 0, 0);
    ASSERT_EQ(2, (int)wixen_search_match_count(&s.engine));

    /* Active match starts at first */
    const WixenSearchMatch *m = wixen_search_active_match(&s.engine);
    ASSERT(m != NULL);
    ASSERT_EQ(0, (int)m->row);

    /* Navigate to next */
    wixen_search_next(&s.engine, WIXEN_SEARCH_FORWARD);
    m = wixen_search_active_match(&s.engine);
    ASSERT_EQ(2, (int)m->row);

    /* Navigate back */
    wixen_search_next(&s.engine, WIXEN_SEARCH_BACKWARD);
    m = wixen_search_active_match(&s.engine);
    ASSERT_EQ(0, (int)m->row);

    wixen_search_free(&s.engine);
    PASS();
}

SUITE(red_search_modal) {
    RUN_TEST(red_search_open_close);
    RUN_TEST(red_search_type_query);
    RUN_TEST(red_search_finds_in_terminal);
    RUN_TEST(red_search_navigate_matches);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_search_modal);
    GREATEST_MAIN_END();
}
