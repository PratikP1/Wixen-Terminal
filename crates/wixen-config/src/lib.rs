//! wixen-config: TOML configuration loading with sensible defaults and hot-reload.

pub mod lua;
pub mod macros;
pub mod plugin;
pub mod serial;
pub mod session;
pub mod ssh;
mod watcher;
pub mod wsl;

pub use lua::{LuaConfigResult, LuaEngine, LuaKeybinding, sandbox_lua};
pub use plugin::{execute_plugin_event, execute_plugin_event_from_source};
pub use session::{SavedTab, SessionState};
pub use ssh::{SshManager, SshTarget, parse_ssh_url, ssh_target_to_command};
pub use watcher::{ConfigDelta, ConfigWatcher};

use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use thiserror::Error;
use tracing::info;

#[derive(Error, Debug)]
pub enum ConfigError {
    #[error("Failed to read config file: {0}")]
    Io(#[from] std::io::Error),
    #[error("Failed to parse TOML: {0}")]
    Parse(#[from] toml::de::Error),
    #[error("Failed to serialize config: {0}")]
    Serialize(#[from] toml::ser::Error),
}

/// A non-fatal semantic validation warning.
///
/// Validation warnings don't prevent the config from loading — the value
/// is clamped or defaulted, and the warning is logged so the user can fix it.
#[derive(Debug, Clone, PartialEq, Eq, JsonSchema)]
pub struct ValidationWarning {
    /// Dotted path to the config field (e.g., "font.size").
    pub field: String,
    /// Human-readable explanation of the problem and what was done.
    pub message: String,
}

/// A named shell profile.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct Profile {
    /// Display name (e.g., "PowerShell", "Command Prompt").
    pub name: String,
    /// Shell program to launch.
    pub program: String,
    /// Arguments to pass to the shell.
    pub args: Vec<String>,
    /// Working directory (empty = inherit).
    pub working_directory: String,
    /// Whether this is the default profile.
    pub is_default: bool,
}

impl Default for Profile {
    fn default() -> Self {
        Self {
            name: "Default".to_string(),
            program: String::new(),
            args: Vec::new(),
            working_directory: String::new(),
            is_default: false,
        }
    }
}

/// A single keybinding entry.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
pub struct Keybinding {
    /// Key chord string, normalized: lowercase, modifiers sorted alphabetically.
    /// Example: "ctrl+shift+t"
    pub chord: String,
    /// Action identifier, e.g. "new_tab", "close_tab", "send_text".
    pub action: String,
    /// Optional argument for the action (e.g., the text to send for "send_text").
    ///
    /// Supports escape sequences: `\n` (LF), `\r` (CR), `\t` (tab), `\x1b` (ESC),
    /// `\\` (backslash).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub args: Option<String>,
}

impl Keybinding {
    /// Create a keybinding with no args (most common case).
    pub fn simple(chord: &str, action: &str) -> Self {
        Self {
            chord: chord.into(),
            action: action.into(),
            args: None,
        }
    }

    /// Create a keybinding with args (e.g., send_text).
    pub fn with_args(chord: &str, action: &str, args: &str) -> Self {
        Self {
            chord: chord.into(),
            action: action.into(),
            args: Some(args.into()),
        }
    }
}

/// Keybinding configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct KeybindingsConfig {
    pub bindings: Vec<Keybinding>,
}

impl Default for KeybindingsConfig {
    fn default() -> Self {
        Self {
            bindings: vec![
                Keybinding::simple("ctrl+shift+t", "new_tab"),
                // Ctrl+Shift+W closes the active pane (and tab if last pane)
                Keybinding::simple("ctrl+shift+w", "close_pane"),
                Keybinding::simple("ctrl+tab", "next_tab"),
                Keybinding::simple("ctrl+shift+tab", "prev_tab"),
                Keybinding::simple("alt+shift+plus", "split_horizontal"),
                Keybinding::simple("alt+shift+minus", "split_vertical"),
                Keybinding::simple("ctrl+shift+c", "copy"),
                Keybinding::simple("ctrl+shift+v", "paste"),
                Keybinding::simple("ctrl+shift+f", "find"),
                Keybinding::simple("ctrl+shift+p", "command_palette"),
                Keybinding::simple("ctrl+comma", "settings"),
                Keybinding::simple("f11", "toggle_fullscreen"),
                Keybinding::simple("ctrl+plus", "zoom_in"),
                Keybinding::simple("ctrl+minus", "zoom_out"),
                Keybinding::simple("ctrl+0", "zoom_reset"),
                Keybinding::simple("ctrl+shift+a", "select_all"),
                Keybinding::simple("shift+pageup", "scroll_up_page"),
                Keybinding::simple("shift+pagedown", "scroll_down_page"),
                Keybinding::simple("ctrl+home", "scroll_to_top"),
                Keybinding::simple("ctrl+end", "scroll_to_bottom"),
                Keybinding::simple("ctrl+shift+up", "jump_to_previous_prompt"),
                Keybinding::simple("ctrl+shift+down", "jump_to_next_prompt"),
                // Tab selection by index
                Keybinding::simple("ctrl+1", "select_tab_1"),
                Keybinding::simple("ctrl+2", "select_tab_2"),
                Keybinding::simple("ctrl+3", "select_tab_3"),
                Keybinding::simple("ctrl+4", "select_tab_4"),
                Keybinding::simple("ctrl+5", "select_tab_5"),
                Keybinding::simple("ctrl+6", "select_tab_6"),
                Keybinding::simple("ctrl+7", "select_tab_7"),
                Keybinding::simple("ctrl+8", "select_tab_8"),
                Keybinding::simple("ctrl+9", "select_tab_9"),
                // Pane navigation (Windows Terminal compatible)
                Keybinding::simple("alt+left", "focus_pane_left"),
                Keybinding::simple("alt+right", "focus_pane_right"),
                Keybinding::simple("alt+up", "focus_pane_up"),
                Keybinding::simple("alt+down", "focus_pane_down"),
                // Pane resize
                Keybinding::simple("alt+shift+left", "resize_pane_shrink"),
                Keybinding::simple("alt+shift+right", "resize_pane_grow"),
                // Tab management
                Keybinding::simple("ctrl+shift+d", "duplicate_tab"),
                Keybinding::simple("ctrl+shift+n", "new_window"),
                Keybinding::simple("ctrl+shift+comma", "open_config_file"),
                // Search navigation
                Keybinding::simple("f3", "find_next"),
                Keybinding::simple("shift+f3", "find_previous"),
                // Fullscreen alternate binding
                Keybinding::simple("alt+enter", "toggle_fullscreen"),
                // Accessibility-specific
                Keybinding::simple("ctrl+shift+z", "toggle_zoom"),
                Keybinding::simple("ctrl+shift+r", "toggle_read_only"),
                Keybinding::simple("ctrl+shift+b", "toggle_broadcast_input"),
                // Miscellaneous
                Keybinding::simple("ctrl+shift+l", "clear_terminal"),
                Keybinding::simple("ctrl+shift+k", "clear_scrollback"),
                // Help
                Keybinding::simple("f1", "open_help"),
            ],
        }
    }
}

/// Normalize a key chord string.
///
/// Lowercases everything, sorts modifier keys alphabetically (alt, ctrl, shift, win),
/// and places the non-modifier key at the end.
///
/// Example: `"Shift+Ctrl+T"` → `"ctrl+shift+t"`
pub fn normalize_chord(chord: &str) -> String {
    let parts: Vec<&str> = chord.split('+').collect();
    let mut modifiers = Vec::new();
    let mut key = String::new();

    for part in parts {
        let lower = part.trim().to_lowercase();
        match lower.as_str() {
            "ctrl" | "control" => modifiers.push("ctrl"),
            "shift" => modifiers.push("shift"),
            "alt" => modifiers.push("alt"),
            "win" | "windows" | "meta" | "super" => modifiers.push("win"),
            _ => key = lower,
        }
    }

    modifiers.sort_unstable();
    modifiers.dedup();

    if key.is_empty() {
        modifiers.join("+")
    } else if modifiers.is_empty() {
        key
    } else {
        format!("{}+{}", modifiers.join("+"), key)
    }
}

/// Parse a normalized chord into its components.
///
/// Returns `(ctrl, shift, alt, win, key)`.
pub fn parse_chord(chord: &str) -> (bool, bool, bool, bool, String) {
    let parts: Vec<&str> = chord.split('+').collect();
    let mut ctrl = false;
    let mut shift = false;
    let mut alt = false;
    let mut win = false;
    let mut key = String::new();

    for part in parts {
        match part {
            "ctrl" => ctrl = true,
            "shift" => shift = true,
            "alt" => alt = true,
            "win" => win = true,
            _ => key = part.to_string(),
        }
    }

    (ctrl, shift, alt, win, key)
}

/// Convert a Windows virtual-key code + modifier state to a normalized chord string.
///
/// Maps VK codes to human-readable key names, prepends active modifiers,
/// and normalizes via `normalize_chord()`.
///
/// Returns the chord string (e.g., `"ctrl+shift+t"`, `"f11"`, `"alt+shift+plus"`).
pub fn vk_to_chord(vk: u16, ctrl: bool, shift: bool, alt: bool) -> String {
    let key_name = match vk {
        // Letters A-Z
        0x41..=0x5A => {
            let ch = (b'a' + (vk - 0x41) as u8) as char;
            ch.to_string()
        }
        // Digits 0-9
        0x30..=0x39 => {
            let ch = (b'0' + (vk - 0x30) as u8) as char;
            ch.to_string()
        }
        // Function keys F1-F24
        0x70..=0x87 => format!("f{}", vk - 0x70 + 1),
        // Special keys
        0x09 => "tab".into(),
        0x0D => "enter".into(),
        0x1B => "escape".into(),
        0x08 => "backspace".into(),
        0x20 => "space".into(),
        0x2E => "delete".into(),
        0x2D => "insert".into(),
        0x24 => "home".into(),
        0x23 => "end".into(),
        0x21 => "pageup".into(),
        0x22 => "pagedown".into(),
        0x25 => "left".into(),
        0x26 => "up".into(),
        0x27 => "right".into(),
        0x28 => "down".into(),
        // OEM keys
        0xBB => "plus".into(),
        0xBD => "minus".into(),
        0xBC => "comma".into(),
        0xBE => "period".into(),
        0xBA => "semicolon".into(),
        0xBF => "slash".into(),
        0xC0 => "backtick".into(),
        0xDB => "lbracket".into(),
        0xDC => "backslash".into(),
        0xDD => "rbracket".into(),
        0xDE => "quote".into(),
        // Numpad
        0x60..=0x69 => format!("numpad{}", vk - 0x60),
        0x6A => "numpad_multiply".into(),
        0x6B => "numpad_add".into(),
        0x6D => "numpad_subtract".into(),
        0x6E => "numpad_decimal".into(),
        0x6F => "numpad_divide".into(),
        // Unknown — return hex representation
        _ => format!("vk{:02x}", vk),
    };

    let mut parts = Vec::new();
    if ctrl {
        parts.push("ctrl");
    }
    if shift {
        parts.push("shift");
    }
    if alt {
        parts.push("alt");
    }
    parts.push(&key_name);

    normalize_chord(&parts.join("+"))
}

/// Parse a quake-mode hotkey string into Win32 `(modifiers, vk)` values.
///
/// The string uses the same `mod+key` format as keybindings (e.g., `"win+backtick"`,
/// `"ctrl+backtick"`). Returns `None` if the key name is unrecognized.
///
/// Win32 MOD constants: MOD_ALT = 1, MOD_CONTROL = 2, MOD_SHIFT = 4, MOD_WIN = 8.
pub fn parse_quake_hotkey(hotkey: &str) -> Option<(u32, u32)> {
    let (ctrl, shift, alt, win, key) = parse_chord(hotkey);
    let vk = key_name_to_vk(&key)?;
    let mut mods = 0u32;
    if alt {
        mods |= 1; // MOD_ALT
    }
    if ctrl {
        mods |= 2; // MOD_CONTROL
    }
    if shift {
        mods |= 4; // MOD_SHIFT
    }
    if win {
        mods |= 8; // MOD_WIN
    }
    Some((mods, vk as u32))
}

/// Map a key name (from `parse_chord`) to a Windows virtual-key code.
fn key_name_to_vk(name: &str) -> Option<u16> {
    match name {
        // Single letters
        s if s.len() == 1 && s.as_bytes()[0].is_ascii_lowercase() => {
            Some(0x41 + (s.as_bytes()[0] - b'a') as u16)
        }
        // Digits
        s if s.len() == 1 && s.as_bytes()[0].is_ascii_digit() => {
            Some(0x30 + (s.as_bytes()[0] - b'0') as u16)
        }
        // Function keys
        s if s.starts_with('f') => {
            let n: u16 = s[1..].parse().ok()?;
            if (1..=24).contains(&n) {
                Some(0x70 + n - 1)
            } else {
                None
            }
        }
        // Special keys
        "tab" => Some(0x09),
        "enter" => Some(0x0D),
        "escape" => Some(0x1B),
        "backspace" => Some(0x08),
        "space" => Some(0x20),
        "delete" => Some(0x2E),
        "insert" => Some(0x2D),
        "home" => Some(0x24),
        "end" => Some(0x23),
        "pageup" => Some(0x21),
        "pagedown" => Some(0x22),
        "left" => Some(0x25),
        "up" => Some(0x26),
        "right" => Some(0x27),
        "down" => Some(0x28),
        // OEM keys
        "plus" => Some(0xBB),
        "minus" => Some(0xBD),
        "comma" => Some(0xBC),
        "period" => Some(0xBE),
        "semicolon" => Some(0xBA),
        "slash" => Some(0xBF),
        "backtick" => Some(0xC0),
        "lbracket" => Some(0xDB),
        "backslash" => Some(0xDC),
        "rbracket" => Some(0xDD),
        "quote" => Some(0xDE),
        _ => None,
    }
}

/// Top-level configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(default)]
pub struct Config {
    pub font: FontConfig,
    pub window: WindowConfig,
    pub terminal: TerminalConfig,
    pub colors: ColorConfig,
    pub shell: ShellConfig,
    pub keybindings: KeybindingsConfig,
    /// Accessibility settings — screen reader behavior, contrast, motion.
    pub accessibility: AccessibilityConfig,
    /// Named shell profiles. If empty, a single profile is synthesized from `[shell]`.
    pub profiles: Vec<Profile>,
    /// SSH connection targets.
    pub ssh: Vec<SshTarget>,
}

/// Screen reader output verbosity level.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "kebab-case")]
pub enum ScreenReaderVerbosity {
    /// Detect screen reader presence and adjust automatically.
    #[default]
    Auto,
    /// Announce all terminal output (default for screen reader users).
    All,
    /// Only announce command completions (prompt, exit code, summary).
    CommandsOnly,
    /// Only announce errors (non-zero exit codes, detected error lines).
    ErrorsOnly,
    /// No automatic announcements — user navigates manually.
    Silent,
}

/// Prompt detection strategy.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "kebab-case")]
pub enum PromptDetection {
    /// Use shell integration (OSC 133) if available, fall back to heuristic.
    #[default]
    Auto,
    /// Only use shell integration markers (OSC 133).
    ShellIntegration,
    /// Only use heuristic regex patterns.
    Heuristic,
    /// Disable prompt detection entirely.
    Disabled,
}

/// Accessibility configuration.
///
/// Controls screen reader behavior, contrast enforcement, and motion
/// preferences. All settings have sensible defaults — screen reader users
/// get a good experience without editing config.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct AccessibilityConfig {
    /// How much terminal output the screen reader announces.
    pub screen_reader_output: ScreenReaderVerbosity,
    /// Announce when a command finishes (includes exit code and line count).
    pub announce_command_complete: bool,
    /// Include exit codes in command completion announcements.
    pub announce_exit_codes: bool,
    /// UIA live region politeness for output announcements.
    ///
    /// `"polite"` queues behind current speech; `"assertive"` interrupts.
    pub live_region_politeness: String,
    /// How prompts are detected for command block structure.
    pub prompt_detection: PromptDetection,
    /// High contrast mode: `"auto"` follows Windows system setting.
    pub high_contrast: String,
    /// Minimum foreground/background contrast ratio (WCAG AA = 4.5).
    ///
    /// When terminal programs set colors below this threshold, Wixen
    /// adjusts the color to meet it. Set to 0.0 to disable.
    pub min_contrast_ratio: f32,
    /// Reduced motion: `"auto"` follows Windows system setting.
    ///
    /// When active, disables cursor blink, visual bell flash, and
    /// smooth scrolling animations.
    pub reduced_motion: String,
    /// Announce inline images when placed (Sixel, iTerm2, Kitty).
    pub announce_images: bool,
    /// Announce pane position when switching (e.g., "Left pane, 1 of 3").
    pub announce_pane_position: bool,
    /// Announce tab details when switching (shell type, cwd, status).
    pub announce_tab_details: bool,
    /// Enable audio feedback tones (beeps) for terminal events.
    ///
    /// When true, the terminal plays short tones for command completion,
    /// errors, progress updates, and mode changes. Works independently
    /// of screen readers — useful for any user who wants audio cues.
    pub audio_feedback: bool,
    /// Enable audio tone specifically for progress bar updates.
    pub audio_progress: bool,
    /// Enable audio tone specifically for error/warning detection.
    pub audio_errors: bool,
    /// Enable audio tone specifically for command completion.
    pub audio_command_complete: bool,
    /// Milliseconds between screen reader output announcements.
    ///
    /// Controls how often batched terminal output is sent to the screen reader.
    /// Lower values feel more responsive but can overwhelm speech; higher values
    /// give a pause between chunks so users can follow along. Range: 50–1000.
    pub output_debounce_ms: u32,
}

impl Default for AccessibilityConfig {
    fn default() -> Self {
        Self {
            screen_reader_output: ScreenReaderVerbosity::Auto,
            announce_command_complete: true,
            announce_exit_codes: true,
            live_region_politeness: "polite".to_string(),
            prompt_detection: PromptDetection::Auto,
            high_contrast: "auto".to_string(),
            min_contrast_ratio: 4.5,
            reduced_motion: "auto".to_string(),
            announce_images: true,
            announce_pane_position: true,
            announce_tab_details: true,
            audio_feedback: false,
            audio_progress: true,
            audio_errors: true,
            audio_command_complete: true,
            output_debounce_ms: 100,
        }
    }
}

/// Resolve the `reduced_motion` config value to a concrete boolean.
///
/// - `"on"`  → always `true`
/// - `"off"` → always `false`
/// - `"auto"` (or any other value) → delegates to `system_prefers_reduced`,
///   which the caller obtains from the platform (e.g.,
///   `wixen_ui::window::system_reduced_motion()`).
pub fn should_reduce_motion(config_value: &str, system_prefers_reduced: bool) -> bool {
    match config_value {
        "on" => true,
        "off" => false,
        _ => system_prefers_reduced,
    }
}

/// Font configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct FontConfig {
    /// Font family name (DirectWrite lookup).
    pub family: String,
    /// Font size in points.
    pub size: f32,
    /// Line height multiplier (1.0 = tight, 1.2 = comfortable).
    pub line_height: f32,
    /// Fallback font families tried when the primary font lacks a glyph.
    pub fallback_fonts: Vec<String>,
    /// Enable programming ligatures (e.g., =>, ->, != in Fira Code / Cascadia Code).
    pub ligatures: bool,
    /// Path to a font file (.ttf/.otf) to load directly instead of by family name.
    ///
    /// When non-empty, this takes priority over `family`.
    #[serde(default)]
    pub font_path: String,
}

/// Window background style.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum BackgroundStyle {
    /// Standard opaque background.
    #[default]
    Opaque,
    /// Acrylic blur (Windows 11 22H2+).
    Acrylic,
    /// Mica material (Windows 11 22H2+).
    Mica,
}

/// Scrollbar display mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum ScrollbarMode {
    /// Show scrollbar only when scrolled into history.
    #[default]
    Auto,
    /// Always show the scrollbar.
    Always,
    /// Never show the scrollbar.
    Never,
}

/// Tab bar display mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "kebab-case")]
pub enum TabBarMode {
    /// Show tab bar only when there are 2+ tabs.
    #[default]
    AutoHide,
    /// Always show the tab bar.
    Always,
    /// Never show the tab bar.
    Never,
}

/// Terminal content padding in pixels.
///
/// Space between the window edge and the terminal grid.
/// A single value sets all four sides; otherwise specify individually.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct Padding {
    /// Padding on the left side in pixels.
    pub left: u16,
    /// Padding on the right side in pixels.
    pub right: u16,
    /// Padding on the top side in pixels.
    pub top: u16,
    /// Padding on the bottom side in pixels.
    pub bottom: u16,
}

impl Default for Padding {
    fn default() -> Self {
        Self {
            left: 4,
            right: 4,
            top: 4,
            bottom: 4,
        }
    }
}

impl Padding {
    /// Uniform padding on all sides.
    pub fn uniform(px: u16) -> Self {
        Self {
            left: px,
            right: px,
            top: px,
            bottom: px,
        }
    }

    /// Total horizontal padding (left + right).
    pub fn horizontal(&self) -> u16 {
        self.left + self.right
    }

    /// Total vertical padding (top + bottom).
    pub fn vertical(&self) -> u16 {
        self.top + self.bottom
    }
}

/// Window configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct WindowConfig {
    /// Initial window width in pixels.
    pub width: u32,
    /// Initial window height in pixels.
    pub height: u32,
    /// Window title.
    pub title: String,
    /// Renderer: "auto", "gpu", or "software".
    pub renderer: String,
    /// Window opacity (0.0 = fully transparent, 1.0 = fully opaque).
    pub opacity: f32,
    /// Window background style: "opaque", "acrylic", or "mica".
    pub background: BackgroundStyle,
    /// Use dark title bar decoration.
    pub dark_title_bar: bool,
    /// Path to a background image (empty = none). Supports PNG, JPEG, GIF.
    pub background_image: String,
    /// Background image opacity (0.0 = invisible, 1.0 = fully opaque). Default 0.3.
    pub background_image_opacity: f32,
    /// Enable quake-mode: a global hotkey toggles the terminal window.
    pub quake_mode: bool,
    /// Quake-mode global hotkey (e.g., "win+backtick"). Default: "win+backtick".
    pub quake_hotkey: String,
    /// Scrollbar display mode: "auto" (when scrolled), "always", or "never".
    pub scrollbar: ScrollbarMode,
    /// Tab bar display mode: "auto-hide" (2+ tabs), "always", or "never".
    pub tab_bar: TabBarMode,
    /// Terminal content padding in pixels.
    pub padding: Padding,
}

/// OSC 52 clipboard access policy.
///
/// Controls whether programs running in the terminal can read/write the system clipboard
/// via escape sequences. Write-only is the safe default — full read access could let a
/// malicious program silently exfiltrate clipboard contents.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "kebab-case")]
pub enum Osc52Policy {
    /// Programs can write to clipboard but not read it.
    #[default]
    WriteOnly,
    /// Programs can both read and write clipboard.
    ReadWrite,
    /// Clipboard access via escape sequences is completely disabled.
    Disabled,
}

/// Bell notification style.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum BellStyle {
    /// Play a system sound.
    Audible,
    /// Flash the window/taskbar.
    Visual,
    /// Both sound and flash.
    #[default]
    Both,
    /// No bell notification.
    None,
}

/// Terminal behavior configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct TerminalConfig {
    /// Maximum scrollback lines (0 = infinite).
    pub scrollback_lines: usize,
    /// Cursor style: "block", "underline", or "bar".
    pub cursor_style: String,
    /// Whether the cursor blinks.
    pub cursor_blink: bool,
    /// Cursor blink interval in milliseconds.
    pub cursor_blink_ms: u64,
    /// Bell style: "audible", "visual", "both", or "none".
    pub bell: BellStyle,
    /// OSC 52 clipboard access policy: "write-only" (default), "read-write", or "disabled".
    pub osc52: Osc52Policy,
    /// Notify when a command finishes after running for at least this many seconds
    /// and the window is not focused. 0 disables the notification.
    pub command_notify_threshold: u64,
    /// Session restore mode: "never" (default), "always", or "ask".
    #[serde(default)]
    pub session_restore: String,
}

/// Color scheme configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct ColorConfig {
    /// Built-in theme name (e.g., "dracula", "catppuccin-mocha").
    /// If set, provides base colors; individual fields below override the theme.
    pub theme: Option<String>,
    /// Foreground color as hex (e.g., "#d9d9d9").
    pub foreground: String,
    /// Background color as hex (e.g., "#0d0d14").
    pub background: String,
    /// Cursor color as hex.
    pub cursor: String,
    /// Selection background color as hex.
    pub selection_bg: String,
    /// ANSI 16-color palette overrides (indexed 0-15).
    pub palette: Vec<String>,
}

/// Shell configuration.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default, JsonSchema)]
#[serde(default)]
pub struct ShellConfig {
    /// Shell program to launch (empty = auto-detect).
    pub program: String,
    /// Arguments to pass to the shell.
    pub args: Vec<String>,
    /// Working directory (empty = inherit).
    pub working_directory: String,
}

impl Default for FontConfig {
    fn default() -> Self {
        Self {
            family: "Cascadia Code".to_string(),
            size: 14.0,
            line_height: 1.0,
            fallback_fonts: vec![
                "Segoe UI Emoji".to_string(),
                "Segoe UI Symbol".to_string(),
                "MS Gothic".to_string(),
            ],
            ligatures: true,
            font_path: String::new(),
        }
    }
}

impl Default for WindowConfig {
    fn default() -> Self {
        Self {
            width: 1024,
            height: 768,
            title: "Wixen Terminal".to_string(),
            renderer: "auto".to_string(),
            opacity: 1.0,
            background: BackgroundStyle::Opaque,
            dark_title_bar: true,
            background_image: String::new(),
            background_image_opacity: 0.3,
            quake_mode: false,
            quake_hotkey: "win+backtick".to_string(),
            scrollbar: ScrollbarMode::Auto,
            tab_bar: TabBarMode::AutoHide,
            padding: Padding::default(),
        }
    }
}

impl Default for TerminalConfig {
    fn default() -> Self {
        Self {
            scrollback_lines: 0,
            cursor_style: "block".to_string(),
            cursor_blink: true,
            cursor_blink_ms: 530,
            bell: BellStyle::Both,
            osc52: Osc52Policy::WriteOnly,
            command_notify_threshold: 10,
            session_restore: "never".to_string(),
        }
    }
}

impl Default for ColorConfig {
    fn default() -> Self {
        Self {
            theme: None,
            foreground: "#d9d9d9".to_string(),
            background: "#0d0d14".to_string(),
            cursor: "#cccccc".to_string(),
            selection_bg: "#264f78".to_string(),
            palette: Vec::new(),
        }
    }
}

impl Config {
    /// Load configuration from a TOML file, falling back to defaults for missing fields.
    ///
    /// After loading TOML, looks for a `config.lua` in the same directory and
    /// applies any Lua overrides and keybindings on top.
    pub fn load(path: &Path) -> Result<Self, ConfigError> {
        let mut config = if path.exists() {
            let content = std::fs::read_to_string(path)?;
            let cfg: Config = toml::from_str(&content)?;
            info!(path = %path.display(), "TOML configuration loaded");
            cfg
        } else {
            info!("No config file found, using defaults");
            Config::default()
        };

        // Look for a Lua config file alongside the TOML file
        let lua_path = path.with_extension("lua");
        if lua_path.exists() {
            match LuaEngine::new() {
                Ok(engine) => match engine.load_file(&lua_path) {
                    Ok(result) => {
                        config.apply_lua_overrides(&result.overrides);
                        for kb in &result.keybindings {
                            config
                                .keybindings
                                .bindings
                                .push(Keybinding::simple(&normalize_chord(&kb.chord), &kb.action));
                        }
                        info!(
                            overrides = result.overrides.len(),
                            keybindings = result.keybindings.len(),
                            "Lua config applied"
                        );
                    }
                    Err(e) => {
                        tracing::warn!(error = %e, "Lua config error — skipping");
                    }
                },
                Err(e) => {
                    tracing::warn!(error = %e, "Failed to init Lua engine — skipping");
                }
            }
        }

        Ok(config)
    }

    /// Apply Lua config overrides to this Config.
    ///
    /// Keys use dotted notation: `"font.size"`, `"terminal.cursor_blink"`, etc.
    pub fn apply_lua_overrides(&mut self, overrides: &std::collections::HashMap<String, String>) {
        for (key, value) in overrides {
            match key.as_str() {
                "font.family" => self.font.family = value.clone(),
                "font.size" => {
                    if let Ok(v) = value.parse::<f32>() {
                        self.font.size = v;
                    }
                }
                "font.line_height" => {
                    if let Ok(v) = value.parse::<f32>() {
                        self.font.line_height = v;
                    }
                }
                "font.ligatures" => {
                    if let Ok(v) = value.parse::<bool>() {
                        self.font.ligatures = v;
                    }
                }
                "terminal.cursor_style" => self.terminal.cursor_style = value.clone(),
                "terminal.cursor_blink" => {
                    if let Ok(v) = value.parse::<bool>() {
                        self.terminal.cursor_blink = v;
                    }
                }
                "terminal.cursor_blink_ms" => {
                    if let Ok(v) = value.parse::<u64>() {
                        self.terminal.cursor_blink_ms = v;
                    }
                }
                "colors.foreground" => self.colors.foreground = value.clone(),
                "colors.background" => self.colors.background = value.clone(),
                "colors.cursor" => self.colors.cursor = value.clone(),
                "colors.selection_bg" => self.colors.selection_bg = value.clone(),
                "colors.theme" => self.colors.theme = Some(value.clone()),
                "window.title" => self.window.title = value.clone(),
                "window.opacity" => {
                    if let Ok(v) = value.parse::<f32>() {
                        self.window.opacity = v;
                    }
                }
                "window.renderer" => self.window.renderer = value.clone(),
                "window.background_image" => self.window.background_image = value.clone(),
                "window.background_image_opacity" => {
                    if let Ok(v) = value.parse::<f32>() {
                        self.window.background_image_opacity = v;
                    }
                }
                "window.quake_mode" => {
                    self.window.quake_mode = value == "true" || value == "1";
                }
                "window.quake_hotkey" => self.window.quake_hotkey = value.clone(),
                "terminal.osc52" => {
                    self.terminal.osc52 = match value.as_str() {
                        "write-only" => Osc52Policy::WriteOnly,
                        "read-write" => Osc52Policy::ReadWrite,
                        "disabled" => Osc52Policy::Disabled,
                        _ => self.terminal.osc52,
                    };
                }
                _ => {
                    tracing::debug!(key, value, "Unknown Lua config override — ignored");
                }
            }
        }
    }

    /// Validate configuration values and clamp/default invalid ones.
    ///
    /// Returns a list of non-fatal warnings for each value that was out of range
    /// or invalid. The config is mutated in place — invalid values are replaced with
    /// safe defaults or clamped to their valid range.
    pub fn validate(&mut self) -> Vec<ValidationWarning> {
        let mut warnings = Vec::new();

        // font.size: [4.0, 128.0]
        if self.font.size < 4.0 {
            warnings.push(ValidationWarning {
                field: "font.size".into(),
                message: format!(
                    "Font size {} is below minimum 4.0, clamped to 4.0",
                    self.font.size
                ),
            });
            self.font.size = 4.0;
        } else if self.font.size > 128.0 {
            warnings.push(ValidationWarning {
                field: "font.size".into(),
                message: format!(
                    "Font size {} exceeds maximum 128.0, clamped to 128.0",
                    self.font.size
                ),
            });
            self.font.size = 128.0;
        }

        // font.line_height: [0.5, 3.0]
        if self.font.line_height < 0.5 {
            warnings.push(ValidationWarning {
                field: "font.line_height".into(),
                message: format!(
                    "Line height {} is below minimum 0.5, clamped to 0.5",
                    self.font.line_height
                ),
            });
            self.font.line_height = 0.5;
        } else if self.font.line_height > 3.0 {
            warnings.push(ValidationWarning {
                field: "font.line_height".into(),
                message: format!(
                    "Line height {} exceeds maximum 3.0, clamped to 3.0",
                    self.font.line_height
                ),
            });
            self.font.line_height = 3.0;
        }

        // window.opacity: [0.0, 1.0]
        if self.window.opacity < 0.0 {
            warnings.push(ValidationWarning {
                field: "window.opacity".into(),
                message: format!(
                    "Opacity {} is below minimum 0.0, clamped to 0.0",
                    self.window.opacity
                ),
            });
            self.window.opacity = 0.0;
        } else if self.window.opacity > 1.0 {
            warnings.push(ValidationWarning {
                field: "window.opacity".into(),
                message: format!(
                    "Opacity {} exceeds maximum 1.0, clamped to 1.0",
                    self.window.opacity
                ),
            });
            self.window.opacity = 1.0;
        }

        // window.width: minimum 100
        if self.window.width < 100 {
            warnings.push(ValidationWarning {
                field: "window.width".into(),
                message: format!(
                    "Window width {} is below minimum 100, clamped to 100",
                    self.window.width
                ),
            });
            self.window.width = 100;
        }

        // window.height: minimum 100
        if self.window.height < 100 {
            warnings.push(ValidationWarning {
                field: "window.height".into(),
                message: format!(
                    "Window height {} is below minimum 100, clamped to 100",
                    self.window.height
                ),
            });
            self.window.height = 100;
        }

        // window.renderer: "auto" | "gpu" | "software"
        match self.window.renderer.as_str() {
            "auto" | "gpu" | "software" => {}
            _ => {
                warnings.push(ValidationWarning {
                    field: "window.renderer".into(),
                    message: format!(
                        "Invalid renderer '{}', reset to 'auto'",
                        self.window.renderer
                    ),
                });
                self.window.renderer = "auto".into();
            }
        }

        // terminal.cursor_style: "block" | "underline" | "bar"
        match self.terminal.cursor_style.as_str() {
            "block" | "underline" | "bar" => {}
            _ => {
                warnings.push(ValidationWarning {
                    field: "terminal.cursor_style".into(),
                    message: format!(
                        "Invalid cursor style '{}', reset to 'block'",
                        self.terminal.cursor_style
                    ),
                });
                self.terminal.cursor_style = "block".into();
            }
        }

        // terminal.cursor_blink_ms: [100, 5000]
        if self.terminal.cursor_blink_ms < 100 {
            warnings.push(ValidationWarning {
                field: "terminal.cursor_blink_ms".into(),
                message: format!(
                    "Cursor blink interval {}ms is below minimum 100, clamped to 100",
                    self.terminal.cursor_blink_ms
                ),
            });
            self.terminal.cursor_blink_ms = 100;
        } else if self.terminal.cursor_blink_ms > 5000 {
            warnings.push(ValidationWarning {
                field: "terminal.cursor_blink_ms".into(),
                message: format!(
                    "Cursor blink interval {}ms exceeds maximum 5000, clamped to 5000",
                    self.terminal.cursor_blink_ms
                ),
            });
            self.terminal.cursor_blink_ms = 5000;
        }

        // window.background_image_opacity: [0.0, 1.0]
        if self.window.background_image_opacity < 0.0 {
            warnings.push(ValidationWarning {
                field: "window.background_image_opacity".into(),
                message: format!(
                    "Background image opacity {} is below minimum 0.0, clamped to 0.0",
                    self.window.background_image_opacity
                ),
            });
            self.window.background_image_opacity = 0.0;
        } else if self.window.background_image_opacity > 1.0 {
            warnings.push(ValidationWarning {
                field: "window.background_image_opacity".into(),
                message: format!(
                    "Background image opacity {} exceeds maximum 1.0, clamped to 1.0",
                    self.window.background_image_opacity
                ),
            });
            self.window.background_image_opacity = 1.0;
        }

        // Color validation helper
        let defaults = ColorConfig::default();
        validate_hex_color(
            &mut self.colors.foreground,
            "colors.foreground",
            &defaults.foreground,
            &mut warnings,
        );
        validate_hex_color(
            &mut self.colors.background,
            "colors.background",
            &defaults.background,
            &mut warnings,
        );
        validate_hex_color(
            &mut self.colors.cursor,
            "colors.cursor",
            &defaults.cursor,
            &mut warnings,
        );
        validate_hex_color(
            &mut self.colors.selection_bg,
            "colors.selection_bg",
            &defaults.selection_bg,
            &mut warnings,
        );

        // Palette color entries
        for i in 0..self.colors.palette.len() {
            let field = format!("colors.palette[{}]", i);
            validate_hex_color(
                &mut self.colors.palette[i],
                &field,
                "#000000",
                &mut warnings,
            );
        }

        warnings
    }

    /// Get the default config file path.
    pub fn default_path() -> PathBuf {
        if let Some(config_dir) = dirs_config() {
            config_dir.join("wixen").join("config.toml")
        } else {
            PathBuf::from("config.toml")
        }
    }

    /// Get the resolved list of profiles. If `[[profiles]]` is non-empty, returns those.
    /// Otherwise synthesizes a single profile from `[shell]`.
    pub fn resolved_profiles(&self) -> Vec<Profile> {
        if self.profiles.is_empty() {
            vec![Profile {
                name: "Default".to_string(),
                program: self.shell.program.clone(),
                args: self.shell.args.clone(),
                working_directory: self.shell.working_directory.clone(),
                is_default: true,
            }]
        } else {
            self.profiles.clone()
        }
    }

    /// Get the default profile. First profile with `is_default = true`, or the first profile.
    pub fn default_profile(&self) -> Profile {
        let profiles = self.resolved_profiles();
        profiles
            .iter()
            .find(|p| p.is_default)
            .cloned()
            .unwrap_or_else(|| profiles.first().cloned().unwrap_or_default())
    }

    /// Get a profile by index (0-based). Returns None if out of bounds.
    pub fn profile_at(&self, index: usize) -> Option<Profile> {
        let profiles = self.resolved_profiles();
        profiles.get(index).cloned()
    }

    /// Save this config to a TOML file (for settings UI persistence).
    pub fn save(&self, path: &Path) -> Result<(), ConfigError> {
        let toml_str = toml::to_string_pretty(self)?;
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(path, toml_str)?;
        info!(path = %path.display(), "Configuration saved");
        Ok(())
    }

    /// Write the default config to a file (for generating initial config).
    pub fn write_default(path: &Path) -> Result<(), ConfigError> {
        let config = Config::default();
        let toml_str = toml::to_string_pretty(&config)?;
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(path, toml_str)?;
        info!(path = %path.display(), "Default config written");
        Ok(())
    }
}

/// Generate a JSON Schema for the `Config` struct.
///
/// Returns a pretty-printed JSON string describing every config field,
/// its type, and documentation. Useful for editor auto-completion and
/// config validation tooling.
pub fn generate_schema() -> String {
    let schema = schemars::schema_for!(Config);
    serde_json::to_string_pretty(&schema).expect("schema serialization cannot fail")
}

/// Check whether a string is a valid `#RRGGBB` hex color.
fn is_valid_hex_color(s: &str) -> bool {
    if s.len() != 7 || !s.starts_with('#') {
        return false;
    }
    s[1..].chars().all(|c| c.is_ascii_hexdigit())
}

/// Validate a hex color field, resetting to `default_value` if invalid.
fn validate_hex_color(
    value: &mut String,
    field: &str,
    default_value: &str,
    warnings: &mut Vec<ValidationWarning>,
) {
    if !is_valid_hex_color(value) {
        warnings.push(ValidationWarning {
            field: field.to_string(),
            message: format!(
                "Invalid hex color '{}', reset to '{}'",
                value, default_value
            ),
        });
        *value = default_value.to_string();
    }
}

/// Get the platform config directory (typically %APPDATA% on Windows).
fn dirs_config() -> Option<PathBuf> {
    std::env::var_os("APPDATA").map(PathBuf::from)
}

/// Get the directory containing the running executable.
fn exe_dir() -> Option<PathBuf> {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
}

/// Check whether portable mode is active.
///
/// Portable mode is triggered by a `wixen.portable` marker file
/// next to the executable.
pub fn is_portable() -> bool {
    exe_dir()
        .map(|d| d.join("wixen.portable").exists())
        .unwrap_or(false)
}

/// Get the config file path, respecting portable mode.
///
/// - **Portable** (`wixen.portable` marker or `--portable` flag):
///   `<exe_dir>/config.toml`
/// - **Normal**: `%APPDATA%/wixen/config.toml`
pub fn config_path(portable: bool) -> PathBuf {
    if portable {
        exe_dir()
            .map(|d| d.join("config.toml"))
            .unwrap_or_else(|| PathBuf::from("config.toml"))
    } else {
        Config::default_path()
    }
}

/// Get the directory where clink scripts should be installed.
///
/// Returns `%LOCALAPPDATA%\clink` if the env var is available.
pub fn clink_scripts_dir() -> Option<PathBuf> {
    std::env::var_os("LOCALAPPDATA").map(|d| PathBuf::from(d).join("clink"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_config() {
        let config = Config::default();
        assert_eq!(config.font.family, "Cascadia Code");
        assert_eq!(config.font.size, 14.0);
        assert_eq!(config.window.width, 1024);
        assert!(config.terminal.cursor_blink);
    }

    #[test]
    fn test_parse_partial_toml() {
        let toml = r#"
[font]
size = 16.0

[terminal]
cursor_style = "bar"
cursor_blink = false
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.font.size, 16.0);
        assert_eq!(config.font.family, "Cascadia Code"); // default preserved
        assert_eq!(config.terminal.cursor_style, "bar");
        assert!(!config.terminal.cursor_blink);
    }

    #[test]
    fn test_serialize_roundtrip() {
        let config = Config::default();
        let toml_str = toml::to_string_pretty(&config).unwrap();
        let parsed: Config = toml::from_str(&toml_str).unwrap();
        assert_eq!(parsed.font.family, config.font.family);
        assert_eq!(parsed.window.title, config.window.title);
    }

    #[test]
    fn test_config_partial_eq() {
        let a = Config::default();
        let b = Config::default();
        assert_eq!(a, b);

        let mut c = Config::default();
        c.font.size = 18.0;
        assert_ne!(a, c);
    }

    #[test]
    fn test_backward_compat_shell_section() {
        let toml = r#"
[shell]
program = "pwsh.exe"
args = ["-NoLogo"]
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert!(config.profiles.is_empty()); // no [[profiles]] in TOML
        let profiles = config.resolved_profiles();
        assert_eq!(profiles.len(), 1);
        assert_eq!(profiles[0].program, "pwsh.exe");
        assert_eq!(profiles[0].args, vec!["-NoLogo"]);
        assert!(profiles[0].is_default);
    }

    #[test]
    fn test_profiles_array() {
        let toml = r#"
[[profiles]]
name = "PowerShell"
program = "pwsh.exe"
is_default = true

[[profiles]]
name = "Command Prompt"
program = "cmd.exe"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.profiles.len(), 2);
        assert_eq!(config.profiles[0].name, "PowerShell");
        assert_eq!(config.profiles[1].name, "Command Prompt");
    }

    #[test]
    fn test_default_profile_selection() {
        let toml = r#"
[[profiles]]
name = "cmd"
program = "cmd.exe"

[[profiles]]
name = "pwsh"
program = "pwsh.exe"
is_default = true
"#;
        let config: Config = toml::from_str(toml).unwrap();
        let default = config.default_profile();
        assert_eq!(default.name, "pwsh");
    }

    #[test]
    fn test_fallback_first_profile() {
        let toml = r#"
[[profiles]]
name = "First"
program = "first.exe"

[[profiles]]
name = "Second"
program = "second.exe"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        let default = config.default_profile();
        assert_eq!(default.name, "First"); // no is_default → first wins
    }

    #[test]
    fn test_bell_style_parse() {
        let toml = r#"
[terminal]
bell = "visual"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.terminal.bell, BellStyle::Visual);
    }

    #[test]
    fn test_theme_field_parse() {
        let toml = r#"
[colors]
theme = "dracula"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.colors.theme, Some("dracula".to_string()));
    }

    #[test]
    fn test_opacity_default() {
        let config = Config::default();
        assert_eq!(config.window.opacity, 1.0);
        assert!(config.window.dark_title_bar);
        assert_eq!(config.window.background, BackgroundStyle::Opaque);
    }

    #[test]
    fn test_background_style_parse() {
        let toml = r#"
[window]
opacity = 0.9
background = "acrylic"
dark_title_bar = false
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert!((config.window.opacity - 0.9).abs() < f32::EPSILON);
        assert_eq!(config.window.background, BackgroundStyle::Acrylic);
        assert!(!config.window.dark_title_bar);
    }

    #[test]
    fn test_background_style_mica() {
        let toml = r#"
[window]
background = "mica"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.window.background, BackgroundStyle::Mica);
    }

    #[test]
    fn test_keybinding_defaults() {
        let kb = KeybindingsConfig::default();
        // Verify we have a substantial set of defaults
        assert!(
            kb.bindings.len() >= 40,
            "Expected at least 40 default bindings, got {}",
            kb.bindings.len()
        );
        // Check first and last are sensible
        assert_eq!(kb.bindings[0].chord, "ctrl+shift+t");
        assert_eq!(kb.bindings[0].action, "new_tab");
        // Verify key Windows Terminal compatible bindings exist
        let has = |action: &str| kb.bindings.iter().any(|b| b.action == action);
        assert!(has("new_tab"), "missing new_tab");
        assert!(has("close_pane"), "missing close_pane");
        assert!(has("next_tab"), "missing next_tab");
        assert!(has("prev_tab"), "missing prev_tab");
        assert!(has("copy"), "missing copy");
        assert!(has("paste"), "missing paste");
        assert!(has("find"), "missing find");
        assert!(has("command_palette"), "missing command_palette");
        assert!(has("settings"), "missing settings");
        assert!(has("toggle_fullscreen"), "missing toggle_fullscreen");
        assert!(has("split_horizontal"), "missing split_horizontal");
        assert!(has("split_vertical"), "missing split_vertical");
        assert!(has("zoom_in"), "missing zoom_in");
        assert!(has("zoom_out"), "missing zoom_out");
        assert!(has("zoom_reset"), "missing zoom_reset");
        assert!(has("select_all"), "missing select_all");
        assert!(has("scroll_up_page"), "missing scroll_up_page");
        assert!(has("scroll_to_top"), "missing scroll_to_top");
        // Pane navigation (Windows Terminal: Alt+Arrow)
        assert!(has("focus_pane_left"), "missing focus_pane_left");
        assert!(has("focus_pane_right"), "missing focus_pane_right");
        assert!(has("focus_pane_up"), "missing focus_pane_up");
        assert!(has("focus_pane_down"), "missing focus_pane_down");
        // Pane resize
        assert!(has("resize_pane_grow"), "missing resize_pane_grow");
        assert!(has("resize_pane_shrink"), "missing resize_pane_shrink");
        // Tab management
        assert!(has("duplicate_tab"), "missing duplicate_tab");
        assert!(has("new_window"), "missing new_window");
        // Search
        assert!(has("find_next"), "missing find_next");
        assert!(has("find_previous"), "missing find_previous");
        // Accessibility
        assert!(has("toggle_zoom"), "missing toggle_zoom");
        assert!(has("toggle_read_only"), "missing toggle_read_only");
        assert!(
            has("toggle_broadcast_input"),
            "missing toggle_broadcast_input"
        );
        // Shell integration
        assert!(
            has("jump_to_previous_prompt"),
            "missing jump_to_previous_prompt"
        );
        assert!(has("jump_to_next_prompt"), "missing jump_to_next_prompt");
        // Terminal
        assert!(has("clear_terminal"), "missing clear_terminal");
        assert!(has("clear_scrollback"), "missing clear_scrollback");
    }

    #[test]
    fn test_keybinding_chords_are_normalized() {
        let kb = KeybindingsConfig::default();
        for binding in &kb.bindings {
            let normalized = normalize_chord(&binding.chord);
            assert_eq!(
                binding.chord, normalized,
                "Binding chord {:?} is not normalized (expected {:?})",
                binding.chord, normalized
            );
        }
    }

    #[test]
    fn test_no_duplicate_chords() {
        let kb = KeybindingsConfig::default();
        let mut seen = std::collections::HashSet::new();
        for binding in &kb.bindings {
            assert!(
                seen.insert(&binding.chord),
                "Duplicate chord: {:?} (action: {:?})",
                binding.chord,
                binding.action
            );
        }
    }

    #[test]
    fn test_normalize_chord() {
        assert_eq!(normalize_chord("Shift+Ctrl+T"), "ctrl+shift+t");
        assert_eq!(normalize_chord("Alt+Shift+Plus"), "alt+shift+plus");
        assert_eq!(normalize_chord("ctrl+shift+tab"), "ctrl+shift+tab");
        assert_eq!(normalize_chord("Win+Ctrl+A"), "ctrl+win+a");
    }

    #[test]
    fn test_normalize_chord_single_key() {
        assert_eq!(normalize_chord("F11"), "f11");
        assert_eq!(normalize_chord("Escape"), "escape");
    }

    #[test]
    fn test_parse_chord() {
        let (ctrl, shift, alt, win, key) = parse_chord("ctrl+shift+t");
        assert!(ctrl);
        assert!(shift);
        assert!(!alt);
        assert!(!win);
        assert_eq!(key, "t");

        let (ctrl, shift, alt, win, key) = parse_chord("f11");
        assert!(!ctrl);
        assert!(!shift);
        assert!(!alt);
        assert!(!win);
        assert_eq!(key, "f11");

        let (ctrl, shift, alt, win, key) = parse_chord("alt+shift+plus");
        assert!(!ctrl);
        assert!(shift);
        assert!(alt);
        assert!(!win);
        assert_eq!(key, "plus");
    }

    #[test]
    fn test_keybindings_serialize_roundtrip() {
        let config = Config::default();
        let toml_str = toml::to_string_pretty(&config).unwrap();
        let parsed: Config = toml::from_str(&toml_str).unwrap();
        assert_eq!(parsed.keybindings, config.keybindings);
        assert!(parsed.keybindings.bindings.len() >= 40);
    }

    #[test]
    fn test_keybinding_simple_constructor() {
        let kb = Keybinding::simple("ctrl+shift+t", "new_tab");
        assert_eq!(kb.chord, "ctrl+shift+t");
        assert_eq!(kb.action, "new_tab");
        assert!(kb.args.is_none());
    }

    #[test]
    fn test_keybinding_with_args_constructor() {
        let kb = Keybinding::with_args("f5", "send_text", r"cargo test\r");
        assert_eq!(kb.chord, "f5");
        assert_eq!(kb.action, "send_text");
        assert_eq!(kb.args.as_deref(), Some(r"cargo test\r"));
    }

    #[test]
    fn test_keybinding_args_toml_roundtrip() {
        let kb = Keybinding::with_args("f5", "send_text", r"cargo test\r");
        let toml_str = toml::to_string_pretty(&kb).unwrap();
        let parsed: Keybinding = toml::from_str(&toml_str).unwrap();
        assert_eq!(parsed, kb);
    }

    #[test]
    fn test_keybinding_args_default_none() {
        // Args field should deserialize as None when missing
        let toml_str = r#"
chord = "ctrl+t"
action = "new_tab"
"#;
        let parsed: Keybinding = toml::from_str(toml_str).unwrap();
        assert!(parsed.args.is_none());
    }

    #[test]
    fn test_vk_to_chord_basic() {
        // Ctrl+Shift+T → "ctrl+shift+t"
        assert_eq!(vk_to_chord(0x54, true, true, false), "ctrl+shift+t");
        // Ctrl+Shift+C → "ctrl+shift+c"
        assert_eq!(vk_to_chord(0x43, true, true, false), "ctrl+shift+c");
    }

    #[test]
    fn test_vk_to_chord_function_key() {
        // F11 → "f11"
        assert_eq!(vk_to_chord(0x7A, false, false, false), "f11");
        // F1 → "f1"
        assert_eq!(vk_to_chord(0x70, false, false, false), "f1");
        // Shift+F3 → "shift+f3"
        assert_eq!(vk_to_chord(0x72, false, true, false), "shift+f3");
    }

    #[test]
    fn test_vk_to_chord_tab() {
        // Ctrl+Tab → "ctrl+tab"
        assert_eq!(vk_to_chord(0x09, true, false, false), "ctrl+tab");
        // Ctrl+Shift+Tab → "ctrl+shift+tab"
        assert_eq!(vk_to_chord(0x09, true, true, false), "ctrl+shift+tab");
    }

    #[test]
    fn test_config_path_portable() {
        let path = config_path(true);
        // Portable mode returns a path ending in config.toml relative to the exe dir
        assert_eq!(path.file_name().unwrap(), "config.toml");
        // Should NOT be inside %APPDATA%
        if let Some(appdata) = std::env::var_os("APPDATA") {
            assert!(!path.starts_with(PathBuf::from(appdata)));
        }
    }

    #[test]
    fn test_config_path_default() {
        let path = config_path(false);
        assert_eq!(path.file_name().unwrap(), "config.toml");
        // Should be inside %APPDATA%/wixen/ when APPDATA is set
        if let Some(appdata) = std::env::var_os("APPDATA") {
            let expected = PathBuf::from(appdata).join("wixen").join("config.toml");
            assert_eq!(path, expected);
        }
    }

    #[test]
    fn test_is_portable_marker() {
        // In test context, there should be no wixen.portable next to the test runner
        // so is_portable() should return false
        assert!(!is_portable());
    }

    #[test]
    fn test_lua_overrides_applied() {
        let dir = std::env::temp_dir().join("wixen_config_test_lua_overrides");
        let _ = std::fs::create_dir_all(&dir);
        let toml_path = dir.join("config.toml");
        let lua_path = dir.join("config.lua");

        // Write a basic TOML config
        std::fs::write(&toml_path, "[font]\nsize = 14.0\n").unwrap();

        // Write a Lua config that overrides font size
        std::fs::write(
            &lua_path,
            "wixen.set_option(\"font.size\", 22)\nwixen.set_option(\"colors.foreground\", \"#abcdef\")\n",
        )
        .unwrap();

        let config = Config::load(&toml_path).unwrap();
        assert_eq!(config.font.size, 22.0); // overridden by Lua
        assert_eq!(config.colors.foreground, "#abcdef"); // overridden by Lua

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_lua_keybindings_merged() {
        let dir = std::env::temp_dir().join("wixen_config_test_lua_keybindings");
        let _ = std::fs::create_dir_all(&dir);
        let toml_path = dir.join("config.toml");
        let lua_path = dir.join("config.lua");

        // Write a minimal TOML config (gets default keybindings)
        std::fs::write(&toml_path, "").unwrap();

        // Write Lua keybindings
        std::fs::write(
            &lua_path,
            r#"wixen.bind("ctrl+shift+r", "reload_config")
wixen.bind("ctrl+shift+n", "new_window")
"#,
        )
        .unwrap();

        let config = Config::load(&toml_path).unwrap();
        let default_count = KeybindingsConfig::default().bindings.len();

        // Should have default bindings + 2 from Lua
        assert_eq!(config.keybindings.bindings.len(), default_count + 2);

        // Check the Lua bindings are present at the end
        let last = &config.keybindings.bindings[config.keybindings.bindings.len() - 2];
        assert_eq!(last.chord, "ctrl+shift+r");
        assert_eq!(last.action, "reload_config");

        let very_last = &config.keybindings.bindings[config.keybindings.bindings.len() - 1];
        assert_eq!(very_last.chord, "ctrl+shift+n");
        assert_eq!(very_last.action, "new_window");

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_apply_lua_overrides_all_keys() {
        let mut config = Config::default();
        let mut overrides = std::collections::HashMap::new();
        overrides.insert("font.family".to_string(), "JetBrains Mono".to_string());
        overrides.insert("font.size".to_string(), "18".to_string());
        overrides.insert("font.line_height".to_string(), "1.3".to_string());
        overrides.insert("terminal.cursor_style".to_string(), "bar".to_string());
        overrides.insert("terminal.cursor_blink".to_string(), "false".to_string());
        overrides.insert("terminal.cursor_blink_ms".to_string(), "600".to_string());
        overrides.insert("colors.foreground".to_string(), "#ffffff".to_string());
        overrides.insert("colors.background".to_string(), "#000000".to_string());
        overrides.insert("colors.cursor".to_string(), "#ff0000".to_string());
        overrides.insert("colors.selection_bg".to_string(), "#333333".to_string());
        overrides.insert("colors.theme".to_string(), "catppuccin".to_string());
        overrides.insert("window.title".to_string(), "My Terminal".to_string());
        overrides.insert("window.opacity".to_string(), "0.85".to_string());
        overrides.insert("window.renderer".to_string(), "software".to_string());

        config.apply_lua_overrides(&overrides);

        assert_eq!(config.font.family, "JetBrains Mono");
        assert_eq!(config.font.size, 18.0);
        assert!((config.font.line_height - 1.3).abs() < f32::EPSILON);
        assert_eq!(config.terminal.cursor_style, "bar");
        assert!(!config.terminal.cursor_blink);
        assert_eq!(config.terminal.cursor_blink_ms, 600);
        assert_eq!(config.colors.foreground, "#ffffff");
        assert_eq!(config.colors.background, "#000000");
        assert_eq!(config.colors.cursor, "#ff0000");
        assert_eq!(config.colors.selection_bg, "#333333");
        assert_eq!(config.colors.theme, Some("catppuccin".to_string()));
        assert_eq!(config.window.title, "My Terminal");
        assert!((config.window.opacity - 0.85).abs() < f32::EPSILON);
        assert_eq!(config.window.renderer, "software");
    }

    #[test]
    fn test_vk_to_chord_special() {
        // Alt+Shift+OEM_PLUS → "alt+shift+plus"
        assert_eq!(vk_to_chord(0xBB, false, true, true), "alt+shift+plus");
        // Alt+Shift+OEM_MINUS → "alt+shift+minus"
        assert_eq!(vk_to_chord(0xBD, false, true, true), "alt+shift+minus");
        // Ctrl+Comma → "ctrl+comma"
        assert_eq!(vk_to_chord(0xBC, true, false, false), "ctrl+comma");
    }

    #[test]
    fn test_ligatures_default_enabled() {
        let config = Config::default();
        assert!(
            config.font.ligatures,
            "Ligatures should be enabled by default"
        );
    }

    #[test]
    fn test_ligatures_toml_parse() {
        let toml = r#"
[font]
ligatures = false
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert!(!config.font.ligatures);
    }

    #[test]
    fn test_ligatures_lua_override() {
        let mut config = Config::default();
        assert!(config.font.ligatures);

        let mut overrides = std::collections::HashMap::new();
        overrides.insert("font.ligatures".to_string(), "false".to_string());
        config.apply_lua_overrides(&overrides);
        assert!(!config.font.ligatures);
    }

    #[test]
    fn test_clipboard_osc52_default() {
        let config = Config::default();
        assert_eq!(config.terminal.osc52, Osc52Policy::WriteOnly);
    }

    #[test]
    fn test_clipboard_osc52_toml_parse() {
        let toml = r#"
[terminal]
osc52 = "read-write"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.terminal.osc52, Osc52Policy::ReadWrite);

        let toml2 = r#"
[terminal]
osc52 = "disabled"
"#;
        let config2: Config = toml::from_str(toml2).unwrap();
        assert_eq!(config2.terminal.osc52, Osc52Policy::Disabled);
    }

    #[test]
    fn test_clipboard_osc52_lua_override() {
        let mut config = Config::default();
        let mut overrides = std::collections::HashMap::new();
        overrides.insert("terminal.osc52".to_string(), "disabled".to_string());
        config.apply_lua_overrides(&overrides);
        assert_eq!(config.terminal.osc52, Osc52Policy::Disabled);
    }

    // ── Tier 4B: SSH integration tests ──

    #[test]
    fn test_ssh_target_defaults() {
        let target = SshTarget::default();
        assert!(target.host.is_empty());
        assert_eq!(target.port, 22);
        assert!(target.user.is_empty());
        assert!(target.identity_file.is_empty());
    }

    #[test]
    fn test_ssh_to_command_simple() {
        let target = SshTarget {
            host: "example.com".into(),
            ..Default::default()
        };
        let (program, args) = target.to_command();
        let expected_prog = if cfg!(windows) { "ssh.exe" } else { "ssh" };
        assert_eq!(program, expected_prog);
        assert_eq!(args, vec!["example.com"]);
    }

    #[test]
    fn test_ssh_to_command_with_user_and_port() {
        let target = SshTarget {
            host: "server.io".into(),
            user: "admin".into(),
            port: 2222,
            ..Default::default()
        };
        let (program, args) = target.to_command();
        let expected_prog = if cfg!(windows) { "ssh.exe" } else { "ssh" };
        assert_eq!(program, expected_prog);
        assert_eq!(args, vec!["-p", "2222", "admin@server.io"]);
    }

    #[test]
    fn test_ssh_to_command_with_identity() {
        let target = SshTarget {
            host: "server.io".into(),
            identity_file: "~/.ssh/id_ed25519".into(),
            ..Default::default()
        };
        let (_, args) = target.to_command();
        assert!(args.contains(&"-i".to_string()));
        assert!(args.contains(&"~/.ssh/id_ed25519".to_string()));
    }

    #[test]
    fn test_ssh_to_command_extra_args() {
        let target = SshTarget {
            host: "server.io".into(),
            extra_args: vec!["-o".into(), "StrictHostKeyChecking=no".into()],
            ..Default::default()
        };
        let (_, args) = target.to_command();
        assert!(args.contains(&"-o".to_string()));
        assert!(args.contains(&"StrictHostKeyChecking=no".to_string()));
    }

    #[test]
    fn test_ssh_targets_toml_parse() {
        let toml = r#"
[[ssh]]
name = "Production"
host = "prod.example.com"
user = "deploy"
port = 22

[[ssh]]
name = "Staging"
host = "staging.example.com"
user = "admin"
port = 2222
identity_file = "~/.ssh/staging_key"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.ssh.len(), 2);
        assert_eq!(config.ssh[0].name, "Production");
        assert_eq!(config.ssh[0].host, "prod.example.com");
        assert_eq!(config.ssh[1].port, 2222);
        assert_eq!(config.ssh[1].identity_file, "~/.ssh/staging_key");
    }

    #[test]
    fn test_ssh_targets_default_empty() {
        let config = Config::default();
        assert!(config.ssh.is_empty());
    }

    // ── Tier 4A: Background image config tests ──

    #[test]
    fn test_background_image_default_empty() {
        let config = Config::default();
        assert!(config.window.background_image.is_empty());
        assert!((config.window.background_image_opacity - 0.3).abs() < f32::EPSILON);
    }

    #[test]
    fn test_background_image_toml_parse() {
        let toml = r#"
[window]
background_image = "C:/wallpapers/space.png"
background_image_opacity = 0.5
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.window.background_image, "C:/wallpapers/space.png");
        assert!((config.window.background_image_opacity - 0.5).abs() < f32::EPSILON);
    }

    #[test]
    fn test_background_image_opacity_validation() {
        let mut config = Config::default();
        config.window.background_image_opacity = -0.5;
        let warnings = config.validate();
        assert!(
            warnings
                .iter()
                .any(|w| w.field == "window.background_image_opacity")
        );
        assert_eq!(config.window.background_image_opacity, 0.0);

        config.window.background_image_opacity = 2.0;
        let warnings = config.validate();
        assert!(
            warnings
                .iter()
                .any(|w| w.field == "window.background_image_opacity")
        );
        assert_eq!(config.window.background_image_opacity, 1.0);
    }

    #[test]
    fn test_background_image_lua_override() {
        let mut config = Config::default();
        let mut overrides = std::collections::HashMap::new();
        overrides.insert(
            "window.background_image".to_string(),
            "/path/to/image.png".to_string(),
        );
        overrides.insert(
            "window.background_image_opacity".to_string(),
            "0.7".to_string(),
        );
        config.apply_lua_overrides(&overrides);
        assert_eq!(config.window.background_image, "/path/to/image.png");
        assert!((config.window.background_image_opacity - 0.7).abs() < f32::EPSILON);
    }

    // ── Quake mode config tests ──

    #[test]
    fn test_quake_mode_default_disabled() {
        let config = Config::default();
        assert!(!config.window.quake_mode);
        assert_eq!(config.window.quake_hotkey, "win+backtick");
    }

    #[test]
    fn test_quake_mode_toml_parse() {
        let toml = r#"
[window]
quake_mode = true
quake_hotkey = "ctrl+backtick"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert!(config.window.quake_mode);
        assert_eq!(config.window.quake_hotkey, "ctrl+backtick");
    }

    #[test]
    fn test_quake_mode_lua_override() {
        let mut config = Config::default();
        let mut overrides = std::collections::HashMap::new();
        overrides.insert("window.quake_mode".to_string(), "true".to_string());
        overrides.insert(
            "window.quake_hotkey".to_string(),
            "alt+backtick".to_string(),
        );
        config.apply_lua_overrides(&overrides);
        assert!(config.window.quake_mode);
        assert_eq!(config.window.quake_hotkey, "alt+backtick");
    }

    #[test]
    fn test_parse_quake_hotkey_win_backtick() {
        let (mods, vk) = parse_quake_hotkey("win+backtick").unwrap();
        assert_eq!(mods, 8); // MOD_WIN
        assert_eq!(vk, 0xC0); // VK_OEM_3
    }

    #[test]
    fn test_parse_quake_hotkey_ctrl_backtick() {
        let (mods, vk) = parse_quake_hotkey("ctrl+backtick").unwrap();
        assert_eq!(mods, 2); // MOD_CONTROL
        assert_eq!(vk, 0xC0);
    }

    #[test]
    fn test_parse_quake_hotkey_alt_f12() {
        let (mods, vk) = parse_quake_hotkey("alt+f12").unwrap();
        assert_eq!(mods, 1); // MOD_ALT
        assert_eq!(vk, 0x7B); // VK_F12
    }

    #[test]
    fn test_parse_quake_hotkey_unknown_key() {
        assert!(parse_quake_hotkey("win+unicorn").is_none());
    }

    // ── Tier 2A: Config semantic validation tests ──

    #[test]
    fn test_validate_default_config_no_warnings() {
        let mut config = Config::default();
        let warnings = config.validate();
        assert!(
            warnings.is_empty(),
            "Default config should produce no warnings"
        );
    }

    #[test]
    fn test_validate_font_size_too_small() {
        let mut config = Config::default();
        config.font.size = 2.0;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "font.size"));
        assert_eq!(config.font.size, 4.0); // clamped to minimum
    }

    #[test]
    fn test_validate_font_size_too_large() {
        let mut config = Config::default();
        config.font.size = 200.0;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "font.size"));
        assert_eq!(config.font.size, 128.0); // clamped to maximum
    }

    #[test]
    fn test_validate_line_height_too_small() {
        let mut config = Config::default();
        config.font.line_height = 0.1;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "font.line_height"));
        assert_eq!(config.font.line_height, 0.5);
    }

    #[test]
    fn test_validate_line_height_too_large() {
        let mut config = Config::default();
        config.font.line_height = 5.0;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "font.line_height"));
        assert_eq!(config.font.line_height, 3.0);
    }

    #[test]
    fn test_validate_opacity_clamped() {
        let mut config = Config::default();
        config.window.opacity = -0.5;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "window.opacity"));
        assert_eq!(config.window.opacity, 0.0);

        config.window.opacity = 1.5;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "window.opacity"));
        assert_eq!(config.window.opacity, 1.0);
    }

    #[test]
    fn test_validate_window_dimensions_minimum() {
        let mut config = Config::default();
        config.window.width = 50;
        config.window.height = 30;
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "window.width"));
        assert!(warnings.iter().any(|w| w.field == "window.height"));
        assert_eq!(config.window.width, 100);
        assert_eq!(config.window.height, 100);
    }

    #[test]
    fn test_validate_renderer_invalid() {
        let mut config = Config::default();
        config.window.renderer = "vulkan".to_string();
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "window.renderer"));
        assert_eq!(config.window.renderer, "auto"); // reset to default
    }

    #[test]
    fn test_validate_renderer_valid_values() {
        for valid in &["auto", "gpu", "software"] {
            let mut config = Config::default();
            config.window.renderer = valid.to_string();
            let warnings = config.validate();
            assert!(
                !warnings.iter().any(|w| w.field == "window.renderer"),
                "'{}' should be a valid renderer",
                valid
            );
        }
    }

    #[test]
    fn test_validate_cursor_style_invalid() {
        let mut config = Config::default();
        config.terminal.cursor_style = "triangle".to_string();
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "terminal.cursor_style"));
        assert_eq!(config.terminal.cursor_style, "block"); // reset to default
    }

    #[test]
    fn test_validate_cursor_style_valid_values() {
        for valid in &["block", "underline", "bar"] {
            let mut config = Config::default();
            config.terminal.cursor_style = valid.to_string();
            let warnings = config.validate();
            assert!(
                !warnings.iter().any(|w| w.field == "terminal.cursor_style"),
                "'{}' should be a valid cursor style",
                valid
            );
        }
    }

    #[test]
    fn test_validate_cursor_blink_ms_range() {
        let mut config = Config::default();
        config.terminal.cursor_blink_ms = 10;
        let warnings = config.validate();
        assert!(
            warnings
                .iter()
                .any(|w| w.field == "terminal.cursor_blink_ms")
        );
        assert_eq!(config.terminal.cursor_blink_ms, 100);

        config.terminal.cursor_blink_ms = 10000;
        let warnings = config.validate();
        assert!(
            warnings
                .iter()
                .any(|w| w.field == "terminal.cursor_blink_ms")
        );
        assert_eq!(config.terminal.cursor_blink_ms, 5000);
    }

    #[test]
    fn test_validate_hex_color_invalid() {
        let mut config = Config::default();
        config.colors.foreground = "red".to_string();
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "colors.foreground"));
        // Should reset to default
        assert_eq!(config.colors.foreground, "#d9d9d9");
    }

    #[test]
    fn test_validate_hex_color_valid() {
        let mut config = Config::default();
        config.colors.foreground = "#abcdef".to_string();
        config.colors.background = "#000000".to_string();
        config.colors.cursor = "#FFFFFF".to_string();
        config.colors.selection_bg = "#123ABC".to_string();
        let warnings = config.validate();
        // No color warnings
        assert!(!warnings.iter().any(|w| w.field.starts_with("colors.")));
    }

    #[test]
    fn test_validate_multiple_warnings() {
        let mut config = Config::default();
        config.font.size = 1.0;
        config.window.opacity = 2.0;
        config.terminal.cursor_style = "invalid".to_string();
        config.colors.foreground = "not-hex".to_string();
        let warnings = config.validate();
        assert!(
            warnings.len() >= 4,
            "Should have at least 4 warnings, got {}",
            warnings.len()
        );
    }

    #[test]
    fn test_validate_palette_colors() {
        let mut config = Config::default();
        config.colors.palette = vec![
            "#000000".to_string(),
            "invalid".to_string(),
            "#ff0000".to_string(),
        ];
        let warnings = config.validate();
        assert!(warnings.iter().any(|w| w.field == "colors.palette[1]"));
        assert_eq!(config.colors.palette[1], "#000000"); // bad entry defaulted
    }

    #[test]
    fn test_tab_bar_mode_default() {
        let config = Config::default();
        assert_eq!(config.window.tab_bar, TabBarMode::AutoHide);
    }

    #[test]
    fn test_tab_bar_mode_parse() {
        let toml = r#"
[window]
tab_bar = "always"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.window.tab_bar, TabBarMode::Always);
    }

    #[test]
    fn test_scrollbar_mode_default() {
        let config = Config::default();
        assert_eq!(config.window.scrollbar, ScrollbarMode::Auto);
    }

    #[test]
    fn test_scrollbar_mode_parse() {
        let toml = r#"
[window]
scrollbar = "always"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.window.scrollbar, ScrollbarMode::Always);
    }

    #[test]
    fn test_scrollbar_mode_never() {
        let toml = r#"
[window]
scrollbar = "never"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.window.scrollbar, ScrollbarMode::Never);
    }

    #[test]
    fn test_scrollbar_mode_roundtrip() {
        let mut config = Config::default();
        config.window.scrollbar = ScrollbarMode::Always;
        let toml_str = toml::to_string_pretty(&config).unwrap();
        let parsed: Config = toml::from_str(&toml_str).unwrap();
        assert_eq!(parsed.window.scrollbar, ScrollbarMode::Always);
    }

    #[test]
    fn test_padding_default() {
        let config = Config::default();
        assert_eq!(config.window.padding.left, 4);
        assert_eq!(config.window.padding.right, 4);
        assert_eq!(config.window.padding.top, 4);
        assert_eq!(config.window.padding.bottom, 4);
    }

    #[test]
    fn test_padding_parse() {
        let toml = r#"
[window.padding]
left = 10
right = 10
top = 8
bottom = 8
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.window.padding.left, 10);
        assert_eq!(config.window.padding.right, 10);
        assert_eq!(config.window.padding.top, 8);
        assert_eq!(config.window.padding.bottom, 8);
    }

    #[test]
    fn test_padding_helpers() {
        let pad = Padding {
            left: 10,
            right: 5,
            top: 8,
            bottom: 2,
        };
        assert_eq!(pad.horizontal(), 15);
        assert_eq!(pad.vertical(), 10);

        let uniform = Padding::uniform(12);
        assert_eq!(uniform.left, 12);
        assert_eq!(uniform.right, 12);
        assert_eq!(uniform.horizontal(), 24);
        assert_eq!(uniform.vertical(), 24);
    }

    #[test]
    fn test_padding_roundtrip() {
        let mut config = Config::default();
        config.window.padding = Padding {
            left: 16,
            right: 16,
            top: 8,
            bottom: 8,
        };
        let toml_str = toml::to_string_pretty(&config).unwrap();
        let parsed: Config = toml::from_str(&toml_str).unwrap();
        assert_eq!(parsed.window.padding, config.window.padding);
    }

    #[test]
    fn test_command_notify_threshold_default() {
        let config = Config::default();
        assert_eq!(config.terminal.command_notify_threshold, 10);
    }

    #[test]
    fn test_command_notify_threshold_parse() {
        let toml = r#"
[terminal]
command_notify_threshold = 30
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.terminal.command_notify_threshold, 30);
    }

    #[test]
    fn test_command_notify_disabled() {
        let toml = r#"
[terminal]
command_notify_threshold = 0
"#;
        let config: Config = toml::from_str(toml).unwrap();
        assert_eq!(config.terminal.command_notify_threshold, 0);
    }

    // ── Tier 6: Config save-back tests ──

    #[test]
    fn test_config_save_roundtrip() {
        let dir = std::env::temp_dir().join("wixen_test_save_roundtrip");
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("config.toml");

        let mut config = Config::default();
        config.font.size = 18.0;
        config.window.title = "Test Terminal".to_string();
        config.save(&path).unwrap();

        let loaded: Config = toml::from_str(&std::fs::read_to_string(&path).unwrap()).unwrap();
        assert!((loaded.font.size - 18.0).abs() < f32::EPSILON);
        assert_eq!(loaded.window.title, "Test Terminal");

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_config_save_creates_dirs() {
        let dir = std::env::temp_dir().join("wixen_test_save_dirs/a/b/c");
        let _ = std::fs::remove_dir_all(std::env::temp_dir().join("wixen_test_save_dirs"));
        let path = dir.join("config.toml");

        let config = Config::default();
        assert!(config.save(&path).is_ok());
        assert!(path.exists());

        let _ = std::fs::remove_dir_all(std::env::temp_dir().join("wixen_test_save_dirs"));
    }

    #[test]
    fn test_config_save_preserves_keybindings() {
        let dir = std::env::temp_dir().join("wixen_test_save_keybindings");
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("config.toml");

        let mut config = Config::default();
        config
            .keybindings
            .bindings
            .push(Keybinding::simple("ctrl+z", "undo"));
        config.save(&path).unwrap();

        let loaded: Config = toml::from_str(&std::fs::read_to_string(&path).unwrap()).unwrap();
        assert!(
            loaded
                .keybindings
                .bindings
                .iter()
                .any(|k| k.chord == "ctrl+z" && k.action == "undo")
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_font_path_default_empty() {
        let config = Config::default();
        assert!(config.font.font_path.is_empty());
    }

    #[test]
    fn test_font_path_roundtrip() {
        let toml_str = r#"
[font]
family = "Consolas"
size = 12.0
line_height = 1.0
ligatures = false
font_path = "C:/fonts/my_font.ttf"
"#;
        let config: Config = toml::from_str(toml_str).unwrap();
        assert_eq!(config.font.font_path, "C:/fonts/my_font.ttf");
    }

    #[test]
    fn test_font_path_absent_in_toml() {
        // When font_path is missing from TOML, it should default to empty
        let toml_str = r#"
[font]
family = "Consolas"
size = 12.0
line_height = 1.0
ligatures = false
"#;
        let config: Config = toml::from_str(toml_str).unwrap();
        assert!(config.font.font_path.is_empty());
    }

    #[test]
    fn test_session_restore_default() {
        let config = Config::default();
        assert_eq!(config.terminal.session_restore, "never");
    }

    #[test]
    fn test_session_restore_roundtrip() {
        let toml_str = r#"
[terminal]
session_restore = "always"
"#;
        let config: Config = toml::from_str(toml_str).unwrap();
        assert_eq!(config.terminal.session_restore, "always");
    }

    #[test]
    fn test_schema_generation_produces_valid_json() {
        let json = generate_schema();
        let parsed: serde_json::Value =
            serde_json::from_str(&json).expect("generate_schema() must produce valid JSON");
        // Verify top-level structure has expected JSON Schema fields.
        assert!(parsed.get("$schema").is_some() || parsed.get("type").is_some());
        assert!(parsed.get("properties").is_some());
    }

    #[test]
    fn test_export_schema_to_file() {
        let json = generate_schema();
        let manifest_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let schema_dir = manifest_dir
            .parent()
            .unwrap()
            .parent()
            .unwrap()
            .join("schemas");
        std::fs::create_dir_all(&schema_dir).unwrap();
        let schema_path = schema_dir.join("config.schema.json");
        std::fs::write(&schema_path, &json).unwrap();
        assert!(schema_path.exists());
        // Verify the written file is valid JSON
        let read_back = std::fs::read_to_string(&schema_path).unwrap();
        let _: serde_json::Value = serde_json::from_str(&read_back).unwrap();
    }

    #[test]
    fn test_should_reduce_motion_on() {
        assert!(should_reduce_motion("on", false));
        assert!(should_reduce_motion("on", true));
    }

    #[test]
    fn test_should_reduce_motion_off() {
        assert!(!should_reduce_motion("off", false));
        assert!(!should_reduce_motion("off", true));
    }

    #[test]
    fn test_should_reduce_motion_auto_delegates_to_system() {
        assert!(!should_reduce_motion("auto", false));
        assert!(should_reduce_motion("auto", true));
    }

    #[test]
    fn test_should_reduce_motion_unknown_falls_back_to_system() {
        assert!(!should_reduce_motion("bogus", false));
        assert!(should_reduce_motion("bogus", true));
    }

    #[test]
    fn test_accessibility_config_default_reduced_motion() {
        let config = AccessibilityConfig::default();
        assert_eq!(config.reduced_motion, "auto");
    }

    #[test]
    fn test_accessibility_config_default_output_debounce() {
        let config = AccessibilityConfig::default();
        assert_eq!(config.output_debounce_ms, 100);
    }

    #[test]
    fn test_accessibility_config_output_debounce_from_toml() {
        let toml_str = r#"
[accessibility]
output_debounce_ms = 250
"#;
        let config: Config = toml::from_str(toml_str).unwrap();
        assert_eq!(config.accessibility.output_debounce_ms, 250);
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(5_000))]

        /// toml::from_str::<Config> must never panic on arbitrary TOML strings.
        /// Invalid TOML should return Err, not panic.
        #[test]
        fn config_parse_never_panics(s in ".*") {
            let _ = toml::from_str::<Config>(&s);
        }

        /// normalize_chord must never panic on arbitrary input.
        #[test]
        fn normalize_chord_never_panics(chord in ".*") {
            let _ = normalize_chord(&chord);
        }

        /// parse_chord must never panic on arbitrary input.
        #[test]
        fn parse_chord_never_panics(chord in ".*") {
            let _ = parse_chord(&chord);
        }

        /// parse_quake_hotkey must never panic on arbitrary input.
        #[test]
        fn parse_quake_hotkey_never_panics(hotkey in ".*") {
            let _ = parse_quake_hotkey(&hotkey);
        }

        /// normalize_chord is idempotent: normalizing twice gives the same result.
        #[test]
        fn normalize_chord_idempotent(chord in "[A-Za-z+]{1,30}") {
            let first = normalize_chord(&chord);
            let second = normalize_chord(&first);
            prop_assert_eq!(&first, &second, "normalize_chord must be idempotent");
        }

        /// parse_chord(normalize_chord(x)) must recover the same modifiers
        /// regardless of input order.
        #[test]
        fn normalize_then_parse_roundtrip(
            ctrl in any::<bool>(),
            shift in any::<bool>(),
            alt in any::<bool>(),
            key in "[a-z]",
        ) {
            let mut parts = Vec::new();
            if ctrl { parts.push("Ctrl"); }
            if shift { parts.push("Shift"); }
            if alt { parts.push("Alt"); }
            parts.push(&key);
            let chord = parts.join("+");
            let normalized = normalize_chord(&chord);
            let (pc, ps, pa, _pw, pk) = parse_chord(&normalized);
            prop_assert_eq!(pc, ctrl);
            prop_assert_eq!(ps, shift);
            prop_assert_eq!(pa, alt);
            prop_assert_eq!(pk, key.to_lowercase());
        }

        /// Valid TOML with known sections must parse without panic.
        #[test]
        fn valid_toml_sections_parse(
            font_size in 6.0f64..72.0,
            scrollback in 100u32..1_000_000,
        ) {
            let toml_str = format!(
                r#"
[font]
size = {font_size}

[scrollback]
lines = {scrollback}
"#
            );
            let result = toml::from_str::<Config>(&toml_str);
            prop_assert!(result.is_ok(), "Valid TOML must parse: {:?}", result.err());
        }
    }
}

#[cfg(test)]
mod keybinding_resolution_tests {
    use super::*;

    #[test]
    fn test_ctrl_comma_resolves_to_settings() {
        let chord = vk_to_chord(0xBC, true, false, false); // Ctrl+Comma
        assert_eq!(chord, "ctrl+comma");
        let kb = KeybindingsConfig::default();
        let found = kb.bindings.iter().find(|b| b.chord == chord);
        assert!(found.is_some(), "ctrl+comma must have a binding");
        assert_eq!(found.unwrap().action, "settings");
    }

    #[test]
    fn test_ctrl_shift_t_resolves_to_new_tab() {
        let chord = vk_to_chord(0x54, true, true, false); // Ctrl+Shift+T
        assert_eq!(chord, "ctrl+shift+t");
        let kb = KeybindingsConfig::default();
        let found = kb.bindings.iter().find(|b| b.chord == chord);
        assert!(found.is_some(), "ctrl+shift+t must have a binding");
        assert_eq!(found.unwrap().action, "new_tab");
    }

    #[test]
    fn test_ctrl_shift_p_resolves_to_command_palette() {
        let chord = vk_to_chord(0x50, true, true, false); // Ctrl+Shift+P
        assert_eq!(chord, "ctrl+shift+p");
        let kb = KeybindingsConfig::default();
        let found = kb.bindings.iter().find(|b| b.chord == chord);
        assert!(found.is_some());
        assert_eq!(found.unwrap().action, "command_palette");
    }

    #[test]
    fn test_f1_resolves_to_open_help() {
        let chord = vk_to_chord(0x70, false, false, false); // F1
        assert_eq!(chord, "f1");
        let kb = KeybindingsConfig::default();
        let found = kb.bindings.iter().find(|b| b.chord == chord);
        assert!(found.is_some(), "f1 must have a binding");
        assert_eq!(found.unwrap().action, "open_help");
    }

    #[test]
    fn test_f11_resolves_to_toggle_fullscreen() {
        let chord = vk_to_chord(0x7A, false, false, false); // F11
        assert_eq!(chord, "f11");
        let kb = KeybindingsConfig::default();
        let found = kb.bindings.iter().find(|b| b.chord == chord);
        assert!(found.is_some());
        assert_eq!(found.unwrap().action, "toggle_fullscreen");
    }

    #[test]
    fn test_alt_arrow_resolves_to_pane_navigation() {
        let left = vk_to_chord(0x25, false, false, true); // Alt+Left
        assert_eq!(left, "alt+left");
        let kb = KeybindingsConfig::default();
        let found = kb.bindings.iter().find(|b| b.chord == left);
        assert!(found.is_some(), "alt+left must have a binding");
        assert_eq!(found.unwrap().action, "focus_pane_left");
    }

    #[test]
    fn test_every_default_keybinding_has_valid_chord() {
        let kb = KeybindingsConfig::default();
        for binding in &kb.bindings {
            assert!(
                !binding.chord.is_empty(),
                "Binding for {:?} has empty chord",
                binding.action
            );
            assert!(
                !binding.action.is_empty(),
                "Binding with chord {:?} has empty action",
                binding.chord
            );
            // Verify chord is normalized
            let normalized = normalize_chord(&binding.chord);
            assert_eq!(
                binding.chord, normalized,
                "Chord {:?} not normalized (got {:?})",
                binding.chord, normalized
            );
        }
    }
}
