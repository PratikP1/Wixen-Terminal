# Wixen Terminal — Comprehensive Project Plan

## 1. Vision & Differentiator

Wixen Terminal is a **Rust-based, GPU-accelerated, accessibility-first terminal emulator** for Windows 10 and 11. Its core differentiator is **structured UI Automation (UIA) support** — transforming the inherently unstructured character grid of terminal output into semantically meaningful, navigable regions for screen readers and assistive technologies.

No existing terminal emulator solves this well. Windows Terminal exposes a flat `TextBuffer` via UIA `TextPattern` — screen readers can read text but cannot distinguish prompts from output, navigate between commands, or understand tabular data. Wixen Terminal treats accessibility as architecture, not afterthought.

---

## 2. Competitive Landscape Analysis

### 2.1 Windows Terminal (Microsoft)
- **Rendering**: DirectX via custom `DxRenderer` (Direct2D/DirectWrite for text, Direct3D 11 for composition). XAML Islands for UI chrome.
- **Terminal Emulation**: Custom C++ VT parser in `microsoft/terminal/src/terminal/parser`. ConPTY (Pseudo Console) for process management.
- **Configuration**: `settings.json` with JSON Schema validation. Profiles, color schemes, actions, fragments (3rd-party extension configs).
- **Accessibility**: UIA `TextPattern` on the text buffer. `UiaTextRange` maps to character ranges. Narrator/NVDA can read text but cannot navigate semantically (no command boundaries, no output regions).
- **Strengths**: Deep Windows integration, default terminal handler, WSL integration, official Microsoft support.
- **Weaknesses**: XAML dependency creates large binary. Accessibility is character-level only. No multiplexing. C++ codebase makes contribution harder.

### 2.2 Alacritty
- **Rendering**: OpenGL ES 2.0+ via `glutin`/`winit`. Glyph atlas texture approach. Instanced rendering of cells.
- **Terminal Emulation**: `alacritty_terminal` crate with custom VT parser (`vte` crate for low-level parsing). Separate library from renderer.
- **Configuration**: TOML files. No GUI editor. Hot-reload supported.
- **Architecture**: Modular crates: `alacritty` (binary), `alacritty_terminal` (emulation), `alacritty_config` (settings), `alacritty_config_derive` (proc macros).
- **Accessibility**: Minimal. No UIA provider. Screen readers cannot meaningfully read Alacritty's OpenGL surface.
- **Strengths**: Raw performance leader. Clean Rust architecture. Benchmarked with `vtebench`.
- **Weaknesses**: No tabs, no splits (by design — delegates to tmux/WM). No accessibility. No ligatures (deliberate omission). Minimalist philosophy limits features.

### 2.3 WezTerm
- **Rendering**: OpenGL via custom renderer. Glyph atlas with texture caching. GLSL shaders.
- **Terminal Emulation**: Custom `wezterm-term` crate. `vtparse` crate for VT parsing. Full multiplexer architecture.
- **Configuration**: Lua scripting via `mlua`. Extremely programmable — event hooks, custom key handlers, dynamic theming.
- **Architecture**: Client-server multiplexer. Crates: `wezterm-gui`, `mux`, `wezterm-font`, `wezterm-ssh`, `wezterm-client`, `wezterm-mux-server`.
- **Accessibility**: Limited. OpenGL rendering surface not exposed via UIA.
- **Strengths**: Built-in multiplexer (SSH mux, domain-based sessions), Lua programmability, rich font handling (fallback chains, ligatures), image protocols (Sixel, iTerm2, Kitty), built-in SSH client.
- **Weaknesses**: Large binary. Complex codebase. Performance not as lean as Alacritty. Active development has slowed.

### 2.4 Kitty
- **Rendering**: OpenGL with GLSL shaders. Custom C rendering pipeline. Cell-based instanced rendering.
- **Terminal Emulation**: Custom C parser. Extensive protocol extensions.
- **Configuration**: `kitty.conf` text file. Python-based kitten extensions.
- **Accessibility**: None meaningful on Windows (primarily Linux/macOS).
- **Strengths**: Kitty graphics protocol (widely adopted), Kitty keyboard protocol (progressive enhancement for modern keyboard handling), kitten extensions (Python scripts), excellent performance.
- **Weaknesses**: Not native Windows (cross-platform but Linux-first). GPL-3.0 license. Mixed C/Python/Go codebase.

### 2.5 Ghostty
- **Rendering**: Platform-native rendering. macOS: Metal + AppKit. Linux: GTK4. Custom `libghostty` core library in Zig.
- **Terminal Emulation**: Custom Zig VT parser. SIMD-optimized parsing.
- **Architecture**: `libghostty` core + platform shells. Core handles terminal state, VT parsing, rendering pipeline. Platform shells handle windowing and native integration.
- **Accessibility**: Uses native platform accessibility (NSAccessibility on macOS, ATK on Linux). Better than GPU-only terminals because of native toolkit integration.
- **Strengths**: SIMD-optimized parsing, native look and feel per platform, clean architecture separation.
- **Weaknesses**: No Windows support yet. Zig ecosystem less mature.

### 2.6 Warp
- **Rendering**: Metal on macOS (Rust + Metal). Custom GPU renderer.
- **Terminal Emulation**: Custom Rust parser with block-based model.
- **Accessibility**: Blocks-based UI provides some structural semantics.
- **Strengths**: Block-based terminal (commands and output are discrete blocks), AI integration, modern collaborative features, rich IDE-like UX.
- **Weaknesses**: Closed source. Requires account login. Heavy resource usage. macOS/Linux only currently.

### 2.7 Rio Terminal
- **Rendering**: **wgpu** (WebGPU abstraction) — runs on Direct3D 12/11 on Windows, Vulkan on Linux, Metal on macOS. Software fallback available.
- **Terminal Emulation**: Uses `rio-terminal` with `sugarloaf` rendering library.
- **Configuration**: TOML-based.
- **Strengths**: wgpu demonstrates the viability of WebGPU abstraction for terminal rendering on Windows. Modern Rust stack.
- **Weaknesses**: Less mature. Smaller community. Limited accessibility.

---

## 3. Architecture Design

### 3.1 High-Level Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Wixen Terminal                         │
├──────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │  Win32 Shell │  │ Command      │  │  Settings UI   │  │
│  │  (HWND,      │  │ Palette      │  │  (optional     │  │
│  │   WndProc)   │  │              │  │   native GUI)  │  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬────────┘  │
│         │                 │                   │           │
│  ┌──────▼─────────────────▼───────────────────▼────────┐ │
│  │              Tab / Pane Manager                      │ │
│  │  (splits, layouts, drag-detach, zoom)                │ │
│  └──────────────────────┬──────────────────────────────┘ │
│                         │                                │
│  ┌──────────────────────▼──────────────────────────────┐ │
│  │            Terminal Instance (per-pane)              │ │
│  │  ┌─────────────┐  ┌────────────┐  ┌──────────────┐ │ │
│  │  │  VT Parser  │  │  Terminal   │  │  Scrollback  │ │ │
│  │  │  (CSI, OSC,  │  │  Grid /    │  │  Buffer      │ │ │
│  │  │   DCS, etc.) │  │  Buffer    │  │              │ │ │
│  │  └─────────────┘  └────────────┘  └──────────────┘ │ │
│  │  ┌─────────────┐  ┌────────────┐  ┌──────────────┐ │ │
│  │  │  Semantic   │  │  Shell     │  │  Selection   │ │ │
│  │  │  Analyzer   │  │  Integr.   │  │  Manager     │ │ │
│  │  │  (structure) │  │  (OSC 133) │  │              │ │ │
│  │  └─────────────┘  └────────────┘  └──────────────┘ │ │
│  └─────────────────────────────────────────────────────┘ │
│                         │                                │
│  ┌──────────────────────▼──────────────────────────────┐ │
│  │               Accessibility Layer                    │ │
│  │  ┌────────────────┐  ┌─────────────────────────┐    │ │
│  │  │  UIA Provider  │  │  Structured A11y Tree   │    │ │
│  │  │  (Fragment     │  │  (semantic regions,     │    │ │
│  │  │   Root, etc.)  │  │   command blocks,       │    │ │
│  │  │                │  │   navigable elements)   │    │ │
│  │  └────────────────┘  └─────────────────────────┘    │ │
│  └─────────────────────────────────────────────────────┘ │
│                         │                                │
│  ┌──────────────────────▼──────────────────────────────┐ │
│  │                GPU Renderer                          │ │
│  │  ┌──────────┐  ┌──────────┐  ┌───────────────────┐ │ │
│  │  │  wgpu /  │  │  Glyph   │  │  Composition      │ │ │
│  │  │  D3D11   │  │  Atlas   │  │  (layers, blur,   │ │ │
│  │  │          │  │  Cache   │  │   transparency)   │ │ │
│  │  └──────────┘  └──────────┘  └───────────────────┘ │ │
│  │  ┌──────────┐  ┌──────────┐                        │ │
│  │  │  Text    │  │  Image   │                        │ │
│  │  │  Shaping │  │  Protocol│                        │ │
│  │  │  Pipeline│  │  Renderer│                        │ │
│  │  └──────────┘  └──────────┘                        │ │
│  └─────────────────────────────────────────────────────┘ │
│                         │                                │
│  ┌──────────────────────▼──────────────────────────────┐ │
│  │                  PTY Layer                           │ │
│  │  ConPTY (Win10+) │ Process Management │ Signal Mgmt │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### 3.2 Core Crate Structure

```
wixen-terminal/
├── crates/
│   ├── wixen-core/          # Terminal state machine, grid, buffer management
│   ├── wixen-vt/            # VT parser (CSI, OSC, DCS, escape sequences)
│   ├── wixen-pty/           # ConPTY wrapper, process spawning, shell management
│   ├── wixen-render/        # GPU renderer (wgpu), glyph atlas, text shaping
│   ├── wixen-a11y/          # UIA provider, structured accessibility tree
│   ├── wixen-config/        # Configuration loading, hot-reload, schema generation
│   ├── wixen-ui/            # Window chrome, tabs, splits, command palette
│   ├── wixen-shell-integ/   # Shell integration (OSC 133, OSC 7, semantic zones)
│   └── wixen-search/        # Terminal search, regex, highlighting
├── src/
│   └── main.rs              # Application entry point, event loop
├── config/
│   └── default.toml         # Default configuration
├── schemas/
│   └── config.schema.json   # JSON Schema for config validation
└── Cargo.toml               # Workspace manifest
```

### 3.3 Threading Model

```
Thread 1: Main / UI Thread
  - Win32 message pump (GetMessage/DispatchMessage)
  - WndProc handler
  - UIA provider responses (MUST be on UI thread)
  - Input event processing
  - Tab/pane management

Thread 2: Renderer Thread
  - wgpu device/queue management
  - Glyph atlas building
  - Frame composition and present
  - Runs at display refresh rate (or on-demand via dirty flag)

Thread 3..N: PTY I/O Threads (one per terminal instance)
  - ConPTY read loop (blocking read from pipe)
  - VT parsing on incoming bytes
  - Grid/buffer mutation (behind lock or via message passing)
  - Write user input back to PTY

Thread N+1: Config Watcher
  - File system watcher (notify crate)
  - Config reload on change
  - Broadcasts config update events

Thread N+2: Semantic Analyzer (optional, for heavy heuristics)
  - Pattern recognition on terminal output
  - AI/heuristic structuring for accessibility
  - Debounced — runs after output settles
```

**Synchronization Strategy**: Message-passing via `crossbeam` channels between threads. Terminal grid protected by `parking_lot::RwLock`. Renderer reads grid snapshot under read-lock. PTY threads write under write-lock. UIA provider reads under read-lock on UI thread.

---

## 4. Structured UIA Accessibility — Multiple Approaches

This is the **core innovation** of Wixen Terminal. Terminal output is fundamentally a character grid — rows and columns of cells with attributes. Screen readers see this as flat text, making terminals nearly unusable for blind users. Wixen Terminal must impose structure.

### 4.1 The Problem in Detail

When a user runs `ls -la` in a terminal:
1. The shell prints a prompt: `PS C:\Users\Pratik>`
2. The user types: `ls -la`
3. The shell outputs a table of files with columns
4. A new prompt appears

To a screen reader, this is all one undifferentiated text stream. The user cannot:
- Jump to the previous command's output
- Navigate the file listing as a table
- Distinguish the prompt from the command from the output
- Get a summary of what happened

### 4.2 Approach A: Shell Integration + Semantic Zones (Recommended Primary)

**How it works**: Modern shells support OSC (Operating System Command) escape sequences that mark semantic boundaries in terminal output.

**Key sequences**:
- `OSC 133;A ST` — Prompt start (FinalTerm/Shell Integration)
- `OSC 133;B ST` — Command start (user has pressed Enter)
- `OSC 133;C ST` — Command output start
- `OSC 133;D;{exit_code} ST` — Command finished with exit code
- `OSC 7;{URI} ST` — Current working directory
- `OSC 9;9;{path} ST` — ConEmu-style CWD (Windows shells)

**UIA structure generated**:

```
Document (Terminal)
├── Group: "Command Block 1" [role=Group, landmark]
│   ├── Text: "PS C:\Users\Pratik> " [role=Prompt, readonly]
│   ├── Edit: "ls -la" [role=CommandInput]
│   └── Group: "Output (exit code 0)" [role=Output]
│       ├── Table: [role=DataGrid, if tabular detected]
│       │   ├── Row: "total 21"
│       │   ├── Row: "drwxr-xr-x ..."
│       │   └── ...
│       └── Text: [role=Text, if non-tabular]
├── Group: "Command Block 2"
│   ├── Text: "PS C:\Users\Pratik> "
│   ├── Edit: "git status"
│   └── Group: "Output (exit code 0)"
│       └── Text: "On branch main..."
└── Group: "Active Input" [role=CurrentPrompt, live region]
    ├── Text: "PS C:\Users\Pratik> "
    └── Edit: "" [focused, editable]
```

**Implementation**:
- `wixen-shell-integ` crate intercepts OSC 133 sequences during VT parsing
- Maintains a `Vec<CommandBlock>` structure parallel to the character grid
- Each `CommandBlock` has: prompt_range, input_range, output_range, exit_code, cwd
- `wixen-a11y` maps CommandBlocks to UIA elements

**Shell support required**:
- PowerShell 7.2+: Built-in OSC 133 support via `$PSStyle`
- Bash: Via PROMPT_COMMAND integration (injected by Wixen)
- Zsh: Via precmd/preexec hooks
- Fish: Native support
- Cmd.exe: Requires clink or custom prompt injection

**Pros**: Accurate structural information. Standard protocol. Works with modern shells.
**Cons**: Requires shell cooperation. Legacy shells (cmd.exe without clink) won't emit sequences. Scrollback commands lose boundaries if they scroll off before parsing.

### 4.3 Approach B: Heuristic Prompt Detection (Fallback)

**How it works**: When shells don't emit OSC 133, detect prompts heuristically by pattern-matching on terminal lines.

**Detection strategies**:
1. **Prompt pattern matching**: Regex patterns for common prompts (`PS .*>`, `\$ $`, `# $`, `.*@.*:.*\$`, `C:\...>`)
2. **Input echo detection**: After the user types and presses Enter, the typed text appears on a line — correlate input keystrokes with the line content
3. **Timing heuristics**: A pause in output followed by cursor positioning to a new line likely indicates a prompt
4. **Cursor position tracking**: Prompts are followed by cursor positioning for user input; output streams continuously
5. **Color/attribute heuristics**: Many prompts use specific colors (green for user, blue for path) that differ from output text

**UIA structure**: Same as Approach A, but with confidence scores. Lower-confidence blocks are exposed as `Text` rather than structured `Group`.

**Implementation**:
- `SemanticAnalyzer` in `wixen-core` runs after each output batch
- Maintains a state machine: `AwaitingPrompt → PromptDetected → CommandInput → OutputStreaming → CommandComplete`
- Configurable prompt regex patterns in config
- Falls back gracefully: if uncertain, expose as flat text (no worse than other terminals)

**Pros**: Works with any shell including cmd.exe. No shell configuration needed.
**Cons**: Imperfect detection. False positives possible. Cannot determine exit codes. Slower than protocol-based approach.

### 4.4 Approach C: Virtual Accessibility Document Model

**How it works**: Build a parallel DOM-like tree that represents the terminal content as a rich document, independent of the character grid. The UIA provider exposes this virtual tree rather than the raw grid.

**Structure layers**:
1. **Document layer**: The full terminal as a `Document` control type
2. **Region layer**: Scrollback vs. viewport vs. active input
3. **Block layer**: Command blocks (from Approach A/B), or line groups
4. **Inline layer**: Individual text runs with semantic roles (prompt text, command text, path, URL, error message)
5. **Table layer**: When tabular output is detected, expose as `DataGrid` with `DataItem` rows and headers

**UIA control patterns implemented**:
- `ITextProvider` / `ITextProvider2` — Full text navigation, word/line/paragraph granularity
- `ITextRangeProvider` — Text ranges with attribute queries (font, color, semantic role)
- `IGridProvider` — For detected tabular output
- `IScrollProvider` — Scrollback navigation
- `IValueProvider` — For the active input line
- `ISelectionProvider` — Text selection

**Implementation**:
```rust
// Accessibility tree node types
enum A11yNode {
    Document { children: Vec<A11yNode> },
    CommandBlock {
        prompt: TextRange,
        input: TextRange,
        output: Vec<A11yNode>,
        exit_code: Option<i32>,
    },
    TextRegion { text: String, role: SemanticRole },
    Table {
        headers: Vec<String>,
        rows: Vec<Vec<String>>,
    },
    LiveRegion { text: String, politeness: Politeness },
}

enum SemanticRole {
    Prompt,
    CommandInput,
    OutputText,
    ErrorText,
    Path,
    Url,
    StatusLine,
}
```

**Pros**: Richest possible screen reader experience. Full navigation (next/previous command, jump to error, table navigation). Framework for future enhancements.
**Cons**: Most complex to implement. Must be kept in sync with the character grid. Performance overhead for tree maintenance.

### 4.5 Approach D: UIA Grid + Text Hybrid

**How it works**: Expose the terminal as both a `Text` document (for reading) and a `DataGrid` (for cell-level navigation), letting the screen reader choose.

**Grid mode**: Every cell is a `DataItem` with row/column coordinates. Screen reader can navigate cell-by-cell (useful for TUI apps, `top`, `htop`, etc.).

**Text mode**: Lines are paragraphs. Semantic grouping from Approach A/B.

**Implementation**: Dual UIA providers on the same HWND. `IGridProvider` for grid navigation. `ITextProvider` for document reading. Control type is `Custom` to avoid assumptions.

**Pros**: Handles both structured output and TUI apps. Screen readers that support grid navigation get cell-level access.
**Cons**: Complex dual-mode UIA. Some screen readers may not handle both patterns well on the same control.

### 4.6 Approach E: AI-Assisted Output Structuring (Experimental/Future)

**How it works**: Use a local ML model or LLM API to interpret terminal output and generate structural metadata.

**Examples**:
- Detect that `ls -la` output is a table and infer column headers
- Recognize error messages and flag them
- Summarize long output for screen reader users
- Detect progress bars and announce percentage

**Implementation**: Optional feature behind a flag. Runs asynchronously. Results augment the accessibility tree from Approaches A/B/C.

**Pros**: Handles novel output formats. Could provide summaries. Impressive UX.
**Cons**: Latency. Privacy concerns (if cloud-based). Accuracy not guaranteed. Not suitable as primary approach.

### 4.7 Recommended Hybrid Strategy

**Layer 1 (Always active)**: Approach C — Virtual Accessibility Document Model as the core framework. This is the tree that UIA exposes.

**Layer 2 (Primary data source)**: Approach A — Shell Integration via OSC 133. When shells provide semantic zones, use them to populate the tree with high-confidence structural data.

**Layer 3 (Fallback)**: Approach B — Heuristic detection when OSC 133 is unavailable. Detects prompts, separates commands from output, infers tables. Lower confidence but better than nothing.

**Layer 4 (Enhancement)**: Approach D — Grid mode for TUI applications detected via alternate screen buffer activation.

**Layer 5 (Future)**: Approach E — AI structuring for advanced scenarios.

---

## 5. UIA Provider Implementation Details

### 5.1 Windows UIA Provider Architecture

Wixen Terminal implements a **server-side UIA provider** (not a proxy/wrapper). This means implementing COM interfaces that UI Automation clients (screen readers) call directly.

**Required COM interfaces**:

```rust
// Root provider — the HWND
IRawElementProviderSimple
IRawElementProviderFragment
IRawElementProviderFragmentRoot
IRawElementProviderAdviseEvents  // optional: know when clients listen

// Text support
ITextProvider
ITextProvider2
ITextRangeProvider

// Grid support (for TUI/table mode)
IGridProvider
IGridItemProvider

// Scrolling
IScrollProvider

// Value (active input)
IValueProvider

// Selection (text selection)
ISelectionProvider
```

**Implementation via `windows-rs`**:
- Use `#[implement]` macro from `windows-rs` to implement COM interfaces
- Or use `windows::core::implement` for manual COM vtable implementation
- All UIA calls happen on the UI thread (Windows requirement)
- Return `UIA_E_ELEMENTNOTAVAILABLE` for elements that have scrolled away

### 5.2 Accessibility Tree Structure

```
HWND (TerminalWindow)
└── IRawElementProviderFragmentRoot
    ├── TabBar [role=TabList]
    │   ├── Tab 1 [role=TabItem, selected]
    │   ├── Tab 2 [role=TabItem]
    │   └── NewTabButton [role=Button]
    └── TerminalView [role=Document]
        ├── ScrollbackRegion [role=Group, offscreen]
        │   ├── CommandBlock [role=Group]
        │   │   ├── Prompt [role=Text, readonly]
        │   │   ├── Command [role=Text, readonly]
        │   │   └── Output [role=Group]
        │   │       └── Text / Table / etc.
        │   └── ... (older blocks)
        ├── VisibleRegion [role=Group]
        │   ├── CommandBlock [role=Group]
        │   │   └── ...
        │   └── ActiveInput [role=Group, live region]
        │       ├── Prompt [role=Text]
        │       └── InputLine [role=Edit, focused]
        └── AlternateScreen [role=DataGrid, when active]
            └── Grid cells for TUI apps
```

### 5.3 Screen Reader Event Notifications

**Events to raise**:
- `UIA_Text_TextChangedEventId` — When terminal output arrives
- `UIA_AutomationFocusChangedEventId` — When active pane changes
- `UIA_LiveRegionChangedEventId` — For new output in the active prompt area
- `UIA_StructureChangedEventId` — When command blocks are added/removed
- `UIA_Text_TextSelectionChangedEventId` — When user selects text
- `UIA_SelectionItem_ElementSelectedEventId` — Tab selection changes

**Output throttling**: Terminal output can be extremely rapid (e.g., `cat` of a large file). Must throttle `LiveRegionChanged` events:
- Batch output into 100ms windows
- Only announce the last N lines of rapid output
- Provide a "screen reader output verbosity" setting: `all`, `commands-only`, `errors-only`, `silent`
- When output is streaming rapidly, announce "Output streaming..." once, then announce completion

### 5.4 Narrator / NVDA / JAWS Compatibility

**Narrator**:
- Reads UIA natively. Primary test target.
- Supports `TextPattern`, `Grid`, landmarks, live regions.
- Test with Scan Mode (Caps+Space) for document navigation.

**NVDA**:
- UIA support via `UIAHandler` module. Good `TextPattern` support.
- Browse mode will use the document structure for navigation.
- Announce live regions as toast-style notifications.
- Test with Browse Mode (NVDA+Space) and Focus Mode.

**JAWS**:
- UIA support but historically prefers MSAA/IAccessible.
- May need `IAccessible` bridge for full compatibility.
- Test with Virtual Cursor mode.
- JAWS uses custom scripts — may need a Wixen Terminal JAWS script.

---

## 6. Rendering Architecture

### 6.1 GPU Rendering Approaches

#### Option 1: wgpu (WebGPU Abstraction) — RECOMMENDED

**Why**: Cross-backend (Direct3D 12, Direct3D 11, Vulkan as fallback). Pure Rust. Active development. Used by Rio Terminal proving viability for terminals. Software rasterizer fallback via `wgpu`'s `Backends::GL` or `softbuffer`.

**Pipeline**:
```
Text Content → Text Shaping (rustybuzz) → Glyph Rasterization (swash/fontdue)
  → Glyph Atlas (texture) → Instance Buffer (cell positions + atlas coords)
  → wgpu Render Pipeline → Direct3D 12/11 → Present to HWND
```

**Key implementation details**:
- One `wgpu::Device` + `wgpu::Queue` per window
- Glyph atlas as a `wgpu::Texture` (RGBA8, dynamic resize)
- Cell grid as instance buffer: each cell has position, atlas UV, fg/bg colors, flags (bold, italic, underline, etc.)
- Background and cursor as separate render passes
- Window composition (acrylic/mica) via DWM APIs + alpha channel

#### Option 2: Direct3D 11 via `windows-rs`

**Why**: Lowest-level Windows GPU access. Maximum control. What Windows Terminal uses (via C++).

**Drawbacks**: More boilerplate. D3D11 API is complex. No cross-platform (not a goal, but limits contributor pool). Shader compilation via HLSL.

#### Option 3: OpenGL via `glutin` + `glow`

**Why**: What Alacritty uses. Proven approach. Simpler than D3D11.

**Drawbacks**: OpenGL is deprecated on Windows (still works, but no new features). Driver quality varies. No Mica/Acrylic integration without hacks.

**Recommendation**: **wgpu** — it provides Direct3D 12/11 on Windows natively, has software fallback, is actively maintained, and the Rust ecosystem support is strong.

### 6.2 Text Shaping Pipeline

```
Input: Unicode text + font configuration
  │
  ▼
Font Loading (wixen-render)
  - System font enumeration via DirectWrite (windows-rs)
  - Font fallback chains (user config → system fallback → last-resort)
  - Variable font support (weight, width axes)
  │
  ▼
Text Shaping (rustybuzz - HarfBuzz port in Rust)
  - Grapheme cluster segmentation (unicode-segmentation)
  - Bidi text support (unicode-bidi)
  - Ligature shaping (fi, ffi, -> →, etc.)
  - Emoji shaping (color emoji via font)
  │
  ▼
Glyph Rasterization
  - Option A: swash crate (Rust-native, good quality)
  - Option B: fontdue (faster, lower quality)
  - Option C: DirectWrite rasterization via windows-rs (highest quality, Windows-specific)
  - Subpixel rendering (ClearType on Windows)
  - Glyph hinting
  │
  ▼
Glyph Atlas Management
  - Dynamic texture atlas (grows as needed)
  - LRU eviction for glyphs not recently used
  - Separate atlas planes: regular, bold, italic, bold-italic
  - Color emoji atlas (RGBA) separate from text atlas (grayscale)
  │
  ▼
Cell Rendering
  - Each terminal cell maps to atlas coordinates
  - Wide characters (CJK) span 2 cells
  - Combining characters rendered as overlays
  - Underline/strikethrough as geometry overlays
```

### 6.3 Window Composition

**Acrylic / Mica (Windows 11)**:
- `DwmSetWindowAttribute` with `DWMWA_SYSTEMBACKDROP_TYPE`
- Mica: `DWMSBT_MAINWINDOW`
- Acrylic: `DWMSBT_TRANSIENTWINDOW`
- Requires `WS_EX_NOREDIRECTIONBITMAP` for proper alpha
- Render terminal content with alpha < 1.0 for transparency

**Background Image**:
- Load via `image` crate
- Render as first layer, terminal cells composited on top
- Gaussian blur option (compute shader in wgpu)

---

## 7. Terminal Emulation

### 7.1 VT Parser

**Option A: Custom parser (Recommended)**

Build a state-machine VT parser based on the [DEC VT500 state machine](https://vt100.net/emu/dec_ansi_parser). This is what most serious terminals do.

**States**: Ground, Escape, EscapeIntermediate, CsiEntry, CsiParam, CsiIntermediate, CsiIgnore, DcsEntry, DcsParam, DcsIntermediate, DcsPassthrough, DcsIgnore, OscString, SosPmApcString

**Supported sequences**:
- **CSI** (Control Sequence Introducer): Cursor movement, erase, scroll, SGR (colors/styles), mode set/reset, device status, window manipulation
- **OSC** (Operating System Command): Set title (OSC 0/1/2), hyperlinks (OSC 8), clipboard (OSC 52), CWD (OSC 7), shell integration (OSC 133), color queries (OSC 4/10/11)
- **DCS** (Device Control String): Sixel graphics, DECRQSS
- **ESC** sequences: Charset selection, save/restore cursor, RIS (reset)
- **SGR** (Select Graphic Rendition): 256-color, true color (24-bit), bold, dim, italic, underline, blink, inverse, strikethrough, colored underlines (CSI 58/59)

**Why custom over `vte` crate**: Full control over OSC 133 handling for accessibility. Can add custom extensions. Performance optimization (SIMD potential). The `vte` crate is a good foundation but Wixen's accessibility integration benefits from tight coupling with the parser.

**Option B: Fork/adapt `vte` crate**

Use the `vte` crate (used by Alacritty) and extend it with OSC 133 handling and custom hooks.

**Pros**: Battle-tested, less initial work.
**Cons**: Less control, may need to fork anyway for deep integration.

### 7.2 Terminal Grid / Buffer

```rust
struct TerminalGrid {
    /// Primary screen buffer
    primary: ScreenBuffer,
    /// Alternate screen buffer (for TUI apps like vim, htop)
    alternate: ScreenBuffer,
    /// Which buffer is active
    active: BufferType,
    /// Scrollback ring buffer
    scrollback: ScrollbackBuffer,
    /// Cursor state
    cursor: CursorState,
    /// Saved cursor (DECSC/DECRC)
    saved_cursor: Option<CursorState>,
    /// Terminal dimensions
    cols: u16,
    rows: u16,
    /// Semantic command blocks (for accessibility)
    command_blocks: Vec<CommandBlock>,
}

struct ScreenBuffer {
    /// Row storage — compact representation
    rows: Vec<Row>,
}

struct Row {
    /// Cell storage (optimized: most cells are simple ASCII)
    cells: Vec<Cell>,
    /// Is this row wrapped from the previous line?
    wrapped: bool,
    /// Semantic role if known
    semantic: Option<SemanticRole>,
}

struct Cell {
    /// Character (or first char of grapheme cluster)
    c: char,
    /// Extended grapheme data (if multi-char)
    extra: Option<Box<str>>,
    /// Foreground color
    fg: Color,
    /// Background color
    bg: Color,
    /// Style flags (bold, italic, underline, etc.)
    flags: CellFlags,
    /// Hyperlink ID (if OSC 8 link)
    hyperlink: Option<HyperlinkId>,
    /// Width (1 for normal, 2 for wide chars)
    width: CellWidth,
}

struct ScrollbackBuffer {
    /// Ring buffer of rows
    rows: VecDeque<Row>,
    /// Maximum capacity
    capacity: usize,
}
```

**Memory optimization**:
- Small-string optimization for `Cell::extra` — most cells are single ASCII chars
- Color interning — most cells share a small set of colors
- Row compression — long runs of empty cells stored as count rather than individually
- Scrollback compaction — old rows can be compressed (zstd) and decompressed on demand

### 7.3 ConPTY Integration

```rust
// wixen-pty crate
struct PtyProcess {
    /// ConPTY handle
    pty: HPCON,
    /// Read pipe (terminal reads PTY output)
    reader: PipeReader,
    /// Write pipe (terminal writes user input)
    writer: PipeWriter,
    /// Child process handle
    process: HANDLE,
    /// Thread handle
    thread: HANDLE,
}

impl PtyProcess {
    fn spawn(config: &ShellConfig) -> Result<Self> {
        // 1. CreatePipe for input/output
        // 2. CreatePseudoConsole (ConPTY)
        // 3. Initialize startup info with PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
        // 4. CreateProcess with the shell command
        // 5. Return PtyProcess with pipe handles
    }

    fn resize(&self, cols: u16, rows: u16) -> Result<()> {
        // ResizePseudoConsole
    }

    fn read(&self, buf: &mut [u8]) -> Result<usize> {
        // ReadFile from reader pipe
    }

    fn write(&self, data: &[u8]) -> Result<usize> {
        // WriteFile to writer pipe
    }
}
```

**Shell discovery**:
- Enumerate: `pwsh.exe` (PowerShell 7+), `powershell.exe` (Windows PowerShell 5.1), `cmd.exe`, `bash.exe` (WSL), `wsl.exe`
- Check PATH and standard install locations
- Auto-detect WSL distributions via `wsl --list`

---

## 8. Feature Specifications

### 8.1 Core Features (MVP - Phase 1)

| Feature | Description | Priority |
|---------|-------------|----------|
| Single terminal pane | Basic terminal with ConPTY | P0 |
| VT100/VT220/xterm emulation | CSI, OSC, SGR sequences | P0 |
| GPU-accelerated rendering | wgpu + glyph atlas | P0 |
| True color (24-bit) | SGR 38/48 with RGB | P0 |
| 256-color support | SGR 38/48 with palette | P0 |
| Font configuration | Family, size, weight, ligatures | P0 |
| Scrollback buffer | Configurable size, mouse scroll | P0 |
| Copy/paste | Ctrl+C/V with smart selection | P0 |
| Text selection | Mouse drag, double-click word, triple-click line | P0 |
| Basic UIA provider | TextPattern on terminal content | P0 |
| Shell integration (OSC 133) | Prompt/command/output zones | P0 |
| Structured accessibility tree | Command blocks in UIA | P0 |
| Configuration file (TOML) | Basic settings, colors, fonts | P0 |
| Cursor styles | Block, underline, bar | P0 |
| Bell handling | Visual bell (flash) | P0 |
| Unicode support | Grapheme clusters, wide chars, emoji | P0 |

### 8.2 Essential Features (Phase 2)

| Feature | Description | Priority |
|---------|-------------|----------|
| Tabs | Create, close, reorder, rename, pin | P1 |
| Split panes | Horizontal, vertical, resize, navigate | P1 |
| Search | Regex, case-insensitive, highlight all | P1 |
| URL detection | Clickable links (Ctrl+Click or auto) | P1 |
| Multiple profiles | Per-shell settings, color schemes | P1 |
| Color scheme management | Built-in schemes, import/export | P1 |
| Key binding customization | JSON/TOML keybind config | P1 |
| High contrast theme | System high-contrast detection + custom | P1 |
| DPI awareness | Per-monitor DPI, dynamic DPI changes | P1 |
| Heuristic prompt detection | Fallback accessibility (Approach B) | P1 |
| Screen reader output throttling | Batched announcements, verbosity control | P1 |
| Command palette | Ctrl+Shift+P fuzzy-search actions | P1 |
| Hot-reload config | File watcher, instant apply | P1 |
| Alternate screen buffer | Proper vim/htop/less support | P1 |
| Mouse reporting | SGR mouse mode, alternate scroll | P1 |

### 8.3 Advanced Features (Phase 3)

| Feature | Description | Priority |
|---------|-------------|----------|
| Background transparency | Acrylic/Mica on Win11, alpha blend | P2 |
| Background images | Configurable, blur, opacity | P2 |
| Ligature rendering | Programming ligatures (Fira Code, etc.) | P2 |
| Sixel graphics | Image display in terminal | P2 |
| iTerm2 image protocol | Inline image support | P2 |
| Kitty graphics protocol | Modern image protocol | P2 |
| Hyperlinks (OSC 8) | Styled, accessible hyperlinks | P2 |
| Broadcast input | Type in multiple panes simultaneously | P2 |
| Read-only mode | Lock a pane from input | P2 |
| Session save/restore | Persist layout across restarts | P2 |
| Quake/dropdown mode | Hotkey to toggle terminal overlay | P2 |
| Jump list integration | Recent profiles in Windows taskbar | P2 |
| Zoom to pane | Maximize single pane temporarily | P2 |
| Grid accessibility for TUI | DataGrid UIA for alternate screen | P2 |
| Custom shaders | User WGSL shaders for effects | P2 |
| Colored underlines | CSI 58/59 for diagnostic underlines | P2 |

### 8.4 Extended Features (Phase 4)

| Feature | Description | Priority |
|---------|-------------|----------|
| SSH connection manager | Built-in SSH profiles | P3 |
| WSL integration | Auto-detect WSL distros, profiles | P3 |
| Default terminal handler | Register as Windows default terminal | P3 |
| Context menu integration | "Open Wixen here" in Explorer | P3 |
| Snippet/macro system | Save/replay command sequences | P3 |
| Plugin/extension API | Lua or WASM-based extensions | P3 |
| AI output structuring | ML-based output parsing for a11y | P3 |
| Kitty keyboard protocol | Progressive enhancement keyboard | P3 |
| System tray | Minimize to tray, tray menu | P3 |
| Serial port connection | COM port terminal | P3 |
| Tab detach/reattach | Drag tab to new window | P3 |
| Notification on command complete | Alert when long-running command finishes | P3 |
| Command history with output | Browse past commands and their outputs | P3 |

---

## 9. Configuration System

### 9.1 Format: TOML (Primary) + JSON Schema

```toml
# wixen.toml — Default configuration

[general]
default_profile = "powershell"
startup_layout = "single"  # single, saved, specific
theme = "wixen-dark"
confirm_close = true
bell_style = "visual"  # visual, audible, none, taskbar

[rendering]
gpu_backend = "auto"  # auto, dx12, dx11, vulkan, software
vsync = true
max_fps = 0  # 0 = unlimited / vsync
font_antialiasing = "cleartype"  # cleartype, grayscale, none
transparency = 1.0  # 0.0 - 1.0
background_material = "none"  # none, acrylic, mica, mica-alt

[font]
family = "Cascadia Code"
size = 12.0
weight = "regular"  # or numeric 100-900
ligatures = true
fallback_families = ["Segoe UI Emoji", "Noto Sans"]
line_height = 1.0
cell_width = 1.0

[cursor]
style = "block"  # block, underline, bar
blinking = true
blink_rate_ms = 500
color = "#ffffff"

[scrollback]
lines = 10000
infinite = false
compression = true  # compress old scrollback to save memory

[keyboard]
# Key bindings in separate file or inline
bindings_file = "keybindings.toml"

[accessibility]
screen_reader_output = "auto"  # auto, all, commands-only, errors-only, silent
announce_command_complete = true
announce_exit_codes = true
live_region_politeness = "polite"  # polite, assertive
prompt_detection = "auto"  # auto, shell-integration, heuristic, disabled
high_contrast = "auto"  # auto, force-on, force-off
min_contrast_ratio = 4.5  # WCAG AA
reduced_motion = "auto"  # auto, on, off

[profiles.powershell]
command = "pwsh.exe"
args = ["-NoLogo"]
starting_directory = "~"
color_scheme = "wixen-dark"
icon = "pwsh"
bell_style = "visual"
env = { TERM = "xterm-256color" }
shell_integration = true

[profiles.cmd]
command = "cmd.exe"
starting_directory = "~"
color_scheme = "wixen-dark"
icon = "cmd"
shell_integration = false  # cmd doesn't support OSC 133
prompt_patterns = ['^\w:\\[^>]*>']  # heuristic fallback

[profiles.wsl]
command = "wsl.exe"
args = ["-d", "Ubuntu"]
starting_directory = "~"
color_scheme = "wixen-dark"
icon = "linux"

[color_schemes.wixen-dark]
foreground = "#d4d4d4"
background = "#1e1e1e"
cursor = "#ffffff"
selection_foreground = "#ffffff"
selection_background = "#264f78"
# ANSI colors
black = "#1e1e1e"
red = "#f44747"
green = "#6a9955"
yellow = "#d7ba7d"
blue = "#569cd6"
magenta = "#c586c0"
cyan = "#4ec9b0"
white = "#d4d4d4"
bright_black = "#808080"
bright_red = "#f44747"
bright_green = "#6a9955"
bright_yellow = "#d7ba7d"
bright_blue = "#569cd6"
bright_magenta = "#c586c0"
bright_cyan = "#4ec9b0"
bright_white = "#ffffff"
```

### 9.2 Hot-Reload System

- `notify` crate watches config file for changes
- On change: parse new config, diff against current, apply delta
- Some settings are instant (colors, fonts, opacity)
- Some settings require restart notification (GPU backend)
- Invalid config: show warning, keep previous valid config

### 9.3 JSON Schema Generation

- Use `schemars` crate to derive JSON Schema from Rust config structs
- Ship schema with the application
- IDE support: VS Code, Neovim users get autocomplete in config files
- Validate config at load time with clear error messages

---

## 10. Build, Packaging, and Distribution

### 10.1 Build System

```toml
# Cargo.toml workspace
[workspace]
members = [
    "crates/wixen-core",
    "crates/wixen-vt",
    "crates/wixen-pty",
    "crates/wixen-render",
    "crates/wixen-a11y",
    "crates/wixen-config",
    "crates/wixen-ui",
    "crates/wixen-shell-integ",
    "crates/wixen-search",
]

[workspace.dependencies]
windows = { version = "0.58", features = [...] }
wgpu = "24"
parking_lot = "0.12"
crossbeam-channel = "0.5"
serde = { version = "1", features = ["derive"] }
toml = "0.8"
unicode-segmentation = "1.11"
unicode-width = "0.2"
tracing = "0.1"
tracing-subscriber = "0.3"
```

**Toolchain**: MSVC (required for `windows-rs` and DirectX). `stable-x86_64-pc-windows-msvc`.

**Build targets**:
- `x86_64-pc-windows-msvc` (primary)
- `aarch64-pc-windows-msvc` (ARM64 Windows)

### 10.2 Packaging

| Format | Tool | Purpose |
|--------|------|---------|
| MSI | WiX Toolset v4 | Enterprise deployment, Group Policy |
| MSIX | makemsix / Windows SDK | Windows Store, auto-update |
| Portable ZIP | cargo build + zip | No-install option |
| WinGet | WinGet manifest | `winget install wixen-terminal` |
| Scoop | Scoop manifest | `scoop install wixen-terminal` |
| Chocolatey | Chocolatey package | `choco install wixen-terminal` |

### 10.3 CI/CD

- GitHub Actions workflow
- Build matrix: x64 + ARM64
- Automated tests: unit tests, VT parser conformance, accessibility tree tests
- Release pipeline: build → sign → package (MSI + MSIX + ZIP) → GitHub Release → WinGet PR

---

## 11. Testing Strategy

### 11.1 Unit Tests
- VT parser: test every CSI/OSC/DCS sequence against expected grid state
- Terminal grid: buffer operations, scrolling, resize
- Semantic analyzer: prompt detection accuracy
- Config: parsing, validation, defaults

### 11.2 VT Conformance Tests
- Use `vttest` (standard VT terminal test suite)
- Use `esctest` (iTerm2's escape sequence test suite)
- Use `wezterm-term`'s test suite as reference

### 11.3 Accessibility Tests
- Automated: UIA tree structure assertions using `UIAutomation` COM APIs in tests
- Manual: Test with Narrator, NVDA, JAWS
- Screen reader scripts: document expected behavior for each screen reader
- Accessibility audit checklist per release

### 11.4 Performance Tests
- `vtebench` for throughput (bytes/second rendering)
- Frame time measurement (must maintain <16ms for 60fps)
- Memory usage profiling (scrollback growth, glyph atlas size)
- Latency measurement: keypress to pixel (target: <10ms)

### 11.5 Integration Tests
- End-to-end: spawn shell, run commands, verify grid state
- ConPTY: resize, signal, multi-process
- Shell integration: verify OSC 133 parsing with real shells

---

## 12. Development Phases & Milestones

### Phase 0: Foundation (Weeks 1-3)
- [ ] Set up Cargo workspace with all crate stubs
- [ ] Implement Win32 window creation (HWND, WndProc)
- [ ] Basic wgpu initialization and triangle rendering
- [ ] ConPTY spawn and raw byte forwarding
- [ ] Basic event loop (input → PTY write, PTY read → screen)

### Phase 1: Basic Terminal (Weeks 4-8)
- [ ] VT parser (CSI, ESC, basic OSC)
- [ ] Terminal grid with primary/alternate buffers
- [ ] Glyph atlas and text rendering pipeline
- [ ] Cursor rendering and movement
- [ ] SGR colors (16, 256, true color)
- [ ] Scrollback buffer
- [ ] Text selection (mouse)
- [ ] Copy/paste
- [ ] Basic TOML configuration

### Phase 2: Accessibility Foundation (Weeks 9-13)
- [ ] UIA provider scaffolding (IRawElementProviderSimple, Fragment, FragmentRoot)
- [ ] TextPattern implementation on terminal buffer
- [ ] OSC 133 shell integration parsing
- [ ] CommandBlock semantic structure
- [ ] Virtual accessibility tree (Approach C)
- [ ] Live region events for new output
- [ ] Screen reader output throttling
- [ ] Narrator testing and fixes
- [ ] NVDA testing and fixes

### Phase 3: Essential Features (Weeks 14-20)
- [ ] Tab system (create, close, reorder, rename)
- [ ] Split panes (horizontal, vertical, resize)
- [ ] Search (regex, highlight)
- [ ] URL detection
- [ ] Multiple profiles
- [ ] Color scheme system
- [ ] Command palette
- [ ] Key binding customization
- [ ] Hot-reload configuration
- [ ] High contrast theme support
- [ ] DPI awareness
- [ ] Heuristic prompt detection (Approach B fallback)

### Phase 4: Polish & Advanced (Weeks 21-28)
- [ ] Acrylic/Mica transparency
- [ ] Ligature rendering
- [ ] Image protocols (Sixel, iTerm2, Kitty)
- [ ] OSC 8 hyperlinks
- [ ] Grid accessibility for TUI apps
- [ ] Colored underlines
- [ ] Session persistence
- [ ] Quake mode
- [ ] Jump list integration
- [ ] Performance optimization pass
- [ ] JAWS testing and compatibility

### Phase 5: Distribution & Extended (Weeks 29+)
- [ ] MSI/MSIX packaging
- [ ] WinGet/Scoop/Chocolatey manifests
- [ ] Default terminal handler registration
- [ ] Explorer context menu integration
- [ ] WSL auto-detection
- [ ] SSH connection manager
- [ ] Plugin/extension API design
- [ ] Public beta release

---

## 13. Architecture Decisions (FINALIZED)

### Architecture Stack

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **1. VT Parser** | **Custom** | Full control over OSC 133 integration for accessibility. Study `vte` and Alacritty's parser as references. Build from DEC VT500 state machine spec. |
| **2. GPU API** | **wgpu** | D3D12 primary, D3D11 fallback, Vulkan optional. Pure Rust, actively maintained. User-selectable renderer (auto/gpu/software via softbuffer). |
| **3. Windowing** | **Raw Win32 via windows-rs** | Full HWND/WndProc control required for deep UIA provider integration, DWM APIs (Mica/Acrylic), and custom title bar. |
| **4. Text Shaping** | **DirectWrite via windows-rs** | Best ClearType, automatic system text scaling respect, high contrast font rendering, native system font fallback chains. Chosen for superior accessibility. |
| **5. Configuration** | **TOML + Lua from start** | TOML for declarative config. Lua (via `mlua`) for programmable config, event hooks, dynamic themes from day one. |

### Accessibility Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **6. A11y Tree Depth** | **Full depth from start** | Command blocks + error/warning detection + table detection + inline semantic roles + URL exposure. Maximum screen reader value. Core differentiator — no half measures. |
| **7. cmd.exe Handling** | **Clink script + heuristic fallback** | Ship a clink integration script that adds OSC 133 to cmd.exe. Fall back to heuristic prompt detection (regex patterns for `C:\path>`) when clink is not installed. |
| **8. TUI App UIA Mode** | **Dual providers (Text + Grid)** | Expose both `ITextProvider` and `IGridProvider` simultaneously. Screen readers choose the appropriate pattern. Most flexible for diverse TUI applications. |
| **9. SR Verbosity** | **User-configurable, default all announced** | All output announced by default (assertive live region). User can configure: `all`, `commands-only`, `errors-only`, `silent`. Smart streaming detection still applies to batch rapid output. |

### UX Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **10. Tab Bar** | **User-configurable, default hidden** | Auto-hide with single tab by default. Setting: `always` / `auto` / `never`. Maximizes terminal space while remaining discoverable. |
| **11. Keybindings** | **WT defaults + a11y extras** | Match Windows Terminal conventions for standard actions (Ctrl+Shift+T, etc.). Add Wixen-specific bindings for command block navigation, error jumping, a11y review mode. |
| **12. Settings UI** | **GUI settings from Phase 1 + config files** | Build basic GUI settings early — an accessibility-focused terminal must be configurable without editing files. Full TOML + Lua config always available for power users. |
| **13. Portable Mode** | **Both methods** | Support `portable` marker file in app directory AND `--portable` / `--config-dir` CLI flags. Maximum flexibility for USB/network deployment. |

### Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **14. Min Windows** | **Windows 10 1903+ (build 18362)** | Stable ConPTY. `UiaRaiseNotificationEvent` available. Covers all currently supported Windows 10/11 versions. |
| **15. License** | **Dual MIT/Apache-2.0** | Rust ecosystem convention. Permissive. Patent protection via Apache-2.0. Maximum adoption potential. |
| **16. Scrollback** | **Infinite with zstd compression** | No line limit by default. Compress old scrollback rows with zstd on a background thread. Highest user value. |
| **17. Multi-Window** | **Design for client-server, ship single process** | Architect with message-passing abstractions that could become IPC. Ship single-process initially. Migration path to client-server planned for session persistence. |
| **18. SW Rendering** | **User-selectable renderer** | Config option: `auto` / `gpu` / `software`. Auto tries wgpu first, falls back to softbuffer. User can force software mode for RDP/troubleshooting. |

---

## 14. Key Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| UIA provider COM complexity | High — COM interfaces are error-prone in Rust | Study `windows-rs` COM implementation examples. Build scaffolding early. Test with Narrator from week 1. |
| Screen reader compatibility | High — each SR has quirks | Test with Narrator, NVDA, JAWS early and often. Engage blind/VI users for feedback. |
| ConPTY bugs | Medium — ConPTY has known issues | Target Win10 1903+. Implement workarounds for known bugs. Test on multiple Windows versions. |
| wgpu maturity | Medium — API changes between versions | Pin wgpu version. Monitor release notes. Have D3D11 fallback plan. |
| Text shaping complexity | Medium — Unicode is hard | Use established crates (rustybuzz, unicode-segmentation). Test with CJK, Arabic, emoji, combining characters. |
| Performance regression | Medium — accessibility overhead | Profile early. Lazy tree construction. Cache UIA elements. Benchmark against Alacritty. |
| Shell integration adoption | Low — users must configure shells | Ship shell integration scripts. Auto-detect and prompt. Heuristic fallback. |

---

## 15. Dependencies Summary

### Core Crates
| Crate | Version | Purpose |
|-------|---------|---------|
| `windows` | 0.58+ | Win32 API, COM, UIA, DWM, ConPTY |
| `wgpu` | 24+ | GPU rendering (D3D12/11/Vulkan) |
| `rustybuzz` | 0.18+ | Text shaping (HarfBuzz port) |
| `swash` | 0.2+ | Font loading and glyph rasterization |
| `cosmic-text` | 0.12+ | Alternative: integrated text layout engine |
| `serde` | 1.x | Serialization framework |
| `toml` | 0.8+ | TOML config parsing |
| `unicode-segmentation` | 1.11+ | Grapheme cluster segmentation |
| `unicode-width` | 0.2+ | Character display width |
| `crossbeam-channel` | 0.5+ | Multi-producer multi-consumer channels |
| `parking_lot` | 0.12+ | Fast mutex/rwlock |
| `notify` | 7+ | File system watching (config reload) |
| `tracing` | 0.1+ | Structured logging |
| `arboard` | 3+ | Clipboard access |
| `dirs` | 5+ | Platform directory paths |
| `regex` | 1.x | Search and prompt detection |
| `image` | 0.25+ | Image loading (backgrounds, Sixel) |
| `schemars` | 0.8+ | JSON Schema generation from config types |
| `zstd` | 0.13+ | Scrollback compression (Phase 3) |

---

## 16. Success Criteria

1. **Performance**: Keystroke-to-pixel latency < 10ms. 60fps rendering. `vtebench` throughput within 80% of Alacritty.
2. **Accessibility**: A blind user can navigate between commands, read output, detect errors, and use tabs/panes with NVDA or Narrator.
3. **Compatibility**: Passes `vttest` core tests. Works with PowerShell 7, PowerShell 5.1, cmd.exe, WSL bash, Python REPL, vim, htop.
4. **Stability**: No crashes in normal usage. Graceful handling of malformed escape sequences.
5. **Memory**: <100MB RSS for single terminal with 10K scrollback. <200MB for 10 tabs.

---

## Appendix A: UIA Provider Implementation Reference

### A.1 WM_GETOBJECT Handler

The entry point for UIA. When the UIA core needs to access the terminal, it sends `WM_GETOBJECT` with `lParam = UiaRootObjectId`:

```rust
fn wndproc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_GETOBJECT => {
            if lparam.0 as i32 == UiaRootObjectId as i32 {
                let provider: IRawElementProviderSimple = get_uia_provider(hwnd);
                unsafe { UiaReturnRawElementProvider(hwnd, wparam, lparam, &provider) }
            } else {
                unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
            }
        }
        _ => unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
    }
}
```

### A.2 Required `windows-rs` Features

```toml
[dependencies.windows]
version = "0.58"
features = [
    "Win32_UI_Accessibility",
    "Win32_Foundation",
    "Win32_System_Com",
    "Win32_UI_WindowsAndMessaging",
    "Win32_Graphics_Dwm",
    "Win32_System_Console",
    "Win32_UI_HiDpi",
    "Win32_UI_Shell",
    "implement",
]
```

### A.3 COM Interface Implementation Pattern

```rust
use windows::Win32::UI::Accessibility::*;
use windows::core::*;

#[implement(
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    IRawElementProviderFragmentRoot,
    ITextProvider,
    ITextProvider2,
)]
struct TerminalUiaProvider {
    hwnd: HWND,
    buffer: Arc<RwLock<TerminalBuffer>>,
    a11y_tree: Arc<RwLock<AccessibilityTree>>,
}
```

### A.4 UIA Text Units Mapping for Terminals

| UIA TextUnit | Terminal Mapping | Notes |
|--------------|-----------------|-------|
| `TextUnit_Character` | Single cell | Linguistic character, varies by grapheme |
| `TextUnit_Format` | Span of uniform SGR attributes | Same fg/bg/bold/italic |
| `TextUnit_Word` | Whitespace-delimited token | Standard word boundary |
| `TextUnit_Line` | Single terminal row | Maps directly to buffer row |
| `TextUnit_Paragraph` | **Command block** (key innovation) | Prompt + command + output grouped together |
| `TextUnit_Page` | Viewport-height chunk | ~rows visible on screen |
| `TextUnit_Document` | Entire scrollback + viewport | Full buffer content |

The `TextUnit_Paragraph` → CommandBlock mapping is the **single highest-impact accessibility improvement** over Windows Terminal's approach, where Paragraph = Line.

### A.5 UIA Event Raising Functions

```rust
fn announce_new_output(provider: &IRawElementProviderSimple, text: &str) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            UiaRaiseNotificationEvent(
                provider,
                NotificationKind_ItemAdded,
                NotificationProcessing_MostRecent,  // Only latest during rapid output
                &BSTR::from(text),
                &BSTR::from("terminal-output"),
            ).ok();
        }
    }
}

fn notify_command_complete(provider: &IRawElementProviderSimple, cmd: &str, exit_code: i32) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            let msg = if exit_code == 0 {
                format!("Command succeeded: {}", cmd)
            } else {
                format!("Command failed (exit {}): {}", exit_code, cmd)
            };
            UiaRaiseNotificationEvent(
                provider,
                NotificationKind_ActionCompleted,
                NotificationProcessing_ImportantAll,
                &BSTR::from(&msg),
                &BSTR::from("command-complete"),
            ).ok();
        }
    }
}

fn notify_text_changed(provider: &IRawElementProviderSimple) {
    unsafe {
        UiaRaiseAutomationEvent(provider, UIA_Text_TextChangedEventId).ok();
    }
}

fn notify_structure_changed(provider: &IRawElementProviderFragment, runtime_id: &[i32]) {
    unsafe {
        UiaRaiseStructureChangedEvent(
            provider,
            StructureChangeType_ChildAdded,
            runtime_id.as_ptr(),
            runtime_id.len() as i32,
        ).ok();
    }
}
```

### A.6 LiveSetting and NotificationProcessing Values

**LiveSetting** (UIA equivalent of ARIA `aria-live`):
- `Off` (0) — No live announcements
- `Polite` (1) — Announce when idle (queued) — **default for output region**
- `Assertive` (2) — Announce immediately, interrupting — **use for errors**

**NotificationProcessing** (queuing behavior):
- `ImportantAll` (0) — Immediate, deliver every notification
- `ImportantMostRecent` (1) — Immediate, only latest — **use for rapid streaming**
- `All` (2) — When idle, deliver all
- `MostRecent` (3) — When idle, only latest — **use for progress bars**
- `CurrentThenMostRecent` (4) — Finish current, then most recent

### A.7 Output Throttling Strategy

Terminal output can be thousands of lines/second. Must throttle UIA events:

1. **Debounce `TextChanged`**: Batch into 100ms windows. One event per batch.
2. **Use `NotificationProcessing_MostRecent`** for streaming output so only the last batch is spoken.
3. **Rate detection**: Track lines/second. When >10 lines/sec, switch to "streaming" mode:
   - Announce "Output streaming..." once
   - Suppress per-line announcements
   - Announce completion when output stops
4. **Progress bar detection**: Lines that overwrite themselves (CR without LF). Extract percentage and announce periodically.
5. **Verbosity setting**: User-configurable: `all`, `commands-only`, `errors-only`, `silent`.

### A.8 MSAA Bridge

Implementing UIA correctly provides automatic MSAA compatibility through the built-in UIA-MSAA bridge:
- UIA `Document` control type → MSAA `ROLE_SYSTEM_DOCUMENT`
- UIA `Name` property → MSAA `accName`
- UIA `Value` property → MSAA `accValue`
- No additional IAccessible implementation needed

This means JAWS (which historically prefers MSAA) gets basic access for free.

### A.9 High Contrast and System Accessibility Detection

```rust
// High contrast detection
fn is_high_contrast() -> bool {
    unsafe {
        let mut hc = HIGHCONTRASTW::default();
        hc.cbSize = std::mem::size_of::<HIGHCONTRASTW>() as u32;
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, hc.cbSize, Some(&mut hc as *mut _ as _), SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0)).ok();
        hc.dwFlags.contains(HCF_HIGHCONTRASTON)
    }
}

// Reduced motion detection
fn prefers_reduced_motion() -> bool {
    unsafe {
        let mut animation: BOOL = BOOL(1);
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, Some(&mut animation as *mut _ as _), SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0)).ok();
        !animation.as_bool()
    }
}

// Text scale factor (Settings > Accessibility > Text size)
fn text_scale_factor() -> f32 {
    // Read from HKCU\SOFTWARE\Microsoft\Accessibility\TextScaleFactor
    // Default is 100 (1.0x), range 100-225
}

// Listen for changes via WM_THEMECHANGED, WM_SYSCOLORCHANGE, WM_DPICHANGED
```

---

## Appendix B: Windows Terminal's UIA Limitations (What We Improve)

| Limitation in Windows Terminal | Wixen Terminal Solution |
|-------------------------------|----------------------|
| Flat text — no semantic regions | CommandBlock groups via OSC 133 + heuristics |
| `TextUnit_Paragraph` = `TextUnit_Line` | `TextUnit_Paragraph` = CommandBlock (prompt+cmd+output) |
| No error detection/navigation | Exit code tracking + color heuristics flag errors |
| No command boundary navigation | Navigate between commands via UIA paragraph/group |
| No live region semantics | `LiveSetting` on output region + `NotificationEvent` |
| No output summarization | Throttled batch announcements + verbosity control |
| TUI apps treated same as text | Mode switch to `GridProvider` on alternate screen |
| No hyperlink exposure in UIA | OSC 8 links as UIA Hyperlink child elements |
| No command completion announcements | `NotificationEvent` with exit code on OSC 133;D |
| High-frequency TextChanged floods | Debounced events + streaming detection + MostRecent processing |

---

## Appendix C: Implications Analysis — Performance, Security, Accessibility

### C.1 Performance Implications

**Rendering Pipeline**: wgpu with D3D12 primary gives near-native GPU performance. The glyph atlas (pre-rasterized via DirectWrite) means text rendering is a single texture lookup + quad draw per cell — no per-frame shaping. The softbuffer fallback trades GPU acceleration for universal compatibility but remains adequate for typical terminal workloads (<10K cells).

**Scrollback Compression**: Infinite scrollback with zstd compression runs on a background thread. Compression ratio for terminal text is typically 10-20x, so 1M lines ≈ 5-10MB RAM. The tradeoff: seeking to arbitrary scroll positions requires decompression of the target chunk (~4KB blocks), adding ~0.1ms latency per seek.

**Custom VT Parser**: A hand-rolled state machine avoids the overhead of regex-based parsing. Expected throughput: 500MB+/s of raw VT data, well above what ConPTY can deliver (~100MB/s peak).

**UIA Event Throttling**: The 100ms debounce window for TextChanged events prevents screen reader floods during rapid output (e.g., `cat large_file`). Without throttling, NVDA/JAWS can consume 100% CPU processing thousands of events/second.

**Message-Passing Architecture**: crossbeam channels add ~50ns per message vs shared mutex. For a terminal doing <10K messages/sec between threads, this is negligible. The win is eliminating lock contention entirely.

**DirectWrite Text Shaping**: Per-glyph shaping is the most expensive text operation. Caching shaped glyph runs for repeated content (prompts, common commands) amortizes the cost. Cold shaping: ~1ms for a full viewport. Cached: <0.1ms.

### C.2 Security Implications

**ConPTY Isolation**: The PTY layer provides process isolation — the terminal emulator and child shell are separate processes with separate address spaces. A malicious escape sequence cannot execute code in the terminal process; it can only manipulate the VT state machine.

**VT Parser Hardening**: Custom parser must handle malicious input: oversized OSC strings (cap at 4KB), deeply nested DCS sequences, and invalid UTF-8. Each parser state must have explicit bounds. Fuzzing with cargo-fuzz is essential — VT parsers are historically a source of CVEs.

**Lua Sandbox**: The mlua configuration layer must sandbox Lua execution: no `os.execute`, no `io.open` on arbitrary paths, no `loadfile` from untrusted sources. Config Lua should only access the Wixen API surface (theme, keybindings, hooks). The sandbox is enforced by removing dangerous globals before user script execution.

**Clipboard Security**: OSC 52 clipboard access (read/write) must require user confirmation or be disabled by default. A malicious program could otherwise silently read or overwrite clipboard contents.

**Hyperlink Safety**: OSC 8 hyperlinks can contain arbitrary URLs. Display the actual URL on hover/focus, not just the link text. Warn on non-HTTPS URLs and URLs with unusual characters (potential IDN homograph attacks).

**Memory Safety**: Rust eliminates buffer overflows and use-after-free in safe code. Unsafe blocks are confined to Win32 FFI and COM interfaces — these must be audited carefully and wrapped in safe abstractions.

**Config File Trust**: TOML config is declarative and safe. Lua config files should only be loaded from user-controlled directories (not from shell CWD or network paths) to prevent config injection.

### C.3 Accessibility Implications

**Structured UIA — The Core Innovation**: Traditional terminals expose a flat character grid. Wixen's CommandBlock model (TextUnit_Paragraph = CommandBlock) lets screen reader users navigate between commands instead of arrow-keying through thousands of lines. This is the single highest-impact accessibility feature.

**Dual Provider Strategy**: ITextProvider for the primary text view + IGridProvider for TUI/alternate screen means screen readers get semantically appropriate interfaces for both use cases. No other terminal does this.

**Output Throttling**: Without intelligent throttling, screen readers either flood users with speech or drop critical output. The streaming detection + MostRecent processing + verbosity settings give users control over the speech-to-output ratio.

**High Contrast / Text Scaling**: Detecting SystemParametersInfo for high contrast and text scaling means Wixen respects the user's system-wide accessibility settings. DirectWrite's built-in ClearType and system font fallback further ensure readability.

**GUI Settings from Phase 1**: A screen reader user cannot easily edit TOML files if they don't have a working accessible terminal. Providing a GUI settings panel (itself fully accessible via standard Win32 UIA controls) breaks this chicken-and-egg problem.

**Shell Integration Fallback Chain**: OSC 133 provides ground truth for command boundaries, but not all shells support it. The heuristic prompt detection fallback + Clink script for cmd.exe ensures accessibility features degrade gracefully rather than disappearing entirely.

**MSAA Bridge**: Implementing UIA correctly provides automatic MSAA compatibility through the system bridge. This means JAWS (which historically prefers MSAA) gets functional access without additional implementation effort.

**Keyboard-First Design**: Windows Terminal default keybindings + accessibility extras (navigate to next/prev error, jump to command boundary, announce current line) ensure all functionality is reachable without a mouse.
