/* cell.h — Terminal cell, attributes, and color types */
#ifndef WIXEN_CORE_CELL_H
#define WIXEN_CORE_CELL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* --- Color --- */

typedef enum {
    WIXEN_COLOR_DEFAULT = 0,
    WIXEN_COLOR_INDEXED,
    WIXEN_COLOR_RGB,
} WixenColorType;

typedef struct {
    WixenColorType type;
    union {
        uint8_t index;           /* 0-255 palette index */
        struct { uint8_t r, g, b; } rgb;
    };
} WixenColor;

/* --- Underline style --- */

typedef enum {
    WIXEN_UNDERLINE_NONE = 0,
    WIXEN_UNDERLINE_SINGLE,
    WIXEN_UNDERLINE_DOUBLE,
    WIXEN_UNDERLINE_CURLY,
    WIXEN_UNDERLINE_DOTTED,
    WIXEN_UNDERLINE_DASHED,
} WixenUnderlineStyle;

/* --- Cell attributes (SGR state) --- */

typedef struct {
    WixenColor fg;
    WixenColor bg;
    WixenColor underline_color;
    WixenUnderlineStyle underline;
    uint32_t hyperlink_id;       /* 0 = no link */
    bool bold;
    bool dim;
    bool italic;
    bool blink;
    bool inverse;
    bool hidden;
    bool strikethrough;
    bool overline;
} WixenCellAttributes;

/* --- Cell --- */

typedef struct {
    char *content;               /* UTF-8, heap-allocated. " " for empty. */
    WixenCellAttributes attrs;
    uint8_t width;               /* 0=wide continuation, 1=normal, 2=wide char */
    uint32_t hyperlink_id;       /* 0=no link, >0=index into HyperlinkStore */
} WixenCell;

/* Lifecycle */
void wixen_cell_init(WixenCell *cell);
void wixen_cell_free(WixenCell *cell);
void wixen_cell_clone(WixenCell *dst, const WixenCell *src);
void wixen_cell_reset(WixenCell *cell);
void wixen_cell_set_content(WixenCell *cell, const char *utf8);

/* Attributes */
void wixen_cell_attrs_default(WixenCellAttributes *attrs);
bool wixen_cell_attrs_equal(const WixenCellAttributes *a, const WixenCellAttributes *b);

/* Color constructors */
WixenColor wixen_color_default(void);
WixenColor wixen_color_indexed(uint8_t index);
WixenColor wixen_color_rgb(uint8_t r, uint8_t g, uint8_t b);
bool wixen_color_equal(const WixenColor *a, const WixenColor *b);

#endif /* WIXEN_CORE_CELL_H */
