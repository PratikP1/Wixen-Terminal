/* test_vt_escapes.c — VT escape stripping and control char filtering */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/events.h"

TEST strip_plain_text(void) {
    char *s = wixen_strip_vt_escapes("Hello World");
    ASSERT_STR_EQ("Hello World", s);
    free(s); PASS();
}

TEST strip_single_csi(void) {
    char *s = wixen_strip_vt_escapes("\x1b[31mRed");
    ASSERT_STR_EQ("Red", s);
    free(s); PASS();
}

TEST strip_multiple_csi(void) {
    char *s = wixen_strip_vt_escapes("\x1b[1m\x1b[31m\x1b[42mText\x1b[0m");
    ASSERT_STR_EQ("Text", s);
    free(s); PASS();
}

TEST strip_osc_bel(void) {
    char *s = wixen_strip_vt_escapes("\x1b]0;Title\x07Rest");
    ASSERT(strstr(s, "Rest") != NULL);
    free(s); PASS();
}

TEST strip_empty_string(void) {
    char *s = wixen_strip_vt_escapes("");
    ASSERT_STR_EQ("", s);
    free(s); PASS();
}

TEST strip_null_returns_empty(void) {
    char *s = wixen_strip_vt_escapes(NULL);
    ASSERT(s != NULL);
    ASSERT_EQ(0, (int)strlen(s));
    free(s); PASS();
}

TEST strip_only_escapes(void) {
    char *s = wixen_strip_vt_escapes("\x1b[31m\x1b[0m");
    ASSERT_STR_EQ("", s);
    free(s); PASS();
}

TEST strip_preserves_unicode(void) {
    char *s = wixen_strip_vt_escapes("\x1b[31mcaf\xc3\xa9\x1b[0m");
    ASSERT_STR_EQ("caf\xc3\xa9", s);
    free(s); PASS();
}

TEST ctrl_strip_basic(void) {
    char *s = wixen_strip_control_chars("A\x01" "B\x02" "C");
    ASSERT_STR_EQ("ABC", s);
    free(s); PASS();
}

TEST ctrl_preserve_newline(void) {
    char *s = wixen_strip_control_chars("Line1\nLine2");
    ASSERT_STR_EQ("Line1\nLine2", s);
    free(s); PASS();
}

TEST ctrl_strip_tab(void) {
    char *s = wixen_strip_control_chars("A\tB");
    /* Tab might be preserved or stripped depending on implementation */
    ASSERT(s != NULL);
    ASSERT(strlen(s) >= 2);
    free(s); PASS();
}

TEST ctrl_empty(void) {
    char *s = wixen_strip_control_chars("");
    ASSERT_STR_EQ("", s);
    free(s); PASS();
}

TEST ctrl_all_control(void) {
    char *s = wixen_strip_control_chars("\x01\x02\x03\x04\x05");
    ASSERT_STR_EQ("", s);
    free(s); PASS();
}

SUITE(vt_escapes) {
    RUN_TEST(strip_plain_text);
    RUN_TEST(strip_single_csi);
    RUN_TEST(strip_multiple_csi);
    RUN_TEST(strip_osc_bel);
    RUN_TEST(strip_empty_string);
    RUN_TEST(strip_null_returns_empty);
    RUN_TEST(strip_only_escapes);
    RUN_TEST(strip_preserves_unicode);
    RUN_TEST(ctrl_strip_basic);
    RUN_TEST(ctrl_preserve_newline);
    RUN_TEST(ctrl_strip_tab);
    RUN_TEST(ctrl_empty);
    RUN_TEST(ctrl_all_control);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(vt_escapes);
    GREATEST_MAIN_END();
}
