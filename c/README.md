# Wixen Terminal — C17 Port

Accessible GPU-accelerated terminal emulator for Windows, written in C17.

## Build

### Prerequisites
- Visual Studio 2022 (with C/C++ workload)
- CMake 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg)

### Dependencies (installed via vcpkg)
- zstd 1.5+ (scrollback compression)
- pcre2 10+ (regex search)
- lua 5.5 (plugin engine)
- cjson 1.7+ (session persistence)

### Build steps

```
vcpkg install zstd:x64-windows pcre2:x64-windows lua:x64-windows cjson:x64-windows

cmake -B build -G "Visual Studio 17 2022" ^
  -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
```

### Run tests

```
ctest --test-dir build --build-config Release
```

### Run with AddressSanitizer

```
cmake --build build --config Release -- /p:EnableASAN=true
ctest --test-dir build --build-config Release
```

## Architecture

```
c/
  src/
    core/       Terminal state machine, grid, cell, buffer, selection
    vt/         VT parser (DEC VT500 state machine)
    pty/        ConPTY spawning + threaded I/O
    render/     D3D11 GPU renderer + DirectWrite glyph atlas + GDI fallback
    a11y/       UIA provider (ITextProvider2, IRawElementProviderSimple/Fragment/Root)
    config/     TOML config, keybindings, Lua plugin engine, session, hot-reload
    search/     PCRE2 regex search with match highlighting
    shell_integ/ OSC 133 command blocks + heuristic prompt detection
    ui/         Win32 window, tabs, panes, settings dialog, palette, tray, audio
    ipc/        Named pipe IPC for multi-window
  include/      Public headers (one per module)
  tests/        197+ test suites using greatest.h
  config/       Default TOML configuration
  shaders/      HLSL vertex/pixel shaders (embedded at compile time)
```

## Accessibility

The terminal area uses manual COM vtable construction for UIA providers:
- `IRawElementProviderSimple` — basic element properties
- `IRawElementProviderFragment` — tree navigation
- `IRawElementProviderFragmentRoot` — root + focus management
- `ITextProvider2` — full text pattern (GetSelection, GetCaretRange, ExpandToEnclosingUnit)
- `ITextRangeProvider` — text range operations (GetText, Move, Clone, Compare)

Native Win32 controls (settings dialog, command palette, tray menu) are automatically accessible.

## Test coverage

1700+ individual tests across 197+ suites covering:
- VT parser (CSI, ESC, OSC, DCS, split chunks, stress)
- Terminal (cursor, erase, scroll, modes, SGR, DSR, tabs, UTF-8, alt screen)
- Grid (resize, reflow, clone, visible text)
- Scrollback (zstd compression, hot/cold tiers)
- Search (regex, wrap, case sensitivity)
- Config (round-trip, malformed input)
- Shell integration (OSC 133 lifecycle, heuristic detection)
- Keyboard/mouse encoding
- Clipboard, IPC, colors, pipeline
- C safety (double-free, buffer overflow, 1MB stress, ASan verified)
- End-to-end (cmd dir, git status, cargo errors, vim alt screen)

## License

Dual MIT/Apache-2.0
