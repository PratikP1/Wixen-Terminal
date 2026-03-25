/* text_boundaries.c — Pure text boundary logic for UIA text ranges
 *
 * No Win32 dependency — usable in tests on any platform.
 */
#include "wixen/a11y/text_boundaries.h"
#include <string.h>
#include <ctype.h>

void wixen_text_line_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end) {
    if (!text || text_len == 0 || offset > text_len) {
        *out_start = *out_end = 0;
        return;
    }
    if (offset >= text_len) offset = text_len > 0 ? text_len - 1 : 0;

    /* Find start of line (scan backward for \n) */
    size_t start = offset;
    while (start > 0 && text[start - 1] != '\n') start--;

    /* Find end of line (scan forward for \n or end) */
    size_t end = offset;
    while (end < text_len && text[end] != '\n') end++;

    *out_start = start;
    *out_end = end;
}

void wixen_text_word_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end) {
    if (!text || text_len == 0 || offset >= text_len) {
        *out_start = *out_end = offset;
        return;
    }

    /* Determine if we're in a word char or whitespace */
    int in_word = isalnum((unsigned char)text[offset]) || text[offset] == '_';

    /* Scan backward to start of word/space run */
    size_t start = offset;
    while (start > 0) {
        int prev_is_word = isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_';
        if (prev_is_word != in_word) break;
        start--;
    }

    /* Scan forward to end of word/space run */
    size_t end = offset;
    while (end < text_len) {
        int cur_is_word = isalnum((unsigned char)text[end]) || text[end] == '_';
        if (cur_is_word != in_word) break;
        end++;
    }

    *out_start = start;
    *out_end = end;
}

void wixen_text_offset_to_rowcol(const char *text, size_t text_len, size_t offset,
                                   size_t *out_row, size_t *out_col) {
    size_t row = 0, col = 0;
    for (size_t i = 0; i < text_len && i < offset; i++) {
        if (text[i] == '\n') {
            row++;
            col = 0;
        } else {
            col++;
        }
    }
    *out_row = row;
    *out_col = col;
}

size_t wixen_text_rowcol_to_offset(const char *text, size_t text_len,
                                     size_t row, size_t col) {
    size_t r = 0, c = 0;
    for (size_t i = 0; i < text_len; i++) {
        if (r == row && c == col) return i;
        if (text[i] == '\n') {
            if (r == row) return i; /* col beyond line length */
            r++;
            c = 0;
        } else {
            c++;
        }
    }
    return text_len;
}
