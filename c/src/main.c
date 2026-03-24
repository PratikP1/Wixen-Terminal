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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-pane state */
typedef struct {
    WixenTerminal terminal;
    WixenParser parser;
    WixenPty pty;
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

    /* Initialize a11y provider */
    wixen_a11y_provider_init(window.hwnd, &ps->terminal);

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
            case WIXEN_EVT_RESIZED:
                wixen_renderer_resize(renderer, evt.resize.width, evt.resize.height);
                break;
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
                        /* No keybinding matched — encode key for PTY */
                        char encoded[32];
                        size_t n = wixen_encode_key(evt.key.vk,
                            evt.key.shift, evt.key.ctrl, evt.key.alt,
                            ps->terminal.modes.cursor_keys_application,
                            encoded, sizeof(encoded));
                        if (n > 0) {
                            wixen_pty_write(&ps->pty, encoded, n);
                        }
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
            static size_t prev_cursor_row = SIZE_MAX;
            static size_t prev_cursor_col = SIZE_MAX;
            static char prev_line[512] = {0};

            bool cursor_moved = (ps->terminal.grid.cursor.row != prev_cursor_row
                              || ps->terminal.grid.cursor.col != prev_cursor_col);

            if (cursor_moved) {
                /* Compute visible text for the current cursor line */
                size_t crow = ps->terminal.grid.cursor.row;
                char line_buf[512] = {0};
                if (crow < ps->terminal.grid.num_rows) {
                    WixenRow *row = &ps->terminal.grid.rows[crow];
                    size_t pos = 0;
                    for (size_t c = 0; c < row->count && pos < sizeof(line_buf) - 1; c++) {
                        const char *ch = row->cells[c].content;
                        if (ch[0]) {
                            size_t len = strlen(ch);
                            if (pos + len < sizeof(line_buf) - 1) {
                                memcpy(line_buf + pos, ch, len);
                                pos += len;
                            }
                        } else {
                            line_buf[pos++] = ' ';
                        }
                    }
                    line_buf[pos] = '\0';
                }

                bool row_changed = (crow != prev_cursor_row);
                bool line_content_changed = (strcmp(line_buf, prev_line) != 0);

                /* Update a11y cursor offset atomically */
                wixen_a11y_update_cursor(&ps->terminal.grid);

                /* Fire TextSelectionChanged for any cursor movement */
                wixen_a11y_raise_selection_changed(window.hwnd);

                /* If cursor moved to a different row, announce the line */
                if (row_changed) {
                    char *stripped = wixen_strip_control_chars(line_buf);
                    if (stripped && stripped[0]) {
                        wixen_a11y_raise_notification(window.hwnd, stripped, "line-read");
                    }
                    free(stripped);
                }

                /* Detect edits: same row, content changed → announce change */
                if (!row_changed && line_content_changed) {
                    /* Character-level announcement handled by throttler */
                }

                prev_cursor_row = crow;
                prev_cursor_col = ps->terminal.grid.cursor.col;
                strncpy(prev_line, line_buf, sizeof(prev_line) - 1);
            }
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
    wixen_a11y_provider_shutdown(window.hwnd);
    wixen_tray_destroy(&tray);
    wixen_watcher_stop(&cfg_watcher);
    wixen_config_free(&config);
    if (ps->pty_running) wixen_pty_close(&ps->pty);
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
