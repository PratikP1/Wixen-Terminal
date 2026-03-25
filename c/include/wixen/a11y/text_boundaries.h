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

#endif /* WIXEN_A11Y_TEXT_BOUNDARIES_H */
