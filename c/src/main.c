/* main.c — Wixen Terminal entry point
 *
 * Wires together: Window → PTY → Parser → Terminal → Renderer
 * This is the skeleton that will be fleshed out as more modules are complete.
 */
#ifdef _WIN32

#include "wixen/ui/window.h"
#include "wixen/ui/tabs.h"
#include "wixen/ui/panes.h"
#include "wixen/pty/pty.h"
#include "wixen/vt/parser.h"
#include "wixen/core/terminal.h"
#include "wixen/render/renderer.h"
#include "wixen/render/colors.h"
#include "wixen/config/keybindings.h"
#include "wixen/config/config.h"
#include "wixen/config/watcher.h"
#include "wixen/core/keyboard.h"
#include "wixen/ui/settings_dialog.h"
#include "wixen/ui/palette_dialog.h"
#include "wixen/ui/tray.h"
#include "wixen/ui/audio.h"
#include "wixen/a11y/provider.h"
#include "wixen/a11y/events.h"
#include "wixen/ui/clipboard.h"
#include "wixen/core/mouse.h"
#include "wixen/shell_integ/shell_integ.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Navigation key tracking for boundary detection */
typedef enum {
    NAV_NONE = 0,
    NAV_LEFT, NAV_RIGHT, NAV_UP, NAV_DOWN,
} NavKey;

/* Per-pane state */
typedef struct {
    WixenTerminal terminal;
    WixenParser parser;
    WixenPty pty;
    WixenShellIntegration shell_integ;
    NavKey last_nav_key;
    bool at_history_start;
    bool at_history_end;
    uint64_t last_shell_gen;
    bool pty_running;
} WixenPaneState;

#define MAX_PANES 16

static WixenPaneState pane_states[MAX_PANES];
static size_t pane_state_count = 0;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    /* Initialize color scheme */
    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    /* Initialize keybindings */
    WixenKeybindingMap keybindings;
    wixen_keybindings_init(&keybindings);
    wixen_keybindings_load_defaults(&keybindings);

    /* Create window */
    WixenWindow window;
    if (!wixen_window_create(&window, L"Wixen Terminal", 1200, 800)) {
        MessageBoxW(NULL, L"Failed to create window", L"Wixen Terminal", MB_ICONERROR);
        return 1;
    }

    /* Create renderer */
    WixenRenderer *renderer = wixen_renderer_create(
        window.hwnd, 1200, 800, "Cascadia Mono", 14.0f, &colors);
    if (!renderer) {
        MessageBoxW(NULL, L"Failed to create renderer", L"Wixen Terminal", MB_ICONERROR);
        wixen_window_destroy(&window);
        return 1;
    }

    /* Initialize tab and pane management */
    WixenTabManager tabs;
    wixen_tabs_init(&tabs);

    WixenPaneTree pane_tree;
    WixenPaneId root_pane;
    wixen_panes_init(&pane_tree, &root_pane);

    /* Detect shell and spawn first PTY */
    wchar_t *shell = wixen_pty_detect_shell();
    uint16_t cols = (uint16_t)(1200 / 8);  /* Approximate */
    uint16_t rows = (uint16_t)(800 / 16);

    WixenPaneState *ps = &pane_states[0];
    wixen_terminal_init(&ps->terminal, cols, rows);
    wixen_shell_integ_init(&ps->shell_integ);
    wixen_parser_init(&ps->parser);
    ps->pty_running = wixen_pty_spawn(&ps->pty, cols, rows, shell, NULL, NULL, window.hwnd);
    pane_state_count = 1;
    free(shell);

    wixen_tabs_add(&tabs, "Shell", root_pane);

    /* Create tray icon */
    WixenTrayIcon tray;
    wixen_tray_create(&tray, window.hwnd, L"Wixen Terminal");

    /* Load config and start hot-reload watcher */
    WixenConfig config;
    wixen_config_init_defaults(&config);
    char cfg_path[MAX_PATH] = {0};
    wixen_config_default_path(cfg_path, sizeof(cfg_path));
    wixen_config_load(&config, cfg_path);

    WixenConfigWatcher cfg_watcher = {0};
    wixen_watcher_start(&cfg_watcher, cfg_path, window.hwnd);

    /* Initialize a11y provider and announce focus */
    wixen_a11y_provider_init(window.hwnd, &ps->terminal);
    wixen_a11y_raise_selection_changed(window.hwnd);
    wixen_a11y_raise_notification(window.hwnd, "Wixen Terminal ready", "terminal-ready");

    /* Output throttler — batches PTY output for screen reader */
    WixenEventThrottler pane_throttler;
    wixen_throttler_init(&pane_throttler, config.accessibility.output_debounce_ms);

    /* Main event loop */
    bool running = true;
    MSG msg;

    while (running) {
        /* Process Win32 messages */
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }

            /* Handle PTY output */
            if (msg.message == WM_PTY_OUTPUT) {
                WixenPtyOutputEvent *evt = (WixenPtyOutputEvent *)msg.lParam;
                if (evt && evt->data) {
                    /* Feed through parser into terminal */
                    WixenAction actions[512];
                    size_t count = wixen_parser_process(
                        &ps->parser, evt->data, evt->len, actions, 512);
                    for (size_t i = 0; i < count; i++) {
                        wixen_terminal_dispatch(&ps->terminal, &actions[i]);
                        /* Free OSC/APC data */
                        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH)
                            free(actions[i].osc.data);
                        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH)
                            free(actions[i].apc.data);
                    }
                    /* Feed output to throttler (NOT directly to NVDA) */
                    if (count > 0 && evt->len > 0) {
                        char *raw = (char *)malloc(evt->len + 1);
                        if (raw) {
                            memcpy(raw, evt->data, evt->len);
                            raw[evt->len] = '\0';
                            wixen_throttler_on_output(&pane_throttler, raw, 0);
                            free(raw);
                        }
                    }
                    free(evt->data);
                }
                free(evt);
                continue;
            }

            if (msg.message == WM_PTY_EXITED) {
                ps->pty_running = false;
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        /* Process window events */
        WixenWindowEvent evt;
        while (wixen_window_poll_event(&window, &evt)) {
            switch (evt.type) {
            case WIXEN_EVT_CLOSE_REQUESTED:
                running = false;
                break;
            case WIXEN_EVT_RESIZED: {
                wixen_renderer_resize(renderer, evt.resize.width, evt.resize.height);
                /* Recalculate terminal dimensions from new window size */
                WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                if (fm.cell_width > 0 && fm.cell_height > 0) {
                    uint16_t new_cols = (uint16_t)(evt.resize.width / fm.cell_width);
                    uint16_t new_rows = (uint16_t)(evt.resize.height / fm.cell_height);
                    if (new_cols > 0 && new_rows > 0) {
                        wixen_terminal_resize(&ps->terminal, new_cols, new_rows);
                        if (ps->pty_running)
                            wixen_pty_resize(&ps->pty, new_cols, new_rows);
                    }
                }
                break;
            }
            case WIXEN_EVT_KEY_INPUT:
                if (evt.key.down) {
                    /* Build chord string from VK + modifiers */
                    char chord[64];
                    size_t cpos = 0;
                    if (evt.key.ctrl) { memcpy(chord + cpos, "ctrl+", 5); cpos += 5; }
                    if (evt.key.shift) { memcpy(chord + cpos, "shift+", 6); cpos += 6; }
                    if (evt.key.alt) { memcpy(chord + cpos, "alt+", 4); cpos += 4; }
                    /* Map VK to key name */
                    if (evt.key.vk >= 'A' && evt.key.vk <= 'Z') {
                        chord[cpos++] = (char)('a' + evt.key.vk - 'A');
                    } else if (evt.key.vk >= '0' && evt.key.vk <= '9') {
                        chord[cpos++] = (char)evt.key.vk;
                    } else if (evt.key.vk >= 0x70 && evt.key.vk <= 0x7B) {
                        cpos += (size_t)snprintf(chord + cpos, sizeof(chord) - cpos, "f%d", evt.key.vk - 0x70 + 1);
                    } else if (evt.key.vk == 0x09) { memcpy(chord + cpos, "tab", 3); cpos += 3; }
                    else if (evt.key.vk == 0xBC) { memcpy(chord + cpos, "comma", 5); cpos += 5; }
                    chord[cpos] = '\0';

                    /* Check keybinding */
                    const char *action = wixen_keybindings_lookup(&keybindings, chord);
                    if (action) {
                        if (strcmp(action, "settings") == 0) {
                            wixen_settings_dialog_show(window.hwnd);
                        } else if (strcmp(action, "command_palette") == 0) {
                            WixenPaletteResult result;
                            if (wixen_palette_dialog_show(window.hwnd, &result)) {
                                /* Handle palette action */
                                free(result.action);
                                free(result.args);
                            }
                        } else if (strcmp(action, "copy") == 0) {
                            if (ps->terminal.selection.active) {
                                /* Extract selected text from grid */
                                WixenGridPoint start, end;
                                wixen_selection_ordered(&ps->terminal.selection, &start, &end);
                                char sel_buf[4096] = {0};
                                size_t pos = 0;
                                for (size_t r = start.row; r <= end.row && r < ps->terminal.grid.num_rows; r++) {
                                    size_t c0 = (r == start.row) ? start.col : 0;
                                    size_t c1 = (r == end.row) ? end.col : ps->terminal.grid.cols;
                                    WixenRow *row = &ps->terminal.grid.rows[r];
                                    for (size_t c = c0; c < c1 && c < row->count; c++) {
                                        const char *ch = row->cells[c].content;
                                        size_t clen = ch ? strlen(ch) : 0;
                                        if (clen > 0 && pos + clen < sizeof(sel_buf) - 2)
                                            { memcpy(sel_buf + pos, ch, clen); pos += clen; }
                                        else if (pos < sizeof(sel_buf) - 2)
                                            sel_buf[pos++] = ' ';
                                    }
                                    if (r < end.row && pos < sizeof(sel_buf) - 2)
                                        sel_buf[pos++] = '\n';
                                }
                                if (pos > 0) wixen_clipboard_set_text(window.hwnd, sel_buf);
                            }
                        } else if (strcmp(action, "paste") == 0) {
                            char *text = wixen_clipboard_get_text(window.hwnd);
                            if (text && ps->pty_running) {
                                if (ps->terminal.modes.bracketed_paste)
                                    wixen_pty_write(&ps->pty, "\x1b[200~", 6);
                                wixen_pty_write(&ps->pty, text, strlen(text));
                                if (ps->terminal.modes.bracketed_paste)
                                    wixen_pty_write(&ps->pty, "\x1b[201~", 6);
                            }
                            free(text);
                        } else if (strcmp(action, "toggle_fullscreen") == 0) {
                            wixen_window_set_fullscreen(&window, !window.fullscreen);
                        } else if (strcmp(action, "close_pane") == 0) {
                            running = false;
                        }
                    } else if (ps->pty_running) {
                        /* Track nav keys for boundary detection */
                        switch (evt.key.vk) {
                        case 0x25: /* VK_LEFT */
                            ps->last_nav_key = NAV_LEFT;
                            break;
                        case 0x27: /* VK_RIGHT */
                            ps->last_nav_key = NAV_RIGHT;
                            break;
                        case 0x26: /* VK_UP */
                            if (ps->at_history_start) {
                                /* Already at boundary — play sound, suppress */
                                { WixenAudioConfig ac; wixen_audio_config_init(&ac); wixen_audio_play(&ac, WIXEN_AUDIO_HISTORY_BOUNDARY); }
                                goto skip_pty_write;
                            }
                            ps->last_nav_key = NAV_UP;
                            ps->at_history_end = false;
                            break;
                        case 0x28: /* VK_DOWN */
                            if (ps->at_history_end) {
                                { WixenAudioConfig ac; wixen_audio_config_init(&ac); wixen_audio_play(&ac, WIXEN_AUDIO_HISTORY_BOUNDARY); }
                                goto skip_pty_write;
                            }
                            ps->last_nav_key = NAV_DOWN;
                            ps->at_history_start = false;
                            break;
                        default:
                            ps->last_nav_key = NAV_NONE;
                            ps->at_history_start = false;
                            ps->at_history_end = false;
                            break;
                        }

                        /* Encode key for PTY */
                        char encoded[32];
                        size_t n = wixen_encode_key(evt.key.vk,
                            evt.key.shift, evt.key.ctrl, evt.key.alt,
                            ps->terminal.modes.cursor_keys_application,
                            encoded, sizeof(encoded));
                        if (n > 0) {
                            wixen_pty_write(&ps->pty, encoded, n);
                        }
                        skip_pty_write:;
                    }
                }
                break;
            case WIXEN_EVT_CHAR_INPUT:
                if (ps->pty_running) {
                    /* Convert UTF-16 codepoint to UTF-8 and send to PTY */
                    char utf8[4] = {0};
                    size_t utf8_len = 0;
                    uint32_t cp = evt.char_input;
                    if (cp < 0x80) {
                        utf8[0] = (char)cp;
                        utf8_len = 1;
                    } else if (cp < 0x800) {
                        utf8[0] = (char)(0xC0 | (cp >> 6));
                        utf8[1] = (char)(0x80 | (cp & 0x3F));
                        utf8_len = 2;
                    } else if (cp < 0x10000) {
                        utf8[0] = (char)(0xE0 | (cp >> 12));
                        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        utf8[2] = (char)(0x80 | (cp & 0x3F));
                        utf8_len = 3;
                    } else {
                        utf8[0] = (char)(0xF0 | (cp >> 18));
                        utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                        utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        utf8[3] = (char)(0x80 | (cp & 0x3F));
                        utf8_len = 4;
                    }
                    if (utf8_len > 0) {
                        wixen_pty_write(&ps->pty, utf8, utf8_len);
                    }
                }
                break;
            case WIXEN_EVT_MOUSE_DOWN:
                if (evt.mouse.button == WIXEN_MB_RIGHT) {
                    wixen_window_show_context_menu(&window, evt.mouse.x, evt.mouse.y);
                }
                break;
            case WIXEN_EVT_CONTEXT_MENU:
                switch (evt.context_action) {
                case WIXEN_CTX_SETTINGS:
                    wixen_settings_dialog_show(window.hwnd);
                    break;
                case WIXEN_CTX_CLOSE_TAB:
                    running = false;
                    break;
                default:
                    break;
                }
                break;
            case WIXEN_EVT_FOCUS_GAINED:
                wixen_a11y_raise_selection_changed(window.hwnd);
                break;
            default:
                break;
            }
        }

        /* Render frame */
        WixenPaneRenderInfo pane_info = {
            .grid = &ps->terminal.grid,
            .x = 0, .y = 0,
            .width = (float)wixen_renderer_width(renderer),
            .height = (float)wixen_renderer_height(renderer),
            .has_focus = true,
            .scroll_offset = 0,
        };
        wixen_renderer_render_frame(renderer, &pane_info, 1);

        /* --- Accessibility state updates --- */
        {
            /* Sync grid visible text to a11y state for ITextProvider */
            if (ps->terminal.dirty) {
                char a11y_text[8192];
                size_t tlen = wixen_grid_visible_text(&ps->terminal.grid, a11y_text, sizeof(a11y_text));
                wixen_a11y_state_update_text_global(a11y_text, tlen);
                ps->terminal.dirty = false;
            }

            /* #1/#2: Drain throttled output — only announce multi-line (real output).
             * Single-line without newline is keyboard echo — let NVDA handle it. */
            bool has_real_output = false;
            char *pending = wixen_throttler_take_pending(&pane_throttler);
            if (pending) {
                char *stripped = wixen_strip_vt_escapes(pending);
                if (stripped) {
                    char *clean = wixen_strip_control_chars(stripped);
                    if (clean && clean[0] && strchr(clean, '\n')) {
                        /* #12: Verbosity filter — respect config */
                        bool should_announce = true;
                        if (config.accessibility.verbosity &&
                            strcmp(config.accessibility.verbosity, "none") == 0) {
                            should_announce = false;
                        }
                        if (should_announce) {
                            wixen_a11y_raise_notification(window.hwnd, clean, "terminal-output");
                        }
                        has_real_output = true;
                    }
                    free(clean);
                    free(stripped);
                }
                free(pending);
            }

            /* Track cursor and line state */
            static size_t prev_cursor_row = SIZE_MAX;
            static size_t prev_cursor_col = SIZE_MAX;
            static char prev_line[512] = {0};

            size_t cur_row = ps->terminal.grid.cursor.row;
            size_t cur_col = ps->terminal.grid.cursor.col;
            bool cursor_moved = (cur_row != prev_cursor_row || cur_col != prev_cursor_col);

            /* Compute current line text */
            char line_buf[512] = {0};
            if (cur_row < ps->terminal.grid.num_rows) {
                wixen_row_text(&ps->terminal.grid.rows[cur_row], line_buf, sizeof(line_buf));
            }

            /* #8: Compute UTF-16 offset atomically for lock-free GetSelection */
            {
                int32_t utf16_off = 0;
                for (size_t r = 0; r < cur_row && r < ps->terminal.grid.num_rows; r++) {
                    char rbuf[512];
                    size_t rlen = wixen_row_text(&ps->terminal.grid.rows[r], rbuf, sizeof(rbuf));
                    utf16_off += (int32_t)rlen + 1; /* +1 for newline */
                }
                utf16_off += (int32_t)cur_col;
                wixen_a11y_set_cursor_offset(utf16_off);
            }
            wixen_a11y_update_cursor(&ps->terminal.grid);

            /* #9: Pump messages so COM calls dispatch against consistent state */
            wixen_a11y_pump_messages(window.hwnd);

            /* Fire TextSelectionChanged on cursor movement */
            if (cursor_moved) {
                wixen_a11y_raise_selection_changed(window.hwnd);
            }

            /* #5/#7: Line change detection with prompt stripping.
             * Detects history recall (up/down changes command).
             * Only fires for replacement — not append or deletion. */
            bool line_changed = (strcmp(line_buf, prev_line) != 0);
            if (line_changed && !has_real_output && prev_line[0]) {
                size_t cur_len = strlen(line_buf);
                size_t prv_len = strlen(prev_line);
                while (cur_len > 0 && line_buf[cur_len - 1] == ' ') cur_len--;
                while (prv_len > 0 && prev_line[prv_len - 1] == ' ') prv_len--;

                bool is_append = (cur_len > prv_len && memcmp(line_buf, prev_line, prv_len) == 0);
                bool is_delete = (cur_len < prv_len && memcmp(prev_line, line_buf, cur_len) == 0);

                if (!is_append && !is_delete) {
                    /* Line replaced (history recall) — strip prompt */
                    const char *text = line_buf;
                    const char *gt = strchr(text, '>');
                    if (gt && gt > text && (text[1] == ':' || (text[0] == 'P' && text[1] == 'S'))) {
                        text = gt + 1;
                        while (*text == ' ') text++;
                    }
                    const char *dollar = strrchr(text, '$');
                    if (!dollar) dollar = strrchr(text, '#');
                    if (dollar && dollar[1] == ' ') text = dollar + 2;

                    if (text[0]) {
                        wixen_a11y_raise_notification(window.hwnd, text, "cursor-line");
                    } else {
                        wixen_a11y_raise_notification(window.hwnd, "line cleared", "cursor-line");
                    }
                }
            }

            /* #6: Command completion notification */
            {
                uint64_t gen = wixen_shell_integ_generation(&ps->shell_integ);
                if (gen != ps->last_shell_gen) {
                    ps->last_shell_gen = gen;
                    /* #11: Tree rebuild would go here */
                    const WixenCommandBlock *blk = wixen_shell_integ_current_block(&ps->shell_integ);
                    if (blk && blk->state == WIXEN_BLOCK_COMPLETED && blk->has_exit_code) {
                        char cmd_msg[256];
                        snprintf(cmd_msg, sizeof(cmd_msg), "Command finished, exit code %d", blk->exit_code);
                        wixen_a11y_raise_notification(window.hwnd, cmd_msg, "command-complete");
                    }
                }
            }

            /* #10: Password prompt detection — if terminal stopped echoing */
            {
                static size_t echo_check_col = SIZE_MAX;
                if (cur_col == echo_check_col && !cursor_moved && !has_real_output) {
                    /* Cursor didn't move after keypress — might be password prompt */
                    /* Only announce once per position */
                    static size_t last_password_col = SIZE_MAX;
                    if (cur_col != last_password_col) {
                        wixen_a11y_raise_notification(window.hwnd, "Text not echoed", "password-prompt");
                        last_password_col = cur_col;
                    }
                }
                echo_check_col = cur_col;
            }

            /* #3/#4: Boundary detection — consume nav key, check if nothing changed */
            if (ps->last_nav_key != NAV_NONE) {
                NavKey nav = ps->last_nav_key;
                ps->last_nav_key = NAV_NONE;
                WixenAudioConfig ac;
                wixen_audio_config_init(&ac);

                switch (nav) {
                case NAV_LEFT:
                    if (cur_col == prev_cursor_col && !cursor_moved) {
                        wixen_audio_play(&ac, WIXEN_AUDIO_EDIT_BOUNDARY);
                    }
                    break;
                case NAV_RIGHT:
                    if (cur_col == prev_cursor_col && !cursor_moved) {
                        wixen_audio_play(&ac, WIXEN_AUDIO_EDIT_BOUNDARY);
                    }
                    break;
                case NAV_UP:
                    if (strcmp(line_buf, prev_line) == 0) {
                        ps->at_history_start = true;
                        wixen_audio_play(&ac, WIXEN_AUDIO_HISTORY_BOUNDARY); /* History start */
                    }
                    break;
                case NAV_DOWN:
                    if (line_buf[0] == '\0' || (line_buf[0] == ' ' && strlen(line_buf) < 3)) {
                        ps->at_history_end = true;
                        wixen_audio_play(&ac, WIXEN_AUDIO_HISTORY_BOUNDARY); /* History end */
                    }
                    break;
                default:
                    break;
                }
            }

            prev_cursor_row = cur_row;
            prev_cursor_col = cur_col;
            strncpy(prev_line, line_buf, sizeof(prev_line) - 1);
        }

        /* Handle terminal state changes */
        if (ps->terminal.bell_pending) {
            WixenAudioConfig audio_cfg;
            wixen_audio_config_init(&audio_cfg);
            wixen_audio_play(&audio_cfg, WIXEN_AUDIO_BELL);
            ps->terminal.bell_pending = false;
        }
        if (ps->terminal.title_dirty && ps->terminal.title) {
            wchar_t wtitle[256];
            MultiByteToWideChar(CP_UTF8, 0, ps->terminal.title, -1, wtitle, 256);
            wixen_window_set_title(&window, wtitle);
            ps->terminal.title_dirty = false;
        }

        /* Send pending DSR responses back to PTY */
        {
            const char *resp;
            while ((resp = wixen_terminal_pop_response(&ps->terminal)) != NULL) {
                if (ps->pty_running) {
                    wixen_pty_write(&ps->pty, resp, strlen(resp));
                }
                free((void *)resp);
            }
        }

        /* Check config hot-reload (watcher sends WM_APP+100 when file changes) */
        /* For now, reload handled via WM message from watcher thread */

        /* Brief sleep to avoid spinning CPU */
        Sleep(1);
    }

    /* Cleanup */
    wixen_throttler_free(&pane_throttler);
    wixen_a11y_provider_shutdown(window.hwnd);
    wixen_tray_destroy(&tray);
    wixen_watcher_stop(&cfg_watcher);
    wixen_config_free(&config);
    if (ps->pty_running) wixen_pty_close(&ps->pty);
    wixen_shell_integ_free(&ps->shell_integ);
    wixen_parser_free(&ps->parser);
    wixen_terminal_free(&ps->terminal);
    wixen_panes_free(&pane_tree);
    wixen_tabs_free(&tabs);
    wixen_keybindings_free(&keybindings);
    wixen_renderer_destroy(renderer);
    wixen_window_destroy(&window);

    return 0;
}

#else
/* Non-Windows stub */
#include <stdio.h>
int main(void) {
    printf("Wixen Terminal requires Windows.\n");
    return 1;
}
#endif
