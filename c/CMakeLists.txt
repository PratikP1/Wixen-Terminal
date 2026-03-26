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

# vcpkg toolchain — use environment variable or CMAKE_TOOLCHAIN_FILE
if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
elseif(NOT DEFINED VCPKG_ROOT)
    set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/../vcpkg" CACHE PATH "vcpkg root")
endif()

# Find vcpkg-installed packages
if(EXISTS "${VCPKG_ROOT}/installed/x64-windows")
    list(APPEND CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/x64-windows")
endif()

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
    src/core/path_util.c
    src/core/table_detect.c
    src/core/image.c
    src/core/shutdown.c
    src/core/bg_task.c
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
    # UAC manifest: asInvoker inherits parent's elevation.
    # When launched from elevated PowerShell, Wixen runs elevated too.
    # Without this, UIPI blocks our non-elevated window from taking
    # foreground over elevated apps, and NVDA can't interact with us.
    set_target_properties(wixen PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/src/wixen.manifest")
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
target_link_libraries(wixen_ui PUBLIC wixen_a11y)

# UIA SDK constants use __declspec(selectany) which can cause LNK2005
# when multiple .obj files include uiautomation.h in the same link unit.
# Add /FORCE:MULTIPLE to all test exes that link wixen_a11y.
if(MSVC)
    add_link_options(/FORCE:MULTIPLE)
endif()

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
        src/a11y/state.c
        src/a11y/text_boundaries.c
        src/a11y/frame_update.c
        src/a11y/child_fragment.c
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

add_executable(test_red_pty_close tests/test_red_pty_close.c)
target_link_libraries(test_red_pty_close wixen_pty)
add_test(NAME test_red_pty_close COMMAND test_red_pty_close)

add_executable(test_red_shell_path tests/test_red_shell_path.c)
target_link_libraries(test_red_shell_path wixen_config wixen_pty)
add_test(NAME test_red_shell_path COMMAND test_red_shell_path)

add_executable(test_red_profile_launch tests/test_red_profile_launch.c)
target_link_libraries(test_red_profile_launch wixen_config)
add_test(NAME test_red_profile_launch COMMAND test_red_profile_launch)

add_executable(test_red_keybinding_edit tests/test_red_keybinding_edit.c)
target_link_libraries(test_red_keybinding_edit wixen_config)
add_test(NAME test_red_keybinding_edit COMMAND test_red_keybinding_edit)

# Copilot-identified fixes (P1-P8)
add_executable(test_red_copilot_p1_focus tests/test_red_copilot_p1_focus.c)
target_link_libraries(test_red_copilot_p1_focus wixen_a11y)
add_test(NAME test_red_copilot_p1_focus COMMAND test_red_copilot_p1_focus)

add_executable(test_red_bg_init tests/test_red_bg_init.c)
target_link_libraries(test_red_bg_init wixen_render wixen_core)
add_test(NAME test_red_bg_init COMMAND test_red_bg_init)

add_executable(test_red_renderer_cleanup tests/test_red_renderer_cleanup.c)
target_link_libraries(test_red_renderer_cleanup wixen_render wixen_core)
add_test(NAME test_red_renderer_cleanup COMMAND test_red_renderer_cleanup)

add_executable(test_red_search_modal tests/test_red_search_modal.c)
target_link_libraries(test_red_search_modal wixen_search wixen_core wixen_vt)
add_test(NAME test_red_search_modal COMMAND test_red_search_modal)

add_executable(test_red_text_nav tests/test_red_text_nav.c)
target_link_libraries(test_red_text_nav wixen_a11y)
add_test(NAME test_red_text_nav COMMAND test_red_text_nav)

add_executable(test_red_com_child tests/test_red_com_child.c)
target_link_libraries(test_red_com_child wixen_a11y wixen_shell_integ)
add_test(NAME test_red_com_child COMMAND test_red_com_child)

add_executable(test_red_path_safety tests/test_red_path_safety.c)
target_link_libraries(test_red_path_safety wixen_core)
add_test(NAME test_red_path_safety COMMAND test_red_path_safety)

add_executable(test_red_findtext_large tests/test_red_findtext_large.c)
target_link_libraries(test_red_findtext_large wixen_a11y)
add_test(NAME test_red_findtext_large COMMAND test_red_findtext_large)

add_executable(test_red_ipc_wire tests/test_red_ipc_wire.c)
target_link_libraries(test_red_ipc_wire wixen_ipc)
add_test(NAME test_red_ipc_wire COMMAND test_red_ipc_wire)

add_executable(test_red_smart_focus tests/test_red_smart_focus.c)
target_link_libraries(test_red_smart_focus wixen_a11y wixen_shell_integ)
add_test(NAME test_red_smart_focus COMMAND test_red_smart_focus)

add_executable(test_red_bounding_rects tests/test_red_bounding_rects.c)
target_link_libraries(test_red_bounding_rects wixen_a11y)
add_test(NAME test_red_bounding_rects COMMAND test_red_bounding_rects)

add_executable(test_red_child_fragment tests/test_red_child_fragment.c)
target_link_libraries(test_red_child_fragment wixen_a11y wixen_shell_integ)
add_test(NAME test_red_child_fragment COMMAND test_red_child_fragment)

add_executable(test_red_copilot_p2_provider_nav tests/test_red_copilot_p2_provider_nav.c)
target_link_libraries(test_red_copilot_p2_provider_nav wixen_a11y wixen_shell_integ)
add_test(NAME test_red_copilot_p2_provider_nav COMMAND test_red_copilot_p2_provider_nav)

add_executable(test_red_copilot_p2_tree_nav tests/test_red_copilot_p2_tree_nav.c)
target_link_libraries(test_red_copilot_p2_tree_nav wixen_a11y)
add_test(NAME test_red_copilot_p2_tree_nav COMMAND test_red_copilot_p2_tree_nav)

add_executable(test_red_copilot_p5_text_provider tests/test_red_copilot_p5_text_provider.c)
target_link_libraries(test_red_copilot_p5_text_provider wixen_a11y)
add_test(NAME test_red_copilot_p5_text_provider COMMAND test_red_copilot_p5_text_provider)

add_executable(test_red_copilot_p6_dynamic_buffers tests/test_red_copilot_p6_dynamic_buffers.c)
target_link_libraries(test_red_copilot_p6_dynamic_buffers wixen_core)
add_test(NAME test_red_copilot_p6_dynamic_buffers COMMAND test_red_copilot_p6_dynamic_buffers)

add_executable(test_red_pty_spawn_profile tests/test_red_pty_spawn_profile.c)
target_link_libraries(test_red_pty_spawn_profile wixen_config wixen_pty)
add_test(NAME test_red_pty_spawn_profile COMMAND test_red_pty_spawn_profile)

add_executable(test_red_settings_integration tests/test_red_settings_integration.c)
target_link_libraries(test_red_settings_integration wixen_config)
add_test(NAME test_red_settings_integration COMMAND test_red_settings_integration)

add_executable(test_red_profile_uuid tests/test_red_profile_uuid.c)
target_link_libraries(test_red_profile_uuid wixen_config)
add_test(NAME test_red_profile_uuid COMMAND test_red_profile_uuid)

add_executable(test_red_alt_f4 tests/test_red_alt_f4.c)
target_link_libraries(test_red_alt_f4 wixen_core wixen_config)
add_test(NAME test_red_alt_f4 COMMAND test_red_alt_f4)

add_executable(test_red_visual_quality tests/test_red_visual_quality.c)
target_link_libraries(test_red_visual_quality wixen_ui wixen_config)
add_test(NAME test_red_visual_quality COMMAND test_red_visual_quality)

add_executable(test_red_keybinding_editor tests/test_red_keybinding_editor.c)
target_link_libraries(test_red_keybinding_editor wixen_ui wixen_config)
add_test(NAME test_red_keybinding_editor COMMAND test_red_keybinding_editor)

add_executable(test_red_window_lifecycle tests/test_red_window_lifecycle.c)
target_link_libraries(test_red_window_lifecycle wixen_core wixen_config)
add_test(NAME test_red_window_lifecycle COMMAND test_red_window_lifecycle)

add_executable(test_red_user_issues tests/test_red_user_issues.c)
target_link_libraries(test_red_user_issues wixen_ui wixen_config)
add_test(NAME test_red_user_issues COMMAND test_red_user_issues)

add_executable(test_red_provider_init tests/test_red_provider_init.c)
target_link_libraries(test_red_provider_init wixen_ui)
add_test(NAME test_red_provider_init COMMAND test_red_provider_init)

add_executable(test_red_nvda_issues tests/test_red_nvda_issues.c)
target_link_libraries(test_red_nvda_issues wixen_a11y wixen_core)
add_test(NAME test_red_nvda_issues COMMAND test_red_nvda_issues)

add_executable(test_red_ui_elements tests/test_red_ui_elements.c)
target_link_libraries(test_red_ui_elements wixen_ui)
add_test(NAME test_red_ui_elements COMMAND test_red_ui_elements)

add_executable(test_red_settings_visual tests/test_red_settings_visual.c)
target_link_libraries(test_red_settings_visual wixen_ui wixen_config)
add_test(NAME test_red_settings_visual COMMAND test_red_settings_visual)

add_executable(test_red_settings_tabs tests/test_red_settings_tabs.c)
target_link_libraries(test_red_settings_tabs wixen_ui wixen_config)
add_test(NAME test_red_settings_tabs COMMAND test_red_settings_tabs)

add_executable(test_red_lua_engine tests/test_red_lua_engine.c)
target_link_libraries(test_red_lua_engine wixen_config)
add_test(NAME test_red_lua_engine COMMAND test_red_lua_engine)

add_executable(test_red_shell_integ_lifecycle tests/test_red_shell_integ_lifecycle.c)
target_link_libraries(test_red_shell_integ_lifecycle wixen_core)
add_test(NAME test_red_shell_integ_lifecycle COMMAND test_red_shell_integ_lifecycle)

add_executable(test_red_hyperlink_lifecycle tests/test_red_hyperlink_lifecycle.c)
target_link_libraries(test_red_hyperlink_lifecycle wixen_core)
add_test(NAME test_red_hyperlink_lifecycle COMMAND test_red_hyperlink_lifecycle)

add_executable(test_red_a11y_tree_rebuild tests/test_red_a11y_tree_rebuild.c)
target_link_libraries(test_red_a11y_tree_rebuild wixen_a11y wixen_core)
add_test(NAME test_red_a11y_tree_rebuild COMMAND test_red_a11y_tree_rebuild)

add_executable(test_red_a11y_frame_loop tests/test_red_a11y_frame_loop.c)
target_link_libraries(test_red_a11y_frame_loop wixen_a11y wixen_core)
add_test(NAME test_red_a11y_frame_loop COMMAND test_red_a11y_frame_loop)

add_executable(test_red_underline_styles tests/test_red_underline_styles.c)
target_link_libraries(test_red_underline_styles wixen_core wixen_vt)
add_test(NAME test_red_underline_styles COMMAND test_red_underline_styles)

add_executable(test_red_origin_mode tests/test_red_origin_mode.c)
target_link_libraries(test_red_origin_mode wixen_core wixen_vt)
add_test(NAME test_red_origin_mode COMMAND test_red_origin_mode)

add_executable(test_red_save_restore tests/test_red_save_restore.c)
target_link_libraries(test_red_save_restore wixen_core wixen_vt)
add_test(NAME test_red_save_restore COMMAND test_red_save_restore)

add_executable(test_red_erase_ops tests/test_red_erase_ops.c)
target_link_libraries(test_red_erase_ops wixen_core wixen_vt)
add_test(NAME test_red_erase_ops COMMAND test_red_erase_ops)

add_executable(test_red_misc_sequences tests/test_red_misc_sequences.c)
target_link_libraries(test_red_misc_sequences wixen_core wixen_vt)
add_test(NAME test_red_misc_sequences COMMAND test_red_misc_sequences)

add_executable(test_red_device_attrs tests/test_red_device_attrs.c)
target_link_libraries(test_red_device_attrs wixen_core wixen_vt)
add_test(NAME test_red_device_attrs COMMAND test_red_device_attrs)

add_executable(test_red_ich_dch tests/test_red_ich_dch.c)
target_link_libraries(test_red_ich_dch wixen_core wixen_vt)
add_test(NAME test_red_ich_dch COMMAND test_red_ich_dch)

add_executable(test_red_paste_sync tests/test_red_paste_sync.c)
target_link_libraries(test_red_paste_sync wixen_core wixen_vt)
add_test(NAME test_red_paste_sync COMMAND test_red_paste_sync)

add_executable(test_red_alt_screen_full tests/test_red_alt_screen_full.c)
target_link_libraries(test_red_alt_screen_full wixen_core wixen_vt)
add_test(NAME test_red_alt_screen_full COMMAND test_red_alt_screen_full)

add_executable(test_red_sgr_colors tests/test_red_sgr_colors.c)
target_link_libraries(test_red_sgr_colors wixen_core wixen_vt)
add_test(NAME test_red_sgr_colors COMMAND test_red_sgr_colors)

add_executable(test_red_tab_stops tests/test_red_tab_stops.c)
target_link_libraries(test_red_tab_stops wixen_core wixen_vt)
add_test(NAME test_red_tab_stops COMMAND test_red_tab_stops)

add_executable(test_red_response_queue tests/test_red_response_queue.c)
target_link_libraries(test_red_response_queue wixen_core wixen_vt)
add_test(NAME test_red_response_queue COMMAND test_red_response_queue)

add_executable(test_red_scrollback_decompress tests/test_red_scrollback_decompress.c)
target_link_libraries(test_red_scrollback_decompress wixen_core)
add_test(NAME test_red_scrollback_decompress COMMAND test_red_scrollback_decompress)

add_executable(test_red_utf16_offset tests/test_red_utf16_offset.c)
target_link_libraries(test_red_utf16_offset wixen_a11y)
add_test(NAME test_red_utf16_offset COMMAND test_red_utf16_offset)

add_executable(test_red_text_provider tests/test_red_text_provider.c)
target_link_libraries(test_red_text_provider wixen_a11y)
add_test(NAME test_red_text_provider COMMAND test_red_text_provider)

add_executable(test_red_toml_edge tests/test_red_toml_edge.c)
target_link_libraries(test_red_toml_edge wixen_config)
add_test(NAME test_red_toml_edge COMMAND test_red_toml_edge)

add_executable(test_red_cwd_tracking tests/test_red_cwd_tracking.c)
target_link_libraries(test_red_cwd_tracking wixen_core wixen_vt)
add_test(NAME test_red_cwd_tracking COMMAND test_red_cwd_tracking)

add_executable(test_red_e2e_advanced tests/test_red_e2e_advanced.c)
target_link_libraries(test_red_e2e_advanced wixen_core wixen_vt wixen_a11y)
add_test(NAME test_red_e2e_advanced COMMAND test_red_e2e_advanced)

add_executable(test_red_config_watcher tests/test_red_config_watcher.c)
target_link_libraries(test_red_config_watcher wixen_config)
add_test(NAME test_red_config_watcher COMMAND test_red_config_watcher)

add_executable(test_red_watcher_stop tests/test_red_watcher_stop.c)
target_link_libraries(test_red_watcher_stop wixen_config)
add_test(NAME test_red_watcher_stop COMMAND test_red_watcher_stop)

add_executable(test_red_audio tests/test_red_audio.c)
target_link_libraries(test_red_audio wixen_ui)
add_test(NAME test_red_audio COMMAND test_red_audio)

add_executable(test_red_mouse_encoding tests/test_red_mouse_encoding.c)
target_link_libraries(test_red_mouse_encoding wixen_core)
add_test(NAME test_red_mouse_encoding COMMAND test_red_mouse_encoding)

add_executable(test_red_tab_lifecycle tests/test_red_tab_lifecycle.c)
target_link_libraries(test_red_tab_lifecycle wixen_ui)
add_test(NAME test_red_tab_lifecycle COMMAND test_red_tab_lifecycle)

add_executable(test_red_pane_tree tests/test_red_pane_tree.c)
target_link_libraries(test_red_pane_tree wixen_ui)
add_test(NAME test_red_pane_tree COMMAND test_red_pane_tree)

add_executable(test_red_selection_wide tests/test_red_selection_wide.c)
target_link_libraries(test_red_selection_wide wixen_core wixen_vt)
add_test(NAME test_red_selection_wide COMMAND test_red_selection_wide)

add_executable(test_red_prompt_strip tests/test_red_prompt_strip.c)
target_link_libraries(test_red_prompt_strip wixen_a11y)
add_test(NAME test_red_prompt_strip COMMAND test_red_prompt_strip)

add_executable(test_red_text_consistency tests/test_red_text_consistency.c)
target_link_libraries(test_red_text_consistency wixen_core wixen_vt)
add_test(NAME test_red_text_consistency COMMAND test_red_text_consistency)

add_executable(test_red_key_encoding tests/test_red_key_encoding.c)
target_link_libraries(test_red_key_encoding wixen_core)
add_test(NAME test_red_key_encoding COMMAND test_red_key_encoding)

add_executable(test_red_parser_recovery tests/test_red_parser_recovery.c)
target_link_libraries(test_red_parser_recovery wixen_core wixen_vt)
add_test(NAME test_red_parser_recovery COMMAND test_red_parser_recovery)

add_executable(test_red_threads tests/test_red_threads.c)
target_link_libraries(test_red_threads wixen_a11y)
add_test(NAME test_red_threads COMMAND test_red_threads)

add_executable(test_red_leaks tests/test_red_leaks.c)
target_link_libraries(test_red_leaks wixen_core wixen_vt wixen_a11y wixen_search wixen_config)
add_test(NAME test_red_leaks COMMAND test_red_leaks)

add_executable(test_red_fuzz tests/test_red_fuzz.c)
target_link_libraries(test_red_fuzz wixen_core wixen_vt)
add_test(NAME test_red_fuzz COMMAND test_red_fuzz)

add_executable(test_red_overflow tests/test_red_overflow.c)
target_link_libraries(test_red_overflow wixen_core wixen_vt wixen_a11y)
add_test(NAME test_red_overflow COMMAND test_red_overflow)

add_executable(test_red_null_safety tests/test_red_null_safety.c)
target_link_libraries(test_red_null_safety wixen_core wixen_vt wixen_a11y wixen_search wixen_config)
add_test(NAME test_red_null_safety COMMAND test_red_null_safety)

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

add_executable(test_red_wm_getobject tests/test_red_wm_getobject.c)
target_link_libraries(test_red_wm_getobject wixen_ui wixen_a11y)
target_link_options(test_red_wm_getobject PRIVATE /FORCE:MULTIPLE)
add_test(NAME test_red_wm_getobject COMMAND test_red_wm_getobject)

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

add_executable(test_red_shutdown tests/test_red_shutdown.c)
target_link_libraries(test_red_shutdown wixen_core)
add_test(NAME test_red_shutdown COMMAND test_red_shutdown)

add_executable(test_red_url_safety tests/test_red_url_safety.c)
target_link_libraries(test_red_url_safety wixen_core)
add_test(NAME test_red_url_safety COMMAND test_red_url_safety)

add_executable(test_red_pty_post tests/test_red_pty_post.c)
target_link_libraries(test_red_pty_post wixen_pty)
add_test(NAME test_red_pty_post COMMAND test_red_pty_post)

add_executable(test_red_bg_task tests/test_red_bg_task.c)
target_link_libraries(test_red_bg_task wixen_core)
add_test(NAME test_red_bg_task COMMAND test_red_bg_task)

add_executable(test_red_a11y_state_lifecycle tests/test_red_a11y_state_lifecycle.c)
target_link_libraries(test_red_a11y_state_lifecycle wixen_a11y)
add_test(NAME test_red_a11y_state_lifecycle COMMAND test_red_a11y_state_lifecycle)

add_executable(test_red_a11y_perf_contract tests/test_red_a11y_perf_contract.c)
target_link_libraries(test_red_a11y_perf_contract wixen_core wixen_a11y)
add_test(NAME test_red_a11y_perf_contract COMMAND test_red_a11y_perf_contract)

add_executable(test_red_long_line tests/test_red_long_line.c)
target_link_libraries(test_red_long_line wixen_core)
add_test(NAME test_red_long_line COMMAND test_red_long_line)

add_executable(test_red_feature_contracts tests/test_red_feature_contracts.c)
target_link_libraries(test_red_feature_contracts wixen_core wixen_vt wixen_search wixen_shell_integ wixen_config wixen_ui)
add_test(NAME test_red_feature_contracts COMMAND test_red_feature_contracts)

add_executable(test_red_a11y_concurrency tests/test_red_a11y_concurrency.c)
target_link_libraries(test_red_a11y_concurrency wixen_a11y)
add_test(NAME test_red_a11y_concurrency COMMAND test_red_a11y_concurrency)
