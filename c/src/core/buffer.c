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

const WixenRow *wixen_scrollback_get(WixenScrollbackBuffer *sb, size_t index) {
    if (index >= sb->total_len) return NULL;

    /* Count total cold rows */
    size_t cold_total = 0;
    for (size_t i = 0; i < sb->cold_count; i++) {
        cold_total += sb->cold[i].row_count;
    }

    if (index < cold_total) {
        /* Find which cold block contains this index */
        size_t offset = 0;
        for (size_t i = 0; i < sb->cold_count; i++) {
            if (index < offset + sb->cold[i].row_count) {
                /* Decompress this block */
                size_t row_in_block = index - offset;
                size_t decompressed_size = ZSTD_getFrameContentSize(
                    sb->cold[i].data, sb->cold[i].data_len);
                if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
                    decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) return NULL;

                uint8_t *decompressed = malloc(decompressed_size);
                if (!decompressed) return NULL;
                size_t result = ZSTD_decompress(decompressed, decompressed_size,
                                                 sb->cold[i].data, sb->cold[i].data_len);
                if (ZSTD_isError(result)) { free(decompressed); return NULL; }

                /* Deserialize using actual format:
                 * Per row: [wrapped:1][cell_count:4LE][cells...]
                 * Per cell: [content_len:2LE][content][width:1][attrs] */
                size_t pos = 0;

                /* Skip to target row */
                for (size_t r = 0; r < row_in_block; r++) {
                    if (pos + 5 > result) { free(decompressed); return NULL; }
                    pos += 1; /* wrapped */
                    uint32_t cc; memcpy(&cc, decompressed + pos, 4); pos += 4;
                    for (uint32_t c = 0; c < cc; c++) {
                        if (pos + 2 > result) { free(decompressed); return NULL; }
                        uint16_t cl; memcpy(&cl, decompressed + pos, 2); pos += 2;
                        pos += cl + 1 + sizeof(WixenCellAttributes);
                    }
                }

                /* Deserialize target row */
                static WixenRow cached_row;
                static bool cached_inited = false;
                if (cached_inited) wixen_row_free(&cached_row);

                if (pos + 5 > result) { free(decompressed); return NULL; }
                bool wrapped = decompressed[pos++] != 0;
                uint32_t cell_count;
                memcpy(&cell_count, decompressed + pos, 4); pos += 4;

                wixen_row_init(&cached_row, cell_count);
                cached_inited = true;
                cached_row.wrapped = wrapped;

                for (uint32_t c = 0; c < cell_count && pos + 2 <= result; c++) {
                    uint16_t clen;
                    memcpy(&clen, decompressed + pos, 2); pos += 2;
                    if (clen > 0 && pos + clen <= result) {
                        char *content = malloc(clen + 1);
                        if (content) {
                            memcpy(content, decompressed + pos, clen);
                            content[clen] = '\0';
                            wixen_cell_set_content(&cached_row.cells[c], content);
                            free(content);
                        }
                    }
                    pos += clen;
                    if (pos + 1 <= result) {
                        cached_row.cells[c].width = decompressed[pos]; pos += 1;
                    }
                    if (pos + sizeof(WixenCellAttributes) <= result) {
                        memcpy(&cached_row.cells[c].attrs, decompressed + pos,
                               sizeof(WixenCellAttributes));
                        pos += sizeof(WixenCellAttributes);
                    }
                }

                free(decompressed);
                return &cached_row;
            }
            offset += sb->cold[i].row_count;
        }
        return NULL;
    } else {
        /* Hot tier */
        size_t hot_index = index - cold_total;
        return wixen_scrollback_get_hot(sb, hot_index);
    }
}
