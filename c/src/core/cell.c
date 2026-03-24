/* cell.c — Terminal cell, attributes, and color implementation */
#include "wixen/core/cell.h"
#include <stdlib.h>
#include <string.h>

/* --- Color --- */

WixenColor wixen_color_default(void) {
    WixenColor c = { .type = WIXEN_COLOR_DEFAULT };
    return c;
}

WixenColor wixen_color_indexed(uint8_t index) {
    WixenColor c = { .type = WIXEN_COLOR_INDEXED, .index = index };
    return c;
}

WixenColor wixen_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    WixenColor c = { .type = WIXEN_COLOR_RGB, .rgb = { r, g, b } };
    return c;
}

bool wixen_color_equal(const WixenColor *a, const WixenColor *b) {
    if (a->type != b->type) return false;
    switch (a->type) {
    case WIXEN_COLOR_DEFAULT:
        return true;
    case WIXEN_COLOR_INDEXED:
        return a->index == b->index;
    case WIXEN_COLOR_RGB:
        return a->rgb.r == b->rgb.r
            && a->rgb.g == b->rgb.g
            && a->rgb.b == b->rgb.b;
    }
    return false;
}

/* --- Attributes --- */

void wixen_cell_attrs_default(WixenCellAttributes *attrs) {
    memset(attrs, 0, sizeof(*attrs));
    attrs->fg = wixen_color_default();
    attrs->bg = wixen_color_default();
    attrs->underline_color = wixen_color_default();
    attrs->underline = WIXEN_UNDERLINE_NONE;
}

bool wixen_cell_attrs_equal(const WixenCellAttributes *a, const WixenCellAttributes *b) {
    return wixen_color_equal(&a->fg, &b->fg)
        && wixen_color_equal(&a->bg, &b->bg)
        && wixen_color_equal(&a->underline_color, &b->underline_color)
        && a->underline == b->underline
        && a->hyperlink_id == b->hyperlink_id
        && a->bold == b->bold
        && a->dim == b->dim
        && a->italic == b->italic
        && a->blink == b->blink
        && a->inverse == b->inverse
        && a->hidden == b->hidden
        && a->strikethrough == b->strikethrough
        && a->overline == b->overline;
}

/* --- Cell --- */

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

void wixen_cell_init(WixenCell *cell) {
    cell->content = dup_str(" ");
    wixen_cell_attrs_default(&cell->attrs);
    cell->width = 1;
}

void wixen_cell_free(WixenCell *cell) {
    free(cell->content);
    cell->content = NULL;
}

void wixen_cell_clone(WixenCell *dst, const WixenCell *src) {
    dst->content = dup_str(src->content);
    dst->attrs = src->attrs;
    dst->width = src->width;
}

void wixen_cell_reset(WixenCell *cell) {
    free(cell->content);
    cell->content = dup_str(" ");
    wixen_cell_attrs_default(&cell->attrs);
    cell->width = 1;
}

void wixen_cell_set_content(WixenCell *cell, const char *utf8) {
    free(cell->content);
    cell->content = dup_str(utf8);
}
