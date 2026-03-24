/* shell_integ.c — OSC 133 shell integration */
#include "wixen/shell_integ/shell_integ.h"
#include <stdlib.h>
#include <string.h>

/* --- Helpers --- */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

static void block_free(WixenCommandBlock *b) {
    free(b->cwd);
    free(b->command_text);
    b->cwd = NULL;
    b->command_text = NULL;
}

static WixenCommandBlock *current_block_mut(WixenShellIntegration *si) {
    if (si->block_count == 0) return NULL;
    return &si->blocks[si->block_count - 1];
}

static void push_block(WixenShellIntegration *si) {
    if (si->block_count >= si->block_cap) {
        size_t new_cap = si->block_cap ? si->block_cap * 2 : 32;
        WixenCommandBlock *new_arr = realloc(si->blocks,
                                              new_cap * sizeof(WixenCommandBlock));
        if (!new_arr) return;
        si->blocks = new_arr;
        si->block_cap = new_cap;
    }
    WixenCommandBlock *b = &si->blocks[si->block_count++];
    memset(b, 0, sizeof(*b));
    b->id = si->next_id++;
    if (si->cwd) b->cwd = dup_str(si->cwd);
    si->generation++;
}

/* --- Lifecycle --- */

void wixen_shell_integ_init(WixenShellIntegration *si) {
    memset(si, 0, sizeof(*si));
}

void wixen_shell_integ_free(WixenShellIntegration *si) {
    for (size_t i = 0; i < si->block_count; i++) {
        block_free(&si->blocks[i]);
    }
    free(si->blocks);
    free(si->cwd);
    memset(si, 0, sizeof(*si));
}

/* --- OSC 133 --- */

void wixen_shell_integ_handle_osc133(WixenShellIntegration *si,
                                      char marker, const char *params,
                                      size_t cursor_row) {
    si->osc133_active = true;

    switch (marker) {
    case 'A': {
        /* Prompt start — create new block */
        push_block(si);
        WixenCommandBlock *b = current_block_mut(si);
        if (!b) break;
        b->state = WIXEN_BLOCK_PROMPT_ACTIVE;
        b->prompt.start = cursor_row;
        b->prompt.end = cursor_row + 1;
        break;
    }
    case 'B': {
        /* Input start (command line begins) */
        WixenCommandBlock *b = current_block_mut(si);
        if (!b) break;
        b->state = WIXEN_BLOCK_INPUT_ACTIVE;
        b->input.start = cursor_row;
        b->input.end = cursor_row + 1;
        /* Finalize prompt range */
        b->prompt.end = cursor_row;
        break;
    }
    case 'C': {
        /* Command execution start (output begins) */
        WixenCommandBlock *b = current_block_mut(si);
        if (!b) break;
        b->state = WIXEN_BLOCK_EXECUTING;
        b->output.start = cursor_row;
        b->output.end = cursor_row;
        /* Finalize input range */
        b->input.end = cursor_row;
        break;
    }
    case 'D': {
        /* Command completed */
        WixenCommandBlock *b = current_block_mut(si);
        if (!b) break;
        b->state = WIXEN_BLOCK_COMPLETED;
        b->output.end = cursor_row;
        if (b->output.end > b->output.start) {
            b->output_line_count = b->output.end - b->output.start;
        }
        /* Parse exit code from params (e.g., "0" or "1") */
        if (params && params[0] != '\0') {
            b->exit_code = atoi(params);
            b->has_exit_code = true;
        }
        si->generation++;
        break;
    }
    }
}

/* --- OSC 7 --- */

void wixen_shell_integ_handle_osc7(WixenShellIntegration *si, const char *uri) {
    free(si->cwd);
    si->cwd = NULL;

    if (!uri) return;

    /* URI format: file://hostname/path — extract path */
    const char *prefix = "file://";
    if (strncmp(uri, prefix, strlen(prefix)) == 0) {
        const char *after = uri + strlen(prefix);
        /* Skip hostname (find next /) */
        const char *path = strchr(after, '/');
        if (path) {
            si->cwd = dup_str(path);
        }
    } else {
        /* Bare path */
        si->cwd = dup_str(uri);
    }
}

/* --- Access --- */

const WixenCommandBlock *wixen_shell_integ_blocks(const WixenShellIntegration *si,
                                                   size_t *out_count) {
    if (out_count) *out_count = si->block_count;
    return si->blocks;
}

const WixenCommandBlock *wixen_shell_integ_current_block(const WixenShellIntegration *si) {
    if (si->block_count == 0) return NULL;
    return &si->blocks[si->block_count - 1];
}

uint64_t wixen_shell_integ_generation(const WixenShellIntegration *si) {
    return si->generation;
}

/* --- Prune --- */

void wixen_shell_integ_prune(WixenShellIntegration *si, size_t max_blocks) {
    if (si->block_count <= max_blocks) return;
    size_t to_remove = si->block_count - max_blocks;
    for (size_t i = 0; i < to_remove; i++) {
        block_free(&si->blocks[i]);
    }
    memmove(si->blocks, &si->blocks[to_remove],
            max_blocks * sizeof(WixenCommandBlock));
    si->block_count = max_blocks;
    si->generation++;
}
