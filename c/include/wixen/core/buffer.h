/* buffer.h — Scrollback buffer with optional zstd compression */
#ifndef WIXEN_CORE_BUFFER_H
#define WIXEN_CORE_BUFFER_H

#include "wixen/core/grid.h"
#include <stddef.h>
#include <stdbool.h>

/* Compressed block: a batch of rows stored as a single blob */
typedef struct {
    uint8_t *data;        /* Compressed (or raw) bytes */
    size_t data_len;
    size_t row_count;     /* Number of rows in this block */
    size_t cols;          /* Column count when rows were stored */
    bool compressed;      /* true if zstd-compressed */
} WixenCompressedBlock;

/* Two-tier scrollback: hot (uncompressed) + cold (compressed blocks) */
typedef struct {
    /* Hot tier: recent rows, instant access */
    WixenRow *hot;
    size_t hot_count;
    size_t hot_cap;

    /* Cold tier: compressed blocks */
    WixenCompressedBlock *cold;
    size_t cold_count;
    size_t cold_cap;

    /* Configuration */
    size_t hot_threshold;   /* Compress when hot exceeds this (default 10000) */
    size_t total_len;       /* Total row count (hot + all cold rows) */
} WixenScrollbackBuffer;

/* Lifecycle */
void wixen_scrollback_init(WixenScrollbackBuffer *sb, size_t hot_threshold);
void wixen_scrollback_free(WixenScrollbackBuffer *sb);
void wixen_scrollback_clear(WixenScrollbackBuffer *sb);

/* Push a row (takes ownership — the row is moved, not copied) */
void wixen_scrollback_push(WixenScrollbackBuffer *sb, WixenRow *row, size_t cols);

/* Get a hot row (index 0 = oldest hot row). Returns NULL if out of range. */
const WixenRow *wixen_scrollback_get_hot(const WixenScrollbackBuffer *sb, size_t index);

/* Get any row by global index (handles cold decompression + hot).
 * index 0 = oldest row overall. Returns NULL if out of range.
 * For cold rows, decompresses on demand — caller must NOT hold the pointer
 * across multiple get calls (decompression buffer is reused). */
const WixenRow *wixen_scrollback_get(WixenScrollbackBuffer *sb, size_t index);

/* Total row count */
size_t wixen_scrollback_len(const WixenScrollbackBuffer *sb);

/* Hot row count only */
size_t wixen_scrollback_hot_len(const WixenScrollbackBuffer *sb);

/* Cold block count */
size_t wixen_scrollback_cold_blocks(const WixenScrollbackBuffer *sb);

#endif /* WIXEN_CORE_BUFFER_H */
