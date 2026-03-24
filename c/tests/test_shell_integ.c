/* test_shell_integ.c — Tests for shell integration (OSC 133) */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/shell_integ/shell_integ.h"

TEST shell_integ_init_empty(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    size_t count;
    wixen_shell_integ_blocks(&si, &count);
    ASSERT_EQ(0, (int)count);
    ASSERT_FALSE(si.osc133_active);
    ASSERT_EQ(0, (int)wixen_shell_integ_generation(&si));
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_prompt_creates_block(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 5);
    ASSERT(si.osc133_active);
    size_t count;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &count);
    ASSERT_EQ(1, (int)count);
    ASSERT_EQ(WIXEN_BLOCK_PROMPT_ACTIVE, blocks[0].state);
    ASSERT_EQ(5, (int)blocks[0].prompt.start);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_full_lifecycle(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* A: prompt at row 0 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    /* B: input starts at row 1 */
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    /* C: command starts executing, output at row 2 */
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 2);
    /* D: completed at row 5, exit code 0 */
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);

    const WixenCommandBlock *b = wixen_shell_integ_current_block(&si);
    ASSERT(b != NULL);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, b->state);
    ASSERT_EQ(0, (int)b->prompt.start);
    ASSERT_EQ(1, (int)b->prompt.end);
    ASSERT_EQ(1, (int)b->input.start);
    ASSERT_EQ(2, (int)b->input.end);
    ASSERT_EQ(2, (int)b->output.start);
    ASSERT_EQ(5, (int)b->output.end);
    ASSERT(b->has_exit_code);
    ASSERT_EQ(0, b->exit_code);
    ASSERT_EQ(3, (int)b->output_line_count);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_error_exit_code(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "127", 3);
    const WixenCommandBlock *b = wixen_shell_integ_current_block(&si);
    ASSERT(b->has_exit_code);
    ASSERT_EQ(127, b->exit_code);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_no_exit_code(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'D', NULL, 0);
    const WixenCommandBlock *b = wixen_shell_integ_current_block(&si);
    ASSERT_FALSE(b->has_exit_code);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_multiple_blocks(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* Block 1 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);
    /* Block 2 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 5);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 6);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 6);
    wixen_shell_integ_handle_osc133(&si, 'D', "1", 10);

    size_t count;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &count);
    ASSERT_EQ(2, (int)count);
    ASSERT_EQ(0, blocks[0].exit_code);
    ASSERT_EQ(1, blocks[1].exit_code);
    ASSERT_EQ(0, (int)blocks[0].id);
    ASSERT_EQ(1, (int)blocks[1].id);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_osc7_sets_cwd(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file://hostname/c/Users/test");
    ASSERT(si.cwd != NULL);
    ASSERT_STR_EQ("/c/Users/test", si.cwd);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_osc7_bare_path(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "/home/user");
    ASSERT_STR_EQ("/home/user", si.cwd);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_osc7_propagates_to_blocks(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file://host/tmp");
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    const WixenCommandBlock *b = wixen_shell_integ_current_block(&si);
    ASSERT(b->cwd != NULL);
    ASSERT_STR_EQ("/tmp", b->cwd);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_generation_bumps(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    uint64_t g0 = wixen_shell_integ_generation(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    uint64_t g1 = wixen_shell_integ_generation(&si);
    ASSERT(g1 > g0);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 1);
    uint64_t g2 = wixen_shell_integ_generation(&si);
    ASSERT(g2 > g1);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_prune(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    for (int i = 0; i < 10; i++) {
        wixen_shell_integ_handle_osc133(&si, 'A', NULL, (size_t)(i * 5));
        wixen_shell_integ_handle_osc133(&si, 'D', "0", (size_t)(i * 5 + 4));
    }
    size_t count;
    wixen_shell_integ_blocks(&si, &count);
    ASSERT_EQ(10, (int)count);
    wixen_shell_integ_prune(&si, 3);
    wixen_shell_integ_blocks(&si, &count);
    ASSERT_EQ(3, (int)count);
    /* Should have kept the last 3 */
    ASSERT_EQ(7, (int)si.blocks[0].id);
    ASSERT_EQ(9, (int)si.blocks[2].id);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_current_block_null_when_empty(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    ASSERT(wixen_shell_integ_current_block(&si) == NULL);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST shell_integ_d_without_c(void) {
    /* D marker without preceding C — should still complete */
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 2);
    const WixenCommandBlock *b = wixen_shell_integ_current_block(&si);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, b->state);
    wixen_shell_integ_free(&si);
    PASS();
}

SUITE(shell_integ_tests) {
    RUN_TEST(shell_integ_init_empty);
    RUN_TEST(shell_integ_prompt_creates_block);
    RUN_TEST(shell_integ_full_lifecycle);
    RUN_TEST(shell_integ_error_exit_code);
    RUN_TEST(shell_integ_no_exit_code);
    RUN_TEST(shell_integ_multiple_blocks);
    RUN_TEST(shell_integ_osc7_sets_cwd);
    RUN_TEST(shell_integ_osc7_bare_path);
    RUN_TEST(shell_integ_osc7_propagates_to_blocks);
    RUN_TEST(shell_integ_generation_bumps);
    RUN_TEST(shell_integ_prune);
    RUN_TEST(shell_integ_current_block_null_when_empty);
    RUN_TEST(shell_integ_d_without_c);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(shell_integ_tests);
    GREATEST_MAIN_END();
}
