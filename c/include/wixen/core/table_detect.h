/* table_detect.h — Heuristic table detection for accessibility */
#ifndef WIXEN_CORE_TABLE_DETECT_H
#define WIXEN_CORE_TABLE_DETECT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t *col_positions;   /* Column boundary positions */
    size_t col_count;
    size_t start_row;
    size_t end_row;          /* Exclusive */
} WixenDetectedTable;

/* Check if a set of lines looks tabular (aligned columns) */
bool wixen_looks_tabular(const char **lines, size_t line_count);

/* Detect table boundaries and column positions.
 * Returns NULL if no table detected. Caller must free result and col_positions. */
WixenDetectedTable *wixen_detect_table(const char **lines, size_t line_count,
                                        size_t start_row);

void wixen_detected_table_free(WixenDetectedTable *table);

#endif /* WIXEN_CORE_TABLE_DETECT_H */
