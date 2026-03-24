/* test_cell.c — Tests for WixenCell, WixenCellAttributes, WixenColor */
#include "greatest.h"
#include "wixen/core/cell.h"

/* --- Cell lifecycle --- */

TEST cell_default_is_space(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    ASSERT_STR_EQ(" ", cell.content);
    ASSERT_EQ(1, cell.width);
    wixen_cell_free(&cell);
    PASS();
}

TEST cell_default_attrs_are_default_color(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, cell.attrs.fg.type);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, cell.attrs.bg.type);
    ASSERT_FALSE(cell.attrs.bold);
    ASSERT_FALSE(cell.attrs.italic);
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, cell.attrs.underline);
    ASSERT_EQ(0, (int)cell.attrs.hyperlink_id);
    wixen_cell_free(&cell);
    PASS();
}

TEST cell_set_content(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    wixen_cell_set_content(&cell, "A");
    ASSERT_STR_EQ("A", cell.content);
    wixen_cell_free(&cell);
    PASS();
}

TEST cell_set_content_utf8_multibyte(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    wixen_cell_set_content(&cell, "\xC3\xA9");  /* é */
    ASSERT_STR_EQ("\xC3\xA9", cell.content);
    wixen_cell_free(&cell);
    PASS();
}

TEST cell_reset_returns_to_default(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    wixen_cell_set_content(&cell, "X");
    cell.attrs.bold = true;
    cell.width = 2;
    wixen_cell_reset(&cell);
    ASSERT_STR_EQ(" ", cell.content);
    ASSERT_EQ(1, cell.width);
    ASSERT_FALSE(cell.attrs.bold);
    wixen_cell_free(&cell);
    PASS();
}

TEST cell_clone_is_independent(void) {
    WixenCell src, dst;
    wixen_cell_init(&src);
    wixen_cell_set_content(&src, "Z");
    src.attrs.bold = true;

    wixen_cell_clone(&dst, &src);
    ASSERT_STR_EQ("Z", dst.content);
    ASSERT(dst.attrs.bold);

    /* Mutating src doesn't affect dst */
    wixen_cell_set_content(&src, "Q");
    ASSERT_STR_EQ("Z", dst.content);

    wixen_cell_free(&src);
    wixen_cell_free(&dst);
    PASS();
}

TEST cell_free_null_content_safe(void) {
    WixenCell cell;
    cell.content = NULL;
    cell.width = 0;
    wixen_cell_attrs_default(&cell.attrs);
    wixen_cell_free(&cell);  /* Should not crash */
    ASSERT_EQ(NULL, cell.content);
    PASS();
}

TEST cell_set_content_empty_string(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    wixen_cell_set_content(&cell, "");
    ASSERT_STR_EQ("", cell.content);
    wixen_cell_free(&cell);
    PASS();
}

TEST cell_set_content_replaces_previous(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    wixen_cell_set_content(&cell, "A");
    wixen_cell_set_content(&cell, "B");
    ASSERT_STR_EQ("B", cell.content);
    wixen_cell_free(&cell);
    PASS();
}

/* --- Color --- */

TEST color_default(void) {
    WixenColor c = wixen_color_default();
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, c.type);
    PASS();
}

TEST color_indexed(void) {
    WixenColor c = wixen_color_indexed(42);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c.type);
    ASSERT_EQ(42, c.index);
    PASS();
}

TEST color_rgb(void) {
    WixenColor c = wixen_color_rgb(255, 128, 0);
    ASSERT_EQ(WIXEN_COLOR_RGB, c.type);
    ASSERT_EQ(255, c.rgb.r);
    ASSERT_EQ(128, c.rgb.g);
    ASSERT_EQ(0, c.rgb.b);
    PASS();
}

TEST color_equality(void) {
    WixenColor a = wixen_color_rgb(10, 20, 30);
    WixenColor b = wixen_color_rgb(10, 20, 30);
    WixenColor c = wixen_color_rgb(10, 20, 31);
    WixenColor d = wixen_color_default();
    ASSERT(wixen_color_equal(&a, &b));
    ASSERT_FALSE(wixen_color_equal(&a, &c));
    ASSERT_FALSE(wixen_color_equal(&a, &d));
    PASS();
}

TEST color_indexed_equality(void) {
    WixenColor a = wixen_color_indexed(5);
    WixenColor b = wixen_color_indexed(5);
    WixenColor c = wixen_color_indexed(6);
    ASSERT(wixen_color_equal(&a, &b));
    ASSERT_FALSE(wixen_color_equal(&a, &c));
    PASS();
}

/* --- Attributes --- */

TEST attrs_default_all_false(void) {
    WixenCellAttributes attrs;
    wixen_cell_attrs_default(&attrs);
    ASSERT_FALSE(attrs.bold);
    ASSERT_FALSE(attrs.dim);
    ASSERT_FALSE(attrs.italic);
    ASSERT_FALSE(attrs.blink);
    ASSERT_FALSE(attrs.inverse);
    ASSERT_FALSE(attrs.hidden);
    ASSERT_FALSE(attrs.strikethrough);
    ASSERT_FALSE(attrs.overline);
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, attrs.underline);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, attrs.fg.type);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, attrs.bg.type);
    PASS();
}

TEST attrs_equality(void) {
    WixenCellAttributes a, b;
    wixen_cell_attrs_default(&a);
    wixen_cell_attrs_default(&b);
    ASSERT(wixen_cell_attrs_equal(&a, &b));

    b.bold = true;
    ASSERT_FALSE(wixen_cell_attrs_equal(&a, &b));
    PASS();
}

TEST attrs_equality_color_difference(void) {
    WixenCellAttributes a, b;
    wixen_cell_attrs_default(&a);
    wixen_cell_attrs_default(&b);
    a.fg = wixen_color_indexed(1);
    ASSERT_FALSE(wixen_cell_attrs_equal(&a, &b));
    PASS();
}

/* --- Suites --- */

SUITE(cell_lifecycle) {
    RUN_TEST(cell_default_is_space);
    RUN_TEST(cell_default_attrs_are_default_color);
    RUN_TEST(cell_set_content);
    RUN_TEST(cell_set_content_utf8_multibyte);
    RUN_TEST(cell_reset_returns_to_default);
    RUN_TEST(cell_clone_is_independent);
    RUN_TEST(cell_free_null_content_safe);
    RUN_TEST(cell_set_content_empty_string);
    RUN_TEST(cell_set_content_replaces_previous);
}

SUITE(color_tests) {
    RUN_TEST(color_default);
    RUN_TEST(color_indexed);
    RUN_TEST(color_rgb);
    RUN_TEST(color_equality);
    RUN_TEST(color_indexed_equality);
}

SUITE(attrs_tests) {
    RUN_TEST(attrs_default_all_false);
    RUN_TEST(attrs_equality);
    RUN_TEST(attrs_equality_color_difference);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(cell_lifecycle);
    RUN_SUITE(color_tests);
    RUN_SUITE(attrs_tests);
    GREATEST_MAIN_END();
}
