/* test_red_cursor_char.c — RED tests for NVDA character-by-character reading
 *
 * BUG: When pressing left arrow, NVDA reads "line feed" for every character
 * instead of the actual character. Root cause: visible_text trims trailing
 * spaces, so cursor offset lands on \n instead of the character.
 *
 * These tests verify that ExpandToEnclosingUnit(Character) + GetText returns
 * the correct character at the cursor position, NOT "\n".
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/grid.h"
#include "wixen/a11y/text_boundaries.h"

#ifdef _WIN32
#define STRDUP _strdup
#else
#define STRDUP strdup
#endif

/* Helper: set up a grid row with a string (one ASCII char per cell) */
static void set_row_text(WixenGrid *g, size_t row, const char *text) {
    size_t len = strlen(text);
    for (size_t i = 0; i < len && i < g->cols; i++) {
        char buf[2] = { text[i], '\0' };
        wixen_cell_set_content(&g->rows[row].cells[i], buf);
    }
}

/* Helper: simulate ExpandToEnclosingUnit(Character) + GetText
 *
 * Given a grid, cursor row/col, extract visible_text, compute cursor offset
 * the same way main.c does, then expand to character unit and return the char.
 *
 * Returns the character at the cursor position (caller must free). */
static char *get_char_at_cursor(WixenGrid *g, size_t cursor_row, size_t cursor_col) {
    char *visible = wixen_grid_visible_text_dynamic(g);
    if (!visible) return NULL;
    size_t text_len = strlen(visible);

    /* Compute cursor offset the same way the fixed main.c does:
     * Each row in visible_text is padded to g->cols characters.
     * For each preceding row: cols + 1 (for \n) UTF-16 units.
     * Then add cursor_col for the current row. */
    int utf16_off = 0;
    for (size_t r = 0; r < cursor_row && r < g->num_rows; r++) {
        char *rbuf = wixen_row_text_dynamic(&g->rows[r]);
        if (rbuf) {
            size_t byte_len = strlen(rbuf);
            size_t content_utf16 = wixen_utf8_to_utf16_offset(rbuf, byte_len);
            size_t padded_utf16 = content_utf16 < g->cols ? g->cols : content_utf16;
            utf16_off += (int)padded_utf16 + 1; /* +1 for \n */
            free(rbuf);
        }
    }
    /* cursor_col maps directly to a UTF-16 offset in the padded row */
    utf16_off += (int)cursor_col;

    /* Simulate ExpandToEnclosingUnit(Character): range [start, start+1) */
    int start = utf16_off;
    int end = start;
    int doc_utf16 = (int)wixen_utf8_to_utf16_offset(visible, text_len);
    if (end <= start && start < doc_utf16) {
        end = start + 1;
    }

    /* Extract the character via byte conversion (simulating GetText) */
    if (start >= end || start >= doc_utf16) {
        free(visible);
        return STRDUP("");
    }
    size_t byte_s = wixen_utf16_to_utf8_offset(visible, text_len, (size_t)start);
    size_t byte_e = wixen_utf16_to_utf8_offset(visible, text_len, (size_t)end);
    size_t char_len = byte_e - byte_s;
    char *result = (char *)malloc(char_len + 1);
    if (result) {
        memcpy(result, visible + byte_s, char_len);
        result[char_len] = '\0';
    }
    free(visible);
    return result;
}

/* Test 1: "Hello World" on row 0, cursor at col 5 -> space (not newline) */
TEST cursor_at_col5_hello_world_not_newline(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    set_row_text(&g, 0, "Hello World");

    /* Col 5 of "Hello World" is the space character */
    char *ch = get_char_at_cursor(&g, 0, 5);
    ASSERT(ch != NULL);
    ASSERT(strcmp(ch, "\n") != 0);  /* Must NOT be newline */
    ASSERT_EQ(ch[0], ' ');         /* Should be space */
    free(ch);

    wixen_grid_free(&g);
    PASS();
}

/* Test 2: Cursor at col 4 should return "o", NOT "\n" */
TEST cursor_at_col4_hello_world_returns_o(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    set_row_text(&g, 0, "Hello World");

    char *ch = get_char_at_cursor(&g, 0, 4);
    ASSERT(ch != NULL);
    ASSERT(strcmp(ch, "\n") != 0);
    ASSERT_EQ(ch[0], 'o');
    free(ch);

    wixen_grid_free(&g);
    PASS();
}

/* Test 3: Moving cursor left to col 3 should return "l", NOT "\n" */
TEST cursor_at_col3_returns_l(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    set_row_text(&g, 0, "Hello World");

    char *ch = get_char_at_cursor(&g, 0, 3);
    ASSERT(ch != NULL);
    ASSERT(strcmp(ch, "\n") != 0);
    ASSERT_EQ(ch[0], 'l');
    free(ch);

    wixen_grid_free(&g);
    PASS();
}

/* Test 4: Cursor at col 0 row 1 with "Second line" should return "S" */
TEST cursor_row1_col0_returns_S(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    set_row_text(&g, 0, "Hello World");
    set_row_text(&g, 1, "Second line");

    char *ch = get_char_at_cursor(&g, 1, 0);
    ASSERT(ch != NULL);
    ASSERT(strcmp(ch, "\n") != 0);
    ASSERT_EQ(ch[0], 'S');
    free(ch);

    wixen_grid_free(&g);
    PASS();
}

/* Test 5: Cursor at end of short line — col 5 on "Hello" (5 chars, 80-col grid)
 * The char after "o" should be space (from padding), NOT "\n" */
TEST cursor_past_end_of_short_line_not_newline(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    set_row_text(&g, 0, "Hello");

    /* Col 5 is right after "Hello" ends — should be space, not \n */
    char *ch = get_char_at_cursor(&g, 0, 5);
    ASSERT(ch != NULL);
    ASSERT(strcmp(ch, "\n") != 0);  /* This is the core bug: currently returns "\n" */
    free(ch);

    wixen_grid_free(&g);
    PASS();
}

/* Test 6: visible_text rows should be padded to grid column width */
TEST visible_text_rows_padded_to_cols(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 2);
    set_row_text(&g, 0, "Hello");
    set_row_text(&g, 1, "World");

    char *visible = wixen_grid_visible_text_dynamic(&g);
    ASSERT(visible != NULL);

    /* Each row should be exactly 10 chars (padded with spaces), separated by \n.
     * Total: 10 + 1 + 10 = 21 chars */
    size_t len = strlen(visible);
    ASSERT_EQ(len, (size_t)21);

    /* First row: "Hello     " (10 chars) */
    ASSERT(memcmp(visible, "Hello     ", 10) == 0);
    /* Newline separator */
    ASSERT_EQ(visible[10], '\n');
    /* Second row: "World     " (10 chars) */
    ASSERT(memcmp(visible + 11, "World     ", 10) == 0);

    free(visible);
    wixen_grid_free(&g);
    PASS();
}

/* Test 7: cursor offset calculation should account for padded rows */
TEST cursor_offset_accounts_for_padding(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 3);
    set_row_text(&g, 0, "AB");
    set_row_text(&g, 1, "CD");

    char *visible = wixen_grid_visible_text_dynamic(&g);
    ASSERT(visible != NULL);

    /* Row 0 padded = "AB        " (10 chars), then \n, then row 1 = "CD        "
     * Cursor at row 1, col 0 should point to 'C' at byte offset 11 */
    size_t byte_off = wixen_text_rowcol_to_offset(visible, strlen(visible), 1, 0);
    ASSERT_EQ(visible[byte_off], 'C');

    /* Cursor at row 1, col 1 should point to 'D' */
    byte_off = wixen_text_rowcol_to_offset(visible, strlen(visible), 1, 1);
    ASSERT_EQ(visible[byte_off], 'D');

    free(visible);
    wixen_grid_free(&g);
    PASS();
}

SUITE(cursor_char) {
    RUN_TEST(cursor_at_col5_hello_world_not_newline);
    RUN_TEST(cursor_at_col4_hello_world_returns_o);
    RUN_TEST(cursor_at_col3_returns_l);
    RUN_TEST(cursor_row1_col0_returns_S);
    RUN_TEST(cursor_past_end_of_short_line_not_newline);
    RUN_TEST(visible_text_rows_padded_to_cols);
    RUN_TEST(cursor_offset_accounts_for_padding);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(cursor_char);
    GREATEST_MAIN_END();
}
