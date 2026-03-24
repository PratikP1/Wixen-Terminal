# Wixen Terminal Architecture

## Overview

Wixen Terminal is a GPU-accelerated terminal emulator for Windows, written in Rust. It targets Windows 10 1903+ (build 18362) and Windows 11. Its core differentiator is structured UI Automation (UIA) support: it transforms the flat character grid of terminal output into semantically meaningful, navigable regions for screen readers. No existing terminal emulator does this well. Windows Terminal exposes a flat `TextBuffer` via UIA `TextPattern`, so screen readers can read text but cannot distinguish prompts from output or navigate between commands. Wixen Terminal treats accessibility as architecture, not afterthought.

**Design philosophy.** Accessibility is not a feature bolted on after rendering works. The accessibility tree, shell integration, and semantic analysis are first-class crates that shape how data flows through the entire system. Every architectural decision, from the choice of raw Win32 windowing to the custom VT parser, was made to give the UIA provider full control over what screen readers see.

**Language.** Rust, edition 2024. Dual-licensed MIT/Apache-2.0.


## Crate Structure

The workspace contains 10 crates plus the binary root.

| Crate | Purpose | Key types |
|---|---|---|
| `wixen-core` | Terminal state machine, grid, scrollback, selection, image protocols | `Terminal`, `Grid`, `ScrollbackBuffer`, `ImageStore` |
| `wixen-vt` | VT escape sequence parser (DEC VT500 state machine) | `Parser`, `Action` |
| `wixen-pty` | ConPTY wrapper, process spawning, shell management | `PtyHandle`, `PtyEvent` |
| `wixen-render` | GPU renderer (wgpu), software fallback (softbuffer), glyph atlas, DirectWrite | `Renderer`, `TerminalRenderer`, `GlyphAtlas`, `SoftwareRenderer` |
| `wixen-a11y` | UIA provider, structured accessibility tree, event throttling | `AccessibilityTree`, `A11yNode`, `EventThrottler`, `TerminalProvider` |
| `wixen-config` | TOML + Lua configuration, hot-reload, JSON Schema, plugin system | `Config`, `LuaEngine`, `ConfigWatcher`, `PluginRegistry` |
| `wixen-ui` | Window chrome, tabs, pane splits, command palette, settings GUI | `PaneTree`, `TabManager`, `CommandPalette`, `SettingsUI` |
| `wixen-shell-integ` | Shell integration (OSC 133, OSC 7, heuristic prompt detection) | `ShellIntegration`, `CommandBlock`, `BlockState` |
| `wixen-search` | Terminal text search, regex, match highlighting | `SearchEngine`, `SearchOptions`, `SearchDirection` |
| `wixen-ipc` | Named pipe IPC (for future multi-window client-server) | `IpcServer`, `IpcClient`, `IpcRequest`, `IpcResponse` |

### Dependency graph

```
wixen-terminal (binary)
  +-- wixen-core
  |     +-- wixen-shell-integ
  +-- wixen-vt
  +-- wixen-pty
  +-- wixen-render
  |     +-- wixen-core
  +-- wixen-a11y
  |     +-- wixen-core
  |     +-- wixen-shell-integ
  +-- wixen-config
  +-- wixen-ui
  +-- wixen-search
  +-- wixen-ipc
```

### Why this structure

Each crate has a single responsibility and can be tested in isolation. `wixen-vt` knows nothing about the grid. `wixen-core` knows nothing about rendering. `wixen-a11y` reads from `wixen-core` and `wixen-shell-integ` but never writes to them. This layering prevents circular dependencies and keeps compilation parallel. It also means the VT parser, terminal state machine, and accessibility tree each have their own test suites that run without a window or GPU.


## Threading Model

Wixen Terminal uses four thread categories.

### Main / UI thread

The main thread runs the Win32 message pump (`GetMessage`/`DispatchMessage`). It handles:

- `WndProc` dispatch for keyboard, mouse, resize, and focus events.
- UIA provider calls. Windows requires all UIA responses on the UI thread.
- Tab and pane management.
- Accessibility tree rebuilds.
- Rendering dispatch.

### PTY reader threads (one per terminal pane)

Each pane spawns a ConPTY child process. A dedicated thread performs blocking reads from the PTY pipe and sends `PtyEvent` messages to the main thread via a `crossbeam_channel::Receiver<PtyEvent>`. The main thread polls these receivers during its event loop.

### Config watcher thread

A background thread watches configuration files for changes and broadcasts `ConfigDelta` messages when the TOML or Lua config is modified. The main thread applies deltas on the next loop iteration.

### Communication

Threads communicate through `crossbeam_channel` channels (bounded, multi-producer single-consumer). The terminal grid is protected by a `parking_lot::RwLock`. The renderer reads the grid under a read-lock. PTY events are processed on the main thread, so grid writes happen without contention.

**Why message passing over shared state.** Shared mutable state requires locking discipline that is hard to audit. Channels make data flow explicit: you can trace every message from producer to consumer. The `RwLock` on the grid is the one exception, used because the renderer needs snapshot access to the grid without copying it.


## VT Parser

The VT parser lives in `wixen-vt`. It is a hand-written state machine based on the DEC VT500 specification (the canonical reference at vt100.net/emu/dec_ansi_parser).

### State machine design

The parser has 15 states:

```rust
enum State {
    Ground,
    Escape,
    EscapeIntermediate,
    CsiEntry,
    CsiParam,
    CsiIntermediate,
    CsiIgnore,
    OscString,
    DcsEntry,
    DcsParam,
    DcsIntermediate,
    DcsPassthrough,
    DcsIgnore,
    SosPmApcString,
    ApcString,
}
```

Each byte advances the state machine and may emit one or more `Action` values. The `Parser` struct holds the current state, parameter accumulator, intermediate bytes, OSC data buffers, and a UTF-8 decoder.

### Why a custom parser instead of the vte crate

The `vte` crate (used by Alacritty) is a reasonable foundation, but Wixen's accessibility integration benefits from tight coupling with the parser. OSC 133 shell integration sequences must be intercepted and routed to `wixen-shell-integ` with precise cursor position context. A custom parser gives full control over:

- OSC 133 handling for accessibility.
- Custom extensions and sequence size limits.
- Performance tuning (the fast path described below).
- Subparameter parsing for colon-delimited CSI params (e.g., SGR `4:3` for curly underlines).

### Supported sequences

| Category | Examples |
|---|---|
| CSI (Control Sequence Introducer) | Cursor movement, erase, scroll, SGR (colors/styles), mode set/reset, device status, window manipulation |
| OSC (Operating System Command) | Set title (OSC 0/1/2), hyperlinks (OSC 8), clipboard (OSC 52), CWD (OSC 7), shell integration (OSC 133), color queries (OSC 4/10/11), iTerm2 images (OSC 1337), ConEmu progress (OSC 9;4) |
| DCS (Device Control String) | Sixel graphics, DECRQSS |
| ESC sequences | Charset selection (G0/G1), save/restore cursor, RIS (reset) |
| APC (Application Program Command) | Kitty graphics protocol |

### The fast path for ASCII text

Most terminal content is printable ASCII (bytes 0x20-0x7E). When the parser is in `Ground` state with no pending UTF-8 sequence, `process()` enters a tight inner loop that skips the state machine dispatch entirely:

```rust
pub fn process(&mut self, bytes: &[u8]) -> Vec<Action> {
    // ...
    if self.state == State::Ground && self.utf8_len == 0 {
        while i < bytes.len() {
            let b = bytes[i];
            if (0x20..=0x7e).contains(&b) {
                actions.push(Action::Print(b as char));
                i += 1;
            } else {
                break;
            }
        }
    }
    // ...
}
```

This avoids the match-on-state overhead for the common case.

### UTF-8 decoding

The parser decodes multi-byte UTF-8 inline. It maintains a 4-byte buffer (`utf8_buf`) and tracks expected length and bytes received. Invalid sequences emit the Unicode replacement character (U+FFFD). Interrupted sequences (a lead byte followed by a non-continuation byte) also emit U+FFFD and reprocess the interrupting byte.

### Security: size caps

- **OSC strings** are capped at 4,096 bytes. Bytes beyond the cap are silently dropped. This prevents a malicious program from exhausting memory with an unbounded OSC payload.
- **APC payloads** (used by Kitty graphics protocol) are capped at 16 MB. Image data can legitimately be large, but this cap prevents runaway allocation.


## Terminal State Machine

The terminal state machine lives in `wixen-core`. The `Terminal` struct is the central data structure.

### Key fields

```rust
pub struct Terminal {
    pub grid: Grid,
    alt_grid: Option<Grid>,
    pub scrollback: ScrollbackBuffer,
    pub modes: TerminalModes,
    scroll_region: ScrollRegion,
    pub title: String,
    pub viewport_offset: usize,
    pub selection: Option<Selection>,
    pub shell_integ: ShellIntegration,
    pub hyperlinks: HyperlinkStore,
    pub images: ImageStore,
    pub osc52_policy: Osc52Policy,
    // ...
}
```

The `Terminal` receives `Action` values from the VT parser and mutates its grid, scrollback, modes, and shell integration state accordingly. It owns the `ShellIntegration` tracker, which means OSC 133 events are processed in the same call path as all other VT sequences.

### Scrollback compression

The `ScrollbackBuffer` uses a two-tier architecture.

**Hot tier.** Recent rows live uncompressed in a `Vec<Row>`. Access is instant. The default threshold is 10,000 rows.

**Cold tier.** When the hot tier exceeds its threshold, the oldest half is serialized into a compact binary format and compressed with zstd at level 3. Each compressed block is a `CompressedBlock` containing the byte data and a row count.

```rust
pub struct ScrollbackBuffer {
    hot: Vec<Row>,
    cold: Vec<CompressedBlock>,
    total_len: usize,
    hot_threshold: usize,
}
```

Cold rows are decompressed on demand via `get_cold()`. For viewport rendering, only hot rows are typically accessed. The binary format encodes each cell as: content bytes (length-prefixed), width, and 16 bytes of packed attributes (foreground color, background color, flags bitfield, underline color, hyperlink ID).

Scrollback is infinite by default. There is no line limit. The zstd compression keeps memory usage manageable for long-running sessions.

### Image protocols

Three inline image protocols are supported:

| Protocol | Trigger | Notes |
|---|---|---|
| Sixel | DCS sequences | DEC raster graphics. Decoded in `wixen-core::image`. |
| iTerm2 | OSC 1337 | Base64-encoded image data. Provides filename metadata. |
| Kitty | APC G... | Chunked transfer, multiple actions (transmit, display, delete). |

Images are stored in `ImageStore` and rendered by a separate GPU pipeline (RGBA textured quads). Screen readers receive a UIA notification with the protocol name, dimensions, and filename (when available).

### Shell integration

The `ShellIntegration` tracker in `wixen-shell-integ` receives OSC 133 markers and maintains a `Vec<CommandBlock>`. Each block tracks the lifecycle of one command:

```rust
pub enum BlockState {
    PromptActive,    // after OSC 133;A
    InputActive,     // after OSC 133;B
    Executing,       // after OSC 133;C
    Completed,       // after OSC 133;D
}
```

A generation counter increments on every structural change. The main loop compares this counter to a cached value to know when the accessibility tree needs rebuilding.

OSC 7 sequences update the current working directory. The shell integration module also ships integration scripts: `wixen.ps1` for PowerShell and `wixen.lua` for cmd.exe via Clink.

### Heuristic prompt detection (fallback)

When shells do not emit OSC 133, the `heuristic` module in `wixen-shell-integ` detects prompts by pattern matching. It recognizes common prompt formats (`PS C:\path>`, `user@host:path$`, `C:\path>`) and infers command boundaries from timing and cursor position. This gives screen readers some structure even with legacy shells.


## Rendering

### Why wgpu

wgpu provides a WebGPU abstraction over platform-native APIs. On Windows, it maps to Direct3D 12 (primary) with Vulkan as a fallback. The choice was driven by:

- **D3D12 performance** without writing D3D12 boilerplate.
- **Pure Rust.** No C/C++ build dependencies for the GPU layer.
- **Active maintenance.** wgpu tracks the WebGPU standard and has a large contributor base.
- **Software fallback.** When no GPU is available (RDP sessions, headless VMs), `softbuffer` provides CPU-based rendering via `SoftwareRenderer`.

The renderer mode is user-selectable via config: `"auto"` (try GPU, fall back to software), `"gpu"` (GPU only), or `"software"` (CPU only).

```rust
pub enum TerminalRenderer {
    Gpu(Box<Renderer>),
    Software(Box<SoftwareRenderer>),
}
```

### Glyph atlas with DirectWrite

Text is rasterized through DirectWrite via `windows-rs`. DirectWrite was chosen over pure-Rust alternatives (rustybuzz + swash) because:

- Best ClearType rendering on Windows.
- Automatic respect for system text scaling and high-contrast mode.
- Native system font fallback chains.
- These properties matter for accessibility.

The `GlyphAtlas` rasterizes glyphs into an R8 texture. The atlas grows dynamically as new glyphs are encountered (CJK, emoji, combining characters). Font metrics (`cell_width`, `cell_height`, `baseline`) drive the entire layout pipeline.

### Cell rendering pipeline

1. The main loop reads the terminal grid.
2. For each visible cell, a `Vertex` is built containing position, atlas UV coordinates, foreground color, and background color.
3. Vertices are uploaded to a GPU buffer.
4. A WGSL shader (`shader.wgsl`) renders the cells as textured triangles with alpha blending.
5. The render pipeline uses `PrimitiveTopology::TriangleList` with the glyph atlas bound as a filtered texture.

### Inline image rendering

Inline images (Sixel, iTerm2, Kitty) use a separate render pipeline (`image.wgsl`) that draws RGBA textured quads. Each image is uploaded as its own GPU texture. The image pipeline shares the uniform buffer (screen dimensions) but has its own bind group layout supporting RGBA textures.

### Background image with blur

Background images are decoded via the `image` crate, opacity-adjusted, and uploaded as a GPU texture. A fullscreen quad renders the background before cell content. Gaussian blur is applied on the CPU before upload. The source pixel data is retained so the blur can be recomputed when the radius changes.

### Software renderer as fallback

`SoftwareRenderer` uses the `softbuffer` crate to write pixels directly to a window surface buffer. It implements the same rendering logic (cells, cursor, tab bar) in CPU-only code. This path exists for RDP, VMs without GPU passthrough, and troubleshooting.

### Contrast ratio enforcement

The color system checks foreground/background contrast ratios against WCAG thresholds. When the computed contrast is too low, the renderer adjusts colors to meet minimum readability standards. This is part of the accessibility-first design.


## Accessibility Architecture

### Why structured UIA matters

Windows Terminal exposes its text buffer through a single `ITextProvider`. Screen readers can read text but see no structure. A user cannot:

- Jump to the previous command's output.
- Distinguish the prompt from the command from the output.
- Navigate a file listing as a table.
- Get a summary of what a command did.

Wixen Terminal builds a structured UIA tree that gives screen readers semantic navigation.

### The accessibility tree

The tree is rooted at a `Document` node. Each shell command cycle becomes a `CommandBlock`. Within each block, text is classified by role.

```rust
pub enum A11yNode {
    Document { id: NodeId, children: Vec<A11yNode> },
    CommandBlock {
        id: NodeId,
        shell_block_id: u64,
        name: String,
        children: Vec<A11yNode>,
        is_error: bool,
        state: BlockState,
    },
    TextRegion {
        id: NodeId,
        text: String,
        role: SemanticRole,
        start_row: usize,
        end_row: usize,
        children: Vec<A11yNode>,
    },
    LiveRegion { id: NodeId, text: String, politeness: u32 },
    Hyperlink { id: NodeId, url: String, text: String, start_row: usize, end_row: usize },
}
```

The tree structure looks like:

```
Document
  +-- CommandBlock: "ls -la (succeeded)"
  |     +-- TextRegion [Prompt]: "C:\Users\foo>"
  |     +-- TextRegion [CommandInput]: "ls -la"
  |     +-- TextRegion [OutputText]: "total 21\ndrwxr-xr-x ..."
  |           +-- Hyperlink: "https://example.com"
  +-- CommandBlock: "cat missing.txt (exit 1)"
  |     +-- TextRegion [Prompt]: "C:\Users\foo>"
  |     +-- TextRegion [CommandInput]: "cat missing.txt"
  |     +-- TextRegion [ErrorText]: "No such file or directory"
  +-- ...
```

### Semantic roles

```rust
pub enum SemanticRole {
    Prompt,
    CommandInput,
    OutputText,
    ErrorText,
    Path,
    Url,
    StatusLine,
}
```

Error detection uses the exit code from OSC 133;D. When the exit code is nonzero, the output region is tagged `ErrorText` instead of `OutputText`.

### UIA providers

The terminal implements several COM interfaces via `windows-rs`:

| Interface | Purpose |
|---|---|
| `IRawElementProviderSimple` | Base UIA provider for each node |
| `IRawElementProviderFragment` | Tree navigation (parent, siblings, children) |
| `IRawElementProviderFragmentRoot` | Root of the fragment tree (the HWND) |
| `ITextProvider` | Text content and range-based navigation |
| `IGridProvider` | Cell-level navigation for TUI applications |
| `IScrollProvider` | Scrollback navigation |
| `IValueProvider` | Active input line content |

All UIA calls are serviced on the UI thread. This is a Windows requirement.

### Event raising and throttling

Terminal output can produce thousands of lines per second. Raising a UIA event for every line would flood screen readers. The `EventThrottler` batches events into 100ms windows.

```rust
pub struct EventThrottler {
    last_text_changed: Instant,
    debounce_interval: Duration,  // 100ms
    pending_text: String,
    lines_since_announce: usize,
    streaming_mode: bool,
    streaming_threshold: usize,   // 10 lines/batch
    // ...
}
```

When output exceeds the streaming threshold (10 lines per batch), the throttler enters streaming mode. In streaming mode, `UiaRaiseNotificationEvent` uses `NotificationProcessing_MostRecent` so only the latest batch is spoken, preventing the screen reader from queuing hundreds of announcements.

Events raised:

| Event | When |
|---|---|
| `UIA_Text_TextChangedEventId` | Terminal output arrives |
| `UIA_LiveRegionChangedEventId` | New output in active prompt area |
| `UIA_AutomationFocusChangedEventId` | Active pane changes |
| `UIA_StructureChangedEventId` | Command blocks added/removed |
| Notification (ActionCompleted) | Command completes (includes exit code) |
| Notification (ActionCompleted) | Mode changes (zoom, read-only, broadcast) |
| Notification (ItemAdded) | Inline image placed |

### Screen reader compatibility

The UIA provider is tested against three screen readers:

- **Narrator.** Reads UIA natively. Supports `TextPattern`, grid navigation, landmarks, live regions. Primary test target.
- **NVDA.** UIA support via `UIAHandler`. Browse mode uses the document structure. Live regions appear as toast-style notifications.
- **JAWS.** UIA support but historically prefers MSAA/IAccessible. May need a JAWS script for full compatibility.

### Mode change announcements

When a pane enters or exits a mode (zoom, read-only, broadcast input), a UIA notification is raised with `NotificationProcessing_ImportantAll`. The announcement text is "{mode} enabled" or "{mode} disabled".

### Image announcements

When an inline image is placed on the grid, a UIA notification is raised. The message includes the protocol name, pixel dimensions, and filename when available (iTerm2 provides filenames, Sixel and Kitty do not).

```
"iTerm2 image: photo.png (800x600 pixels)"
"Sixel image: 320x240 pixels, 40x15 cells"
```


## Security Model

### OSC 52 clipboard policy

Programs running in the terminal can request clipboard access via OSC 52. The default policy is write-only:

```rust
pub enum Osc52Policy {
    WriteOnly,   // default: programs can paste to clipboard, not read
    ReadWrite,   // programs can both read and write
    Disabled,    // OSC 52 completely blocked
}
```

Write-only is the safe default. A malicious program cannot exfiltrate clipboard contents. Users who need bidirectional clipboard access (e.g., for tmux) can opt in.

### URL scheme filtering

URLs detected in terminal output are filtered before being exposed to the accessibility tree or opened by the user.

```rust
pub fn is_safe_url_scheme(url: &str) -> bool {
    let lower = url.to_ascii_lowercase();
    lower.starts_with("http://")
        || lower.starts_with("https://")
        || lower.starts_with("ftp://")
}
```

Blocked schemes: `file://`, `javascript:`, `data:`. A program printing `javascript:alert(1)` will not produce a clickable or screen-reader-navigable link.

### OSC 133 command text sanitization

Command text extracted from shell integration markers is sanitized before display:

1. ANSI escape sequences are stripped (regex-based).
2. Control characters (0x00-0x1F except tab, newline, carriage return) are replaced with spaces.
3. Text is truncated to 1,024 characters.

This prevents a malicious program from injecting fake command markers with escape sequences or excessively long strings.

### Lua config sandbox

User configuration can include Lua scripts (via mlua's Luau backend). The Lua VM is sandboxed:

- `os` is removed. Even though Luau's `os` only exposes time functions, removing it eliminates future risk if mlua adds more members.
- `debug` is removed. `debug.info` can leak implementation details.
- `io` and `loadfile` are already absent in Luau.
- Safe libraries remain: `string`, `table`, `math`, `utf8`, `bit32`, `buffer`.

```rust
pub fn sandbox_lua(lua: &mlua::Lua) -> LuaResult<()> {
    let globals = lua.globals();
    globals.set("os", mlua::Value::Nil)?;
    globals.set("debug", mlua::Value::Nil)?;
    Ok(())
}
```

### APC payload size limits

APC data (used by Kitty graphics protocol) is capped at 16 MB. Bytes beyond the cap are silently dropped. This prevents memory exhaustion from a program sending an unbounded APC payload.


## Configuration System

### TOML primary, Lua for programmable config

The primary configuration file is TOML. It covers profiles, color schemes, keybindings, appearance, terminal behavior, and accessibility settings. TOML was chosen over JSON (no comments) and YAML (indentation-sensitive, ambiguous types).

Lua (via mlua's Luau backend) is available from day one for programmable configuration: event hooks, dynamic themes, computed keybindings. Lua scripts run in the sandboxed VM described above.

### Hot reload

A `ConfigWatcher` thread monitors the configuration directory for file changes. When a change is detected, the config is re-parsed and a `ConfigDelta` is sent to the main thread. The main thread applies changes without restarting: color scheme updates, font changes (the glyph atlas is rebuilt), keybinding remaps, and accessibility settings.

### JSON Schema for IDE support

A `config.schema.json` file is generated from the `Config` struct. Editors with JSON Schema support (VS Code with the Even Better TOML extension) get autocompletion and validation for the TOML config.

### Accessibility config section

The configuration includes a dedicated accessibility section: screen reader verbosity (`all`, `commands-only`, `errors-only`, `silent`), long-running command notification threshold, and announce mode changes. An accessibility-focused terminal must be configurable without editing files, so a GUI settings panel is provided alongside the config files.


## Plugin System

### Plugin manifest format

Plugins are described by a TOML manifest:

```rust
pub struct PluginManifest {
    pub name: String,
    pub version: String,
    pub description: String,
    pub entry_point: String,          // path to Lua entry-point file
    pub events: Vec<String>,          // subscribed events
}
```

The `PluginRegistry` tracks loaded plugins and indexes them by event subscription.

### Event dispatch model

The plugin system defines a fixed set of events:

| Event name | Trigger |
|---|---|
| `on_tab_created` | A new tab is opened |
| `on_tab_closed` | A tab is closed |
| `on_command_complete` | A shell command finishes (with command text, exit code, duration) |
| `on_output` | Terminal output is received |
| `on_config_reloaded` | Configuration files are reloaded |

When an event fires, the registry finds all plugins subscribed to that event. For each plugin, the Lua entry-point file is loaded and the matching handler function is called with a Lua table containing event-specific fields. If the handler does not exist, the call is a no-op. If the handler raises an error, the error is logged but does not crash the terminal.

### Lua execution sandbox

Plugin Lua code runs in the same sandboxed VM as configuration scripts. Plugins cannot access the filesystem, execute system commands, or load arbitrary Lua files.


## Build and Distribution

### Cargo workspace

The workspace uses Rust 2024 edition with resolver v2. Release builds use thin LTO, single codegen unit, and symbol stripping for small binaries.

```toml
[profile.release]
lto = "thin"
codegen-units = 1
strip = "symbols"
```

### CI/CD

GitHub Actions runs on every push and pull request:

1. `cargo fmt --all -- --check` (zero formatting issues).
2. `cargo clippy --workspace -- -D warnings` (zero warnings).
3. `cargo test --workspace` (all tests pass).

Builds target x64 and ARM64.

### Installers

| Format | Tool | Purpose |
|---|---|---|
| EXE installer | InnoSetup | Standard Windows installer with Start Menu shortcuts and PATH setup |
| MSI | WiX | Enterprise deployment via Group Policy and SCCM |
| Portable | ZIP | No installation needed, runs from any directory |

### WinGet manifest

A WinGet manifest is maintained for `winget install wixen-terminal`.


## Testing Strategy

### Unit tests per crate

Each crate has focused unit tests for its public API. Tests run without a window, GPU, or PTY. The VT parser is tested with raw byte sequences. The terminal state machine is tested by feeding `Action` values and inspecting the grid. The accessibility tree is tested by providing mock `CommandBlock` data and verifying tree structure.

### Property-based testing with proptest

Six fuzz targets use proptest to find edge cases:

- `osc133_never_panics`: arbitrary marker strings, params, and cursor rows.
- `osc7_never_panics`: arbitrary URIs.
- `full_cycle_produces_one_block`: random row positions and exit codes.
- `prune_enforces_limit`: random block counts and max limits.
- `generation_monotonically_increases`: random cycle counts.
- URL detection on arbitrary strings.

### VT conformance tests

Integration tests feed known VT sequences through the parser and terminal, then verify grid state against expected output. These cover cursor movement, scrolling, SGR attributes, OSC sequences, and DCS payloads.

### Accessibility tree integration tests

Tests verify that the accessibility tree correctly maps shell integration blocks to `A11yNode` structures. They check semantic role assignment, error detection via exit codes, hyperlink detection in output, and the tree rebuild path.

### Performance benchmarks

Three Criterion benchmark suites run against the hot paths:

| Benchmark | What it measures |
|---|---|
| `vt_parser` | Parser throughput (bytes/second for ASCII, mixed content, heavy-CSI streams) |
| `terminal_grid` | Grid operations (cell writes, scrolling, line wrapping) |
| `scrollback` | Compression throughput and cold-tier decompression latency |


## Key Design Decisions

| Decision | Chosen | Rejected | Why |
|---|---|---|---|
| VT parser | Custom state machine | `vte` crate | Full control over OSC 133 for accessibility. Custom extensions. Performance tuning (ASCII fast path). Subparameter parsing. |
| GPU API | wgpu | Direct3D 11, OpenGL | D3D12 primary with D3D11/Vulkan fallback. Pure Rust. No C++ build deps. Software fallback via softbuffer. Active maintenance. |
| Windowing | Raw Win32 via windows-rs | winit | Full HWND/WndProc control required for UIA provider integration, DWM APIs (Mica/Acrylic), and custom title bar. winit abstracts away the control UIA needs. |
| Text shaping | DirectWrite via windows-rs | rustybuzz + swash | Best ClearType on Windows. System text scaling respect. High-contrast mode. Native font fallback. These matter for accessibility. |
| Config format | TOML + Lua | JSON, YAML | TOML has comments and clear types. Lua provides programmability (event hooks, dynamic themes). JSON lacks comments. YAML has ambiguous type coercion. |
| A11y tree depth | Full-depth structured tree | Flat text buffer (like Windows Terminal) | Core differentiator. Command blocks, semantic roles, error detection, URL exposure. Maximum screen reader value. |
| Scrollback | Infinite with zstd compression | Fixed-size ring buffer | No arbitrary line limit. Hot/cold tier keeps memory bounded. zstd at level 3 compresses terminal text well (high redundancy). |
| Multi-window | Single process now, designed for client-server later | Client-server from day one | Simpler initial architecture. Message-passing abstractions (channels, `wixen-ipc` crate) can become IPC. Migration path is clear without premature complexity. |
| Lua runtime | Luau (via mlua) | Standard Lua 5.4 | Luau has built-in sandboxing (no `io`, `loadfile`, `dofile` by default). Type annotations. Better error messages. |
| PTY | ConPTY via portable-pty | Raw Win32 console API | ConPTY is the supported pseudo-terminal API on Windows 10+. portable-pty handles the FFI boilerplate. |
