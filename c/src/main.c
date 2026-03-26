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
#include "wixen/core/path_util.h"
#include "wixen/search/search.h"
#include "wixen/a11y/frame_update.h"
#include "wixen/a11y/state.h"
#include "wixen/a11y/tree.h"
#include "wixen/a11y/text_boundaries.h"
#include "wixen/ui/clipboard.h"
#include "wixen/core/mouse.h"
#include "wixen/shell_integ/shell_integ.h"
#include "wixen/config/session.h"
#include "wixen/core/error_detect.h"
#include "wixen/core/url.h"
#include "wixen/core/shutdown.h"
#include "wixen/core/bg_task.h"
#include "wixen/core/startup_timer.h"
#include "wixen/ui/explorer_menu.h"
#include "wixen/ui/jumplist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <objbase.h>

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
static WixenShutdownState g_shutdown;

/* Watchdog: if cleanup takes >3s, force-kill. Prevents zombie processes. */
/* Pump all pending Win32 messages. Must be called periodically during
 * init so UseComThreading COM calls (from NVDA) can be dispatched.
 * Without this, NVDA's queries queue up and its 500ms watchdog fires. */
static void pump_messages(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* Background renderer init thread — runs D3D11 device + shaders + atlas
 * without blocking the UI thread's message pump. */
typedef struct BgRendererInitArgs {
    const char *font; float size; uint32_t dpi;
    volatile WixenRendererBgResult **out; WixenBgTask *task;
} BgRendererInitArgs;

static DWORD WINAPI bg_renderer_init_thread(LPVOID param) {
    BgRendererInitArgs *a = (BgRendererInitArgs *)param;
    WixenRendererBgResult *r = wixen_renderer_init_background(a->font, a->size, a->dpi);
    *a->out = r;
    wixen_bg_task_mark_done(a->task);
    return 0;
}

static DWORD WINAPI exit_watchdog_thread(LPVOID param) {
    (void)param;
    /* Poll every 100ms. Force-kill only if shutdown is stuck (not complete
     * after 3 seconds). If cleanup finishes normally, the watchdog exits
     * without killing — allowing proper DLL cleanup and profiler/sanitizer
     * teardown. */
    for (;;) {
        Sleep(100);
        if (wixen_shutdown_is_complete(&g_shutdown))
            return 0; /* Clean exit — no force-kill needed */
        if (wixen_shutdown_should_force_kill(&g_shutdown, 3000)) {
            TerminateProcess(GetCurrentProcess(), 0);
            __assume(0);
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    wixen_shutdown_init(&g_shutdown);

    WixenStartupTimer startup_timer;
    wixen_timer_init(&startup_timer);

    /* COM apartment for UIA UseComThreading — without this, NVDA's
     * marshaled UIA calls hang and focus never completes. */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* Initialize color scheme */
    WixenColorScheme colors;
    wixen_colors_init_default(&colors);

    /* Load config FIRST — needed for profile selection and window size */
    WixenConfig config;
    wixen_config_init_defaults(&config);
    char cfg_path[MAX_PATH] = {0};
    wixen_config_default_path(cfg_path, sizeof(cfg_path));
    wixen_config_load(&config, cfg_path);

    /* Initialize keybindings */
    WixenKeybindingMap keybindings;
    wixen_keybindings_init(&keybindings);
    wixen_keybindings_load_defaults(&keybindings);

    /* --- Shell integration: Explorer context menu + Jumplist --- */
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    /* Register Explorer context menu if not already registered */
    if (!wixen_explorer_menu_is_registered()) {
        wixen_explorer_menu_register(exe_path, L"Open Wixen Terminal Here");
    }

    /* Update taskbar jumplist with profile names */
    {
        const wchar_t **jl_names = NULL;
        size_t jl_count = config.profile_count;
        if (jl_count > 0) {
            jl_names = (const wchar_t **)malloc(jl_count * sizeof(wchar_t *));
            if (jl_names) {
                for (size_t i = 0; i < jl_count; i++) {
                    /* Convert UTF-8 profile name to wide string */
                    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                        config.profiles[i].name, -1, NULL, 0);
                    wchar_t *wname = (wchar_t *)malloc(wlen * sizeof(wchar_t));
                    if (wname) {
                        MultiByteToWideChar(CP_UTF8, 0,
                            config.profiles[i].name, -1, wname, wlen);
                    }
                    jl_names[i] = wname;
                }
                wixen_jumplist_update(exe_path, jl_names, jl_count);
                for (size_t i = 0; i < jl_count; i++) free((void *)jl_names[i]);
                free(jl_names);
            }
        }
    }

    /* Create window */
    WixenWindow window;
    if (!wixen_window_create(&window, L"Wixen Terminal", 1200, 800)) {
        MessageBoxW(NULL, L"Failed to create window", L"Wixen Terminal", MB_ICONERROR);
        return 1;
    }

    wixen_timer_mark(&startup_timer, "window");

    /* Show window immediately with a solid background — gives instant visual
     * feedback while the heavy renderer/PTY init runs. Fixes slow perceived
     * startup time. */
    {
        HDC hdc = GetDC(window.hwnd);
        if (hdc) {
            RECT rc;
            GetClientRect(window.hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30)); /* Dark background */
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
            ReleaseDC(window.hwnd, hdc);
        }
    }
    pump_messages(); /* Let NVDA see the window + dispatch UIA queries */

    /* BACKGROUND INIT: Heavy renderer work (D3D11 device + shaders + atlas)
     * runs on a background thread. UI thread stays responsive for NVDA. */
    WixenRenderer *renderer = NULL;
    volatile WixenRendererBgResult *bg_result = NULL;
    WixenBgTask bg_task;
    wixen_bg_task_init(&bg_task);

    static BgRendererInitArgs s_bg_args;
    s_bg_args.font = config.font.family ? config.font.family : "Cascadia Mono";
    s_bg_args.size = config.font.size > 0 ? config.font.size : 14.0f;
    s_bg_args.dpi = 96;
    s_bg_args.out = &bg_result;
    s_bg_args.task = &bg_task;
    wixen_bg_task_start(&bg_task, bg_renderer_init_thread, &s_bg_args);
    wixen_timer_mark(&startup_timer, "renderer_bg");

    bool init_phase = true;

    /* Initialize tab and pane management */
    WixenTabManager tabs;
    wixen_tabs_init(&tabs);

    WixenPaneTree pane_tree;
    WixenPaneId root_pane;
    wixen_panes_init(&pane_tree, &root_pane);

    /* Use default profile shell, fall back to detect_shell */
    wchar_t *shell = NULL;
    const char *tab_title = "Shell";
    {
        const WixenProfile *prof = wixen_config_default_profile(&config);
        if (prof && prof->program) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, prof->program, -1, NULL, 0);
            if (wlen > 0) {
                shell = malloc(wlen * sizeof(wchar_t));
                if (shell) MultiByteToWideChar(CP_UTF8, 0, prof->program, -1, shell, wlen);
            }
            if (prof->name) tab_title = prof->name;
        }
        if (!shell) shell = wixen_pty_detect_shell();
    }

    /* Set initial window title from profile name or shell basename.
     * Fallback chain: profile->name → basename of shell exe → NULL (bare title).
     * This gives the user immediate feedback about which shell is loading,
     * before any PTY output or OSC title sequence arrives. */
    {
        const char *initial_name = NULL;
        char shell_base[260] = {0};
        if (tab_title && strcmp(tab_title, "Shell") != 0) {
            /* Profile name is available */
            initial_name = tab_title;
        } else if (shell) {
            /* Extract basename from the wide shell path */
            char narrow[260];
            WideCharToMultiByte(CP_UTF8, 0, shell, -1, narrow, sizeof(narrow), NULL, NULL);
            const char *base = strrchr(narrow, '\\');
            if (!base) base = strrchr(narrow, '/');
            base = base ? base + 1 : narrow;
            /* Copy and strip .exe extension if present */
            strncpy(shell_base, base, sizeof(shell_base) - 1);
            size_t blen = strlen(shell_base);
            if (blen > 4 && _stricmp(shell_base + blen - 4, ".exe") == 0)
                shell_base[blen - 4] = '\0';
            if (shell_base[0]) initial_name = shell_base;
        }
        char *init_title = wixen_format_window_title(initial_name, NULL);
        if (init_title) {
            wchar_t wtitle[512];
            MultiByteToWideChar(CP_UTF8, 0, init_title, -1, wtitle, 512);
            wixen_window_set_title(&window, wtitle);
            free(init_title);
        }
    }

    uint16_t cols = (uint16_t)(config.window.width / 8);
    uint16_t rows = (uint16_t)(config.window.height / 16);

    /* Try to restore session */
    char session_path[MAX_PATH];
    if (!wixen_session_path_from_config(cfg_path, session_path, sizeof(session_path))) {
        session_path[0] = '\0';
    }
    WixenSessionState saved_session;
    wixen_session_init(&saved_session);
    bool has_session = wixen_session_load(&saved_session, session_path);

    if (has_session && saved_session.tab_count > 0) {
        /* Restore tabs from saved session — spawn PTY for each */
        for (size_t i = 0; i < saved_session.tab_count && pane_state_count < MAX_PANES; i++) {
            WixenPaneState *pst = &pane_states[pane_state_count];
            wixen_terminal_init(&pst->terminal, cols, rows);
            wixen_shell_integ_init(&pst->shell_integ);
            wixen_parser_init(&pst->parser);
            /* Resolve profile by UUID. Session saves UUIDs, not program paths.
             * If UUID not found (profile deleted), falls back to default. */
            const char *cwd = saved_session.tabs[i].working_directory;
            wchar_t wcwd[MAX_PATH] = {0};
            if (cwd && cwd[0]) MultiByteToWideChar(CP_UTF8, 0, cwd, -1, wcwd, MAX_PATH);
            wchar_t *spawn_shell = shell; /* Default from profile */
            wchar_t resolved_shell[MAX_PATH] = {0};
            const char *saved_uuid = saved_session.tabs[i].profile_name;
            if (saved_uuid && saved_uuid[0]) {
                const WixenProfile *prof = wixen_config_profile_by_uuid(&config, saved_uuid);
                if (prof && prof->program) {
                    MultiByteToWideChar(CP_UTF8, 0, prof->program, -1, resolved_shell, MAX_PATH);
                    spawn_shell = resolved_shell;
                }
            }
            /* PTY spawn deferred to frame 4 — just init terminal/parser here */
            pst->pty_running = false;
            pane_state_count++;
            WixenPaneId np = (WixenPaneId)(i + 1);
            wixen_tabs_add(&tabs, saved_session.tabs[i].title, np);
        }
    } else {
        /* No saved session — start with single shell tab */
        WixenPaneState *ps = &pane_states[0];
        wixen_terminal_init(&ps->terminal, cols, rows);
        wixen_shell_integ_init(&ps->shell_integ);
        wixen_parser_init(&ps->parser);
        /* PTY spawn is DEFERRED to frame 4 of the main loop.
         * This keeps the message pump active during heavy init. */
        ps->pty_running = false;
        pane_state_count = 1;
        wixen_tabs_add(&tabs, tab_title, root_pane);
    }
    wixen_session_free(&saved_session);
    /* Don't free shell yet — needed for deferred PTY spawn */

    /* ps points to the active pane */
    WixenPaneState *ps = &pane_states[0];

    /* Create tray icon */
    WixenTrayIcon tray;
    wixen_tray_create(&tray, window.hwnd, L"Wixen Terminal");

    /* Config already loaded above — start hot-reload watcher */
    WixenConfigWatcher cfg_watcher = {0};
    wixen_watcher_start(&cfg_watcher, cfg_path, window.hwnd);

    /* Initialize a11y provider BEFORE showing window.
     * BUG #23: If window is shown first, NVDA sends WM_GETOBJECT
     * before the provider is registered and permanently caches the
     * window as non-UIA. */
    wixen_a11y_provider_init(window.hwnd, &ps->terminal);
    wixen_a11y_state_update_title_global(L"Wixen Terminal");
    pump_messages(); /* Dispatch any queued UIA queries */

    /* NOW show the window — provider is ready for WM_GETOBJECT */
    wixen_window_show(&window);
    pump_messages(); /* Let NVDA process the focus change */

    wixen_a11y_raise_focus_changed(window.hwnd);
    wixen_a11y_raise_selection_changed(window.hwnd);
    wixen_a11y_raise_notification(window.hwnd, "Wixen Terminal ready", "terminal-ready");
    pump_messages(); /* Deliver the events to NVDA */

    /* Output throttler — batches PTY output for screen reader */
    WixenEventThrottler pane_throttler;
    wixen_throttler_init(&pane_throttler, config.accessibility.output_debounce_ms);

    /* Frame-level a11y integration — tested in test_red_a11y_frame_loop */
    WixenFrameA11yState frame_a11y;
    wixen_frame_a11y_init(&frame_a11y, config.accessibility.output_debounce_ms);

    /* A11y tree for screen reader fragment navigation */
    WixenA11yTree a11y_tree;
    wixen_a11y_tree_init(&a11y_tree);
    wixen_a11y_set_tree(&a11y_tree); /* P2: Wire tree into provider for Navigate() */

    /* Search state */
    bool search_active = false;
    char search_query[256] = {0};
    size_t search_query_len = 0;
    WixenSearchEngine search_engine;
    wixen_search_init(&search_engine);

    /* Main event loop */
    bool running = true;
    bool char_typed_this_frame = false;
    WixenEchoCheckState echo_state;
    wixen_echo_check_init(&echo_state);
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
                        /* Dispatch OSC to shell integration before freeing */
                        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH && actions[i].osc.data) {
                            uint8_t *d = actions[i].osc.data;
                            size_t dl = actions[i].osc.data_len;
                            /* Parse OSC command number */
                            size_t sep = 0;
                            while (sep < dl && d[sep] != ';') sep++;
                            int cmd = 0;
                            for (size_t k = 0; k < sep; k++)
                                if (d[k] >= '0' && d[k] <= '9') cmd = cmd * 10 + (d[k] - '0');
                            const char *payload = sep + 1 < dl ? (const char *)(d + sep + 1) : "";
                            size_t plen = sep + 1 < dl ? dl - sep - 1 : 0;

                            if (cmd == 133 && plen >= 1) {
                                char marker = payload[0];
                                char *params = NULL;
                                if (plen > 2) {
                                    params = (char *)malloc(plen - 1);
                                    if (params) { memcpy(params, payload + 2, plen - 2); params[plen - 2] = '\0'; }
                                }
                                wixen_shell_integ_handle_osc133(&ps->shell_integ, marker, params,
                                    ps->terminal.grid.cursor.row);
                                free(params);
                            } else if (cmd == 7 && plen > 0) {
                                char *uri = (char *)malloc(plen + 1);
                                if (uri) { memcpy(uri, payload, plen); uri[plen] = '\0';
                                    wixen_shell_integ_handle_osc7(&ps->shell_integ, uri);
                                    free(uri);
                                }
                            }
                            free(actions[i].osc.data);
                        } else if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) {
                            free(actions[i].osc.data);
                        }
                        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH)
                            free(actions[i].apc.data);
                    }
                    /* Feed ALL PTY output to throttler, even if parser produced
                     * zero actions (incomplete escape sequence). The raw bytes
                     * are real terminal output that NVDA needs to hear. */
                    if (evt->len > 0) {
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

        /* BACKGROUND INIT: Poll for background thread completion.
         * UI thread NEVER blocks. Just pumps messages and presents dark bg. */
        if (init_phase) {
            if (wixen_bg_task_is_done(&bg_task)) {
                {
                    FILE *f = fopen("wixen-a11y-debug.log", "a");
                    if (f) { fprintf(f, "=== BG INIT DONE: finalizing ===\n"); fflush(f); fclose(f); }
                }
                /* Background init done — finalize on UI thread (fast: just swapchain) */
                renderer = wixen_renderer_finalize(
                    (WixenRendererBgResult *)bg_result, window.hwnd,
                    config.window.width ? config.window.width : 1200,
                    config.window.height ? config.window.height : 800);
                wixen_renderer_bg_result_free((WixenRendererBgResult *)bg_result);
                bg_result = NULL;
                wixen_bg_task_finalize(&bg_task);
                wixen_timer_mark(&startup_timer, "renderer_done");

                /* Spawn PTY */
                WixenFontMetrics fm = renderer ? wixen_renderer_font_metrics(renderer)
                    : (WixenFontMetrics){8,16,0,0,0};
                uint16_t c = (uint16_t)((config.window.width ? config.window.width : 1200)
                    / (fm.cell_width > 0 ? fm.cell_width : 8));
                uint16_t r = (uint16_t)((config.window.height ? config.window.height : 800)
                    / (fm.cell_height > 0 ? fm.cell_height : 16));
                if (c == 0) c = 80; if (r == 0) r = 24;
                cols = c; rows = r;
                for (size_t pi = 0; pi < pane_state_count; pi++) {
                    WixenPaneState *pst = &pane_states[pi];
                    if (pst->terminal.modes.alternate_screen)
                        wixen_terminal_resize(&pst->terminal, cols, rows);
                    else
                        wixen_terminal_resize_reflow(&pst->terminal, cols, rows);
                    if (!pst->pty_running)
                        pst->pty_running = wixen_pty_spawn(&pst->pty, cols, rows,
                            shell, NULL, NULL, window.hwnd);
                }
                free(shell); shell = NULL;
                wixen_timer_mark(&startup_timer, "pty");

                init_phase = false;
                {
                    FILE *f = fopen("wixen-a11y-debug.log", "a");
                    if (f) { fprintf(f, "=== INIT COMPLETE: firing events ===\n"); fflush(f); fclose(f); }
                }
                wixen_timer_mark(&startup_timer, "ready");
                wixen_timer_log(&startup_timer, "wixen-startup.log");
                wixen_a11y_raise_notification(window.hwnd,
                    "Wixen Terminal ready", "terminal-ready");
                wixen_a11y_raise_focus_changed(window.hwnd);
            } else {
                /* Still waiting — just pump and present dark bg */
                MsgWaitForMultipleObjects(0, NULL, FALSE, 16, QS_ALLINPUT);
                continue;
            }
        }

        /* COM pump: before window events — dispatch COM calls queued during
         * PTY processing. UseComThreading delivers all UIA provider calls as
         * Win32 messages; they can ONLY run during PeekMessage/pump_messages.
         * Without this, NVDA's GetSelection/GetCaretRange wait until the next
         * full message pump, adding ~1 second latency. */
        pump_messages();

        /* Process window events */
        static DWORD last_dblclick_tick = 0;
        WixenWindowEvent evt;
        while (wixen_window_poll_event(&window, &evt)) {
            switch (evt.type) {
            case WIXEN_EVT_CLOSE_REQUESTED:
                running = false;
                /* Hide window immediately — user expects instant response */
                ShowWindow(window.hwnd, SW_HIDE);
                break;
            case WIXEN_EVT_RESIZED: {
                wixen_renderer_resize(renderer, evt.resize.width, evt.resize.height);
                /* Recalculate terminal dimensions from new window size */
                WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                if (fm.cell_width > 0 && fm.cell_height > 0) {
                    uint16_t new_cols = (uint16_t)(evt.resize.width / fm.cell_width);
                    uint16_t new_rows = (uint16_t)(evt.resize.height / fm.cell_height);
                    if (new_cols > 0 && new_rows > 0) {
                        if (ps->terminal.modes.alternate_screen)
                            wixen_terminal_resize(&ps->terminal, new_cols, new_rows);
                        else
                            wixen_terminal_resize_reflow(&ps->terminal, new_cols, new_rows);
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
                        } else if (strcmp(action, "close_pane") == 0 || strcmp(action, "close_tab") == 0) {
                            running = false;
                        } else if (strcmp(action, "new_tab") == 0) {
                            /* Spawn new PTY in new tab */
                            wchar_t *sh = wixen_pty_detect_shell();
                            WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                            uint16_t nc = fm.cell_width > 0 ? (uint16_t)(wixen_renderer_width(renderer) / fm.cell_width) : 80;
                            uint16_t nr = fm.cell_height > 0 ? (uint16_t)(wixen_renderer_height(renderer) / fm.cell_height) : 24;
                            if (pane_state_count < MAX_PANES) {
                                WixenPaneState *nps = &pane_states[pane_state_count];
                                wixen_terminal_init(&nps->terminal, nc, nr);
                                wixen_parser_init(&nps->parser);
                                wixen_shell_integ_init(&nps->shell_integ);
                                nps->pty_running = wixen_pty_spawn(&nps->pty, nc, nr, sh, NULL, NULL, window.hwnd);
                                pane_state_count++;
                                WixenPaneId np;
                                wixen_panes_init(&pane_tree, &np);
                                wixen_tabs_add(&tabs, "Shell", np);
                                ps = nps; /* Switch to new pane */
                            }
                            free(sh);
                        } else if (strcmp(action, "next_tab") == 0) {
                            wixen_tabs_cycle(&tabs, true);
                            {
                                const WixenTab *at = wixen_tabs_active(&tabs);
                                if (at) wixen_a11y_raise_notification(window.hwnd, at->title, "tab-switch");
                            }
                        } else if (strcmp(action, "prev_tab") == 0) {
                            wixen_tabs_cycle(&tabs, false);
                            {
                                const WixenTab *at = wixen_tabs_active(&tabs);
                                if (at) wixen_a11y_raise_notification(window.hwnd, at->title, "tab-switch");
                            }
                        } else if (strcmp(action, "find") == 0) {
                            if (!search_active) {
                                search_active = true;
                                search_query[0] = '\0';
                                search_query_len = 0;
                                wixen_a11y_raise_notification(window.hwnd,
                                    "Search: type to search, Escape to close",
                                    "search-open");
                            } else {
                                search_active = false;
                                wixen_a11y_raise_notification(window.hwnd,
                                    "Search closed", "search-close");
                            }
                        } else if (strcmp(action, "find_next") == 0) {
                            if (search_active && wixen_search_match_count(&search_engine) > 0) {
                                wixen_search_next(&search_engine, WIXEN_SEARCH_FORWARD);
                                const WixenSearchMatch *m = wixen_search_active_match(&search_engine);
                                if (m) {
                                    char smsg[128];
                                    snprintf(smsg, sizeof(smsg), "Match %zu of %zu",
                                        search_engine.active + 1,
                                        wixen_search_match_count(&search_engine));
                                    wixen_a11y_raise_notification(window.hwnd, smsg, "search-nav");
                                }
                            }
                        } else if (strcmp(action, "find_previous") == 0) {
                            if (search_active && wixen_search_match_count(&search_engine) > 0) {
                                wixen_search_next(&search_engine, WIXEN_SEARCH_BACKWARD);
                                const WixenSearchMatch *m = wixen_search_active_match(&search_engine);
                                if (m) {
                                    char smsg[128];
                                    snprintf(smsg, sizeof(smsg), "Match %zu of %zu",
                                        search_engine.active + 1,
                                        wixen_search_match_count(&search_engine));
                                    wixen_a11y_raise_notification(window.hwnd, smsg, "search-nav");
                                }
                            }
                        } else if (strcmp(action, "select_all") == 0) {
                            wixen_selection_clear(&ps->terminal.selection);
                            wixen_selection_start(&ps->terminal.selection, 0, 0, WIXEN_SEL_NORMAL);
                            wixen_selection_update(&ps->terminal.selection,
                                ps->terminal.grid.cols - 1, ps->terminal.grid.num_rows - 1);
                            ps->terminal.dirty = true;
                        } else if (strcmp(action, "clear") == 0 || strcmp(action, "clear_terminal") == 0) {
                            if (ps->pty_running)
                                wixen_pty_write(&ps->pty, "\x1b[2J\x1b[H", 7);
                        } else if (strcmp(action, "clear_scrollback") == 0) {
                            if (ps->pty_running)
                                wixen_pty_write(&ps->pty, "\x1b[3J", 4);
                        } else if (strcmp(action, "reset_terminal") == 0) {
                            wixen_terminal_reset(&ps->terminal);
                            ps->terminal.dirty = true;
                        } else if (strcmp(action, "scroll_up_page") == 0) {
                            wixen_terminal_scroll_viewport(&ps->terminal, (int32_t)ps->terminal.grid.num_rows);
                        } else if (strcmp(action, "scroll_down_page") == 0) {
                            wixen_terminal_scroll_viewport(&ps->terminal, -(int32_t)ps->terminal.grid.num_rows);
                        } else if (strcmp(action, "scroll_to_top") == 0) {
                            wixen_terminal_scroll_viewport(&ps->terminal, 999999);
                        } else if (strcmp(action, "scroll_to_bottom") == 0) {
                            ps->terminal.scroll_offset = 0;
                            ps->terminal.dirty = true;
                        } else if (strcmp(action, "split_horizontal") == 0) {
                            WixenPaneId active = wixen_panes_active(&pane_tree);
                            wixen_panes_split(&pane_tree, active, WIXEN_SPLIT_HORIZONTAL);
                            ps->terminal.dirty = true;
                        } else if (strcmp(action, "split_vertical") == 0) {
                            WixenPaneId active = wixen_panes_active(&pane_tree);
                            wixen_panes_split(&pane_tree, active, WIXEN_SPLIT_VERTICAL);
                            ps->terminal.dirty = true;
                        } else if (strcmp(action, "zoom_pane") == 0 || strcmp(action, "toggle_zoom") == 0) {
                            if (wixen_panes_is_zoomed(&pane_tree))
                                wixen_panes_unzoom(&pane_tree);
                            else
                                wixen_panes_zoom(&pane_tree, wixen_panes_active(&pane_tree));
                            ps->terminal.dirty = true;
                        } else if (strcmp(action, "reload_config") == 0) {
                            WixenConfig new_cfg;
                            wixen_config_init_defaults(&new_cfg);
                            char cfg_path_buf[MAX_PATH];
                            wixen_config_default_path(cfg_path_buf, sizeof(cfg_path_buf));
                            if (wixen_config_load(&new_cfg, cfg_path_buf)) {
                                wixen_config_free(&config);
                                config = new_cfg;
                            } else {
                                wixen_config_free(&new_cfg);
                            }
                        } else if (strcmp(action, "copy_cwd") == 0) {
                            if (ps->shell_integ.cwd)
                                wixen_clipboard_set_text(window.hwnd, ps->shell_integ.cwd);
                        } else if (strcmp(action, "open_cwd_in_explorer") == 0) {
                            if (ps->shell_integ.cwd) {
                                wchar_t wcwd[MAX_PATH];
                                MultiByteToWideChar(CP_UTF8, 0, ps->shell_integ.cwd, -1, wcwd, MAX_PATH);
                                ShellExecuteW(NULL, L"explore", wcwd, NULL, NULL, SW_SHOWNORMAL);
                            }
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
                /* Only printable characters count as "typed" for echo
                 * detection.  Control chars (Enter=0x0D, Tab=0x09,
                 * Backspace=0x08, Escape=0x1B) must NOT set the flag,
                 * otherwise pressing Enter triggers a false-positive
                 * "Text not echoed" when the shell redraws the prompt. */
                if (wixen_is_printable_for_echo(evt.char_input))
                    char_typed_this_frame = true;
                /* Enter = new command: reset echo tracking state so
                 * subsequent shell output doesn't count against us. */
                if (evt.char_input == 0x0D)
                    wixen_echo_check_reset(&echo_state);
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
            case WIXEN_EVT_MOUSE_DOWN: {
                WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                size_t mcol = fm.cell_width > 0 ? (size_t)(evt.mouse.x / fm.cell_width) : 0;
                size_t mrow = fm.cell_height > 0 ? (size_t)(evt.mouse.y / fm.cell_height) : 0;
                if (evt.mouse.button == WIXEN_MB_LEFT && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    /* Ctrl+click — open hyperlink if cell has one */
                    bool opened = false;
                    if (mrow < ps->terminal.grid.num_rows && mcol < ps->terminal.grid.cols) {
                        WixenCell *hc = &ps->terminal.grid.rows[mrow].cells[mcol];
                        if (hc->attrs.hyperlink_id > 0) {
                            const WixenHyperlink *hl = wixen_hyperlinks_get(
                                &ps->terminal.hyperlinks, hc->attrs.hyperlink_id);
                            if (hl && hl->uri && wixen_is_safe_url_scheme(hl->uri)) {
                                wchar_t wurl[1024];
                                MultiByteToWideChar(CP_UTF8, 0, hl->uri, -1, wurl, 1024);
                                ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
                                opened = true;
                            }
                        }
                    }
                    if (!opened) {
                        /* No hyperlink or unsafe scheme — fall through to normal selection */
                        wixen_selection_clear(&ps->terminal.selection);
                        wixen_selection_start(&ps->terminal.selection, mcol, mrow, WIXEN_SEL_NORMAL);
                        ps->terminal.dirty = true;
                    }
                } else if (evt.mouse.button == WIXEN_MB_LEFT) {
                    /* Check for triple-click (line select) */
                    DWORD now = GetTickCount();
                    if (last_dblclick_tick > 0 && (now - last_dblclick_tick) < GetDoubleClickTime()) {
                        /* Triple-click — line selection */
                        wixen_selection_clear(&ps->terminal.selection);
                        wixen_selection_start(&ps->terminal.selection, 0, mrow, WIXEN_SEL_LINE);
                        wixen_selection_update(&ps->terminal.selection,
                            ps->terminal.grid.cols - 1, mrow);
                        last_dblclick_tick = 0;
                    } else if (GetKeyState(VK_MENU) & 0x8000) {
                        /* Alt+click — block/rectangle selection */
                        wixen_selection_clear(&ps->terminal.selection);
                        wixen_selection_start(&ps->terminal.selection, mcol, mrow, WIXEN_SEL_BLOCK);
                    } else {
                        /* Normal click — start selection */
                        wixen_selection_clear(&ps->terminal.selection);
                        wixen_selection_start(&ps->terminal.selection, mcol, mrow, WIXEN_SEL_NORMAL);
                    }
                    ps->terminal.dirty = true;
                } else if (evt.mouse.button == WIXEN_MB_RIGHT) {
                    wixen_window_show_context_menu(&window, evt.mouse.x, evt.mouse.y);
                } else if (evt.mouse.button == WIXEN_MB_MIDDLE) {
                    /* Middle click = paste */
                    char *text = wixen_clipboard_get_text(window.hwnd);
                    if (text && ps->pty_running) {
                        wixen_pty_write(&ps->pty, text, strlen(text));
                    }
                    free(text);
                }
                /* Mouse reporting to terminal app */
                if (ps->terminal.modes.mouse_tracking != WIXEN_MOUSE_NONE) {
                    char mbuf[32];
                    int mb = (evt.mouse.button == WIXEN_MB_LEFT) ? WIXEN_MBTN_LEFT :
                        (evt.mouse.button == WIXEN_MB_RIGHT) ? WIXEN_MBTN_RIGHT : WIXEN_MBTN_MIDDLE;
                    size_t mn = wixen_encode_mouse(ps->terminal.modes.mouse_tracking,
                        ps->terminal.modes.mouse_sgr, (WixenMouseBtn)mb,
                        (uint16_t)mcol, (uint16_t)mrow, false, false, true, mbuf, sizeof(mbuf));
                    if (mn > 0 && ps->pty_running)
                        wixen_pty_write(&ps->pty, mbuf, mn);
                }
                break;
            }
            case WIXEN_EVT_MOUSE_UP: {
                /* End selection + mouse release reporting */
                if (ps->terminal.modes.mouse_tracking != WIXEN_MOUSE_NONE) {
                    WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                    size_t mcol = fm.cell_width > 0 ? (size_t)(evt.mouse.x / fm.cell_width) : 0;
                    size_t mrow = fm.cell_height > 0 ? (size_t)(evt.mouse.y / fm.cell_height) : 0;
                    char mbuf[32];
                    size_t mn = wixen_encode_mouse(ps->terminal.modes.mouse_tracking,
                        ps->terminal.modes.mouse_sgr, WIXEN_MBTN_RELEASE,
                        (uint16_t)mcol, (uint16_t)mrow, false, false, false, mbuf, sizeof(mbuf));
                    if (mn > 0 && ps->pty_running)
                        wixen_pty_write(&ps->pty, mbuf, mn);
                }
                break;
            }
            case WIXEN_EVT_MOUSE_MOVE: {
                /* Drag selection update */
                if (ps->terminal.selection.active) {
                    WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                    size_t mcol = fm.cell_width > 0 ? (size_t)(evt.mouse_move.x / fm.cell_width) : 0;
                    size_t mrow = fm.cell_height > 0 ? (size_t)(evt.mouse_move.y / fm.cell_height) : 0;
                    wixen_selection_update(&ps->terminal.selection, mcol, mrow);
                    ps->terminal.dirty = true;
                }
                break;
            }
            case WIXEN_EVT_DOUBLE_CLICK: {
                last_dblclick_tick = GetTickCount();
                /* Word selection */
                WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                size_t mcol = fm.cell_width > 0 ? (size_t)(evt.dbl_click.x / fm.cell_width) : 0;
                size_t mrow = fm.cell_height > 0 ? (size_t)(evt.dbl_click.y / fm.cell_height) : 0;
                if (mrow < ps->terminal.grid.num_rows) {
                    WixenRow *row = &ps->terminal.grid.rows[mrow];
                    /* Find word boundaries */
                    size_t ws = mcol, we = mcol;
                    while (ws > 0 && row->cells[ws - 1].content[0] != ' '
                           && row->cells[ws - 1].content[0] != '\0') ws--;
                    while (we < row->count - 1 && row->cells[we + 1].content[0] != ' '
                           && row->cells[we + 1].content[0] != '\0') we++;
                    wixen_selection_clear(&ps->terminal.selection);
                    wixen_selection_start(&ps->terminal.selection, ws, mrow, WIXEN_SEL_WORD);
                    wixen_selection_update(&ps->terminal.selection, we, mrow);
                    ps->terminal.dirty = true;
                }
                break;
            }
            case WIXEN_EVT_MOUSE_WHEEL: {
                WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                size_t mcol = fm.cell_width > 0 ? (size_t)(evt.wheel.x / fm.cell_width) : 0;
                size_t mrow = fm.cell_height > 0 ? (size_t)(evt.wheel.y / fm.cell_height) : 0;
                if (ps->terminal.modes.alternate_screen) {
                    /* In alt screen, translate wheel to arrow keys */
                    int count = abs(evt.wheel.delta) * 3;
                    const char *key = evt.wheel.delta > 0 ? "\x1b[A" : "\x1b[B";
                    for (int i = 0; i < count && ps->pty_running; i++)
                        wixen_pty_write(&ps->pty, key, 3);
                } else if (ps->terminal.modes.mouse_tracking != WIXEN_MOUSE_NONE) {
                    /* Mouse wheel reporting */
                    char mbuf[32];
                    WixenMouseBtn wb = evt.wheel.delta > 0 ? WIXEN_MBTN_WHEEL_UP : WIXEN_MBTN_WHEEL_DOWN;
                    size_t mn = wixen_encode_mouse(ps->terminal.modes.mouse_tracking,
                        ps->terminal.modes.mouse_sgr, wb,
                        (uint16_t)mcol, (uint16_t)mrow, false, false, true, mbuf, sizeof(mbuf));
                    if (mn > 0 && ps->pty_running)
                        wixen_pty_write(&ps->pty, mbuf, mn);
                } else {
                    /* Scrollback viewport scroll */
                    int32_t lines = evt.wheel.delta > 0 ? 3 : -3;
                    wixen_terminal_scroll_viewport(&ps->terminal, lines);
                }
                break;
            }
            case WIXEN_EVT_CONTEXT_MENU:
                switch (evt.context_action) {
                case WIXEN_CTX_COPY:
                    if (ps->terminal.selection.active) {
                        WixenGridPoint cs, ce;
                        wixen_selection_ordered(&ps->terminal.selection, &cs, &ce);
                        char cbuf[4096] = {0};
                        size_t cpos = 0;
                        for (size_t r = cs.row; r <= ce.row && r < ps->terminal.grid.num_rows; r++) {
                            size_t c0 = (r == cs.row) ? cs.col : 0;
                            size_t c1 = (r == ce.row) ? ce.col : ps->terminal.grid.cols;
                            WixenRow *crow = &ps->terminal.grid.rows[r];
                            for (size_t c = c0; c < c1 && c < crow->count && cpos < sizeof(cbuf) - 2; c++) {
                                const char *ch = crow->cells[c].content;
                                size_t cl = ch ? strlen(ch) : 0;
                                if (cl > 0) { memcpy(cbuf + cpos, ch, cl); cpos += cl; }
                                else cbuf[cpos++] = ' ';
                            }
                            if (r < ce.row && cpos < sizeof(cbuf) - 2) cbuf[cpos++] = '\n';
                        }
                        if (cpos > 0) wixen_clipboard_set_text(window.hwnd, cbuf);
                    }
                    break;
                case WIXEN_CTX_PASTE: {
                    char *pt = wixen_clipboard_get_text(window.hwnd);
                    if (pt && ps->pty_running) {
                        if (ps->terminal.modes.bracketed_paste)
                            wixen_pty_write(&ps->pty, "\x1b[200~", 6);
                        wixen_pty_write(&ps->pty, pt, strlen(pt));
                        if (ps->terminal.modes.bracketed_paste)
                            wixen_pty_write(&ps->pty, "\x1b[201~", 6);
                    }
                    free(pt);
                    break;
                }
                case WIXEN_CTX_SELECT_ALL:
                    wixen_selection_clear(&ps->terminal.selection);
                    wixen_selection_start(&ps->terminal.selection, 0, 0, WIXEN_SEL_NORMAL);
                    wixen_selection_update(&ps->terminal.selection,
                        ps->terminal.grid.cols - 1, ps->terminal.grid.num_rows - 1);
                    ps->terminal.dirty = true;
                    break;
                case WIXEN_CTX_SPLIT_H: {
                    WixenPaneId ap = wixen_panes_active(&pane_tree);
                    wixen_panes_split(&pane_tree, ap, WIXEN_SPLIT_HORIZONTAL);
                    break;
                }
                case WIXEN_CTX_SPLIT_V: {
                    WixenPaneId ap = wixen_panes_active(&pane_tree);
                    wixen_panes_split(&pane_tree, ap, WIXEN_SPLIT_VERTICAL);
                    break;
                }
                case WIXEN_CTX_NEW_TAB:
                    /* Same as new_tab keybinding */
                    break;
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
            case WIXEN_EVT_TRAY_COMMAND:
                if (evt.tray_action == -1) {
                    /* Sentinel: right-click on tray icon — show popup menu */
                    wixen_tray_show_menu(&tray);
                } else {
                    switch (evt.tray_action) {
                    case WIXEN_TRAY_SHOW_HIDE:
                        if (IsWindowVisible(window.hwnd)) {
                            ShowWindow(window.hwnd, SW_HIDE);
                        } else {
                            ShowWindow(window.hwnd, SW_SHOW);
                            SetForegroundWindow(window.hwnd);
                        }
                        break;
                    case WIXEN_TRAY_NEW_TAB:
                        /* Same as new_tab keybinding — spawn new PTY in new tab */
                        if (renderer && pane_state_count < MAX_PANES) {
                            wchar_t *sh = wixen_pty_detect_shell();
                            WixenFontMetrics fm = wixen_renderer_font_metrics(renderer);
                            uint16_t nc = fm.cell_width > 0 ? (uint16_t)(wixen_renderer_width(renderer) / fm.cell_width) : 80;
                            uint16_t nr = fm.cell_height > 0 ? (uint16_t)(wixen_renderer_height(renderer) / fm.cell_height) : 24;
                            WixenPaneState *nps = &pane_states[pane_state_count];
                            wixen_terminal_init(&nps->terminal, nc, nr);
                            wixen_parser_init(&nps->parser);
                            wixen_shell_integ_init(&nps->shell_integ);
                            nps->pty_running = wixen_pty_spawn(&nps->pty, nc, nr, sh, NULL, NULL, window.hwnd);
                            pane_state_count++;
                            WixenPaneId np;
                            wixen_panes_init(&pane_tree, &np);
                            wixen_tabs_add(&tabs, "Shell", np);
                            ps = nps;
                            free(sh);
                        }
                        break;
                    case WIXEN_TRAY_SETTINGS:
                        wixen_settings_dialog_show(window.hwnd);
                        break;
                    case WIXEN_TRAY_EXIT:
                        running = false;
                        ShowWindow(window.hwnd, SW_HIDE);
                        break;
                    default:
                        break;
                    }
                }
                break;
            case WIXEN_EVT_FOCUS_GAINED:
                /* P1 FIX: Update provider state BEFORE raising events.
                 * HasKeyboardFocus and GetCaretRange.isActive read has_focus.
                 * Without this, NVDA queries the provider and gets false. */
                wixen_a11y_state_update_focus_global(true);
                wixen_a11y_raise_focus_changed(window.hwnd);
                wixen_a11y_raise_selection_changed(window.hwnd);
                if (ps->terminal.modes.focus_reporting && ps->pty_running)
                    wixen_pty_write(&ps->pty, "\x1b[I", 3);
                break;
            case WIXEN_EVT_FOCUS_LOST:
                wixen_a11y_state_update_focus_global(false);
                if (ps->terminal.modes.focus_reporting && ps->pty_running)
                    wixen_pty_write(&ps->pty, "\x1b[O", 3);
                break;
            case WIXEN_EVT_DPI_CHANGED:
                /* Recalculate font metrics from new DPI */
                window.dpi = evt.dpi;
                break;
            default:
                break;
            }
        }

        /* COM pump: before render — dispatch COM calls that arrived during
         * window event processing. Without this, COM calls wait for the full
         * render pass (~8-16ms) before getting dispatched. */
        pump_messages();

        /* Render frame */
        WixenPaneRenderInfo pane_info = {
            .grid = &ps->terminal.grid,
            .image_store = &ps->terminal.images,
            .x = 0, .y = 0,
            .width = (float)wixen_renderer_width(renderer),
            .height = (float)wixen_renderer_height(renderer),
            .has_focus = true,
            .scroll_offset = 0,
        };
        wixen_renderer_render_frame(renderer, &pane_info, 1);

        /* COM pump: after render — dispatch COM calls that queued during
         * the render pass. NVDA's GetText/ExpandToEnclosingUnit arrive as
         * Win32 messages; pumping here ensures they don't wait for the
         * full a11y update section before being serviced. */
        pump_messages();

        /* --- Accessibility state updates --- */
        {
            /* Sync grid visible text to a11y state for ITextProvider */
            if (ps->terminal.dirty) {
                /* P6 FIX: Use dynamic allocation — no truncation for large grids */
                char *a11y_text = wixen_grid_visible_text_dynamic(&ps->terminal.grid);
                if (a11y_text) {
                    wixen_a11y_state_update_text_global(a11y_text, strlen(a11y_text));

                    /* Raise UIA TextChanged if visible text actually changed.
                     * Without this, NVDA never learns that text was updated. */
                    static char *prev_a11y_text = NULL;
                    if (wixen_a11y_should_raise_text_changed(prev_a11y_text, a11y_text)) {
                        wixen_a11y_raise_text_changed_global();
                    }
                    free(prev_a11y_text);
                    prev_a11y_text = _strdup(a11y_text);

                    free(a11y_text);
                }
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
                        /* #12: Verbosity filter */
                        bool should_announce = true;
                        if (config.accessibility.verbosity &&
                            strcmp(config.accessibility.verbosity, "none") == 0) {
                            should_announce = false;
                        }
                        if (should_announce) {
                            /* #3 raise_notification: use MostRecent when streaming
                             * to avoid flooding screen reader queue */
                            if (wixen_throttler_is_streaming(&pane_throttler)) {
                                /* Streaming mode — only announce last batch */
                                wixen_a11y_raise_notification(window.hwnd, clean, "terminal-output");
                            } else {
                                wixen_a11y_raise_notification(window.hwnd, clean, "terminal-output");
                            }
                        }
                        has_real_output = true;
                        /* #5 Error/warning detection + audio */
                        WixenOutputLineClass lc = wixen_classify_output_line(clean);
                        if (lc == WIXEN_LINE_ERROR) {
                            WixenAudioConfig eac;
                            wixen_audio_config_init(&eac);
                            wixen_audio_play(&eac, WIXEN_AUDIO_COMMAND_ERROR);
                        } else if (lc == WIXEN_LINE_WARNING) {
                            WixenAudioConfig wac;
                            wixen_audio_config_init(&wac);
                            wixen_audio_play(&wac, WIXEN_AUDIO_OUTPUT_WARNING);
                        }
                    }
                    free(clean);
                    free(stripped);
                }
                free(pending);
            }

            /* Track cursor and line state */
            static size_t prev_cursor_row = SIZE_MAX;
            static size_t prev_cursor_col = SIZE_MAX;
            static char *prev_line = NULL;

            size_t cur_row = ps->terminal.grid.cursor.row;
            size_t cur_col = ps->terminal.grid.cursor.col;
            bool cursor_moved = (cur_row != prev_cursor_row || cur_col != prev_cursor_col);

            /* Compute current line text (dynamic — no truncation on long lines) */
            char *line_buf = NULL;
            if (cur_row < ps->terminal.grid.num_rows) {
                line_buf = wixen_row_text_dynamic(&ps->terminal.grid.rows[cur_row]);
            }
            if (!line_buf) {
                line_buf = _strdup("");
            }

            /* #8: Compute UTF-16 offset atomically for lock-free GetSelection.
             * visible_text pads each row to exactly grid.cols characters,
             * so each preceding row contributes cols + 1 (for \n) UTF-16 units
             * (for ASCII content; multi-byte chars handled below). */
            /* Cursor offset for UIA: each row in visible_text is exactly
             * grid_cols characters (content + space padding) + 1 newline.
             * For ASCII, that's grid_cols + 1 UTF-16 units per row.
             * The cursor column indexes INTO the padded row, never hitting \n.
             * BUG FIX: previous code used wixen_row_text_dynamic per row which
             * is trimmed, causing offset mismatch with padded visible_text. */
            {
                int32_t utf16_off = 0;
                size_t grid_cols = ps->terminal.grid.cols;
                /* Each preceding row = grid_cols (padded) + 1 (newline) */
                utf16_off = (int32_t)(cur_row * (grid_cols + 1));
                /* Column within current row — clamped to grid width */
                size_t clamped_col = cur_col < grid_cols ? cur_col : grid_cols - 1;
                utf16_off += (int32_t)clamped_col;
                wixen_a11y_set_cursor_offset(utf16_off);
            }
            wixen_a11y_update_cursor(&ps->terminal.grid);

            /* #9: Pump messages so COM calls dispatch against consistent state */
            wixen_a11y_pump_messages(window.hwnd);

            /* Fire TextSelectionChanged on cursor movement */
            if (cursor_moved) {
                wixen_a11y_raise_selection_changed(window.hwnd);
            }

            /* Arrow key line reading: when cursor moves to a different row
             * (up/down arrow, page up/down, etc.), announce the current line.
             * This supplements NVDA's native caret tracking which may not
             * read the line for our control type. */
            bool row_changed = (cur_row != prev_cursor_row && prev_cursor_row != SIZE_MAX);
            if (row_changed && cursor_moved && !has_real_output) {
                /* Strip prompt prefix — announce only the content */
                char *stripped = wixen_strip_prompt(line_buf);
                if (stripped && stripped[0]) {
                    wixen_a11y_raise_notification(window.hwnd, stripped, "cursor-line");
                } else if (!line_buf || !line_buf[0]) {
                    wixen_a11y_raise_notification(window.hwnd, "blank", "cursor-line");
                }
                free(stripped);
            }

            /* #5/#7: Line change detection with prompt stripping.
             * Detects history recall (up/down changes command).
             * Only fires for replacement — not append or deletion. */
            const char *prev_line_safe = prev_line ? prev_line : "";
            bool line_changed = (strcmp(line_buf, prev_line_safe) != 0);
            if (line_changed && !has_real_output && prev_line_safe[0]) {
                size_t cur_len = strlen(line_buf);
                size_t prv_len = strlen(prev_line_safe);
                while (cur_len > 0 && line_buf[cur_len - 1] == ' ') cur_len--;
                while (prv_len > 0 && prev_line_safe[prv_len - 1] == ' ') prv_len--;

                bool is_append = (cur_len > prv_len && memcmp(line_buf, prev_line_safe, prv_len) == 0);
                bool is_delete = (cur_len < prv_len && memcmp(prev_line_safe, line_buf, cur_len) == 0);

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
                    /* #11: Rebuild a11y tree from shell_integ blocks */
                    {
                        size_t bc = 0;
                        const WixenCommandBlock *all_blocks =
                            wixen_shell_integ_blocks(&ps->shell_integ, &bc);
                        wixen_a11y_tree_rebuild(&a11y_tree, all_blocks, bc);
                        /* P3 FIX: Raise structure changed so SRs re-query children */
                        wixen_a11y_raise_structure_changed_global();
                    }
                    const WixenCommandBlock *blk = wixen_shell_integ_current_block(&ps->shell_integ);
                    if (blk && blk->state == WIXEN_BLOCK_COMPLETED && blk->has_exit_code) {
                        char cmd_msg[256];
                        snprintf(cmd_msg, sizeof(cmd_msg), "Command finished, exit code %d", blk->exit_code);
                        wixen_a11y_raise_notification(window.hwnd, cmd_msg, "command-complete");
                    }
                }
            }

            /* #10: Password prompt detection — if terminal stopped echoing.
             * Only fires after 3+ consecutive frames with no cursor movement
             * AND no output, AND the user typed a printable character.
             * char_typed_this_frame is only set for printable chars (0x20..0x7E).
             * Enter/Tab/Backspace do NOT set the flag, preventing false
             * positives when the shell redraws after command submission. */
            {
                WixenEchoResult echo_result = wixen_echo_check_update(
                    &echo_state, char_typed_this_frame, cur_col,
                    cursor_moved, has_real_output);
                if (echo_result == WIXEN_ECHO_RESULT_PASSWORD) {
                    wixen_a11y_raise_notification(window.hwnd,
                        "Text not echoed", "password-prompt");
                }
                char_typed_this_frame = false;
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
                    if (strcmp(line_buf, prev_line_safe) == 0) {
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
            free(prev_line);
            prev_line = _strdup(line_buf);
            free(line_buf);
        }

        /* Handle terminal state changes */
        if (ps->terminal.bell_pending) {
            WixenAudioConfig audio_cfg;
            wixen_audio_config_init(&audio_cfg);
            wixen_audio_play(&audio_cfg, WIXEN_AUDIO_BELL);
            ps->terminal.bell_pending = false;
        }
        if (ps->terminal.title_dirty && ps->terminal.title) {
            char *formatted = wixen_window_format_title(ps->terminal.title, NULL);
            if (formatted) {
                wchar_t wtitle[512];
                MultiByteToWideChar(CP_UTF8, 0, formatted, -1, wtitle, 512);
                wixen_window_set_title(&window, wtitle);
                wixen_a11y_state_update_title_global(wtitle);
                free(formatted);
            }
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

        /* Config hot-reload — check for WM_CONFIG_CHANGED in message stream */
        /* The watcher thread posts WM_APP+20 when config file changes */
        {
            static DWORD last_reload_tick = 0;
            DWORD now = GetTickCount();
            /* Throttle to max once per 500ms */
            if (cfg_watcher.changed && (now - last_reload_tick > 500)) {
                cfg_watcher.changed = false;
                last_reload_tick = now;
                WixenConfig new_cfg;
                wixen_config_init_defaults(&new_cfg);
                if (wixen_config_load(&new_cfg, cfg_path)) {
                    WixenConfigDelta delta;
                    wixen_config_diff(&config, &new_cfg, &delta);

                    /* Apply font changes — rebuild atlas only, keep device/swapchain */
                    if (delta.font_changed && renderer) {
                        wixen_renderer_update_font(renderer,
                            new_cfg.font.family, new_cfg.font.size);
                    }

                    /* Apply theme/color changes */
                    if (delta.colors_changed && renderer) {
                        WixenColorScheme cs;
                        wixen_colors_init_default(&cs);
                        wixen_renderer_set_colors(renderer, &cs);
                    }

                    /* Apply debounce change to the output throttler */
                    if (delta.accessibility_changed) {
                        pane_throttler.debounce_ms =
                            new_cfg.accessibility.output_debounce_ms;
                    }

                    /* Apply keybinding changes — rebuild live map */
                    if (delta.keybindings_changed) {
                        wixen_keybindings_free(&keybindings);
                        wixen_keybindings_init(&keybindings);
                        for (size_t i = 0; i < new_cfg.keybindings.count; i++) {
                            const WixenKeybinding *kb =
                                wixen_keybindings_get_at(&new_cfg.keybindings, i);
                            if (kb) {
                                wixen_keybindings_add(&keybindings,
                                    kb->chord, kb->action, kb->args);
                            }
                        }
                    }

                    wixen_config_free(&config);
                    config = new_cfg;

                    /* Update taskbar jumplist with new profile names */
                    {
                        size_t jl_count = config.profile_count;
                        if (jl_count > 0) {
                            const wchar_t **jl_names = (const wchar_t **)malloc(
                                jl_count * sizeof(wchar_t *));
                            if (jl_names) {
                                for (size_t ji = 0; ji < jl_count; ji++) {
                                    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        config.profiles[ji].name, -1, NULL, 0);
                                    wchar_t *wname = (wchar_t *)malloc(wlen * sizeof(wchar_t));
                                    if (wname) {
                                        MultiByteToWideChar(CP_UTF8, 0,
                                            config.profiles[ji].name, -1, wname, wlen);
                                    }
                                    jl_names[ji] = wname;
                                }
                                wixen_jumplist_update(exe_path, jl_names, jl_count);
                                for (size_t ji = 0; ji < jl_count; ji++)
                                    free((void *)jl_names[ji]);
                                free(jl_names);
                            }
                        }
                    }
                } else {
                    wixen_config_free(&new_cfg);
                }
            }
        }

        /* Wait for messages or 16ms timeout (~60fps).
         * MsgWaitForMultipleObjects wakes on COM dispatches from
         * UIA threads — Sleep(1) would not. This prevents freezes
         * when NVDA calls GetSelection during frame processing. */
        MsgWaitForMultipleObjects(0, NULL, FALSE, 16, QS_ALLINPUT);
    }

    /* Ensure bg renderer thread handle is closed (no-op if already finalized). */
    wixen_bg_task_cleanup(&bg_task);

    /* BUG #36: Start a watchdog thread that force-kills after 3 seconds.
     * If PTY close or session save blocks, the watchdog ensures no zombie. */
    wixen_shutdown_begin(&g_shutdown);
    CreateThread(NULL, 0, exit_watchdog_thread, NULL, 0, NULL);

    /* Save session before exit */
    {
        WixenSessionState session;
        wixen_session_init(&session);
        size_t tab_count;
        const WixenTab *all_tabs = wixen_tabs_all(&tabs, &tab_count);
        for (size_t i = 0; i < tab_count; i++) {
            const char *cwd = (pane_state_count > i && pane_states[i].shell_integ.cwd)
                ? pane_states[i].shell_integ.cwd : "";
            /* Save the profile UUID, not program path. On restore, look up
             * the profile by UUID to get the current program. BUG #33 fix. */
            const WixenProfile *prof = wixen_config_default_profile(&config);
            const char *uuid = (prof && prof->uuid) ? prof->uuid : "";
            wixen_session_add_tab(&session, all_tabs[i].title, uuid, cwd);
        }
        char save_path[MAX_PATH];
        wixen_session_path_from_config(cfg_path, save_path, sizeof(save_path));
        wixen_session_save(&session, save_path);
        wixen_session_free(&session);
    }

    wixen_search_free(&search_engine);

    /* Clear taskbar jumplist on shutdown */
    wixen_jumplist_clear();

    /* P0.3 fix: Mark shutdown complete so the watchdog thread exits cleanly.
     * Normal exit now returns from wWinMain, allowing proper DLL cleanup,
     * sanitizer teardown, and profiler flushing. TerminateProcess is kept
     * only as the watchdog fallback for genuinely stuck shutdowns. */
    wixen_shutdown_mark_complete(&g_shutdown);

    /* BUG #30 fallback: If somehow we're not complete (should not happen),
     * force-kill to avoid the ExitProcess hang described in BUG #30. */
    if (!wixen_shutdown_is_complete(&g_shutdown)) {
        TerminateProcess(GetCurrentProcess(), 0);
        __assume(0);
    }

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
