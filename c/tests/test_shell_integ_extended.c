/* test_shell_integ_extended.c — Shell integration tests */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/shell_integ/shell_integ.h"

TEST integ_init_empty(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    ASSERT_EQ(0, (int)si.block_count);
    ASSERT_FALSE(si.osc133_active);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_prompt_start(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    ASSERT(si.osc133_active);
    ASSERT_EQ(1, (int)si.block_count);
    ASSERT_EQ(WIXEN_BLOCK_PROMPT_ACTIVE, si.blocks[0].state);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_command_input(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    ASSERT_EQ(WIXEN_BLOCK_INPUT_ACTIVE, si.blocks[0].state);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_command_executing(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 2);
    ASSERT_EQ(WIXEN_BLOCK_EXECUTING, si.blocks[0].state);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_command_done(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 2);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[0].state);
    ASSERT(si.blocks[0].has_exit_code);
    ASSERT_EQ(0, si.blocks[0].exit_code);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_exit_code_nonzero(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 2);
    wixen_shell_integ_handle_osc133(&si, 'D', "127", 3);
    ASSERT_EQ(127, si.blocks[0].exit_code);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_cwd_osc7(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file:///C:/Users/test");
    ASSERT(si.cwd != NULL);
    ASSERT(strlen(si.cwd) > 0);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_multiple_blocks(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* Block 1 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 2);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);
    /* Block 2 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 6);
    ASSERT_EQ(2, (int)si.block_count);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[0].state);
    ASSERT_EQ(WIXEN_BLOCK_PROMPT_ACTIVE, si.blocks[1].state);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_generation_bumps(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    uint64_t g0 = wixen_shell_integ_generation(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    uint64_t g1 = wixen_shell_integ_generation(&si);
    ASSERT(g1 > g0);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST integ_prune(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    for (int i = 0; i < 10; i++) {
        wixen_shell_integ_handle_osc133(&si, 'A', NULL, (size_t)(i * 3));
        wixen_shell_integ_handle_osc133(&si, 'D', "0", (size_t)(i * 3 + 2));
    }
    ASSERT_EQ(10, (int)si.block_count);
    wixen_shell_integ_prune(&si, 5);
    ASSERT(si.block_count <= 5);
    wixen_shell_integ_free(&si);
    PASS();
}

SUITE(shell_integ_extended) {
    RUN_TEST(integ_init_empty);
    RUN_TEST(integ_prompt_start);
    RUN_TEST(integ_command_input);
    RUN_TEST(integ_command_executing);
    RUN_TEST(integ_command_done);
    RUN_TEST(integ_exit_code_nonzero);
    RUN_TEST(integ_cwd_osc7);
    RUN_TEST(integ_multiple_blocks);
    RUN_TEST(integ_generation_bumps);
    RUN_TEST(integ_prune);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(shell_integ_extended);
    GREATEST_MAIN_END();
}
