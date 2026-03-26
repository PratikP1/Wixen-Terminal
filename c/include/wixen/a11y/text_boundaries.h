/* text_boundaries.h — Pure text boundary logic for UIA text ranges
 *
 * No Win32 dependency — usable in tests and on any platform.
 * Used by ITextRangeProvider::ExpandToEnclosingUnit.
 */
#ifndef WIXEN_A11Y_TEXT_BOUNDARIES_H
#define WIXEN_A11Y_TEXT_BOUNDARIES_H

#include <stddef.h>

/* Find the line containing offset. Sets start/end to line boundaries. */
void wixen_text_line_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end);

/* Find the word containing offset. */
void wixen_text_word_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end);

/* Convert byte offset to row/col. */
void wixen_text_offset_to_rowcol(const char *text, size_t text_len, size_t offset,
                                   size_t *out_row, size_t *out_col);

/* Convert row/col to byte offset. */
size_t wixen_text_rowcol_to_offset(const char *text, size_t text_len,
                                     size_t row, size_t col);

/* Individual boundary queries */
size_t wixen_text_word_start(const char *text, size_t text_len, size_t offset);
size_t wixen_text_word_end(const char *text, size_t text_len, size_t offset);
size_t wixen_text_line_start(const char *text, size_t text_len, size_t offset);
size_t wixen_text_line_end(const char *text, size_t text_len, size_t offset);

/* Text unit enum (matches UIA TextUnit) */
typedef enum {
    WIXEN_TEXT_UNIT_CHAR = 0,
    WIXEN_TEXT_UNIT_FORMAT = 1,
    WIXEN_TEXT_UNIT_WORD = 2,
    WIXEN_TEXT_UNIT_LINE = 3,
    WIXEN_TEXT_UNIT_PARAGRAPH = 4,
    WIXEN_TEXT_UNIT_PAGE = 5,
    WIXEN_TEXT_UNIT_DOCUMENT = 6,
} WixenTextUnit;

/* Move by unit (returns actual count moved, negative for backward) */
int wixen_text_move_by_char(const char *text, size_t text_len, size_t *pos, int count);
int wixen_text_move_by_word(const char *text, size_t text_len, size_t *pos, int count);
int wixen_text_move_by_line(const char *text, size_t text_len, size_t *pos, int count);

/* Generic move by unit (dispatches to char/word/line/document) */
int wixen_text_move_by_unit(const char *text, size_t text_len,
                             size_t *pos, WixenTextUnit unit, int count);

/* Move endpoint by unit (expands/contracts range end) */
int wixen_text_move_endpoint(const char *text, size_t text_len,
                              size_t *endpoint, WixenTextUnit unit, int count);

/* Find substring. Returns true if found, sets out_start/out_end.
 * backward=true searches before from_offset. ignore_case for case-insensitive. */
#include <stdbool.h>
bool wixen_text_find(const char *text, size_t text_len, const char *query,
                      size_t from_offset, bool backward, bool ignore_case,
                      size_t *out_start, size_t *out_end);

#endif /* WIXEN_A11Y_TEXT_BOUNDARIES_H */
