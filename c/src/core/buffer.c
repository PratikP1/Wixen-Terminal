/* buffer.c — Scrollback buffer with two-tier hot/cold storage
 *
 * Hot tier: recent rows in an uncompressed array (instant access).
 * Cold tier: older rows serialized into compressed blocks.
 *
 * When hot_count exceeds hot_threshold, the oldest half is serialized
 * and moved to a cold block. Without zstd, blocks are stored uncompressed.
 */
#include "wixen/core/buffer.h"
#include <zstd.h>
#include <stdlib.h>
#include <string.h>

/* --- Serialization format ---
 * Each row: [wrapped:1][cell_count:4LE][cells...]
 * Each cell: [content_len:2LE][content:bytes][width:1][attrs:sizeof(WixenCellAttributes)]
 */

static size_t serialize_rows(const WixenRow *rows, size_t count,
                             uint8_t **out_data) {
    /* First pass: compute total size */
    size_t total = 0;
    for (size_t r = 0; r < count; r++) {
        total += 1; /* wrapped */
        total += 4; /* cell_count */
        for (size_t c = 0; c < rows[r].count; c++) {
            const char *content = rows[r].cells[c].content;
            size_t clen = content ? strlen(content) : 0;
            total += 2 + clen + 1 + sizeof(WixenCellAttributes);
        }
    }

    uint8_t *buf = malloc(total);
    if (!buf) { *out_data = NULL; return 0; }

    size_t pos = 0;
    for (size_t r = 0; r < count; r++) {
        buf[pos++] = rows[r].wrapped ? 1 : 0;
        uint32_t cell_count = (uint32_t)rows[r].count;
        memcpy(&buf[pos], &cell_count, 4); pos += 4;
        for (size_t c = 0; c < rows[r].count; c++) {
            const char *content = rows[r].cells[c].content;
            uint16_t clen = content ? (uint16_t)strlen(content) : 0;
            memcpy(&buf[pos], &clen, 2); pos += 2;
            if (clen > 0) { memcpy(&buf[pos], content, clen); pos += clen; }
            buf[pos++] = rows[r].cells[c].width;
            memcpy(&buf[pos], &rows[r].cells[c].attrs, sizeof(WixenCellAttributes));
            pos += sizeof(WixenCellAttributes);
        }
    }

    *out_data = buf;
    return total;
}

static void compress_oldest_hot(WixenScrollbackBuffer *sb, size_t cols) {
    /* Move the oldest half of hot rows to a cold block */
    size_t to_compress = sb->hot_count / 2;
    if (to_compress == 0) return;

    uint8_t *data;
    size_t data_len = serialize_rows(sb->hot, to_compress, &data);
    if (!data) return;

    /* Compress with zstd level 3 */
    bool is_compressed = false;
    {
        size_t compressed_bound = ZSTD_compressBound(data_len);
        uint8_t *compressed = malloc(compressed_bound);
        if (compressed) {
            size_t compressed_size = ZSTD_compress(compressed, compressed_bound,
                                                    data, data_len, 3);
            if (!ZSTD_isError(compressed_size)) {
                free(data);
                data = compressed;
                data_len = compressed_size;
                is_compressed = true;
            } else {
                free(compressed);
            }
        }
    }

    /* Add cold block */
    if (sb->cold_count >= sb->cold_cap) {
        size_t new_cap = sb->cold_cap ? sb->cold_cap * 2 : 16;
        WixenCompressedBlock *new_arr = realloc(sb->cold,
                                                 new_cap * sizeof(WixenCompressedBlock));
        if (!new_arr) { free(data); return; }
        sb->cold = new_arr;
        sb->cold_cap = new_cap;
    }

    WixenCompressedBlock *block = &sb->cold[sb->cold_count++];
    block->data = data;
    block->data_len = data_len;
    block->row_count = to_compress;
    block->cols = cols;
    block->compressed = is_compressed;

    /* Free compressed rows and shift remaining hot rows */
    for (size_t i = 0; i < to_compress; i++) {
        wixen_row_free(&sb->hot[i]);
    }
    size_t remaining = sb->hot_count - to_compress;
    if (remaining > 0) {
        memmove(sb->hot, &sb->hot[to_compress], remaining * sizeof(WixenRow));
    }
    sb->hot_count = remaining;
}

/* --- Lifecycle --- */

void wixen_scrollback_init(WixenScrollbackBuffer *sb, size_t hot_threshold) {
    memset(sb, 0, sizeof(*sb));
    sb->hot_threshold = hot_threshold > 0 ? hot_threshold : 10000;
}

void wixen_scrollback_free(WixenScrollbackBuffer *sb) {
    for (size_t i = 0; i < sb->hot_count; i++) {
        wixen_row_free(&sb->hot[i]);
    }
    free(sb->hot);
    for (size_t i = 0; i < sb->cold_count; i++) {
        free(sb->cold[i].data);
    }
    free(sb->cold);
    memset(sb, 0, sizeof(*sb));
}

void wixen_scrollback_clear(WixenScrollbackBuffer *sb) {
    size_t threshold = sb->hot_threshold;
    wixen_scrollback_free(sb);
    wixen_scrollback_init(sb, threshold);
}

/* --- Push --- */

void wixen_scrollback_push(WixenScrollbackBuffer *sb, WixenRow *row, size_t cols) {
    /* Ensure hot capacity */
    if (sb->hot_count >= sb->hot_cap) {
        size_t new_cap = sb->hot_cap ? sb->hot_cap * 2 : 256;
        WixenRow *new_arr = realloc(sb->hot, new_cap * sizeof(WixenRow));
        if (!new_arr) return;
        sb->hot = new_arr;
        sb->hot_cap = new_cap;
    }

    /* Move the row into hot storage (transfer ownership) */
    sb->hot[sb->hot_count++] = *row;
    memset(row, 0, sizeof(*row)); /* Caller's row is now empty */
    sb->total_len++;

    /* Compress if threshold exceeded */
    if (sb->hot_count > sb->hot_threshold) {
        compress_oldest_hot(sb, cols);
    }
}

/* --- Access --- */

const WixenRow *wixen_scrollback_get_hot(const WixenScrollbackBuffer *sb, size_t index) {
    if (index >= sb->hot_count) return NULL;
    return &sb->hot[index];
}

size_t wixen_scrollback_len(const WixenScrollbackBuffer *sb) {
    return sb->total_len;
}

size_t wixen_scrollback_hot_len(const WixenScrollbackBuffer *sb) {
    return sb->hot_count;
}

size_t wixen_scrollback_cold_blocks(const WixenScrollbackBuffer *sb) {
    return sb->cold_count;
}
