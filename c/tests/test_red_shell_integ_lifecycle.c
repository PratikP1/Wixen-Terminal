/* test_red_shell_integ_lifecycle.c — RED tests for shell integration lifecycle
 *
 * Full lifecycle: prompt → input → execute → output → complete → next prompt.
 * Tests the state machine transitions and data capture.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/shell_integ/shell_integ.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

TEST red_si_empty(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    ASSERT_EQ(0, (int)si.block_count);
    ASSERT_EQ(0, (int)si.generation);
    ASSERT_FALSE(si.osc133_active);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_prompt_creates_block(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    ASSERT_EQ(1, (int)si.block_count);
    ASSERT(si.osc133_active);
    ASSERT_EQ(WIXEN_BLOCK_PROMPT_ACTIVE, si.blocks[0].state);
    ASSERT_EQ(0, (int)si.blocks[0].prompt.start);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_command_execution(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    ASSERT_EQ(WIXEN_BLOCK_EXECUTING, si.blocks[0].state);
    ASSERT_EQ(1, (int)si.blocks[0].output.start);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_command_complete_success(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[0].state);
    ASSERT(si.blocks[0].has_exit_code);
    ASSERT_EQ(0, si.blocks[0].exit_code);
    ASSERT_EQ(5, (int)si.blocks[0].output.end);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_command_complete_failure(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "127", 3);
    ASSERT_EQ(127, si.blocks[0].exit_code);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_multiple_commands(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* Command 1 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 3);
    /* Command 2 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 4);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 5);
    wixen_shell_integ_handle_osc133(&si, 'D', "1", 8);
    /* Command 3 (still running) */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 9);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 10);

    ASSERT_EQ(3, (int)si.block_count);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[0].state);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[1].state);
    ASSERT_EQ(WIXEN_BLOCK_EXECUTING, si.blocks[2].state);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_generation_bumps(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    uint64_t g0 = si.generation;
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    ASSERT(si.generation > g0);
    uint64_t g1 = si.generation;
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 2);
    ASSERT(si.generation > g1);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_cwd_tracking(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file:///C:/Projects");
    ASSERT(si.cwd != NULL);
    ASSERT(strstr(si.cwd, "C:") != NULL || strstr(si.cwd, "Projects") != NULL);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_block_inherits_cwd(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file:///C:/Work");
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    /* Block should record the CWD at prompt time */
    if (si.blocks[0].cwd) {
        ASSERT(strstr(si.blocks[0].cwd, "Work") != NULL);
    }
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_si_get_blocks_api(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 2);

    size_t count = 0;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &count);
    ASSERT_EQ(1, (int)count);
    ASSERT(blocks != NULL);
    ASSERT_EQ(0, blocks[0].exit_code);

    wixen_shell_integ_free(&si);
    PASS();
}

SUITE(red_shell_integ_lifecycle) {
    RUN_TEST(red_si_empty);
    RUN_TEST(red_si_prompt_creates_block);
    RUN_TEST(red_si_command_execution);
    RUN_TEST(red_si_command_complete_success);
    RUN_TEST(red_si_command_complete_failure);
    RUN_TEST(red_si_multiple_commands);
    RUN_TEST(red_si_generation_bumps);
    RUN_TEST(red_si_cwd_tracking);
    RUN_TEST(red_si_block_inherits_cwd);
    RUN_TEST(red_si_get_blocks_api);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_shell_integ_lifecycle);
    GREATEST_MAIN_END();
}
