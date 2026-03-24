/* test_cell_extended.c — Cell and attribute tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/cell.h"

TEST cell_init_empty(void) {
    WixenCell c;
    wixen_cell_init(&c);
    /* Cell initializes to space or NUL */
    ASSERT(c.content[0] == '\0' || c.content[0] == ' ');
    ASSERT_FALSE(c.attrs.bold);
    ASSERT_FALSE(c.attrs.italic);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_set_content(void) {
    WixenCell c;
    wixen_cell_init(&c);
    wixen_cell_set_content(&c, "A");
    ASSERT_STR_EQ("A", c.content);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_set_utf8(void) {
    WixenCell c;
    wixen_cell_init(&c);
    wixen_cell_set_content(&c, "\xc3\xa9"); /* é */
    ASSERT_STR_EQ("\xc3\xa9", c.content);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_overwrite(void) {
    WixenCell c;
    wixen_cell_init(&c);
    wixen_cell_set_content(&c, "X");
    wixen_cell_set_content(&c, "Y");
    ASSERT_STR_EQ("Y", c.content);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_clear_via_reinit(void) {
    WixenCell c;
    wixen_cell_init(&c);
    wixen_cell_set_content(&c, "Z");
    wixen_cell_free(&c);
    wixen_cell_init(&c);
    ASSERT(c.content[0] == '\0' || c.content[0] == ' ');
    wixen_cell_free(&c);
    PASS();
}

TEST cell_attrs_default(void) {
    WixenCell c;
    wixen_cell_init(&c);
    ASSERT_FALSE(c.attrs.bold);
    ASSERT_FALSE(c.attrs.italic);
    ASSERT_FALSE(c.attrs.underline);
    ASSERT_FALSE(c.attrs.strikethrough);
    ASSERT_FALSE(c.attrs.inverse);
    ASSERT_FALSE(c.attrs.hidden);
    ASSERT_FALSE(c.attrs.dim);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, c.attrs.fg.type);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, c.attrs.bg.type);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_attrs_set_fg_indexed(void) {
    WixenCell c;
    wixen_cell_init(&c);
    c.attrs.fg.type = WIXEN_COLOR_INDEXED;
    c.attrs.fg.index = 5;
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c.attrs.fg.type);
    ASSERT_EQ(5, (int)c.attrs.fg.index);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_attrs_set_fg_rgb(void) {
    WixenCell c;
    wixen_cell_init(&c);
    c.attrs.fg.type = WIXEN_COLOR_RGB;
    c.attrs.fg.rgb.r = 255;
    c.attrs.fg.rgb.g = 128;
    c.attrs.fg.rgb.b = 0;
    ASSERT_EQ(WIXEN_COLOR_RGB, c.attrs.fg.type);
    ASSERT_EQ(255, (int)c.attrs.fg.rgb.r);
    ASSERT_EQ(128, (int)c.attrs.fg.rgb.g);
    ASSERT_EQ(0, (int)c.attrs.fg.rgb.b);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_width_default(void) {
    WixenCell c;
    wixen_cell_init(&c);
    ASSERT_EQ(1, (int)c.width);
    wixen_cell_free(&c);
    PASS();
}

TEST cell_width_double(void) {
    WixenCell c;
    wixen_cell_init(&c);
    c.width = 2;
    ASSERT_EQ(2, (int)c.width);
    wixen_cell_free(&c);
    PASS();
}

SUITE(cell_extended) {
    RUN_TEST(cell_init_empty);
    RUN_TEST(cell_set_content);
    RUN_TEST(cell_set_utf8);
    RUN_TEST(cell_overwrite);
    RUN_TEST(cell_clear_via_reinit);
    RUN_TEST(cell_attrs_default);
    RUN_TEST(cell_attrs_set_fg_indexed);
    RUN_TEST(cell_attrs_set_fg_rgb);
    RUN_TEST(cell_width_default);
    RUN_TEST(cell_width_double);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(cell_extended);
    GREATEST_MAIN_END();
}
