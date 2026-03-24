/* search.c — Terminal search engine (plain text + regex via PCRE2 when available) */
#define PCRE2_CODE_UNIT_WIDTH 8
#include "wixen/search/search.h"
#include <pcre2.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* --- Helpers --- */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

/* Case-insensitive strstr */
static const char *strcasestr_impl(const char *haystack, const char *needle) {
    if (!needle[0]) return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        bool match = true;
        for (size_t i = 0; i < nlen; i++) {
            if (!p[i] || tolower((unsigned char)p[i]) != tolower((unsigned char)needle[i])) {
                match = false;
                break;
            }
        }
        if (match) return p;
    }
    return NULL;
}

static void add_match(WixenSearchEngine *se, size_t row, size_t col_start, size_t col_end) {
    if (se->match_count >= se->match_cap) {
        size_t new_cap = se->match_cap ? se->match_cap * 2 : 64;
        WixenSearchMatch *new_arr = realloc(se->matches, new_cap * sizeof(WixenSearchMatch));
        if (!new_arr) return;
        se->matches = new_arr;
        se->match_cap = new_cap;
    }
    WixenSearchMatch *m = &se->matches[se->match_count++];
    m->row = row;
    m->col_start = col_start;
    m->col_end = col_end;
}

/* --- Lifecycle --- */

void wixen_search_init(WixenSearchEngine *se) {
    memset(se, 0, sizeof(*se));
    se->active = (size_t)-1;
}

void wixen_search_free(WixenSearchEngine *se) {
    free(se->query);
    free(se->matches);
    memset(se, 0, sizeof(*se));
    se->active = (size_t)-1;
}

void wixen_search_clear(WixenSearchEngine *se) {
    free(se->query);
    se->query = NULL;
    free(se->matches);
    se->matches = NULL;
    se->match_count = 0;
    se->match_cap = 0;
    se->active = (size_t)-1;
}

/* --- Execute --- */

void wixen_search_execute(WixenSearchEngine *se, const char *query,
                          WixenSearchOptions options,
                          const char **rows, size_t row_count,
                          size_t row_offset, size_t cursor_row) {
    wixen_search_clear(se);
    if (!query || query[0] == '\0') return;

    se->query = dup_str(query);
    se->options = options;
    size_t qlen = strlen(query);

    /* Regex search using PCRE2 */
    if (options.regex) {
        int errcode;
        PCRE2_SIZE erroffset;
        uint32_t pcre_opts = options.case_sensitive ? 0 : PCRE2_CASELESS;
        pcre2_code *re = pcre2_compile(
            (PCRE2_SPTR)query, PCRE2_ZERO_TERMINATED, pcre_opts,
            &errcode, &erroffset, NULL);
        if (re) {
            pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
            for (size_t r = 0; r < row_count; r++) {
                const char *line = rows[r];
                if (!line) continue;
                PCRE2_SIZE start = 0;
                size_t line_len = strlen(line);
                while (start < line_len) {
                    int rc = pcre2_match(re, (PCRE2_SPTR)line, line_len,
                                          start, 0, match_data, NULL);
                    if (rc < 1) break;
                    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
                    add_match(se, row_offset + r, (size_t)ovector[0], (size_t)ovector[1]);
                    start = ovector[1] > ovector[0] ? ovector[1] : ovector[0] + 1;
                }
            }
            pcre2_match_data_free(match_data);
            pcre2_code_free(re);
        }
        /* Set active match */
        se->active = (size_t)-1;
        for (size_t i = 0; i < se->match_count; i++) {
            if (se->matches[i].row >= cursor_row) { se->active = i; break; }
        }
        if (se->active == (size_t)-1 && se->match_count > 0 && options.wrap_around)
            se->active = 0;
        return;
    }

    /* Plain text search */
    for (size_t r = 0; r < row_count; r++) {
        const char *line = rows[r];
        if (!line) continue;
        const char *p = line;
        while (*p) {
            const char *found;
            if (options.case_sensitive) {
                found = strstr(p, query);
            } else {
                found = strcasestr_impl(p, query);
            }
            if (!found) break;
            size_t col_start = (size_t)(found - line);
            size_t col_end = col_start + qlen;
            add_match(se, row_offset + r, col_start, col_end);
            p = found + 1; /* Continue searching for overlapping matches */
        }
    }

    /* Set active to first match at or after cursor */
    se->active = (size_t)-1;
    for (size_t i = 0; i < se->match_count; i++) {
        if (se->matches[i].row >= cursor_row) {
            se->active = i;
            break;
        }
    }
    /* If no match at/after cursor and wrap is on, use first match */
    if (se->active == (size_t)-1 && se->match_count > 0 && options.wrap_around) {
        se->active = 0;
    }
}

/* --- Navigation --- */

const WixenSearchMatch *wixen_search_next(WixenSearchEngine *se, WixenSearchDirection dir) {
    if (se->match_count == 0) return NULL;

    if (se->active == (size_t)-1) {
        se->active = 0;
        return &se->matches[0];
    }

    if (dir == WIXEN_SEARCH_FORWARD) {
        if (se->active + 1 < se->match_count) {
            se->active++;
        } else if (se->options.wrap_around) {
            se->active = 0;
        }
    } else {
        if (se->active > 0) {
            se->active--;
        } else if (se->options.wrap_around) {
            se->active = se->match_count - 1;
        }
    }

    return &se->matches[se->active];
}

const WixenSearchMatch *wixen_search_active_match(const WixenSearchEngine *se) {
    if (se->active == (size_t)-1 || se->active >= se->match_count) return NULL;
    return &se->matches[se->active];
}

/* --- Status --- */

size_t wixen_search_match_count(const WixenSearchEngine *se) {
    return se->match_count;
}

char *wixen_search_status_text(const WixenSearchEngine *se) {
    char buf[64];
    if (se->match_count == 0) {
        if (se->query && se->query[0]) {
            snprintf(buf, sizeof(buf), "No results");
        } else {
            buf[0] = '\0';
        }
    } else if (se->active != (size_t)-1) {
        snprintf(buf, sizeof(buf), "%zu/%zu", se->active + 1, se->match_count);
    } else {
        snprintf(buf, sizeof(buf), "%zu matches", se->match_count);
    }
    return dup_str(buf);
}

/* --- Cell match state --- */

WixenCellMatchState wixen_search_cell_state(const WixenSearchEngine *se,
                                             size_t abs_row, size_t col) {
    for (size_t i = 0; i < se->match_count; i++) {
        const WixenSearchMatch *m = &se->matches[i];
        if (m->row == abs_row && col >= m->col_start && col < m->col_end) {
            return (i == se->active) ? WIXEN_MATCH_ACTIVE : WIXEN_MATCH_HIGHLIGHTED;
        }
    }
    return WIXEN_MATCH_NONE;
}
