# Wixen Terminal User Guide

## Getting Started

Wixen Terminal is a terminal emulator for Windows built with accessibility as a core design goal. Screen reader users get structured navigation of command blocks, output regions, and live announcements out of the box. Sighted users get GPU-accelerated rendering, font ligatures, tabs, and split panes.

### System Requirements

- Windows 10 version 1903 (build 18362) or later
- Windows 11 supported
- Any screen reader that supports UI Automation (Narrator, NVDA, JAWS)

### Installation

There are three ways to install Wixen Terminal:

1. **Installer** (recommended): Run the `.msi` installer. It adds Wixen to your Start menu and PATH.
2. **Portable**: Extract the `.zip` archive anywhere. Place a file named `portable` (no extension) next to `wixen.exe` to activate portable mode. Configuration is stored next to the executable instead of in `%APPDATA%`.
3. **winget**: Run `winget install wixen` from any terminal.

### First Launch

When you start Wixen Terminal, it opens a single tab running your default shell. On most systems this is PowerShell. If PowerShell is not found, Wixen falls back to `cmd.exe`.

The window title shows the current working directory when shell integration is active. The tab bar is hidden until you open a second tab.

## Terminal Basics

### The Terminal Window

The window has three areas:

- **Title bar**: Shows the window title. Supports dark mode by default.
- **Tab bar**: Appears when you have two or more tabs (configurable). Each tab shows the shell name or running command.
- **Terminal area**: The main area where your shell runs. This is where you type commands and see output.

### Typing Commands

Type commands as you would in any terminal. Press Enter to run them. The terminal supports full VT/ANSI escape sequences, so programs like `vim`, `htop`, and `git log` work correctly.

### Copying and Pasting Text

| Action | Shortcut |
|--------|----------|
| Copy selected text | Ctrl+Shift+C |
| Paste from clipboard | Ctrl+Shift+V |
| Select all text | Ctrl+Shift+A |

Select text by clicking and dragging with the mouse. The selection is highlighted with a blue background. Programs running in the terminal can also write to the clipboard via OSC 52 escape sequences (read access is disabled by default for security).

### Scrolling Through Output

| Action | Shortcut |
|--------|----------|
| Scroll up one page | Shift+PageUp |
| Scroll down one page | Shift+PageDown |
| Scroll to top of history | Ctrl+Home |
| Scroll to bottom | Ctrl+End |

Scrollback is infinite by default. Old output is compressed automatically to save memory.

### Clearing the Terminal

| Action | Shortcut |
|--------|----------|
| Clear the terminal screen | Ctrl+Shift+L |
| Clear scrollback history | Ctrl+Shift+K |

### Zoom (Font Size)

| Action | Shortcut |
|--------|----------|
| Increase font size | Ctrl+Plus |
| Decrease font size | Ctrl+Minus |
| Reset font size to default | Ctrl+0 |

## Tabs

### Working with Tabs

| Action | Shortcut |
|--------|----------|
| New tab | Ctrl+Shift+T |
| Close current pane (closes tab if last pane) | Ctrl+Shift+W |
| Next tab | Ctrl+Tab |
| Previous tab | Ctrl+Shift+Tab |
| Duplicate current tab | Ctrl+Shift+D |
| Switch to tab 1 | Ctrl+1 |
| Switch to tab 2 | Ctrl+2 |
| Switch to tab 3 | Ctrl+3 |
| Switch to tab 4 | Ctrl+4 |
| Switch to tab 5 | Ctrl+5 |
| Switch to tab 6 | Ctrl+6 |
| Switch to tab 7 | Ctrl+7 |
| Switch to tab 8 | Ctrl+8 |
| Switch to tab 9 | Ctrl+9 |

### Tab Indicators

Tabs show visual indicators for events:

- **Bell indicator**: Appears when a background tab triggers a bell (BEL character).
- **Exit status**: Tabs show the exit status of the last command when shell integration is active. A non-zero exit code is flagged.

### Tab Bar Display

The tab bar has three modes, controlled by the `tab_bar` setting:

| Mode | Behavior |
|------|----------|
| `auto-hide` (default) | Tab bar appears only when 2 or more tabs are open |
| `always` | Tab bar is always visible |
| `never` | Tab bar is never shown |

## Split Panes

Split panes let you run multiple terminals side by side within a single tab.

### Creating Splits

| Action | Shortcut |
|--------|----------|
| Split horizontally (left/right) | Alt+Shift+Plus |
| Split vertically (top/bottom) | Alt+Shift+Minus |

### Navigating Between Panes

| Action | Shortcut |
|--------|----------|
| Focus pane to the left | Alt+Left |
| Focus pane to the right | Alt+Right |
| Focus pane above | Alt+Up |
| Focus pane below | Alt+Down |

Navigation finds the nearest pane in the given direction based on layout position.

### Resizing Panes

| Action | Shortcut |
|--------|----------|
| Shrink current pane | Alt+Shift+Left |
| Grow current pane | Alt+Shift+Right |

The split ratio is clamped between 10% and 90% to prevent panes from becoming too small.

### Closing Panes

Press Ctrl+Shift+W to close the active pane. If the pane is the last one in the tab, the tab closes. If the tab is the last tab, the window closes.

### Zooming a Pane

Press Ctrl+Shift+Z to toggle zoom on the active pane. When zoomed, the pane fills the entire tab area. Other panes remain in the tree but are hidden. Press Ctrl+Shift+Z again to restore the original layout.

### Read-Only Mode

Press Ctrl+Shift+R to toggle read-only mode on the active pane. In read-only mode, all keyboard input to the pane is blocked. This prevents accidental input to a pane you are monitoring.

### Broadcast Input

Press Ctrl+Shift+B to toggle broadcast mode on the active pane. When broadcast is enabled, anything you type in the active pane is also sent to all other panes that have broadcast mode turned on. This is useful for running the same command on multiple servers at once.

### Pane Shortcuts Reference

| Action | Shortcut |
|--------|----------|
| Split horizontally | Alt+Shift+Plus |
| Split vertically | Alt+Shift+Minus |
| Focus pane left | Alt+Left |
| Focus pane right | Alt+Right |
| Focus pane up | Alt+Up |
| Focus pane down | Alt+Down |
| Shrink pane | Alt+Shift+Left |
| Grow pane | Alt+Shift+Right |
| Close pane | Ctrl+Shift+W |
| Toggle zoom | Ctrl+Shift+Z |
| Toggle read-only | Ctrl+Shift+R |
| Toggle broadcast input | Ctrl+Shift+B |

## Search

### Opening Search

Press Ctrl+Shift+F to open the search bar. A text field appears at the top of the terminal.

### Searching Text

Type your search term. Matches are highlighted in the terminal output as you type. The match count is displayed in the search bar.

### Navigating Matches

| Action | Shortcut |
|--------|----------|
| Next match | F3 |
| Previous match | Shift+F3 |

### Search Options

- **Case sensitivity**: Search is case-insensitive by default. Toggle case sensitivity from the search bar.
- **Regex mode**: Enable regex to search with regular expression patterns. For example, `error|warning` matches lines containing either word.

Press Escape to close the search bar.

## Command Palette

### Opening the Palette

Press Ctrl+Shift+P to open the command palette. A text field appears with a list of all available actions.

### Using the Palette

Type to filter the list. The palette uses fuzzy matching, so you can type partial words. For example, typing "split h" matches "Split Horizontal".

Press Enter to run the selected action. Press Escape to close the palette without running anything.

### SSH Targets in the Palette

If you have SSH targets configured, they appear in the command palette. Select one to open a new tab connected to that host.

## Shell Integration

Shell integration connects your shell to Wixen Terminal so it can track command boundaries, exit codes, and the current working directory. This powers several features: command block navigation for screen readers, prompt jumping, command completion announcements, and exit code detection.

### What Shell Integration Provides

- **Command blocks**: Each prompt/command/output cycle is a separate block. Screen readers can navigate between blocks.
- **Exit codes**: The terminal knows whether a command succeeded or failed.
- **Working directory**: The terminal updates the tab title and provides accurate path information.
- **Prompt jumping**: Navigate between prompts with keyboard shortcuts.

### PowerShell Setup

Add this line to your PowerShell profile (`$PROFILE`):

```powershell
. "$env:APPDATA\wixen\wixen.ps1"
```

The script checks for the `WIXEN_TERMINAL` environment variable and only activates inside Wixen Terminal. It emits OSC 133 markers (prompt start, command start, output start, command complete) and OSC 7 for the working directory.

### cmd.exe Setup (via Clink)

Install [Clink](https://chrisant996.github.io/clink/) for cmd.exe. Place the `wixen.lua` script in your Clink scripts directory. Clink loads it automatically when cmd.exe runs inside Wixen Terminal.

The Clink scripts directory is typically `%LOCALAPPDATA%\clink`.

### Jumping Between Commands

With shell integration active, you can jump between command prompts:

| Action | Shortcut |
|--------|----------|
| Jump to previous prompt | Ctrl+Shift+Up |
| Jump to next prompt | Ctrl+Shift+Down |

If shell integration is not active, Wixen falls back to heuristic prompt detection using regex patterns that match common prompt formats.

## Accessibility

Wixen Terminal is built for screen reader users from the ground up. It exposes a full UI Automation (UIA) tree with structured command blocks, live output regions, and labeled controls.

### Screen Reader Support

Wixen works with any screen reader that supports UI Automation:

- **Narrator** (built into Windows)
- **NVDA** (free, open source)
- **JAWS** (commercial)

No extra configuration is needed. Wixen detects your screen reader automatically.

### Command Block Navigation

When shell integration is active, the terminal organizes output into command blocks. Each block contains:

- The prompt text
- The command you typed
- The command's output
- The exit code

Screen readers can navigate between these blocks using their standard object navigation commands. Each block is announced with a summary like "cargo build completed: 42 lines of output" or "cargo test failed (exit 1): 100 lines of output".

### Output Announcements

Wixen announces terminal output through UIA live regions. The behavior depends on the verbosity setting.

### Verbosity Levels

Set `screen_reader_output` in your config to control how much output is announced:

| Level | Behavior |
|-------|----------|
| `auto` (default) | Detects screen reader presence. Announces all output when a screen reader is running. |
| `all` | Announces all terminal output. |
| `commands-only` | Announces only command completions (prompt, exit code, output summary). |
| `errors-only` | Announces only errors (non-zero exit codes, detected error lines). |
| `silent` | No automatic announcements. You navigate output manually. |

### Live Region Politeness

The `live_region_politeness` setting controls how announcements interact with speech already in progress:

| Value | Behavior |
|-------|----------|
| `polite` (default) | Queues announcements behind current speech. |
| `assertive` | Interrupts current speech with the new announcement. |

### Audio Feedback

Wixen can play short audio tones for terminal events. These work with or without a screen reader.

| Setting | Default | Description |
|---------|---------|-------------|
| `audio_feedback` | `false` | Master switch for all audio tones. |
| `audio_progress` | `true` | Tone when a progress bar is detected. |
| `audio_errors` | `true` | Tone when an error or warning is detected in output. |
| `audio_command_complete` | `true` | Tone when a command finishes. |

Note: `audio_progress`, `audio_errors`, and `audio_command_complete` only produce sound when `audio_feedback` is `true`.

### Error and Warning Detection

Wixen scans command output for error and warning patterns. When detected, these lines are marked in the UIA tree so screen readers can identify them. If audio feedback is enabled, an error tone plays.

### Progress Bar Detection

Wixen detects progress indicators in terminal output (percentage displays, progress bars). When detected and audio feedback is enabled, a tone plays to indicate progress.

### Password Prompt Detection

Wixen detects password prompts in terminal output. This prevents the terminal from announcing sensitive input and lets screen readers inform the user that a password is being requested.

### High Contrast Mode

The `high_contrast` setting controls high contrast rendering:

| Value | Behavior |
|-------|----------|
| `auto` (default) | Follows the Windows system high contrast setting. |
| `on` | Forces high contrast mode on. |
| `off` | Forces high contrast mode off. |

### Minimum Contrast Ratio

The `min_contrast_ratio` setting (default `4.5`) adjusts terminal colors to meet a minimum foreground-to-background contrast ratio. WCAG AA requires 4.5:1. WCAG AAA requires 7.0:1. Set to `0.0` to disable contrast adjustment.

When a program sets colors below the threshold, Wixen adjusts the foreground color to meet the minimum ratio.

### Reduced Motion

The `reduced_motion` setting controls animations:

| Value | Behavior |
|-------|----------|
| `auto` (default) | Follows the Windows "Show animations" system setting. |
| `on` | Disables cursor blink, visual bell flash, and smooth scrolling. |
| `off` | Keeps all animations enabled. |

### Pane Position Announcements

When `announce_pane_position` is `true` (default), switching panes announces the position: "Left pane, 1 of 3".

### Tab Detail Announcements

When `announce_tab_details` is `true` (default), switching tabs announces the tab title, shell type, and status.

### Image Announcements

When `announce_images` is `true` (default), inline images placed via Sixel, iTerm2, or Kitty protocols are announced with their dimensions and filename (if available).

## Settings

### Opening Settings

Press Ctrl+Comma to open the settings overlay. The settings panel appears on top of the terminal and is fully keyboard-navigable.

### Settings Tabs

The settings panel has seven tabs:

| Tab | Contents |
|-----|----------|
| Font | Font family, size, line height, ligatures, fallback fonts, font file path |
| Window | Window size, title, renderer, opacity, background style, background image, quake mode, scrollbar mode, tab bar mode, padding |
| Terminal | Scrollback lines, cursor style, cursor blink, bell style, OSC 52 policy, command notification threshold, session restore |
| Colors | Theme selection, foreground color, background color, cursor color, selection color, palette overrides |
| Profiles | Shell profiles (name, program, arguments, working directory, default flag) |
| Keybindings | All keyboard shortcuts with chord editor (modifier checkboxes and key input) |
| Accessibility | Screen reader verbosity, announcements, audio feedback, contrast, motion, prompt detection |

### Navigating Settings

- Press **Left/Right arrow** to switch between tabs.
- Press **Up/Down arrow** to move between fields within a tab.
- Press **Enter** or **Space** to edit a field (toggle, dropdown, or text input).
- Press **Escape** to stop editing a field, or to close the settings panel.

### Saving Settings

Changes are collected into a draft. When you close the settings panel, you are prompted to save or discard. Saved changes are written to your `config.toml` file and take effect immediately.

## Configuration File

### File Location

Wixen looks for configuration in this order:

1. `config.toml` next to the executable (portable mode, requires a `portable` marker file)
2. `%APPDATA%\wixen\config.toml`

### Format

Configuration files use [TOML](https://toml.io/) format. Lua configuration (`config.lua` in the same directory) runs after TOML and can override any setting.

### Hot Reload

Changes to the configuration file are detected automatically. Most settings take effect immediately without restarting Wixen.

### Full Configuration Reference

#### [font]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `family` | string | `"Cascadia Code"` | Font family name (DirectWrite lookup). |
| `size` | float | `14.0` | Font size in points. Valid range: 4.0 to 128.0. |
| `line_height` | float | `1.0` | Line height multiplier. 1.0 is tight, 1.2 is comfortable. Valid range: 0.5 to 3.0. |
| `fallback_fonts` | string array | `["Segoe UI Emoji", "Segoe UI Symbol", "MS Gothic"]` | Fallback fonts tried when the primary font lacks a glyph. |
| `ligatures` | bool | `true` | Enable programming ligatures (e.g., `=>`, `->`, `!=`). |
| `font_path` | string | `""` | Path to a `.ttf` or `.otf` file to load directly. When set, takes priority over `family`. |

#### [window]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `width` | integer | `1024` | Initial window width in pixels. Minimum: 100. |
| `height` | integer | `768` | Initial window height in pixels. Minimum: 100. |
| `title` | string | `"Wixen Terminal"` | Window title text. |
| `renderer` | string | `"auto"` | Renderer backend: `"auto"`, `"gpu"`, or `"software"`. Auto tries GPU first (D3D12, then D3D11), then falls back to software. |
| `opacity` | float | `1.0` | Window opacity. 0.0 is fully transparent, 1.0 is fully opaque. |
| `background` | string | `"opaque"` | Background style: `"opaque"`, `"acrylic"`, or `"mica"`. Acrylic and Mica require Windows 11 22H2 or later. |
| `dark_title_bar` | bool | `true` | Use dark title bar decoration. |
| `background_image` | string | `""` | Path to a background image (PNG, JPEG, GIF). Empty disables. |
| `background_image_opacity` | float | `0.3` | Background image opacity. 0.0 is invisible, 1.0 is fully opaque. |
| `quake_mode` | bool | `false` | Enable quake mode. A global hotkey toggles the terminal window. |
| `quake_hotkey` | string | `"win+backtick"` | Global hotkey chord for quake mode. |
| `scrollbar` | string | `"auto"` | Scrollbar display: `"auto"` (visible when scrolled), `"always"`, or `"never"`. |
| `tab_bar` | string | `"auto-hide"` | Tab bar display: `"auto-hide"` (2+ tabs), `"always"`, or `"never"`. |

#### [window.padding]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `left` | integer | `4` | Left padding in pixels between window edge and terminal grid. |
| `right` | integer | `4` | Right padding in pixels. |
| `top` | integer | `4` | Top padding in pixels. |
| `bottom` | integer | `4` | Bottom padding in pixels. |

#### [terminal]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `scrollback_lines` | integer | `0` | Maximum scrollback lines. 0 means infinite scrollback with automatic compression. |
| `cursor_style` | string | `"block"` | Cursor style: `"block"`, `"underline"`, or `"bar"`. |
| `cursor_blink` | bool | `true` | Whether the cursor blinks. |
| `cursor_blink_ms` | integer | `530` | Cursor blink interval in milliseconds. Valid range: 100 to 5000. |
| `bell` | string | `"both"` | Bell notification: `"audible"` (system sound), `"visual"` (flash), `"both"`, or `"none"`. |
| `osc52` | string | `"write-only"` | Clipboard access for programs: `"write-only"` (safe default), `"read-write"`, or `"disabled"`. |
| `command_notify_threshold` | integer | `10` | Seconds a command must run before triggering a completion notification when the window is unfocused. 0 disables. |
| `session_restore` | string | `"never"` | Session restore on startup: `"never"`, `"always"`, or `"ask"`. |

#### [colors]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `theme` | string | (none) | Built-in theme name. Sets base colors that individual fields below can override. See [Color Schemes](#color-schemes). |
| `foreground` | string | `"#d9d9d9"` | Foreground text color as `#RRGGBB` hex. |
| `background` | string | `"#0d0d14"` | Background color as `#RRGGBB` hex. |
| `cursor` | string | `"#cccccc"` | Cursor color as `#RRGGBB` hex. |
| `selection_bg` | string | `"#264f78"` | Selection background color as `#RRGGBB` hex. |
| `palette` | string array | `[]` | ANSI 16-color palette overrides, indexed 0 through 15. Each entry is a `#RRGGBB` hex string. Empty uses theme or default colors. |

#### [shell]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `program` | string | `""` | Shell program to launch. Empty auto-detects (PowerShell, then cmd.exe). |
| `args` | string array | `[]` | Arguments to pass to the shell. |
| `working_directory` | string | `""` | Starting directory. Empty inherits from the parent process. |

#### [keybindings]

Keybindings are defined as an array of entries. Each entry has a `chord` (key combination) and an `action`. Some actions accept an optional `args` field.

```toml
[[keybindings.bindings]]
chord = "ctrl+shift+t"
action = "new_tab"

[[keybindings.bindings]]
chord = "ctrl+alt+1"
action = "send_text"
args = "echo hello\n"
```

Chords are written as modifier keys joined by `+`, followed by the key name. Modifiers are normalized to alphabetical order: `alt`, `ctrl`, `shift`, `win`.

The `args` field supports escape sequences: `\n` (newline), `\r` (carriage return), `\t` (tab), `\x1b` (escape), `\\` (literal backslash).

#### [accessibility]

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `screen_reader_output` | string | `"auto"` | Verbosity: `"auto"`, `"all"`, `"commands-only"`, `"errors-only"`, `"silent"`. |
| `announce_command_complete` | bool | `true` | Announce when commands finish (includes exit code and output line count). |
| `announce_exit_codes` | bool | `true` | Include exit codes in completion announcements. |
| `live_region_politeness` | string | `"polite"` | UIA live region mode: `"polite"` (queues) or `"assertive"` (interrupts). |
| `prompt_detection` | string | `"auto"` | How prompts are detected: `"auto"`, `"shell-integration"`, `"heuristic"`, `"disabled"`. |
| `high_contrast` | string | `"auto"` | High contrast mode: `"auto"` (follow Windows), `"on"`, `"off"`. |
| `min_contrast_ratio` | float | `4.5` | Minimum foreground/background contrast ratio. WCAG AA is 4.5, AAA is 7.0. Set 0.0 to disable. |
| `reduced_motion` | string | `"auto"` | Reduced motion: `"auto"` (follow Windows), `"on"`, `"off"`. |
| `announce_images` | bool | `true` | Announce inline images (Sixel, iTerm2, Kitty) with dimensions and filename. |
| `announce_pane_position` | bool | `true` | Announce pane position when switching panes (e.g., "Left pane, 1 of 3"). |
| `announce_tab_details` | bool | `true` | Announce tab details when switching (title, shell, status). |
| `audio_feedback` | bool | `false` | Master switch for audio feedback tones. |
| `audio_progress` | bool | `true` | Play a tone on progress bar detection (requires `audio_feedback = true`). |
| `audio_errors` | bool | `true` | Play a tone on error/warning detection (requires `audio_feedback = true`). |
| `audio_command_complete` | bool | `true` | Play a tone on command completion (requires `audio_feedback = true`). |

## Profiles

### What Profiles Are

Profiles let you define multiple shell configurations. Each profile has a name, a shell program, arguments, and a working directory. You can open a new tab with a specific profile from the command palette.

If you define no profiles, Wixen creates one from the `[shell]` section of your config.

### Creating a Profile

Add profile entries to your `config.toml`:

```toml
[[profiles]]
name = "PowerShell"
program = "pwsh.exe"
args = ["-NoLogo"]
working_directory = ""
is_default = true

[[profiles]]
name = "Command Prompt"
program = "cmd.exe"
args = []
working_directory = "C:\\Projects"
is_default = false
```

The profile with `is_default = true` is used for new tabs opened with Ctrl+Shift+T.

### SSH Profiles

Define SSH targets in your config. They appear in the command palette.

```toml
[[ssh]]
name = "dev-server"
host = "dev.example.com"
port = 22
user = "deploy"
identity_file = ""
extra_args = []
```

Opening an SSH target launches a new tab running `ssh.exe` with the configured host, port, and user.

### WSL Profiles

Wixen auto-detects installed WSL distributions by running `wsl -l -v`. Detected distributions appear as profiles in the command palette. You do not need to configure them manually.

## Color Schemes

### Built-in Schemes

Wixen includes five built-in color schemes:

| Theme name | Description |
|------------|-------------|
| `dracula` | Dark theme with purple and pink accents |
| `catppuccin-mocha` | Warm dark theme (also accepts `catppuccin`) |
| `solarized-dark` | Ethan Schoonover's dark Solarized palette (also accepts `solarized`) |
| `one-dark` | Atom One Dark colors (also accepts `onedark`) |
| `gruvbox-dark` | Retro groove colors (also accepts `gruvbox`) |

To use a built-in scheme, set the `theme` field in your `[colors]` section:

```toml
[colors]
theme = "catppuccin-mocha"
```

Individual color fields override theme colors. For example, you can use the Dracula theme but change the background:

```toml
[colors]
theme = "dracula"
background = "#1a1a2e"
```

### Custom Color Schemes

Override individual colors without using a theme:

```toml
[colors]
foreground = "#e0e0e0"
background = "#1c1c1c"
cursor = "#ff6600"
selection_bg = "#3a3a5c"
palette = [
    "#1c1c1c",  # black
    "#ff5555",  # red
    "#50fa7b",  # green
    "#f1fa8c",  # yellow
    "#bd93f9",  # blue
    "#ff79c6",  # magenta
    "#8be9fd",  # cyan
    "#f8f8f2",  # white
    "#6272a4",  # bright black
    "#ff6e6e",  # bright red
    "#69ff94",  # bright green
    "#ffffa5",  # bright yellow
    "#d6acff",  # bright blue
    "#ff92df",  # bright magenta
    "#a4ffff",  # bright cyan
    "#ffffff",  # bright white
]
```

The `palette` array contains 16 colors in standard ANSI order: black, red, green, yellow, blue, magenta, cyan, white, then the bright variants of each.

## Keyboard Shortcuts Reference

### General

| Action | Shortcut |
|--------|----------|
| Command palette | Ctrl+Shift+P |
| Settings | Ctrl+Comma |
| Open config file | Ctrl+Shift+Comma |
| Toggle fullscreen | F11 |
| Toggle fullscreen (alternate) | Alt+Enter |
| New window | Ctrl+Shift+N |

### Clipboard

| Action | Shortcut |
|--------|----------|
| Copy | Ctrl+Shift+C |
| Paste | Ctrl+Shift+V |
| Select all | Ctrl+Shift+A |

### Tabs

| Action | Shortcut |
|--------|----------|
| New tab | Ctrl+Shift+T |
| Close pane/tab | Ctrl+Shift+W |
| Next tab | Ctrl+Tab |
| Previous tab | Ctrl+Shift+Tab |
| Duplicate tab | Ctrl+Shift+D |
| Switch to tab 1-9 | Ctrl+1 through Ctrl+9 |

### Panes

| Action | Shortcut |
|--------|----------|
| Split horizontally | Alt+Shift+Plus |
| Split vertically | Alt+Shift+Minus |
| Focus pane left | Alt+Left |
| Focus pane right | Alt+Right |
| Focus pane up | Alt+Up |
| Focus pane down | Alt+Down |
| Shrink pane | Alt+Shift+Left |
| Grow pane | Alt+Shift+Right |
| Toggle zoom | Ctrl+Shift+Z |
| Toggle read-only | Ctrl+Shift+R |
| Toggle broadcast input | Ctrl+Shift+B |

### Scrolling

| Action | Shortcut |
|--------|----------|
| Scroll up one page | Shift+PageUp |
| Scroll down one page | Shift+PageDown |
| Scroll to top | Ctrl+Home |
| Scroll to bottom | Ctrl+End |
| Jump to previous prompt | Ctrl+Shift+Up |
| Jump to next prompt | Ctrl+Shift+Down |

### Search

| Action | Shortcut |
|--------|----------|
| Open search | Ctrl+Shift+F |
| Next match | F3 |
| Previous match | Shift+F3 |

### Zoom

| Action | Shortcut |
|--------|----------|
| Zoom in | Ctrl+Plus |
| Zoom out | Ctrl+Minus |
| Reset zoom | Ctrl+0 |

### Terminal

| Action | Shortcut |
|--------|----------|
| Clear terminal | Ctrl+Shift+L |
| Clear scrollback | Ctrl+Shift+K |

### Customizing Shortcuts

Override or add shortcuts in `config.toml`. New bindings are added to the default set. To replace a default binding, define a new binding with the same chord and a different action.

```toml
[[keybindings.bindings]]
chord = "ctrl+shift+t"
action = "new_tab"

[[keybindings.bindings]]
chord = "ctrl+shift+e"
action = "split_horizontal"
```

You can also add keybindings through Lua configuration (`config.lua`):

```lua
wixen.bind("ctrl+shift+e", "split_horizontal")
wixen.bind("ctrl+alt+1", "send_text", "echo hello\n")
```

### Available Actions

| Action name | Description |
|-------------|-------------|
| `new_tab` | Open a new tab |
| `close_pane` | Close the active pane (closes tab if last pane) |
| `next_tab` | Switch to the next tab |
| `prev_tab` | Switch to the previous tab |
| `duplicate_tab` | Duplicate the current tab |
| `select_tab_1` through `select_tab_9` | Switch to tab by number |
| `split_horizontal` | Split the pane left/right |
| `split_vertical` | Split the pane top/bottom |
| `focus_pane_left` | Move focus to the pane on the left |
| `focus_pane_right` | Move focus to the pane on the right |
| `focus_pane_up` | Move focus to the pane above |
| `focus_pane_down` | Move focus to the pane below |
| `resize_pane_shrink` | Shrink the current pane |
| `resize_pane_grow` | Grow the current pane |
| `toggle_zoom` | Toggle pane zoom |
| `toggle_read_only` | Toggle read-only mode on the current pane |
| `toggle_broadcast_input` | Toggle broadcast input on the current pane |
| `copy` | Copy selected text to clipboard |
| `paste` | Paste from clipboard |
| `select_all` | Select all text in the terminal |
| `find` | Open the search bar |
| `find_next` | Jump to the next search match |
| `find_previous` | Jump to the previous search match |
| `command_palette` | Open the command palette |
| `settings` | Open the settings panel |
| `open_config_file` | Open the config file in your default editor |
| `toggle_fullscreen` | Toggle fullscreen mode |
| `new_window` | Open a new Wixen Terminal window |
| `zoom_in` | Increase font size |
| `zoom_out` | Decrease font size |
| `zoom_reset` | Reset font size to the configured default |
| `scroll_up_page` | Scroll up one page |
| `scroll_down_page` | Scroll down one page |
| `scroll_to_top` | Scroll to the top of scrollback |
| `scroll_to_bottom` | Scroll to the bottom |
| `jump_to_previous_prompt` | Jump to the previous command prompt |
| `jump_to_next_prompt` | Jump to the next command prompt |
| `clear_terminal` | Clear the terminal screen |
| `clear_scrollback` | Clear the scrollback buffer |
| `send_text` | Send text to the terminal (requires `args`) |

## Troubleshooting

### Terminal Won't Start

If the GPU renderer fails, Wixen falls back to software rendering automatically. If you see a blank window or crash on startup:

1. Try setting `renderer = "software"` in your `config.toml`.
2. If the config file is corrupt, delete it and restart. Wixen recreates it with defaults.
3. Check that your Windows version is 1903 (build 18362) or later.

### No Shell Integration Markers

If prompt jumping and command completion announcements do not work:

1. Verify the integration script is loaded. For PowerShell, check that `. "$env:APPDATA\wixen\wixen.ps1"` is in your `$PROFILE`. For cmd.exe, check that `wixen.lua` is in your Clink scripts directory.
2. Confirm the `WIXEN_TERMINAL` environment variable is set. Run `echo $env:WIXEN_TERMINAL` in PowerShell or `echo %WIXEN_TERMINAL%` in cmd.exe. It should print a value.
3. If shell integration is unavailable, Wixen uses heuristic prompt detection as a fallback. You can force this with `prompt_detection = "heuristic"` in your config.

### Screen Reader Not Reading Output

1. Check the `screen_reader_output` setting. It should be `"auto"` or `"all"`. If set to `"silent"`, no output is announced.
2. Verify your screen reader is running and UIA support is enabled.
3. Try setting `live_region_politeness = "assertive"` if announcements are being lost.

### High CPU Usage

1. If scrollback is large, the terminal may use more memory and CPU. Set `scrollback_lines` to a fixed number (e.g., `50000`) to limit it.
2. Check if `cursor_blink` is causing redraws. Try `cursor_blink = false`.
3. Try `renderer = "software"` if the GPU driver is causing issues.

### Reporting Issues

Report bugs on the GitHub issues page. Include:

- Your Windows version (`winver`)
- Your screen reader and version (if applicable)
- Steps to reproduce the problem
- The relevant section of your `config.toml`
