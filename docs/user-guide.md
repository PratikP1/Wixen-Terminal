# Wixen Terminal User Guide

## Getting Started

Wixen Terminal is a terminal emulator for Windows built for screen reader users first. It gives screen readers structured navigation of commands and output, spoken announcements, and audio cues. Sighted users get GPU-accelerated rendering, font ligatures, tabs, and split panes.

New to terminals? A terminal runs a shell, which is a program that reads the commands you type and runs them. Wixen adds structure on top: it knows where each command starts and ends, so your screen reader can move through your session command by command instead of reading a wall of text.

### System Requirements

- Windows 10 version 1903 (build 18362) or later
- Windows 11 supported
- Any screen reader that supports UI Automation (Narrator, NVDA, JAWS)

### Installation

There are three ways to install Wixen Terminal:

1. **Installer** (recommended): Run the `.msi` installer from the Releases page. It adds Wixen Terminal to your Start menu.
2. **Portable**: Extract the `.zip` archive anywhere and run `wixen.exe`. The archive includes a marker file named `wixen.portable` next to the program. This marker keeps your settings next to `wixen.exe` instead of in `%APPDATA%`, so you can carry the whole folder on a USB drive. You can also start portable mode with the `--portable` command line flag.
3. **winget**: Run `winget install PratikP1.WixenTerminal` from any terminal.

### First Launch

When you start Wixen Terminal, it opens a single tab running your default shell. On most systems this is PowerShell. If PowerShell is not found, Wixen falls back to `cmd.exe`.

The window title shows the current working directory when shell integration is active. The tab bar stays hidden until you open a second tab.

## Terminal Basics

### The Terminal Window

The window has three areas:

- **Title bar**: Shows the window title. Uses dark mode by default.
- **Tab bar**: Appears when you have two or more tabs (configurable). Each tab shows the shell name or running command.
- **Terminal area**: The main area where your shell runs. This is where you type commands and see output.

### Typing Commands

Type commands as you would in any terminal. Press Enter to run them. The terminal supports the full set of VT/ANSI escape sequences (the control codes that full-screen programs use), so programs like `vim`, `htop`, and `git log` work correctly.

### Copying and Pasting Text

| Action | Shortcut |
|--------|----------|
| Copy selected text | Ctrl+Shift+C |
| Paste from clipboard | Ctrl+Shift+V |
| Select all text | Ctrl+Shift+A |

Select text by clicking and dragging with the mouse. The selection shows a blue background.

Programs can also put text on the clipboard through an escape code called OSC 52. For safety, programs can write to your clipboard but cannot read it. The `osc52` setting controls this.

### Scrolling Through Output

Output that scrolls off screen is kept in a history called scrollback. You can scroll back through it at any time:

| Action | Shortcut |
|--------|----------|
| Scroll up one page | Shift+PageUp |
| Scroll down one page | Shift+PageDown |
| Scroll to top of history | Ctrl+Home |
| Scroll to bottom | Ctrl+End |

Scrollback is unlimited by default. Wixen compresses old output automatically to save memory.

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

Tabs let you run several shell sessions in one window, like tabs in a web browser. Use them to keep a long build running in one tab while you work in another.

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

For example: press Ctrl+Shift+T to open a second tab, start a build there, then press Ctrl+Tab to return to your first tab. When you switch tabs, Wixen announces the tab title, shell type, and status (see [Tab Detail Announcements](#accessibility)).

### Tab Indicators

Tabs show visual indicators for events:

- **Bell indicator**: Appears when a background tab triggers a bell (the BEL character some programs send to get your attention).
- **Exit status**: Tabs show the exit status of the last command when shell integration is active. A non-zero exit code is flagged.

### Tab Bar Display

The tab bar has three modes, controlled by the `tab_bar` setting:

| Mode | Behavior |
|------|----------|
| `auto-hide` (default) | Tab bar appears only when 2 or more tabs are open |
| `always` | Tab bar is always visible |
| `never` | Tab bar is never shown |

## Split Panes

Split panes let you run multiple terminals side by side within a single tab. Use a split when you want two things on screen at once, such as a server log in one pane and a shell prompt in the other.

### Creating Splits

| Action | Shortcut |
|--------|----------|
| Split horizontally (left/right) | Alt+Shift+Plus |
| Split vertically (top/bottom) | Alt+Shift+Minus |

For example: press Alt+Shift+Plus to place a second pane to the right of the current one. Each pane runs its own shell.

### Navigating Between Panes

| Action | Shortcut |
|--------|----------|
| Focus pane to the left | Alt+Left |
| Focus pane to the right | Alt+Right |
| Focus pane above | Alt+Up |
| Focus pane below | Alt+Down |

When you press Alt plus an arrow key, focus moves to the nearest pane in that direction. By default, Wixen announces the new position, such as "Left pane, 1 of 3".

### Resizing Panes

| Action | Shortcut |
|--------|----------|
| Shrink current pane | Alt+Shift+Left |
| Grow current pane | Alt+Shift+Right |

The split ratio stays between 10% and 90%, so a pane can never shrink to nothing.

### Closing Panes

Press Ctrl+Shift+W to close the active pane. If the pane is the last one in the tab, the tab closes. If the tab is the last tab, the window closes.

### Zooming a Pane

Press Ctrl+Shift+Z to toggle zoom on the active pane. When zoomed, the pane fills the entire tab area. Other panes stay open but are hidden. Press Ctrl+Shift+Z again to restore the original layout.

Use zoom when you need to focus on one pane temporarily, such as reading long output, without tearing down your split layout.

### Read-Only Mode

Press Ctrl+Shift+R to toggle read-only mode on the active pane. In read-only mode, the pane ignores all keyboard input. This protects a pane you are only monitoring, such as a log viewer, from stray keystrokes.

### Broadcast Input

Press Ctrl+Shift+B to toggle broadcast mode on the active pane. When broadcast is on, anything you type in the active pane is also sent to every other pane that has broadcast on. Use this to run the same command on several servers at once: open one pane per server, turn on broadcast in each, then type the command once.

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

Search finds text anywhere in the terminal, including scrollback. Use it to jump straight to an error message instead of scrolling through pages of output.

### Opening Search

Press Ctrl+Shift+F to open the search bar. A text field appears at the top of the terminal.

### Searching Text

Type your search term. Matches highlight in the terminal output as you type, and the search bar shows the match count.

### Navigating Matches

| Action | Shortcut |
|--------|----------|
| Next match | F3 |
| Previous match | Shift+F3 |

### Search Options

- **Case sensitivity**: Search ignores case by default, so `Error` and `error` both match. Toggle case sensitivity from the search bar.
- **Regex mode**: Regex mode matches patterns instead of exact text. Regex (regular expressions) is a pattern language: for example, `error|warning` matches lines that contain either word.

Press Escape to close the search bar.

### Example: Finding a Failed Test

1. Press Ctrl+Shift+F to open search.
2. Type `FAILED`. The search bar shows how many matches exist.
3. Press F3 to jump to the next match, or Shift+F3 for the previous one.
4. Press Escape to close search and return to the prompt.

To find errors and warnings in one pass, turn on regex mode and search for `error|warning`.

## Command Palette

The command palette is a searchable list of every action Wixen can run. Use it when you forget a shortcut, or for actions that have no shortcut.

### Opening the Command Palette

Press Ctrl+Shift+P to open the command palette. A text field appears with a list of all available actions.

### Using the Command Palette

Type to filter the list. The palette uses fuzzy matching, so partial words work. For example, typing "split h" matches "Split Horizontal".

Press Enter to run the selected action. Press Escape to close the palette without running anything.

### SSH Targets in the Command Palette

If you have SSH targets configured, they appear in the command palette. Select one to open a new tab connected to that host. See [SSH Profiles](#profiles) for setup.

## Shell Integration

Shell integration is a small script you add to your shell. The script prints invisible markers, called OSC 133 sequences, that tell Wixen exactly where each prompt, command, and block of output begins and ends. It also reports your current folder (OSC 7).

Why set it up? Without the markers, Wixen guesses where prompts are. With them, command navigation, exit codes, and completion announcements are exact. Setup takes about a minute.

### What Shell Integration Provides

- **Command blocks**: Each prompt/command/output cycle becomes a separate block. Screen readers can move between blocks.
- **Exit codes**: The terminal knows whether a command succeeded or failed.
- **Working directory**: The terminal updates the tab title with your current folder.
- **Prompt jumping**: Move between prompts with keyboard shortcuts.

### PowerShell Setup

The integration script `wixen.ps1` is installed in the `scripts` folder of your Wixen installation (for the `.msi` installer, `C:\Program Files\Wixen Terminal\scripts`). Copy it to `%APPDATA%\wixen` and load it from your PowerShell profile. Type these commands in PowerShell:

```powershell
New-Item -ItemType Directory -Force "$env:APPDATA\wixen"
Copy-Item "C:\Program Files\Wixen Terminal\scripts\wixen.ps1" "$env:APPDATA\wixen\"
if (-not (Test-Path $PROFILE)) { New-Item -ItemType File -Force $PROFILE }
Add-Content $PROFILE '. "$env:APPDATA\wixen\wixen.ps1"'
```

Then restart your shell, or run `. $PROFILE` to load it now.

In portable mode, `wixen.ps1` already sits next to `wixen.exe` and loads automatically.

The script only activates inside Wixen Terminal. It checks the `WIXEN_TERMINAL` environment variable, so it is safe to keep in your profile when you use other terminals.

### cmd.exe Setup (via Clink)

cmd.exe cannot run integration scripts on its own, so Wixen uses [Clink](https://chrisant996.github.io/clink/), a free add-on that extends cmd.exe.

1. Install Clink.
2. Copy `wixen.lua` from the Wixen `scripts` folder into your Clink scripts directory (typically `%LOCALAPPDATA%\clink`). In cmd.exe:

```
copy "C:\Program Files\Wixen Terminal\scripts\wixen.lua" "%LOCALAPPDATA%\clink\"
```

Clink loads the script automatically when cmd.exe runs inside Wixen Terminal.

### Checking That It Works

Run `echo $env:WIXEN_TERMINAL` in PowerShell (or `echo %WIXEN_TERMINAL%` in cmd.exe). It should print `1`. Then run any command and press Ctrl+Shift+Up: the cursor should jump to the previous prompt.

### Jumping Between Commands

With shell integration active, you can jump between command prompts:

| Action | Shortcut |
|--------|----------|
| Jump to previous prompt | Ctrl+Shift+Up |
| Jump to next prompt | Ctrl+Shift+Down |

If shell integration is not active, Wixen falls back to heuristic prompt detection: it recognizes common prompt formats by their text patterns. This works for typical prompts but is less reliable than the markers.

## Accessibility

Wixen Terminal is built for screen reader users from the ground up. It exposes its content through UI Automation (UIA), the Windows interface that screen readers use to read applications. Instead of a flat wall of text, screen readers see structured command blocks, live output regions, and labeled controls.

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
- The exit code (0 means success; anything else means failure)

Screen readers move between blocks with their standard object navigation commands. Each block is announced with a summary. For example, after a successful build you hear "cargo build completed: 42 lines of output". After a failed test run you hear "cargo test failed (exit 1): 100 lines of output".

### Output Announcements

Wixen announces terminal output through UIA live regions. A live region is a signal that tells the screen reader to speak new text on its own, without you moving focus to it. How much is spoken depends on the Screen Reader Output setting below.

Output is sent to the screen reader in batches. The `output_debounce_ms` setting (default `100`, range 50 to 1000) controls the pause between batches. Lower values feel more responsive; higher values give speech more room to keep up.

### Screen Reader Output Levels

Set `screen_reader_output` in your config (the "Screen Reader Output" field in Settings) to control how much output is announced:

| Level | Behavior |
|-------|----------|
| `auto` (default) | Detects screen reader presence. Announces all output when a screen reader is running. |
| `all` | Announces all terminal output. |
| `commands-only` | Announces only command completions (prompt, exit code, output summary). |
| `errors-only` | Announces only errors (non-zero exit codes, detected error lines). |
| `silent` | No automatic announcements. You navigate output manually. |

### Live Region Politeness

The `live_region_politeness` setting controls what happens when an announcement arrives while your screen reader is already speaking:

| Value | Behavior |
|-------|----------|
| `polite` (default) | Queues announcements behind current speech. |
| `assertive` | Interrupts current speech with the new announcement. |

### Error and Warning Detection

Wixen scans command output for error and warning patterns and marks those lines in the UIA tree, so screen readers can find them. If audio feedback is on, an error tone plays as well.

For example, run a build that fails, such as `cargo build` on code with a mistake. With completion announcements on (the default), you hear a summary in the form "cargo build failed (exit 101): 15 lines of output". The exit code and line count depend on the actual run. With `screen_reader_output = "errors-only"`, only the failing lines and the failure summary are spoken.

### Structured Output Announcements

When a command's output is a JSON, YAML, or XML document (for example, a `curl` API response), Wixen replaces the raw line count with a format summary so you are not read a stream of punctuation. For example, `cargo metadata` produces "cargo metadata completed: JSON output: 42 lines".

### Progress Bar Detection

Wixen detects progress indicators (percentage displays, progress bars) in terminal output. When audio feedback is on, a tone tracks the progress, so you can follow a long download or build without listening to every repaint.

### Password Prompt Detection

Wixen detects password prompts in terminal output. It suppresses announcement of the sensitive input and lets your screen reader tell you that a password is being requested.

### Audio Feedback

Wixen can play short tones for terminal events. The tones work with or without a screen reader, and they carry information that is faster to hear than speech: a failure tone tells you a build broke before the summary is spoken.

`audio_feedback` is the master switch and is off by default. The per-event settings below only make sound when the master switch is on:

| Setting | Default | Description |
|---------|---------|-------------|
| `audio_feedback` | `false` | Master switch for all audio tones. |
| `audio_progress` | `true` | Tone when a progress bar is detected. |
| `audio_errors` | `true` | Tone when an error or warning is detected in output. |
| `audio_command_complete` | `true` | Tone when a command finishes. |
| `audio_mode_toggle` | `true` | Tone when read-only, broadcast, or pane zoom toggles. |
| `audio_password_prompt` | `true` | Tone when a password prompt is detected. |
| `audio_navigation` | `true` | Tone when switching tabs or panes. |
| `audio_selection` | `true` | Tone when the text selection changes. |
| `audio_boundaries` | `true` | Tone at history and line-edit boundaries. |

For example, to hear a tone whenever a command fails: open Settings (Ctrl+Comma), go to the Accessibility tab, and turn on Audio Feedback. Audio Errors is already on by default. Now run a failing build; the error tone plays as soon as the error appears in the output.

### High Contrast Mode

The `high_contrast` setting controls high contrast rendering:

| Value | Behavior |
|-------|----------|
| `auto` (default) | Follows the Windows system high contrast setting. |
| `on` | Forces high contrast mode on. |
| `off` | Forces high contrast mode off. |

### Minimum Contrast Ratio

Some programs print text in colors that are hard to read against the background. The `min_contrast_ratio` setting (default `4.5`) fixes this: when a program picks a color below the threshold, Wixen adjusts the foreground color until it meets the minimum contrast ratio.

WCAG AA (the common accessibility standard) requires 4.5:1. WCAG AAA requires 7.0:1. Set the value to `0.0` to turn the adjustment off.

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

When `announce_images` is `true` (default), inline images placed through the Sixel, iTerm2, or Kitty image protocols are announced with their dimensions and filename (if available).

## Settings

### Opening Settings

Press Ctrl+Comma to open the settings overlay. The settings panel appears on top of the terminal and is fully keyboard-navigable.

### Settings Tabs

The settings panel has seven tabs:

| Tab | Contents |
|-----|----------|
| Font | Font family, font size, line height, fallback fonts |
| Window | Window title, width, height, renderer, opacity, background style, dark title bar, scrollbar, tab bar |
| Terminal | Scrollback lines, cursor style, cursor blink, blink interval, bell style |
| Colors | Theme, foreground, background, cursor color, selection background, and the 16 ANSI palette colors |
| Profiles | For each profile: name, program, arguments, default flag |
| Keybindings | Every keyboard shortcut, with a chord editor (modifier checkboxes and key input) |
| Accessibility | Screen reader output, announcements, prompt detection, contrast, motion, and all audio tones |

Some options are not in the settings panel yet: ligatures, font file path, background image, quake mode, window padding, OSC 52 policy, command notification threshold, session restore, and profile working directories. Set these in `config.toml` (see the [Configuration File](#configuration-file) section).

### Navigating Settings

- Press **Left/Right arrow** to switch between tabs.
- Press **Up/Down arrow** to move between fields within a tab.
- Press **Enter** or **Space** to edit a field (toggle, dropdown, or text input).
- Press **Escape** to stop editing a field, or to close the settings panel.

### Saving Settings

Your changes go into a draft while you edit. When you close the settings panel, Wixen asks whether to save or discard the draft. Saved changes are written to your `config.toml` file and take effect immediately.

## Configuration File

### File Location

Wixen looks for configuration in this order:

1. `config.toml` next to the executable (portable mode, requires the `wixen.portable` marker file)
2. `%APPDATA%\wixen\config.toml`

### Format

Configuration files use [TOML](https://toml.io/) format. Lua configuration (`config.lua` in the same directory) runs after TOML and can override any setting.

### Hot Reload

Wixen detects changes to the configuration file automatically. Most settings take effect immediately without a restart.

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
| `scrollback_lines` | integer | `0` | Maximum scrollback lines. 0 means unlimited scrollback with automatic compression. |
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
| `screen_reader_output` | string | `"auto"` | Output level: `"auto"`, `"all"`, `"commands-only"`, `"errors-only"`, `"silent"`. |
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
| `audio_mode_toggle` | bool | `true` | Play a tone when read-only, broadcast, or zoom toggles (requires `audio_feedback = true`). |
| `audio_password_prompt` | bool | `true` | Play a tone when a password prompt is detected (requires `audio_feedback = true`). |
| `audio_navigation` | bool | `true` | Play a tone when switching tabs or panes (requires `audio_feedback = true`). |
| `audio_selection` | bool | `true` | Play a tone when the text selection changes (requires `audio_feedback = true`). |
| `audio_boundaries` | bool | `true` | Play a tone at history and line-edit boundaries (requires `audio_feedback = true`). |
| `output_debounce_ms` | integer | `100` | Milliseconds between batched screen reader announcements. Range: 50 to 1000. |

## Profiles

### What Profiles Are

A profile is a saved shell configuration: a name, a shell program, arguments, and a working directory. Define one profile per shell you use, then open any of them as a new tab from the command palette.

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

An SSH target is a saved remote connection. Define targets in your config and they appear in the command palette, so you can open a connection to a server without typing the full `ssh` command each time.

```toml
[[ssh]]
name = "dev-server"
host = "dev.example.com"
port = 22
user = "deploy"
identity_file = ""
extra_args = []
```

Opening an SSH target launches a new tab running `ssh.exe` with the configured host, port, and user. Leave `identity_file` empty to use your default SSH key. Use `extra_args` for additional `ssh` options, such as `["-o", "StrictHostKeyChecking=no"]`.

### WSL Profiles

Wixen auto-detects installed WSL distributions by running `wsl -l -v`. Detected distributions appear as profiles in the command palette, named like "WSL: Ubuntu". You do not need to configure them manually.

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
| Open user guide (help) | F1 |
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

For example, to bind Ctrl+Shift+E to split the terminal:

```toml
[[keybindings.bindings]]
chord = "ctrl+shift+e"
action = "split_horizontal"
```

You can also add keybindings through Lua configuration (`config.lua`). `wixen.bind` takes a chord and an action name:

```lua
wixen.bind("ctrl+shift+e", "split_horizontal")
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
| `open_help` | Open the user guide in your default browser |
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

**Symptom**: Blank window, crash on startup, or nothing appears.

**Likely cause**: A GPU driver problem or a corrupt config file. (When the GPU renderer fails cleanly, Wixen falls back to software rendering on its own; a hard driver fault can still prevent startup.)

**Fix**:

1. Set `renderer = "software"` in your `config.toml` (in `%APPDATA%\wixen`).
2. If that does not help, the config file may be corrupt. Rename or delete it and restart. Wixen recreates it with defaults.
3. Check that your Windows version is 1903 (build 18362) or later.

### No Shell Integration Markers

**Symptom**: Prompt jumping (Ctrl+Shift+Up/Down) and command completion announcements do not work.

**Likely cause**: The integration script is not loaded in your shell.

**Fix**:

1. For PowerShell, check that `. "$env:APPDATA\wixen\wixen.ps1"` is in your `$PROFILE` and that the file exists at that path. For cmd.exe, check that `wixen.lua` is in your Clink scripts directory.
2. Confirm the `WIXEN_TERMINAL` environment variable is set. Run `echo $env:WIXEN_TERMINAL` in PowerShell or `echo %WIXEN_TERMINAL%` in cmd.exe. It should print `1`.
3. If you cannot use shell integration, Wixen falls back to heuristic prompt detection. You can force this mode with `prompt_detection = "heuristic"` in your config.

### Screen Reader Not Reading Output

**Symptom**: The terminal works, but your screen reader says nothing when output appears.

**Likely cause**: Output announcements are turned down or the announcements are losing a race with other speech.

**Fix**:

1. Check the `screen_reader_output` setting. It should be `"auto"` or `"all"`. If it is `"silent"`, no output is announced.
2. Verify your screen reader is running and supports UI Automation.
3. If announcements seem to disappear behind other speech, try `live_region_politeness = "assertive"`.

### No Sound from Audio Feedback

**Symptom**: You expect tones for errors or command completion, but hear nothing.

**Likely cause**: The master switch is off. The per-event settings (`audio_errors`, `audio_command_complete`, and the rest) make no sound on their own.

**Fix**: Set `audio_feedback = true` in the `[accessibility]` section of your config, or turn on Audio Feedback in the Settings Accessibility tab.

### High CPU Usage

**Symptom**: Wixen uses noticeably more CPU than expected, even when idle.

**Likely cause**: Very large scrollback, constant cursor-blink redraws, or a misbehaving GPU driver.

**Fix**:

1. Set `scrollback_lines` to a fixed number (e.g., `50000`) instead of unlimited.
2. Try `cursor_blink = false` to stop blink redraws.
3. Try `renderer = "software"` to rule out the GPU driver.

### Reporting Issues

Report bugs on the GitHub issues page. Include:

- Your Windows version (`winver`)
- Your screen reader and version (if applicable)
- Steps to reproduce the problem
- The relevant section of your `config.toml`
