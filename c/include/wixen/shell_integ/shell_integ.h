/* shell_integ.h — Shell integration (OSC 133 command blocks) */
#ifndef WIXEN_SHELL_INTEG_H
#define WIXEN_SHELL_INTEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Row range (start inclusive, end exclusive) */
typedef struct {
    size_t start;
    size_t end;
} WixenRowRange;

/* Command block lifecycle */
typedef enum {
    WIXEN_BLOCK_PROMPT_ACTIVE = 0,
    WIXEN_BLOCK_INPUT_ACTIVE,
    WIXEN_BLOCK_EXECUTING,
    WIXEN_BLOCK_COMPLETED,
} WixenBlockState;

/* A command block (prompt → input → output → exit code) */
typedef struct {
    uint64_t id;
    WixenBlockState state;
    WixenRowRange prompt;
    WixenRowRange input;
    WixenRowRange output;
    int exit_code;
    bool has_exit_code;
    char *cwd;               /* From OSC 7, heap-allocated */
    char *command_text;       /* Extracted input text, sanitized */
    size_t output_line_count;
} WixenCommandBlock;

/* Shell integration tracker */
typedef struct {
    WixenCommandBlock *blocks;
    size_t block_count;
    size_t block_cap;
    uint64_t next_id;
    char *cwd;               /* Current working directory (OSC 7) */
    bool osc133_active;      /* Has received at least one OSC 133 marker */
    uint64_t generation;     /* Bumped on structural change */
} WixenShellIntegration;

/* Lifecycle */
void wixen_shell_integ_init(WixenShellIntegration *si);
void wixen_shell_integ_free(WixenShellIntegration *si);

/* Handle OSC 133 markers (A/B/C/D) */
void wixen_shell_integ_handle_osc133(WixenShellIntegration *si,
                                      char marker, const char *params,
                                      size_t cursor_row);

/* Handle OSC 7 (working directory) */
void wixen_shell_integ_handle_osc7(WixenShellIntegration *si, const char *uri);

/* Access */
const WixenCommandBlock *wixen_shell_integ_blocks(const WixenShellIntegration *si,
                                                   size_t *out_count);
const WixenCommandBlock *wixen_shell_integ_current_block(const WixenShellIntegration *si);
uint64_t wixen_shell_integ_generation(const WixenShellIntegration *si);

/* Prune old blocks */
void wixen_shell_integ_prune(WixenShellIntegration *si, size_t max_blocks);

#endif /* WIXEN_SHELL_INTEG_H */
