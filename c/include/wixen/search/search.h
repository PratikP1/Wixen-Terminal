/* search.h — Terminal search engine */
#ifndef WIXEN_SEARCH_H
#define WIXEN_SEARCH_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WIXEN_SEARCH_FORWARD = 0,
    WIXEN_SEARCH_BACKWARD,
} WixenSearchDirection;

typedef struct {
    bool case_sensitive;
    bool regex;
    bool wrap_around;
} WixenSearchOptions;

typedef struct {
    size_t row;        /* Absolute row (scrollback + grid) */
    size_t col_start;
    size_t col_end;    /* Exclusive */
} WixenSearchMatch;

typedef enum {
    WIXEN_MATCH_NONE = 0,
    WIXEN_MATCH_HIGHLIGHTED,
    WIXEN_MATCH_ACTIVE,
} WixenCellMatchState;

typedef struct {
    char *query;
    WixenSearchOptions options;
    WixenSearchMatch *matches;
    size_t match_count;
    size_t match_cap;
    size_t active;     /* Index of currently focused match, SIZE_MAX if none */
} WixenSearchEngine;

/* Lifecycle */
void wixen_search_init(WixenSearchEngine *se);
void wixen_search_free(WixenSearchEngine *se);
void wixen_search_clear(WixenSearchEngine *se);

/* Search within row text arrays.
 * rows/row_count: array of row text strings (UTF-8, newline-free).
 * row_offset: absolute row number of rows[0] (for scrollback). */
void wixen_search_execute(WixenSearchEngine *se, const char *query,
                          WixenSearchOptions options,
                          const char **rows, size_t row_count,
                          size_t row_offset, size_t cursor_row);

/* Navigation */
const WixenSearchMatch *wixen_search_next(WixenSearchEngine *se, WixenSearchDirection dir);
const WixenSearchMatch *wixen_search_active_match(const WixenSearchEngine *se);

/* Query status */
size_t wixen_search_match_count(const WixenSearchEngine *se);
/* Returns "N/M" style status string. Caller must free. */
char *wixen_search_status_text(const WixenSearchEngine *se);

/* Cell-level match checking (for renderer) */
WixenCellMatchState wixen_search_cell_state(const WixenSearchEngine *se,
                                             size_t abs_row, size_t col);

#endif /* WIXEN_SEARCH_H */
