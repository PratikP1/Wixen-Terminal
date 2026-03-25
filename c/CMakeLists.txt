cmake_minimum_required(VERSION 3.20)
project(wixen-terminal C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Unicode by default on Windows
if(WIN32)
    add_compile_definitions(UNICODE _UNICODE)
endif()

# Strict warnings
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Suppress C4127 (conditional expression is constant) from greatest.h macros
    add_compile_options(/wd4127)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# vcpkg toolchain
set(VCPKG_ROOT "C:/Users/prati/Documents/projects/vcpkg")

# Find vcpkg-installed packages
list(APPEND CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/x64-windows")

# Include paths
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find dependencies
find_package(zstd CONFIG REQUIRED)
find_package(pcre2 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(cJSON CONFIG REQUIRED)

# --- Core library ---
add_library(wixen_core STATIC
    src/core/cell.c
    src/core/cursor.c
    src/core/grid.c
    src/core/modes.c
    src/core/selection.c
    src/core/terminal.c
    src/core/buffer.c
    src/core/error_detect.c
    src/core/url.c
    src/core/sixel.c
    src/core/keyboard.c
    src/core/xmalloc.c
    src/core/mouse.c
    src/core/hyperlink.c
    src/core/table_detect.c
    src/core/image.c
)
target_link_libraries(wixen_core PUBLIC zstd::libzstd wixen_shell_integ)

# --- Test framework (greatest.h) ---
include_directories(${CMAKE_SOURCE_DIR}/tests)

# --- Tests ---
# --- Main executable ---
if(WIN32)
    add_executable(wixen WIN32 src/main.c)
    target_link_libraries(wixen wixen_core wixen_vt wixen_pty wixen_render wixen_ui
                          wixen_shell_integ wixen_search wixen_config wixen_a11y
                          wixen_ipc)
    # UIA constants are __declspec(selectany) but MSVC still complains across TUs
    target_link_options(wixen PRIVATE /FORCE:MULTIPLE)
endif()

enable_testing()

add_executable(test_scaffolding tests/test_scaffolding.c)
add_test(NAME test_scaffolding COMMAND test_scaffolding)

add_executable(test_cell tests/test_cell.c)
target_link_libraries(test_cell wixen_core)
add_test(NAME test_cell COMMAND test_cell)

add_executable(test_cursor tests/test_cursor.c)
target_link_libraries(test_cursor wixen_core)
add_test(NAME test_cursor COMMAND test_cursor)

add_executable(test_grid tests/test_grid.c)
target_link_libraries(test_grid wixen_core)
add_test(NAME test_grid COMMAND test_grid)

add_executable(test_modes tests/test_modes.c)
target_link_libraries(test_modes wixen_core)
add_test(NAME test_modes COMMAND test_modes)

add_executable(test_selection tests/test_selection.c)
target_link_libraries(test_selection wixen_core)
add_test(NAME test_selection COMMAND test_selection)

# --- VT Parser library ---
add_library(wixen_vt STATIC
    src/vt/parser.c
)

add_executable(test_parser tests/test_parser.c)
target_link_libraries(test_parser wixen_vt)
add_test(NAME test_parser COMMAND test_parser)

add_executable(test_terminal tests/test_terminal.c)
target_link_libraries(test_terminal wixen_core wixen_vt)
add_test(NAME test_terminal COMMAND test_terminal)

add_executable(test_buffer tests/test_buffer.c)
target_link_libraries(test_buffer wixen_core)
add_test(NAME test_buffer COMMAND test_buffer)

# --- PTY library (Win32 only) ---
if(WIN32)
    add_library(wixen_pty STATIC
        src/pty/pty.c
    )
endif()

# --- Shell integration library ---
add_library(wixen_shell_integ STATIC
    src/shell_integ/shell_integ.c
    src/shell_integ/heuristic.c
)

add_executable(test_shell_integ tests/test_shell_integ.c)
target_link_libraries(test_shell_integ wixen_shell_integ)
add_test(NAME test_shell_integ COMMAND test_shell_integ)

# --- Search library ---
add_library(wixen_search STATIC
    src/search/search.c
)

target_link_libraries(wixen_search PUBLIC PCRE2::8BIT)

add_executable(test_search tests/test_search.c)
target_link_libraries(test_search wixen_search)
add_test(NAME test_search COMMAND test_search)

# --- UI library ---
add_library(wixen_ui STATIC
    src/ui/tabs.c
    src/ui/panes.c
    src/ui/history.c
    $<$<PLATFORM_ID:Windows>:src/ui/window.c>
    $<$<PLATFORM_ID:Windows>:src/ui/settings_dialog.c>
    $<$<PLATFORM_ID:Windows>:src/ui/palette_dialog.c>
    $<$<PLATFORM_ID:Windows>:src/ui/tray.c>
    src/ui/audio.c
    $<$<PLATFORM_ID:Windows>:src/ui/clipboard.c>
    $<$<PLATFORM_ID:Windows>:src/ui/explorer_menu.c>
    $<$<PLATFORM_ID:Windows>:src/ui/jumplist.c>
    $<$<PLATFORM_ID:Windows>:src/ui/default_terminal.c>
)

add_executable(test_tabs tests/test_tabs.c)
target_link_libraries(test_tabs wixen_ui)
add_test(NAME test_tabs COMMAND test_tabs)

add_executable(test_panes tests/test_panes.c)
target_link_libraries(test_panes wixen_ui)
add_test(NAME test_panes COMMAND test_panes)

add_executable(test_history tests/test_history.c)
target_link_libraries(test_history wixen_ui)
add_test(NAME test_history COMMAND test_history)

# --- Accessibility library (Win32 only) ---
if(WIN32)
    add_library(wixen_a11y STATIC
        src/a11y/provider.c
        src/a11y/text_provider.c
        src/a11y/events.c
        src/a11y/tree.c
    )
endif()

# --- Render library ---
add_library(wixen_render STATIC
    src/render/colors.c
    src/render/pipeline.c
    $<$<PLATFORM_ID:Windows>:src/render/renderer.c>
    $<$<PLATFORM_ID:Windows>:src/render/atlas.cpp>
    $<$<PLATFORM_ID:Windows>:src/render/software.c>
)

add_executable(test_colors tests/test_colors.c)
target_link_libraries(test_colors wixen_render)
add_test(NAME test_colors COMMAND test_colors)

# --- Config library ---
add_library(wixen_config STATIC
    src/config/keybindings.c
    src/config/config.c
    src/config/lua_engine.c
    src/config/session.c
    src/config/ssh.c
    src/config/wsl.c
    src/config/serial.c
    $<$<PLATFORM_ID:Windows>:src/config/watcher.c>
)
target_include_directories(wixen_config PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(wixen_config PUBLIC ${LUA_LIBRARIES} cjson)

# --- IPC library (Win32 only) ---
if(WIN32)
    add_library(wixen_ipc STATIC
        src/ipc/ipc.c
    )
endif()

add_executable(test_keybindings tests/test_keybindings.c)
target_link_libraries(test_keybindings wixen_config)
add_test(NAME test_keybindings COMMAND test_keybindings)

add_executable(test_error_detect tests/test_error_detect.c)
target_link_libraries(test_error_detect wixen_core)
add_test(NAME test_error_detect COMMAND test_error_detect)

add_executable(test_url tests/test_url.c)
target_link_libraries(test_url wixen_core)
add_test(NAME test_url COMMAND test_url)

add_executable(test_keyboard tests/test_keyboard.c)
target_link_libraries(test_keyboard wixen_core)
add_test(NAME test_keyboard COMMAND test_keyboard)

add_executable(test_heuristic tests/test_heuristic.c)
target_link_libraries(test_heuristic wixen_shell_integ)
add_test(NAME test_heuristic COMMAND test_heuristic)

add_executable(test_config tests/test_config.c)
target_link_libraries(test_config wixen_config)
add_test(NAME test_config COMMAND test_config)

add_executable(test_integration tests/test_integration.c)
target_link_libraries(test_integration wixen_core wixen_vt)
add_test(NAME test_integration COMMAND test_integration)

add_executable(test_events tests/test_events.c)
target_link_libraries(test_events wixen_a11y)
add_test(NAME test_events COMMAND test_events)

add_executable(test_audio tests/test_audio.c)
target_link_libraries(test_audio wixen_ui)
add_test(NAME test_audio COMMAND test_audio)

add_executable(test_lua tests/test_lua.c)
target_link_libraries(test_lua wixen_config)
add_test(NAME test_lua COMMAND test_lua)

add_executable(test_pipeline tests/test_pipeline.c)
target_link_libraries(test_pipeline wixen_render wixen_core)
add_test(NAME test_pipeline COMMAND test_pipeline)

add_executable(test_hyperlink tests/test_hyperlink.c)
target_link_libraries(test_hyperlink wixen_core)
add_test(NAME test_hyperlink COMMAND test_hyperlink)

add_executable(test_table_detect tests/test_table_detect.c)
target_link_libraries(test_table_detect wixen_core)
add_test(NAME test_table_detect COMMAND test_table_detect)

add_executable(test_a11y_tree tests/test_a11y_tree.c)
target_link_libraries(test_a11y_tree wixen_a11y)
add_test(NAME test_a11y_tree COMMAND test_a11y_tree)

add_executable(test_session tests/test_session.c)
target_link_libraries(test_session wixen_config)
add_test(NAME test_session COMMAND test_session)

add_executable(test_image tests/test_image.c)
target_link_libraries(test_image wixen_core)
add_test(NAME test_image COMMAND test_image)

add_executable(test_ssh tests/test_ssh.c)
target_link_libraries(test_ssh wixen_config)
add_test(NAME test_ssh COMMAND test_ssh)

add_executable(test_wsl tests/test_wsl.c)
target_link_libraries(test_wsl wixen_config)
add_test(NAME test_wsl COMMAND test_wsl)

add_executable(test_serial tests/test_serial.c)
target_link_libraries(test_serial wixen_config)
add_test(NAME test_serial COMMAND test_serial)

add_executable(test_config_extended tests/test_config_extended.c)
target_link_libraries(test_config_extended wixen_config)
add_test(NAME test_config_extended COMMAND test_config_extended)

add_executable(test_stress_minimal tests/test_stress_minimal.c)
target_link_libraries(test_stress_minimal wixen_core wixen_vt)
add_test(NAME test_stress_minimal COMMAND test_stress_minimal)

add_executable(test_grid_visible_text tests/test_grid_visible_text.c)
target_link_libraries(test_grid_visible_text wixen_core)
add_test(NAME test_grid_visible_text COMMAND test_grid_visible_text)

add_executable(test_terminal_scroll tests/test_terminal_scroll.c)
target_link_libraries(test_terminal_scroll wixen_core wixen_vt)
add_test(NAME test_terminal_scroll COMMAND test_terminal_scroll)

add_executable(test_red_reset_cleanup tests/test_red_reset_cleanup.c)
target_link_libraries(test_red_reset_cleanup wixen_core wixen_vt)
add_test(NAME test_red_reset_cleanup COMMAND test_red_reset_cleanup)

add_executable(test_red_terminal_reflow tests/test_red_terminal_reflow.c)
target_link_libraries(test_red_terminal_reflow wixen_core wixen_vt)
add_test(NAME test_red_terminal_reflow COMMAND test_red_terminal_reflow)

add_executable(test_red_hyperlink_open tests/test_red_hyperlink_open.c)
target_link_libraries(test_red_hyperlink_open wixen_core wixen_vt)
add_test(NAME test_red_hyperlink_open COMMAND test_red_hyperlink_open)

add_executable(test_red_select_all tests/test_red_select_all.c)
target_link_libraries(test_red_select_all wixen_core wixen_vt)
add_test(NAME test_red_select_all COMMAND test_red_select_all)

add_executable(test_red_dpi tests/test_red_dpi.c)
target_link_libraries(test_red_dpi wixen_render wixen_core)
add_test(NAME test_red_dpi COMMAND test_red_dpi)

add_executable(test_red_config_reload tests/test_red_config_reload.c)
target_link_libraries(test_red_config_reload wixen_config)
add_test(NAME test_red_config_reload COMMAND test_red_config_reload)

add_executable(test_red_sixel tests/test_red_sixel.c)
target_link_libraries(test_red_sixel wixen_core)
add_test(NAME test_red_sixel COMMAND test_red_sixel)

add_executable(test_red_grid_lines tests/test_red_grid_lines.c)
target_link_libraries(test_red_grid_lines wixen_core wixen_vt)
add_test(NAME test_red_grid_lines COMMAND test_red_grid_lines)

add_executable(test_red_lua_config tests/test_red_lua_config.c)
target_link_libraries(test_red_lua_config wixen_config)
add_test(NAME test_red_lua_config COMMAND test_red_lua_config)

add_executable(test_red_clipboard tests/test_red_clipboard.c)
target_link_libraries(test_red_clipboard wixen_core wixen_vt)
add_test(NAME test_red_clipboard COMMAND test_red_clipboard)

add_executable(test_red_ipc tests/test_red_ipc.c)
target_link_libraries(test_red_ipc wixen_ipc)
add_test(NAME test_red_ipc COMMAND test_red_ipc)

add_executable(test_red_a11y_events tests/test_red_a11y_events.c)
target_link_libraries(test_red_a11y_events wixen_a11y)
add_test(NAME test_red_a11y_events COMMAND test_red_a11y_events)

add_executable(test_red_text_extract tests/test_red_text_extract.c)
target_link_libraries(test_red_text_extract wixen_core wixen_vt)
add_test(NAME test_red_text_extract COMMAND test_red_text_extract)

add_executable(test_red_echo_timeout tests/test_red_echo_timeout.c)
target_link_libraries(test_red_echo_timeout wixen_core wixen_vt)
add_test(NAME test_red_echo_timeout COMMAND test_red_echo_timeout)

add_executable(test_red_prompt_jump tests/test_red_prompt_jump.c)
target_link_libraries(test_red_prompt_jump wixen_core wixen_vt)
add_test(NAME test_red_prompt_jump COMMAND test_red_prompt_jump)

add_executable(test_red_reduced_motion tests/test_red_reduced_motion.c)
target_link_libraries(test_red_reduced_motion wixen_config)
add_test(NAME test_red_reduced_motion COMMAND test_red_reduced_motion)

add_executable(test_red_viewport tests/test_red_viewport.c)
target_link_libraries(test_red_viewport wixen_core wixen_vt)
add_test(NAME test_red_viewport COMMAND test_red_viewport)

add_executable(test_red_shipped_debt tests/test_red_shipped_debt.c)
target_link_libraries(test_red_shipped_debt wixen_core wixen_vt wixen_config)
add_test(NAME test_red_shipped_debt COMMAND test_red_shipped_debt)

add_executable(test_red_mouse_select tests/test_red_mouse_select.c)
target_link_libraries(test_red_mouse_select wixen_core)
add_test(NAME test_red_mouse_select COMMAND test_red_mouse_select)

add_executable(test_red_c_safety tests/test_red_c_safety.c)
target_link_libraries(test_red_c_safety wixen_core wixen_vt wixen_search)
add_test(NAME test_red_c_safety COMMAND test_red_c_safety)

add_executable(test_red_session_hyperlink tests/test_red_session_hyperlink.c)
target_link_libraries(test_red_session_hyperlink wixen_config wixen_core)
add_test(NAME test_red_session_hyperlink COMMAND test_red_session_hyperlink)

add_executable(test_red_keybinding_chords tests/test_red_keybinding_chords.c)
target_link_libraries(test_red_keybinding_chords wixen_config)
add_test(NAME test_red_keybinding_chords COMMAND test_red_keybinding_chords)

add_executable(test_red_a11y_consistency tests/test_red_a11y_consistency.c)
target_link_libraries(test_red_a11y_consistency wixen_core wixen_vt wixen_a11y)
add_test(NAME test_red_a11y_consistency COMMAND test_red_a11y_consistency)

add_executable(test_red_e2e tests/test_red_e2e.c)
target_link_libraries(test_red_e2e wixen_core wixen_vt)
add_test(NAME test_red_e2e COMMAND test_red_e2e)

add_executable(test_red_pipeline tests/test_red_pipeline.c)
target_link_libraries(test_red_pipeline wixen_render wixen_core)
add_test(NAME test_red_pipeline COMMAND test_red_pipeline)

add_executable(test_red_resize_interaction tests/test_red_resize_interaction.c)
target_link_libraries(test_red_resize_interaction wixen_core wixen_vt)
add_test(NAME test_red_resize_interaction COMMAND test_red_resize_interaction)

add_executable(test_red_shell_deep tests/test_red_shell_deep.c)
target_link_libraries(test_red_shell_deep wixen_shell_integ)
add_test(NAME test_red_shell_deep COMMAND test_red_shell_deep)

add_executable(test_red_config_roundtrip tests/test_red_config_roundtrip.c)
target_link_libraries(test_red_config_roundtrip wixen_config)
add_test(NAME test_red_config_roundtrip COMMAND test_red_config_roundtrip)

add_executable(test_red_parser_stress tests/test_red_parser_stress.c)
target_link_libraries(test_red_parser_stress wixen_core wixen_vt)
add_test(NAME test_red_parser_stress COMMAND test_red_parser_stress)

add_executable(test_red_deep tests/test_red_deep.c)
target_link_libraries(test_red_deep wixen_core wixen_vt)
add_test(NAME test_red_deep COMMAND test_red_deep)

add_executable(test_red_sync_output tests/test_red_sync_output.c)
target_link_libraries(test_red_sync_output wixen_core wixen_vt)
add_test(NAME test_red_sync_output COMMAND test_red_sync_output)

add_executable(test_red_osc52 tests/test_red_osc52.c)
target_link_libraries(test_red_osc52 wixen_core wixen_vt)
add_test(NAME test_red_osc52 COMMAND test_red_osc52)

add_executable(test_red_cursor_shape tests/test_red_cursor_shape.c)
target_link_libraries(test_red_cursor_shape wixen_core wixen_vt)
add_test(NAME test_red_cursor_shape COMMAND test_red_cursor_shape)

add_executable(test_red_sgr_subparams tests/test_red_sgr_subparams.c)
target_link_libraries(test_red_sgr_subparams wixen_core wixen_vt)
add_test(NAME test_red_sgr_subparams COMMAND test_red_sgr_subparams)

add_executable(test_red_reflow tests/test_red_reflow.c)
target_link_libraries(test_red_reflow wixen_core)
add_test(NAME test_red_reflow COMMAND test_red_reflow)

add_executable(test_red_mouse_modes tests/test_red_mouse_modes.c)
target_link_libraries(test_red_mouse_modes wixen_core wixen_vt)
add_test(NAME test_red_mouse_modes COMMAND test_red_mouse_modes)

add_executable(test_red_window_ops tests/test_red_window_ops.c)
target_link_libraries(test_red_window_ops wixen_core wixen_vt)
add_test(NAME test_red_window_ops COMMAND test_red_window_ops)

add_executable(test_red_terminal_responses tests/test_red_terminal_responses.c)
target_link_libraries(test_red_terminal_responses wixen_core wixen_vt)
add_test(NAME test_red_terminal_responses COMMAND test_red_terminal_responses)

add_executable(test_red_harder tests/test_red_harder.c)
target_link_libraries(test_red_harder wixen_core wixen_vt)
add_test(NAME test_red_harder COMMAND test_red_harder)

add_executable(test_red_edge_cases tests/test_red_edge_cases.c)
target_link_libraries(test_red_edge_cases wixen_core wixen_vt wixen_search wixen_config wixen_shell_integ)
add_test(NAME test_red_edge_cases COMMAND test_red_edge_cases)

add_executable(test_ipc tests/test_ipc.c)
target_link_libraries(test_ipc wixen_ipc)
add_test(NAME test_ipc COMMAND test_ipc)

add_executable(test_clipboard tests/test_clipboard.c)
target_link_libraries(test_clipboard wixen_ui)
add_test(NAME test_clipboard COMMAND test_clipboard)

add_executable(test_terminal_attrs tests/test_terminal_attrs.c)
target_link_libraries(test_terminal_attrs wixen_core wixen_vt)
add_test(NAME test_terminal_attrs COMMAND test_terminal_attrs)

add_executable(test_selection_text tests/test_selection_text.c)
target_link_libraries(test_selection_text wixen_core)
add_test(NAME test_selection_text COMMAND test_selection_text)

add_executable(test_pane_zoom tests/test_pane_zoom.c)
target_link_libraries(test_pane_zoom wixen_ui)
add_test(NAME test_pane_zoom COMMAND test_pane_zoom)

add_executable(test_vt_escapes tests/test_vt_escapes.c)
target_link_libraries(test_vt_escapes wixen_a11y)
add_test(NAME test_vt_escapes COMMAND test_vt_escapes)

add_executable(test_scrollback tests/test_scrollback.c)
target_link_libraries(test_scrollback wixen_core)
add_test(NAME test_scrollback COMMAND test_scrollback)

add_executable(test_terminal_integration tests/test_terminal_integration.c)
target_link_libraries(test_terminal_integration wixen_core wixen_vt)
add_test(NAME test_terminal_integration COMMAND test_terminal_integration)

add_executable(test_parser_osc tests/test_parser_osc.c)
target_link_libraries(test_parser_osc wixen_vt)
add_test(NAME test_parser_osc COMMAND test_parser_osc)

add_executable(test_mouse tests/test_mouse.c)
target_link_libraries(test_mouse wixen_core)
add_test(NAME test_mouse COMMAND test_mouse)

add_executable(test_terminal_misc tests/test_terminal_misc.c)
target_link_libraries(test_terminal_misc wixen_core wixen_vt)
add_test(NAME test_terminal_misc COMMAND test_terminal_misc)

add_executable(test_terminal_tabstops tests/test_terminal_tabstops.c)
target_link_libraries(test_terminal_tabstops wixen_core wixen_vt)
add_test(NAME test_terminal_tabstops COMMAND test_terminal_tabstops)

add_executable(test_terminal_dsr tests/test_terminal_dsr.c)
target_link_libraries(test_terminal_dsr wixen_core wixen_vt)
add_test(NAME test_terminal_dsr COMMAND test_terminal_dsr)

add_executable(test_keybindings_extended tests/test_keybindings_extended.c)
target_link_libraries(test_keybindings_extended wixen_config)
add_test(NAME test_keybindings_extended COMMAND test_keybindings_extended)

add_executable(test_cell_extended tests/test_cell_extended.c)
target_link_libraries(test_cell_extended wixen_core)
add_test(NAME test_cell_extended COMMAND test_cell_extended)

add_executable(test_shell_integ_extended tests/test_shell_integ_extended.c)
target_link_libraries(test_shell_integ_extended wixen_shell_integ)
add_test(NAME test_shell_integ_extended COMMAND test_shell_integ_extended)

add_executable(test_lua_extended tests/test_lua_extended.c)
target_link_libraries(test_lua_extended wixen_config)
add_test(NAME test_lua_extended COMMAND test_lua_extended)

add_executable(test_terminal_write tests/test_terminal_write.c)
target_link_libraries(test_terminal_write wixen_core wixen_vt)
add_test(NAME test_terminal_write COMMAND test_terminal_write)

add_executable(test_parser_csi tests/test_parser_csi.c)
target_link_libraries(test_parser_csi wixen_vt)
add_test(NAME test_parser_csi COMMAND test_parser_csi)

add_executable(test_colors_extended tests/test_colors_extended.c)
target_link_libraries(test_colors_extended wixen_render)
add_test(NAME test_colors_extended COMMAND test_colors_extended)

add_executable(test_terminal_utf8 tests/test_terminal_utf8.c)
target_link_libraries(test_terminal_utf8 wixen_core wixen_vt)
add_test(NAME test_terminal_utf8 COMMAND test_terminal_utf8)

add_executable(test_panes_extended tests/test_panes_extended.c)
target_link_libraries(test_panes_extended wixen_ui)
add_test(NAME test_panes_extended COMMAND test_panes_extended)

add_executable(test_tabs_extended tests/test_tabs_extended.c)
target_link_libraries(test_tabs_extended wixen_ui)
add_test(NAME test_tabs_extended COMMAND test_tabs_extended)

add_executable(test_terminal_sgr tests/test_terminal_sgr.c)
target_link_libraries(test_terminal_sgr wixen_core wixen_vt)
add_test(NAME test_terminal_sgr COMMAND test_terminal_sgr)

add_executable(test_terminal_scroll_region tests/test_terminal_scroll_region.c)
target_link_libraries(test_terminal_scroll_region wixen_core wixen_vt)
add_test(NAME test_terminal_scroll_region COMMAND test_terminal_scroll_region)

add_executable(test_terminal_cursor tests/test_terminal_cursor.c)
target_link_libraries(test_terminal_cursor wixen_core wixen_vt)
add_test(NAME test_terminal_cursor COMMAND test_terminal_cursor)

add_executable(test_terminal_erase tests/test_terminal_erase.c)
target_link_libraries(test_terminal_erase wixen_core wixen_vt)
add_test(NAME test_terminal_erase COMMAND test_terminal_erase)

add_executable(test_terminal_modes tests/test_terminal_modes.c)
target_link_libraries(test_terminal_modes wixen_core wixen_vt)
add_test(NAME test_terminal_modes COMMAND test_terminal_modes)

add_executable(test_keyboard_extended tests/test_keyboard_extended.c)
target_link_libraries(test_keyboard_extended wixen_core)
add_test(NAME test_keyboard_extended COMMAND test_keyboard_extended)

add_executable(test_events_extended tests/test_events_extended.c)
target_link_libraries(test_events_extended wixen_a11y)
add_test(NAME test_events_extended COMMAND test_events_extended)

add_executable(test_url_extended tests/test_url_extended.c)
target_link_libraries(test_url_extended wixen_core)
add_test(NAME test_url_extended COMMAND test_url_extended)

add_executable(test_error_detect_extended tests/test_error_detect_extended.c)
target_link_libraries(test_error_detect_extended wixen_core)
add_test(NAME test_error_detect_extended COMMAND test_error_detect_extended)

add_executable(test_search_extended tests/test_search_extended.c)
target_link_libraries(test_search_extended wixen_search)
add_test(NAME test_search_extended COMMAND test_search_extended)

add_executable(test_selection_extended tests/test_selection_extended.c)
target_link_libraries(test_selection_extended wixen_core)
add_test(NAME test_selection_extended COMMAND test_selection_extended)

add_executable(test_buffer_extended tests/test_buffer_extended.c)
target_link_libraries(test_buffer_extended wixen_core)
add_test(NAME test_buffer_extended COMMAND test_buffer_extended)

add_executable(test_parser_extended tests/test_parser_extended.c)
target_link_libraries(test_parser_extended wixen_vt)
add_test(NAME test_parser_extended COMMAND test_parser_extended)

add_executable(test_grid_extended tests/test_grid_extended.c)
target_link_libraries(test_grid_extended wixen_core)
add_test(NAME test_grid_extended COMMAND test_grid_extended)

add_executable(test_terminal_extended tests/test_terminal_extended.c)
target_link_libraries(test_terminal_extended wixen_core wixen_vt)
add_test(NAME test_terminal_extended COMMAND test_terminal_extended)

add_executable(test_stress tests/test_stress.c)
target_link_libraries(test_stress wixen_core wixen_vt)
add_test(NAME test_stress COMMAND test_stress)
