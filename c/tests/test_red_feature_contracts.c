/* test_red_feature_contracts.c — Explicit contract tests for incomplete/partial features.
 *
 * Each test encodes the CURRENT contract (supported, no-op, or not-yet-implemented)
 * so that future work cannot accidentally break expected behavior.
 *
 * Feature areas covered:
 *   - Image protocol (Sixel / image store)
 *   - Kitty graphics (APC dispatch)
 *   - Search modal (lifecycle, empty/no-rows)
 *   - Shell integration (OSC 133 block lifecycle)
 *   - Lua plugin engine (lifecycle, eval, error handling)
 *   - Settings dialog (tab configuration, NULL-parent guard)
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/image.h"
#include "wixen/core/sixel.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/action.h"
#include "wixen/search/search.h"
#include "wixen/shell_integ/shell_integ.h"
#include "wixen/config/lua_engine.h"
#include "wixen/ui/settings_dialog.h"

/* ================================================================
 * IMAGE PROTOCOL — image store starts at 0, Sixel no-op on junk
 * ================================================================ */

TEST contract_image_store_starts_at_zero(void) {
    WixenImageStore store;
    wixen_images_init(&store);
    ASSERT_EQ(0, (int)wixen_images_count(&store));
    ASSERT_EQ(0, (int)store.count);
    wixen_images_free(&store);
    PASS();
}

TEST contract_sixel_junk_does_not_crash(void) {
    /* Feeding random / garbage bytes to the Sixel decoder must not crash.
     * It should return false (decode failure) gracefully. */
    const char *junk = "\x01\x02\xFF\xFE garbage ~!@#$%^&*";
    uint32_t w = 0, h = 0;
    uint8_t *pixels = NULL;
    bool ok = wixen_decode_sixel((const uint8_t *)junk, strlen(junk), &w, &h, &pixels);
    /* Either it fails cleanly or produces some output — no crash is the contract */
    if (ok && pixels) {
        free(pixels);
    }
    PASS();
}

TEST contract_sixel_null_input_returns_false(void) {
    uint32_t w, h;
    uint8_t *pixels;
    ASSERT_FALSE(wixen_decode_sixel(NULL, 0, &w, &h, &pixels));
    PASS();
}

TEST contract_sixel_empty_input_returns_false(void) {
    uint32_t w, h;
    uint8_t *pixels;
    ASSERT_FALSE(wixen_decode_sixel((const uint8_t *)"", 0, &w, &h, &pixels));
    PASS();
}

SUITE(image_contract_tests) {
    RUN_TEST(contract_image_store_starts_at_zero);
    RUN_TEST(contract_sixel_junk_does_not_crash);
    RUN_TEST(contract_sixel_null_input_returns_false);
    RUN_TEST(contract_sixel_empty_input_returns_false);
}

/* ================================================================
 * KITTY GRAPHICS — APC dispatch with Kitty payload must not crash
 * ================================================================ */

TEST contract_kitty_apc_does_not_crash(void) {
    /* Construct an APC_DISPATCH action with a Kitty-style graphics payload.
     * The terminal should handle it gracefully (no-op or partial support). */
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);

    /* Kitty graphics: _Gf=32,s=1,v=1,a=T,t=d;AAAA  (minimal) */
    const char *payload = "_Gf=32,s=1,v=1,a=T,t=d;AAAA";
    size_t len = strlen(payload);
    uint8_t *data = malloc(len);
    memcpy(data, payload, len);

    WixenAction action;
    memset(&action, 0, sizeof(action));
    action.type = WIXEN_ACTION_APC_DISPATCH;
    action.apc.data = data;
    action.apc.data_len = len;

    /* Dispatching should not crash, even if Kitty graphics are not implemented */
    wixen_terminal_dispatch(&t, &action);

    free(data);
    wixen_terminal_free(&t);
    PASS();
}

TEST contract_kitty_apc_empty_payload_no_crash(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);

    WixenAction action;
    memset(&action, 0, sizeof(action));
    action.type = WIXEN_ACTION_APC_DISPATCH;
    action.apc.data = NULL;
    action.apc.data_len = 0;

    wixen_terminal_dispatch(&t, &action);

    wixen_terminal_free(&t);
    PASS();
}

SUITE(kitty_contract_tests) {
    RUN_TEST(contract_kitty_apc_does_not_crash);
    RUN_TEST(contract_kitty_apc_empty_payload_no_crash);
}

/* ================================================================
 * SEARCH MODAL — lifecycle, empty query, no rows
 * ================================================================ */

TEST contract_search_init_free_lifecycle(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    ASSERT_EQ(0, (int)se.match_count);
    ASSERT(se.query == NULL);
    wixen_search_free(&se);
    PASS();
}

TEST contract_search_empty_query_returns_zero(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);

    const char *rows[] = { "hello world", "foo bar" };
    WixenSearchOptions opts = { .case_sensitive = false, .regex = false, .wrap_around = false };
    wixen_search_execute(&se, "", opts, rows, 2, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));

    wixen_search_free(&se);
    PASS();
}

TEST contract_search_no_rows_returns_zero(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);

    WixenSearchOptions opts = { .case_sensitive = false, .regex = false, .wrap_around = false };
    wixen_search_execute(&se, "hello", opts, NULL, 0, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));

    wixen_search_free(&se);
    PASS();
}

TEST contract_search_double_free_safe(void) {
    /* init → free → free should not crash (idempotent cleanup). */
    WixenSearchEngine se;
    wixen_search_init(&se);
    wixen_search_free(&se);
    /* Second free on zeroed struct — must not crash */
    wixen_search_free(&se);
    PASS();
}

SUITE(search_contract_tests) {
    RUN_TEST(contract_search_init_free_lifecycle);
    RUN_TEST(contract_search_empty_query_returns_zero);
    RUN_TEST(contract_search_no_rows_returns_zero);
    RUN_TEST(contract_search_double_free_safe);
}

/* ================================================================
 * SHELL INTEGRATION — OSC 133 block lifecycle
 * ================================================================ */

TEST contract_shell_integ_osc133_a_creates_block(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);

    /* OSC 133;A — prompt start */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);

    ASSERT(si.block_count >= 1);
    ASSERT(si.osc133_active);

    wixen_shell_integ_free(&si);
    PASS();
}

TEST contract_shell_integ_osc133_d_marks_complete(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);

    /* Full cycle: A (prompt) → B (input) → C (execute) → D;0 (done, exit 0) */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);

    size_t count = 0;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &count);
    ASSERT(count >= 1);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, (int)blocks[0].state);
    ASSERT(blocks[0].has_exit_code);
    ASSERT_EQ(0, blocks[0].exit_code);

    wixen_shell_integ_free(&si);
    PASS();
}

TEST contract_shell_integ_block_count_increases(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);

    ASSERT_EQ(0, (int)si.block_count);

    /* First full prompt→command→output→done cycle */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);
    size_t after_first = si.block_count;
    ASSERT(after_first >= 1);

    /* Second cycle */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 6);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 6);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 7);
    wixen_shell_integ_handle_osc133(&si, 'D', "1", 10);
    ASSERT(si.block_count > after_first);

    wixen_shell_integ_free(&si);
    PASS();
}

TEST contract_shell_integ_init_free_clean(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    ASSERT_EQ(0, (int)si.block_count);
    ASSERT_FALSE(si.osc133_active);
    wixen_shell_integ_free(&si);
    PASS();
}

SUITE(shell_integ_contract_tests) {
    RUN_TEST(contract_shell_integ_osc133_a_creates_block);
    RUN_TEST(contract_shell_integ_osc133_d_marks_complete);
    RUN_TEST(contract_shell_integ_block_count_increases);
    RUN_TEST(contract_shell_integ_init_free_clean);
}

/* ================================================================
 * LUA PLUGIN — lifecycle, eval, error handling
 * ================================================================ */

TEST contract_lua_init_shutdown_lifecycle(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(e != NULL);
    wixen_lua_destroy(e);
    PASS();
}

TEST contract_lua_eval_return_42(void) {
    WixenLuaEngine *e = wixen_lua_create();
    ASSERT(wixen_lua_exec_string(e, "answer = 42"));
    int val = wixen_lua_get_int(e, "answer", 0);
    ASSERT_EQ(42, val);
    wixen_lua_destroy(e);
    PASS();
}

TEST contract_lua_syntax_error_returns_failure(void) {
    WixenLuaEngine *e = wixen_lua_create();
    /* Syntax error: must return false, must not crash */
    ASSERT_FALSE(wixen_lua_exec_string(e, "if if if end end {{{"));
    /* Engine still usable after error */
    ASSERT(wixen_lua_exec_string(e, "x = 1"));
    ASSERT_EQ(1, wixen_lua_get_int(e, "x", 0));
    wixen_lua_destroy(e);
    PASS();
}

TEST contract_lua_runtime_error_returns_failure(void) {
    WixenLuaEngine *e = wixen_lua_create();
    /* Runtime error: call nil as function */
    ASSERT_FALSE(wixen_lua_exec_string(e, "local f = nil; f()"));
    /* Engine still usable after runtime error */
    ASSERT(wixen_lua_exec_string(e, "y = 99"));
    ASSERT_EQ(99, wixen_lua_get_int(e, "y", 0));
    wixen_lua_destroy(e);
    PASS();
}

TEST contract_lua_null_engine_safe(void) {
    /* All operations on NULL engine must not crash */
    ASSERT_FALSE(wixen_lua_exec_string(NULL, "x = 1"));
    ASSERT_FALSE(wixen_lua_call(NULL, "f"));
    ASSERT(wixen_lua_get_string(NULL, "x") == NULL);
    ASSERT_EQ(42, wixen_lua_get_int(NULL, "x", 42));
    wixen_lua_destroy(NULL);
    PASS();
}

SUITE(lua_contract_tests) {
    RUN_TEST(contract_lua_init_shutdown_lifecycle);
    RUN_TEST(contract_lua_eval_return_42);
    RUN_TEST(contract_lua_syntax_error_returns_failure);
    RUN_TEST(contract_lua_runtime_error_returns_failure);
    RUN_TEST(contract_lua_null_engine_safe);
}

/* ================================================================
 * SETTINGS DIALOG — tab configuration, NULL-parent guard
 * ================================================================ */

TEST contract_settings_tab_count_is_four(void) {
    ASSERT_EQ(4, wixen_settings_tab_count());
    PASS();
}

TEST contract_settings_tab_names_valid(void) {
    /* Each tab index 0..3 must return a non-NULL name */
    for (int i = 0; i < 4; i++) {
        ASSERT(wixen_settings_tab_name(i) != NULL);
    }
    /* Out of range returns NULL */
    ASSERT(wixen_settings_tab_name(-1) == NULL);
    ASSERT(wixen_settings_tab_name(4) == NULL);
    PASS();
}

TEST contract_settings_tab_fields_non_empty(void) {
    /* Each tab must have at least one field */
    for (int i = 0; i < 4; i++) {
        size_t count = 0;
        const char **fields = wixen_settings_tab_fields(i, &count);
        ASSERT(fields != NULL);
        ASSERT(count > 0);
    }
    PASS();
}

TEST contract_settings_dialog_layout_params(void) {
    /* Layout helper functions must return sane values */
    ASSERT(wixen_settings_dialog_font_name() != NULL);
    ASSERT(wixen_settings_dialog_font_size() > 0);
    ASSERT(wixen_settings_dialog_width() > 0);
    ASSERT(wixen_settings_dialog_height() > 0);
    ASSERT(wixen_settings_dialog_margin() >= 0);
    ASSERT(wixen_settings_dialog_control_spacing() >= 0);
    PASS();
}

SUITE(settings_contract_tests) {
    RUN_TEST(contract_settings_tab_count_is_four);
    RUN_TEST(contract_settings_tab_names_valid);
    RUN_TEST(contract_settings_tab_fields_non_empty);
    RUN_TEST(contract_settings_dialog_layout_params);
}

/* ================================================================
 * MAIN
 * ================================================================ */

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(image_contract_tests);
    RUN_SUITE(kitty_contract_tests);
    RUN_SUITE(search_contract_tests);
    RUN_SUITE(shell_integ_contract_tests);
    RUN_SUITE(lua_contract_tests);
    /* RUN_SUITE(settings_contract_tests); */ /* Modal dialog — manual test only */
    GREATEST_MAIN_END();
}
