# Wixen Terminal C Port — Implementation Log

## Project Overview
- **Language**: C17
- **Build system**: CMake + vcpkg
- **Compiler**: MSVC (Visual Studio 2022)
- **Dependencies**: zstd 1.5.7, pcre2 10.47, lua 5.5.0, cjson 1.7.19
- **Target**: Windows 10 1903+ / Windows 11

## Status
- **Source files**: 100
- **Test files**: 75 suites, 900 individual tests
- **Total LOC**: ~24,700
- **TODOs**: 0
- **Compiler warnings**: 0
- **Test pass rate**: 100%

---

## Phase 1: Project Scaffolding
- CMakeLists.txt with vcpkg integration
- Directory structure: src/, include/, tests/, config/, shaders/
- greatest.h test framework
- CI-ready (CTest)
- First test: grid init

## Phase 2: Core Data Structures
- `WixenCell` — content (UTF-8), attributes (bold/italic/underline/etc), color (default/indexed/RGB), width
- `WixenGrid` — 2D cell array, cursor, resize, clone, visible text extraction
- `WixenRow` — cell array with wrap flag, text extraction with trim
- `WixenCellAttributes` — full SGR attribute set
- `WixenColor` — default/indexed(0-255)/truecolor(RGB)
- `WixenCursor` — row, col, shape
- `WixenSelection` — normal/block/line modes, contains, ordered, row count
- `WixenTerminalModes` — all DEC/ANSI modes (DECAWM, DECTCEM, DECCKM, IRM, LNM, origin, bracketed paste, focus reporting, mouse tracking, SGR mouse, synchronized output, reverse video)

## Phase 3: VT Parser
- DEC VT500 state machine (Ground, Escape, EscapeIntermediate, CsiEntry, CsiParam, CsiIntermediate, CsiIgnore, OscString, DcsEntry, DcsParam, DcsIntermediate, DcsPassthrough, DcsIgnore)
- Action types: Print, Execute, CsiDispatch, EscDispatch, OscDispatch, DcsHook/Put/Unhook, ApcDispatch
- Handles split-across-chunks (stateful)
- Up to 32 params, 8 subparams per param, 4 intermediates
- OSC payload up to 4KB, DCS up to 64KB, APC up to 4MB (Kitty graphics)

## Phase 4: Terminal State Machine
- Character printing with East Asian width detection (CJK, Hangul, fullwidth)
- Cursor movement: CUP, CUU, CUD, CUF, CUB, CNL, CPL, CHA, VPA, HVP
- Erase operations: ED (0/1/2/3), EL (0/1/2), ECH
- Line operations: IL, DL, ICH, DCH
- Scroll: SU, SD, within regions
- Scroll regions: DECSTBM (set/reset)
- Modes: all toggled via CSI ?h/l and CSI h/l
- Alt screen: 1049h/l with save/restore
- SGR: 0-9 (attributes), 22-29 (off), 30-37/40-47 (8-color), 38;5/48;5 (256), 38;2/48;2 (truecolor), 90-97/100-107 (bright), 39/49 (default)
- DSR: device status (5n), cursor position (6n), device attributes (c)
- Tab stops: HTS, TBC (0/3), default every-8
- Cursor save/restore: DECSC/DECRC (ESC 7/8)
- Reverse index: RI (ESC M)
- Bell: BEL (0x07)
- Title: OSC 0/2
- Charset: SO/SI (0x0E/0x0F)
- OSC 7: CWD (working directory)
- OSC 8: Hyperlinks (open/close lifecycle with id grouping)
- OSC 10/11: Color query responses
- OSC 133: Shell integration markers (A/B/C/D)
- DCS: Sixel graphics hook (framework)
- Pending wrap behavior
- Insert mode (IRM)

## Phase 5: Scrollback Buffer
- Hot tier: ring buffer of WixenRow
- Cold tier: zstd-compressed blocks
- Configurable compression threshold
- Push, clear, get_hot, cold_blocks

## Phase 6: PTY Integration
- ConPTY via CreatePseudoConsole
- Shell detection: PowerShell Core > PowerShell > cmd.exe
- Threaded reader (posts WM_PTY_OUTPUT to UI thread)
- Resize support
- Process exit detection (WM_PTY_EXITED)

## Phase 7: Window
- Raw Win32 HWND via RegisterClassExW/CreateWindowExW
- WndProc with event queue (WM_KEYDOWN, WM_CHAR, WM_SIZE, WM_CLOSE, WM_GETOBJECT)
- Fullscreen toggle
- DPI awareness
- Context menu (right-click)

## Phase 8: D3D11 Renderer
- Device + SwapChain creation (hardware, WARP fallback)
- HLSL shaders (embedded source, runtime D3DCompile)
- Vertex layout: position, texcoord, fg_color, bg_color
- Dynamic vertex buffer (256 cols * 64 rows * 6 verts)
- Uniform buffer (screen size)
- Atlas texture (R8_UNORM) with dirty re-upload
- Full frame: clear → set pipeline → build vertices → draw → present

## Phase 9: DirectWrite Glyph Atlas
- IDWriteFactory + IDWriteTextFormat
- Glyph rasterization via CreateTextLayout
- Atlas packing with LRU eviction
- Pre-cached ASCII (0x20-0x7E)
- Font metrics: cell_width, cell_height, baseline, underline_pos, underline_thickness
- Dirty flag for GPU re-upload

## Phase 10: GDI Software Renderer
- DIB section back buffer
- GDI TextOutW for cell rendering
- CreateFont from config
- Resize support
- Fallback when D3D11 unavailable

## Phase 11: Vertex Pipeline
- wixen_build_cell_vertices: grid → quad array
- Background quads for all cells
- Foreground quads with atlas UV mapping
- Selection highlighting
- Cursor rendering (block/underline/bar)

## Phase 12: Accessibility (UIA)
- IRawElementProviderSimple (manual COM vtable in C)
- IRawElementProviderFragment (Navigate, BoundingRectangle, SetFocus)
- IRawElementProviderFragmentRoot (ElementProviderFromPoint, GetFocus)
- ITextProvider2 (GetSelection, GetVisibleRanges, GetCaretRange, RangeFromPoint)
- ITextRangeProvider (Clone, Compare, CompareEndpoints, ExpandToEnclosingUnit, FindText, GetText, Move, MoveEndpointByUnit/Range, GetBoundingRectangles, GetEnclosingElement, GetAttributeValue, Select, ScrollIntoView, GetChildren)
- WixenA11yState with SRWLOCK for thread-safe access
- Events: TextSelectionChanged, FocusChanged, StructureChanged, Notification
- High-level convenience API (provider_init/shutdown, update_cursor, raise_selection_changed, raise_notification)
- WM_GETOBJECT handler with UiaReturnRawElementProvider
- Split header (public API vs internal COM types)

## Phase 13: Search
- PCRE2 regex engine
- Case-sensitive and case-insensitive
- Forward/backward navigation with wrap
- Match highlighting per cell
- Status text ("3 of 15 matches")

## Phase 14: Configuration
- TOML parser (font, window, terminal, accessibility, behavior, profiles, keybindings)
- Keybinding map with chord normalization
- Default keybindings (Ctrl+Shift+C/V, Ctrl+Comma, Ctrl+Shift+P, F11, etc.)
- Config default path (%APPDATA%/wixen/config.toml)
- Config save/load round-trip
- Hot-reload watcher (ReadDirectoryChangesW)

## Phase 15: Lua Plugin Engine
- Lua 5.5 sandboxed engine
- exec_string, exec_file, call, call_str
- get_string, get_int, get_bool

## Phase 16: Shell Integration
- OSC 133 command block tracker (A→B→C→D lifecycle)
- Block states: PromptActive, InputActive, Executing, Completed
- Exit code tracking
- OSC 7 CWD tracking with file:// URI parsing
- Generation counter for tree invalidation
- Pruning old blocks
- Heuristic prompt detection (cmd.exe, PowerShell, Unix-style)

## Phase 17: UI
- Tab manager (add, close, switch, cycle, rename, index_of)
- Pane tree (binary splits, horizontal/vertical, zoom, ratio adjustment, layout)
- Native Win32 settings dialog (PropertySheet)
- Native Win32 command palette (modal dialog + listbox)
- System tray icon with menu
- Context menu with settings/close actions
- Explorer shell extension ("Open Wixen here")
- Jumplist (ICustomDestinationList)
- Audio (bell, boundary, error sounds via PlaySound/Beep)

## Phase 18: Clipboard
- Win32 clipboard set/get with UTF-8↔UTF-16 conversion
- Bracketed paste mode support (ESC[200~/ESC[201~)

## Phase 19: Input Encoding
- Keyboard: arrow keys (normal + app cursor), F1-F12, Home/End/Insert/Delete/PageUp/PageDown, Ctrl+letter, Alt+letter (ESC prefix), Shift+Tab (backtab), Enter, Backspace (DEL), Escape, Tab, modifier combinations (CSI 1;mod format)
- Mouse: X10, Normal, Button, Any-event modes; Legacy encoding (CSI M) and SGR encoding (CSI <); Shift/Ctrl modifiers; Wheel events; Large coordinate support via SGR

## Phase 20: Remaining
- Hyperlink store (OSC 8) with deduplication and grouping
- Session persistence (JSON save/restore)
- Error detection (pattern-based output classification + progress bar detection)
- URL detection (regex-based with scheme safety check)
- Color scheme (256-palette initialization, hex parsing, RGB↔float conversion)
- IPC named pipes (multi-window framework)
- OSC 52 clipboard (framework)
- Sixel DCS hook (framework)

## Phase 21: main.c Wiring
- Full event loop: Win32 messages → PTY output → parse → dispatch → render
- Keybinding dispatch (settings, palette, copy, paste, fullscreen, close)
- UTF-16 → UTF-8 character encoding to PTY
- A11y state updates in frame loop (cursor tracking, selection changed, line notifications)
- Bell audio + title updates + DSR response forwarding
- Tray icon lifecycle
- Config loading + hot-reload watcher
- Provider init/shutdown + WM_GETOBJECT bridge
- Context menu action dispatch
- Clipboard copy (selection extraction) + paste (with bracketed paste)

## Phase 22: RED/GREEN TDD Audit
Features found missing via RED tests and fixed to GREEN:

### RED → GREEN: CSI b (REP — Repeat Last Character)
- **RED test**: `red_rep_repeats_char` — wrote "A" then CSI 3b, expected cols 1-3 to also be "A"
- **Failure**: REP was not implemented — only col 0 had "A"
- **Fix**: Added `case 'b'` to CSI dispatch, tracks `last_printed_char` in terminal state
- **GREEN**: All 3 repeat positions now correctly filled

### RED → GREEN: CSI Z (CBT — Cursor Backward Tabulation)
- **RED test**: `red_cbt_back_tab` — cursor at col 24 (tab stop), sent CSI Z, expected col < 24
- **Failure**: CBT was not implemented — cursor stayed at col 24
- **Fix**: Added `case 'Z'` to CSI dispatch, walks backward through tab_stops array
- **GREEN**: Cursor now correctly moves to previous tab stop

### Verified Solid (24 edge case tests, all passed first run):
- Backspace at col 0 (no underflow)
- Massive cursor movements (clamped to grid bounds)
- OSC without terminator (parser recovers)
- CSI with >32 params (ignored gracefully)
- SGR with invalid 256-color index (no crash)
- Incomplete truecolor SGR (no crash)
- Inverted scroll region (rejected)
- Double alt screen enter (idempotent)
- Grid resize to 1x1 (works)
- Selection ordered without update (returns start point)
- Scrollback get_hot out of bounds (returns NULL)
- Hyperlink close with no active (no crash)
- Invalid regex search (0 matches, no crash)
- Search with NULL rows (0 matches)
- Shell integ invalid marker (ignored)
- Shell integ D without ABC (handled)
- Lua get_string on nil (returns NULL)
- Lua runtime error recovery (engine still usable)
- URL detect on NULL/empty (0 results)
- Config save to invalid path (returns false)
- Parser byte-at-a-time (correct output)

### Test coverage gaps filled:
- Clipboard: 7 tests (set/get roundtrip, UTF-8, null, overwrite)
- IPC: 6 tests (server create/destroy, exists check, client no-server, messages)
- Terminal responses: 7 tests (DA2, DECRQM, XTVERSION, DSR after scroll, color queries)

### Continued RED/GREEN — Bugs #3-5:

### RED → GREEN: Colon-Separated SGR Subparams (Bug #3)
- **RED test**: `red_curly_underline` — CSI 4:3m should set curly underline
- **Failure**: Parser routed digits after colon to main param instead of subparam
- **Fix**: Added `current_subparam_value` + `has_subparam_digit` to parser state; digits after colon accumulate into subparam array, main param preserved
- **GREEN**: `CSI 4:3m` correctly sets WIXEN_UNDERLINE_CURLY

### RED → GREEN: X10 Mouse Mode (Bug #4)
- **RED test**: `red_mouse_x10_mode` — CSI ?9h should set X10 tracking
- **Failure**: Mode 9 missing from private mode dispatch
- **Fix**: Added `case 9: t->modes.mouse_tracking = set ? WIXEN_MOUSE_X10 : WIXEN_MOUSE_NONE`
- **GREEN**: X10 mouse mode now toggles correctly

### RED → GREEN: DECSCNM Reverse Video (Bug #5)
- **RED test**: `red_reverse_video_mode` — CSI ?5h should set reverse video
- **Failure**: Mode 5 missing from private mode dispatch
- **Fix**: Added `case 5: t->modes.reverse_video = set; t->dirty = true`
- **GREEN**: Reverse video mode now toggles correctly

### Status at 1000 tests:
- **Tests**: 1000
- **Suites**: 87
- **TODOs**: 0
- **Total bugs found via RED tests**: 5
- **Source files**: 100
- **LOC**: ~26,200

### Continued RED/GREEN — Bugs #6:

### RED → GREEN: Config Save Opacity Precision (Bug #6)
- **RED test**: `red_config_roundtrip_window` — save opacity 0.85, reload, expect 0.84-0.86
- **Failure**: Save used `%.1f` → wrote `0.8`, load read `0.8` not `0.85`
- **Fix**: Changed `fprintf` format from `%.1f` to `%.2f`
- **GREEN**: Opacity now round-trips correctly

### Additional verified solid (no RED failures):
- DECSC/DECRC saves and restores cursor + SGR attributes
- Alt screen preserves main buffer content
- Resize during alt screen works
- IL within scroll region respects boundaries
- ECH erases in place (no shift), DCH shifts left
- No-wrap mode overwrites at right margin
- Complex SGR combos (256-fg + truecolor-bg + bold + italic)
- LF scrolls at bottom, RI scrolls at top
- OSC 52 set/query/clear (no crash)
- DECSCUSR cursor shapes (all 6 styles)
- Synchronized output mode 2026
- Reverse video DECSCNM (was bug #5, now fixed)
- Parser: interleaved escapes, double ESC, NUL bytes, CAN/SUB cancel, 4KB OSC, split chunks
- Shell integ: full A→B→C→D lifecycle, generation counter, prune
- Config: round-trip font/terminal/a11y, malformed file, empty file
- Heuristic: cmd.exe, PowerShell, Unix prompts, negative cases

## Phase 23: C-Specific Safety Audit

### Issues identified:
1. **20 unchecked malloc calls** — `dup_str` helpers return NULL on OOM but callers don't check
2. **1 unsafe `sprintf`** in main.c keybinding chord builder → fixed to `snprintf`
3. **1 unsafe `strcpy`** in serial config → fixed to `strncpy`
4. **No AddressSanitizer runs** — MSVC ASan setup attempted but not completed
5. **No leak detection** — 100-iteration stress tests pass but can't prove no leaks without external tool

### Fixes applied:
- `sprintf` → `snprintf` with bounded buffer size
- `strcpy` → `strncpy` with explicit null termination
- Created `wixen_xmalloc.h/c` — abort-on-failure allocator for terminal (fail fast > limp with NULL)
- `wixen_xmalloc`, `wixen_xcalloc`, `wixen_xrealloc`, `wixen_xstrdup` available for migration

### C safety tests added (16 tests):
- Double-free safety for terminal, grid, parser, search, scrollback, hyperlinks
- Buffer boundary: long cell content, tiny output buffers
- Large operations: 256x64 grid, 200x50 terminal with 100 lines
- Leak stress: 100 init/free cycles, 200 rapid resizes
- Huge selection (10000 rows)
- Parser 1MB stress input
- Search with 1000 rows × 3 matches each

### E2E tests added (9 tests):
- cmd.exe dir listing
- Colored git status (green branch, red modified files)
- Cargo error output (bold, 256-color)
- htop-style clear + redraw
- Vim alt screen lifecycle
- PowerShell OSC 133 command blocks
- Scroll 20 lines in 5-row terminal
- Progress bar with CR overwrites
- Tab-separated columns

### A11y consistency tests (7 tests):
- Visible text matches grid after write, erase, scroll
- VT escape stripping for screen reader
- Control char removal for notifications
- Colored content produces plain text

### Remaining known risks:
- Unchecked mallocs in dup_str callers (migration to xstrdup recommended)
- No AddressSanitizer CI pipeline yet
- COM ref counting in UIA provider not formally verified
- Thread safety of a11y state updates not stress-tested under concurrency

### Final status:
- **Tests**: 1103
- **Suites**: 98
- **TODOs**: 0
- **Total bugs found via RED/GREEN TDD**: 6
- **Unsafe string ops fixed**: 2
- **Source files**: 101
- **LOC**: ~28,200
