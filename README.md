# Wixen Terminal

A terminal emulator for Windows with structured accessibility built into the architecture.

Screen reader users navigate between commands, read output by region, detect errors by sound, and control every setting through the keyboard. Sighted users get GPU-accelerated rendering, split panes, tabs, and a configurable keybinding system. Both experiences are first-class.

## Quick Start

**Install** using one of these methods:

| Method | Command |
|--------|---------|
| WinGet | `winget install PratikP1.WixenTerminal` |
| Installer | Download the `.exe` installer from [Releases](https://github.com/PratikP1/Wixen-Terminal/releases) |
| Portable | Download the `.zip`, extract anywhere, run `wixen.exe` |

**Launch.** Wixen Terminal opens PowerShell by default. If PowerShell is not installed, it falls back to `cmd.exe`.

**Set up shell integration** for command block navigation and exit code tracking:

PowerShell (add to your `$PROFILE`):

```powershell
. "$env:APPDATA\wixen\wixen.ps1"
```

cmd.exe (place in your clink scripts directory):

```
Copy wixen.lua to %LocalAppData%\clink\
```

**Press F1** at any time to open the full user guide.

## Core Features

**Accessibility.** Wixen exposes a structured UIA tree, not a flat text buffer. Screen readers see individual command blocks with prompts, input, output, and exit codes as separate navigable regions. The terminal announces errors, progress percentages, and mode changes through UIA notifications and optional audio tones.

**Tabs and panes.** Split the terminal horizontally or vertically. Navigate between panes with Alt+Arrow keys. Zoom a pane to fill the window with Ctrl+Shift+Z. Broadcast input to all panes simultaneously.

**Shell integration.** With OSC 133 markers from your shell, you can jump between commands (Ctrl+Shift+Up/Down), see exit codes, and get summaries of command output. Without shell integration, heuristic prompt detection provides basic command boundaries.

**GPU rendering.** The wgpu renderer uses Direct3D 12 (with D3D11 and software fallbacks). Text shaping goes through DirectWrite for ClearType and ligature support.

**Configuration.** TOML config with Lua scripting and hot reload. A JSON Schema is included for editor autocompletion. Every setting is also available through the in-terminal settings UI (Ctrl+Comma), which is fully keyboard-navigable.

## Common Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Shift+T | New tab |
| Ctrl+Shift+W | Close pane (closes tab if last pane) |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab |
| Alt+Shift+Plus | Split pane horizontally |
| Alt+Shift+Minus | Split pane vertically |
| Alt+Arrow keys | Move between panes |
| Ctrl+Shift+F | Search |
| Ctrl+Shift+P | Command palette |
| Ctrl+Comma | Settings |
| Ctrl+Shift+Up/Down | Jump to previous/next command |
| Ctrl+Shift+Z | Zoom pane (toggle) |
| F1 | Open user guide |
| F11 | Toggle fullscreen |

See the [full keyboard reference](docs/user-guide.md#keyboard-shortcuts-reference) for all 48 default shortcuts.

## Accessibility

Wixen Terminal was designed for screen reader users from the start. Key features:

- Command blocks exposed as navigable UIA regions (not flat text)
- Error and warning lines detected and announced
- Progress bar percentages extracted and spoken
- Password prompts detected (no-echo notification)
- Configurable verbosity: all output, commands only, errors only, or silent
- Audio feedback tones for errors, progress, and command completion (works without a screen reader)
- Minimum contrast ratio enforcement (WCAG AA 4.5:1 default)
- Reduced motion support (follows Windows system setting)
- Pane position and tab context announced on switch

All accessibility settings are available in Settings (Ctrl+Comma) under the Accessibility tab.

## Documentation

| Document | Description |
|----------|-------------|
| [User Guide](docs/user-guide.md) | Complete usage documentation. Covers every feature, setting, and keyboard shortcut. |
| [Architecture](docs/architecture.md) | Technical documentation for developers. Crate structure, threading model, rendering pipeline, accessibility design. |
| [Default Config](config/default.toml) | Annotated configuration file with every option and its default value. |
| [Config Schema](schemas/config.schema.json) | JSON Schema for editor autocompletion in config.toml files. |

## Building from Source

Requirements: Rust stable toolchain (MSVC target), Windows 10 SDK.

```
cargo build --release
```

The binary is `target\release\wixen.exe`.

Run the test suite:

```
cargo test --workspace
```

Run benchmarks:

```
cargo bench
```

## Requirements

- Windows 10 version 1903 (build 18362) or later
- Windows 11 supported

## License

Dual-licensed under MIT and Apache 2.0. See [LICENSE-MIT](LICENSE-MIT) and [LICENSE-APACHE](LICENSE-APACHE).
