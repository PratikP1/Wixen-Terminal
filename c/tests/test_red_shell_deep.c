/* test_red_shell_deep.c — RED tests for shell integration + OSC 7 edge cases */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/shell_integ/shell_integ.h"
#include "wixen/shell_integ/heuristic.h"

/* OSC 7 with spaces in path */
TEST red_osc7_spaces_in_path(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file:///C:/Users/John%20Doe/Documents");
    ASSERT(si.cwd != NULL);
    ASSERT(strlen(si.cwd) > 0);
    wixen_shell_integ_free(&si);
    PASS();
}

/* OSC 7 with hostname */
TEST red_osc7_with_hostname(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "file://DESKTOP-ABC/C:/Users/test");
    ASSERT(si.cwd != NULL);
    wixen_shell_integ_free(&si);
    PASS();
}

/* OSC 7 empty URI */
TEST red_osc7_empty(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, "");
    /* Should not crash */
    wixen_shell_integ_free(&si);
    PASS();
}

/* OSC 7 NULL URI */
TEST red_osc7_null(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc7(&si, NULL);
    wixen_shell_integ_free(&si);
    PASS();
}

/* Full command lifecycle: A → B → C → D, then new A */
TEST red_full_command_lifecycle(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* Command 1 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 2);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);
    ASSERT_EQ(1, (int)si.block_count);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[0].state);
    ASSERT_EQ(0, si.blocks[0].exit_code);
    /* Command 2 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 6);
    ASSERT_EQ(2, (int)si.block_count);
    ASSERT_EQ(WIXEN_BLOCK_PROMPT_ACTIVE, si.blocks[1].state);
    wixen_shell_integ_handle_osc133(&si, 'B', NULL, 7);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 8);
    wixen_shell_integ_handle_osc133(&si, 'D', "1", 10);
    ASSERT_EQ(WIXEN_BLOCK_COMPLETED, si.blocks[1].state);
    ASSERT_EQ(1, si.blocks[1].exit_code);
    wixen_shell_integ_free(&si);
    PASS();
}

/* Current block should be the last incomplete one */
TEST red_current_block(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    const WixenCommandBlock *cur = wixen_shell_integ_current_block(&si);
    ASSERT(cur != NULL);
    ASSERT_EQ(WIXEN_BLOCK_PROMPT_ACTIVE, cur->state);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 3);
    /* After completion, current block may be NULL or the completed one */
    wixen_shell_integ_free(&si);
    PASS();
}

/* Prune keeps only N most recent blocks */
TEST red_prune_keeps_recent(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    for (int i = 0; i < 20; i++) {
        wixen_shell_integ_handle_osc133(&si, 'A', NULL, (size_t)(i * 4));
        wixen_shell_integ_handle_osc133(&si, 'D', "0", (size_t)(i * 4 + 3));
    }
    ASSERT_EQ(20, (int)si.block_count);
    wixen_shell_integ_prune(&si, 5);
    ASSERT(si.block_count <= 5);
    /* Last block's exit code should still be 0 */
    if (si.block_count > 0) {
        ASSERT_EQ(0, si.blocks[si.block_count - 1].exit_code);
    }
    wixen_shell_integ_free(&si);
    PASS();
}

/* Heuristic prompt detection — cmd.exe style */
TEST red_heuristic_cmd(void) {
    ASSERT(wixen_is_prompt_line("C:\\Users\\test>"));
    ASSERT(wixen_is_prompt_line("C:\\>"));
    ASSERT(wixen_is_prompt_line("D:\\Projects\\Wixen>"));
    /* With command typed: prompt char is not at end — not detected by line heuristic */
    /* That's correct — the prompt line is before the command is typed */
    PASS();
}

/* Heuristic — PowerShell style */
TEST red_heuristic_powershell(void) {
    ASSERT(wixen_is_prompt_line("PS C:\\Users\\test> "));
    ASSERT(wixen_is_prompt_line("PS> "));
    PASS();
}

/* Heuristic — Unix style */
TEST red_heuristic_unix(void) {
    ASSERT(wixen_is_prompt_line("user@host:~/projects$ "));
    ASSERT(wixen_is_prompt_line("$ "));
    ASSERT(wixen_is_prompt_line("# "));
    ASSERT(wixen_is_prompt_line("% "));
    PASS();
}

/* Not a prompt */
TEST red_heuristic_not_prompt(void) {
    ASSERT_FALSE(wixen_is_prompt_line("Hello World"));
    ASSERT_FALSE(wixen_is_prompt_line("  file1.txt"));
    ASSERT_FALSE(wixen_is_prompt_line("error: something failed"));
    ASSERT_FALSE(wixen_is_prompt_line(""));
    PASS();
}

/* Generation counter */
TEST red_generation_increments(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    uint64_t g0 = wixen_shell_integ_generation(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    uint64_t g1 = wixen_shell_integ_generation(&si);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 3);
    uint64_t g2 = wixen_shell_integ_generation(&si);
    ASSERT(g1 > g0);
    ASSERT(g2 > g1);
    wixen_shell_integ_free(&si);
    PASS();
}

SUITE(red_shell_deep) {
    RUN_TEST(red_osc7_spaces_in_path);
    RUN_TEST(red_osc7_with_hostname);
    RUN_TEST(red_osc7_empty);
    RUN_TEST(red_osc7_null);
    RUN_TEST(red_full_command_lifecycle);
    RUN_TEST(red_current_block);
    RUN_TEST(red_prune_keeps_recent);
    RUN_TEST(red_heuristic_cmd);
    RUN_TEST(red_heuristic_powershell);
    RUN_TEST(red_heuristic_unix);
    RUN_TEST(red_heuristic_not_prompt);
    RUN_TEST(red_generation_increments);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_shell_deep);
    GREATEST_MAIN_END();
}
