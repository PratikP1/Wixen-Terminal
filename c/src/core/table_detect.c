/* table_detect.c — Heuristic table detection
 *
 * Detects tabular output (ls -la, docker ps, ps aux) by finding
 * consistently aligned space columns across multiple rows.
 */
#include "wixen/core/table_detect.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Find columns where ALL lines have spaces (potential column boundaries) */
static size_t find_common_gaps(const char **lines, size_t count,
                                size_t *out_positions, size_t max_positions) {
    if (count < 2 || !lines[0]) return 0;
    size_t min_len = strlen(lines[0]);
    for (size_t i = 1; i < count; i++) {
        size_t len = strlen(lines[i]);
        if (len < min_len) min_len = len;
    }

    size_t gap_count = 0;
    for (size_t col = 1; col < min_len - 1 && gap_count < max_positions; col++) {
        bool all_space = true;
        for (size_t row = 0; row < count; row++) {
            if (lines[row][col] != ' ') {
                all_space = false;
                break;
            }
        }
        /* Must be preceded and followed by non-space on at least one row */
        if (all_space) {
            bool has_left = false, has_right = false;
            for (size_t row = 0; row < count; row++) {
                if (col > 0 && lines[row][col - 1] != ' ') has_left = true;
                if (col + 1 < min_len && lines[row][col + 1] != ' ') has_right = true;
            }
            if (has_left && has_right) {
                out_positions[gap_count++] = col;
            }
        }
    }
    return gap_count;
}

bool wixen_looks_tabular(const char **lines, size_t line_count) {
    if (line_count < 2) return false;
    size_t positions[64];
    size_t gaps = find_common_gaps(lines, line_count, positions, 64);
    /* Need at least 2 column gaps for it to be a table */
    return gaps >= 2;
}

WixenDetectedTable *wixen_detect_table(const char **lines, size_t line_count,
                                        size_t start_row) {
    if (!wixen_looks_tabular(lines, line_count)) return NULL;

    size_t positions[64];
    size_t gaps = find_common_gaps(lines, line_count, positions, 64);

    WixenDetectedTable *table = calloc(1, sizeof(WixenDetectedTable));
    if (!table) return NULL;

    table->col_positions = calloc(gaps, sizeof(size_t));
    if (!table->col_positions) { free(table); return NULL; }
    memcpy(table->col_positions, positions, gaps * sizeof(size_t));
    table->col_count = gaps + 1; /* N gaps = N+1 columns */
    table->start_row = start_row;
    table->end_row = start_row + line_count;

    return table;
}

void wixen_detected_table_free(WixenDetectedTable *table) {
    if (!table) return;
    free(table->col_positions);
    free(table);
}
