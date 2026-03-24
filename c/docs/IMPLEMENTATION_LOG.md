# Wixen Terminal C Port — Implementation Log

## Phase 1: Project Scaffolding ✅

### Accomplished
- [x] CMakeLists.txt with C17, MSVC /W4 /WX, CTest
- [x] Directory structure (include/wixen/*, src/*, tests/*)
- [x] greatest.h test framework (single-header, 1,266 lines)
- [x] test_scaffolding.c — 2 tests verifying build system

### Not Implemented
- [ ] GitHub Actions CI workflow (deferred — will add when pushing to remote)
- [ ] vcpkg.json dependency manifest (not needed yet — no external deps)

### Test Summary
- 2 tests, 3 assertions, all passing

---

## Phase 2: Core Data Structures ✅

### Accomplished
- [x] WixenCell — init, free, clone, reset, set_content (9 tests)
- [x] WixenColor — default, indexed, rgb constructors + equality (5 tests)
- [x] WixenCellAttributes — default, equality including color comparison (3 tests)
- [x] WixenCursor — init, move_to, clamp with bounds checking (8 tests)
- [x] WixenGrid — init, free, clear, write_char, scroll_up/down, scroll_region, erase_in_line, erase_in_display, resize, insert/delete cells, visible_text (32 tests)
- [x] WixenRow — init, free, clone, clear, text extraction with trailing space trimming (5 tests in grid suite)
- [x] WixenSelection — init, start, update, clear, ordered, contains (normal/line/block), row_count (15 tests)
- [x] WixenTerminalModes — init with defaults (auto_wrap=true, cursor_visible=true), reset (3 tests)

### Edge cases tested beyond Rust version
- cell_free_null_content_safe — NULL content doesn't crash
- cell_set_content_empty_string — empty string handled
- grid_init_zero_cols — 0-width grid is safe
- grid_scroll_more_than_size — scrolling more than grid height is clamped
- grid_erase_with_cursor_at_end — erase at last column
- cursor_clamp_zero_size_grid — 0-size grid doesn't crash
- selection_block_reversed_anchor — block selection with reversed endpoints
- selection_single_cell — degenerate single-cell selection

### Not Implemented
- [ ] Grid resize_with_reflow (line rewrapping) — deferred to Phase 4 terminal
- [ ] Hyperlink store — deferred to Phase 4
- [ ] Image store — deferred to Phase 16

### Test Summary
- 75 tests, 251 assertions, all passing

---

## Phase 3: VT Parser ✅

### Accomplished
- [x] WixenParser — DEC VT500 state machine with 15 states
- [x] All state transitions: Ground, Escape, EscapeIntermediate, CsiEntry, CsiParam, CsiIntermediate, CsiIgnore, OscString, DcsEntry, DcsParam, DcsIntermediate, DcsPassthrough, DcsIgnore, SosPmApcString, ApcString
- [x] CSI parameter parsing with semicolons (up to 32 params)
- [x] Colon-delimited subparameter support (SGR 4:3 style)
- [x] Private mode markers (? in intermediates)
- [x] OSC string accumulation with BEL and ESC\ terminators
- [x] DCS hook/put/unhook lifecycle
- [x] APC accumulation (Kitty graphics protocol)
- [x] UTF-8 decoder (2/3/4-byte sequences)
- [x] Overlong sequence rejection → U+FFFD
- [x] Invalid continuation byte → U+FFFD
- [x] CAN/SUB abort from any state
- [x] C0 controls executed within CSI sequences
- [x] Dynamic buffer allocation for OSC/APC data

### Edge cases tested beyond Rust version
- parser_empty_input — NULL/0-length data
- parser_reset_clears_state — mid-sequence reset
- parser_many_params — 10 semicolons in one CSI
- parser_can_aborts_csi — CAN mid-CSI
- parser_csi_embedded_control — BEL inside CSI
- parser_utf8_overlong_rejected — overlong encoding

### Not Implemented
- [ ] Fast-path ASCII batch optimization (printable ASCII batch into single action) — optimization, deferred
- [ ] Surrogate pair detection in UTF-8 (validated in decoder, but not explicitly tested)

### Test Summary
- 32 tests, 132 assertions, all passing

---

---

## Phase 4: Terminal State Machine (first batch) ✅

### Accomplished
- [x] WixenTerminal struct with full lifecycle (init, free, reset, resize)
- [x] Cursor movement: CUU, CUD, CUF, CUB, CUP, CHA, CNL, CPL, VPA, HPA
- [x] Printing with auto-wrap (DECAWM), pending_wrap state, wide char support
- [x] Insert mode (IRM)
- [x] Linefeed with scroll region awareness
- [x] Reverse index (RI)
- [x] Carriage return
- [x] Backspace
- [x] Tab stops (default every 8, set/clear)
- [x] Erase: ED (0/1/2/3), EL (0/1/2), ECH
- [x] Line ops: IL, DL, ICH, DCH
- [x] Scroll region (DECSTBM) with linefeed scrolling
- [x] SGR: bold, dim, italic, underline (single/double/curly/dotted/dashed via subparams), blink, inverse, hidden, strikethrough, overline
- [x] SGR colors: standard 8, bright 8, 256-color (38;5;N), RGB (38;2;R;G;B), foreground + background
- [x] SGR reset (0)
- [x] Modes: DECCKM, DECOM, DECAWM, DECTCEM, alt screen (47/1047/1049), mouse tracking (1000/1002/1003/1006), bracketed paste (2004), focus reporting (1004), synchronized output (2026), insert mode (4), LNM (20)
- [x] Alt screen enter/exit with save/restore cursor (1049)
- [x] Save/restore cursor (DECSC/DECRC via ESC 7/8 and CSI s/u)
- [x] OSC 0/2: set title
- [x] ESC D (index), ESC M (reverse index), ESC E (next line), ESC H (set tab), ESC c (reset)
- [x] ESC #8 (DECALN — fill with E)
- [x] CSI n (DSR) — cursor position report and status report
- [x] CSI c (DA1/DA2) — device attributes
- [x] CSI S/T (scroll up/down)
- [x] CSI q (DECSCUSR — cursor style: block/underline/bar, blinking)
- [x] Bell detection (BEL → bell_pending flag)
- [x] Response queue for DSR/DA responses
- [x] Full dispatch from WixenAction (print, execute, CSI, ESC, OSC, DCS, APC)

### Not Implemented (deferred)
- [ ] Remaining 106+ terminal tests (will be added incrementally)
- [ ] OSC 4 (color palette), OSC 7 (CWD), OSC 8 (hyperlinks), OSC 52 (clipboard), OSC 133 (shell integration)
- [ ] DCS passthrough (Sixel)
- [ ] APC handling (Kitty graphics)
- [ ] Character set switching (G0/G1, SO/SI)
- [ ] Grid resize with reflow
- [ ] Kitty keyboard protocol
- [ ] Mouse encoding
- [ ] Window operations CSI t
- [ ] Hyperlink store
- [ ] Image store

### Deviations
- Tests use `\r\n` instead of `\n` alone to match real terminal behavior (LF doesn't imply CR)
- `param_or` returns `size_t` to match grid dimensions; explicit casts to `int`/`uint16_t` where needed

### Test Summary
- 38 tests, 89 assertions, all passing

---

---

## Phase 5: Scrollback Buffer ✅

### Accomplished
- [x] WixenScrollbackBuffer with two-tier hot/cold architecture
- [x] Hot tier: dynamic array of uncompressed WixenRow
- [x] Cold tier: serialized blocks (binary format: wrapped+cells with attrs)
- [x] Auto-compression trigger when hot_count exceeds threshold
- [x] Push with ownership transfer (caller's row zeroed)
- [x] Hot row access by index
- [x] Clear/free lifecycle

### Not Implemented
- [ ] zstd compression (requires vcpkg/zstd dependency — blocks stored uncompressed)
- [ ] Cold tier deserialization (read back from compressed blocks)
- [ ] Cold row access by absolute index

### Test Summary
- 10 tests, all passing

---

## Phase 9 (partial): Shell Integration ✅

### Accomplished
- [x] WixenShellIntegration tracker with command block lifecycle
- [x] OSC 133 markers: A (prompt), B (input), C (execute), D (complete)
- [x] Exit code parsing from D params
- [x] OSC 7 working directory (file:// URI parsing)
- [x] CWD propagation to new command blocks
- [x] Generation counter for a11y tree invalidation
- [x] Prune oldest blocks (configurable max)
- [x] Multiple block lifecycle
- [x] Edge cases: D without C, empty exit code, bare path

### Not Implemented
- [ ] Heuristic prompt detection (fallback when OSC 133 unavailable)
- [ ] Tmux passthrough
- [ ] Command text sanitization (strip ANSI, truncate)
- [ ] Command completion notifications (duration tracking)

### Test Summary
- 13 tests, all passing

---

## Running Totals
- **Source files**: 12 (.c implementation) + 9 (.h headers) + 10 (test .c) = 31 files
- **Tests**: 170 tests, 540+ assertions
- **Warnings**: 0 (/W4 /WX)
- **Build time**: ~3 seconds
- **Libraries**: wixen_core (static), wixen_vt (static), wixen_shell_integ (static)
