//! Settings UI — multi-tabbed configuration overlay (Ctrl+Comma).
//!
//! Provides an accessible, keyboard-driven settings interface with tabs:
//! Font, Window, Terminal, Colors, Profiles, Keybindings, and Accessibility.
//! All changes are collected into a draft config that can be saved to disk
//! or discarded.

use wixen_config::{
    BackgroundStyle, BellStyle, Config, Keybinding, Profile, PromptDetection,
    ScreenReaderVerbosity, normalize_chord, parse_chord,
};

/// Which settings tab is active.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SettingsTab {
    Font,
    Window,
    Terminal,
    Colors,
    Profiles,
    Keybindings,
    Accessibility,
}

impl SettingsTab {
    /// All tabs in display order.
    pub const ALL: &'static [SettingsTab] = &[
        SettingsTab::Font,
        SettingsTab::Window,
        SettingsTab::Terminal,
        SettingsTab::Colors,
        SettingsTab::Profiles,
        SettingsTab::Keybindings,
        SettingsTab::Accessibility,
    ];

    /// Human-readable label.
    pub fn label(self) -> &'static str {
        match self {
            Self::Font => "Font",
            Self::Window => "Window",
            Self::Terminal => "Terminal",
            Self::Colors => "Colors",
            Self::Profiles => "Profiles",
            Self::Keybindings => "Keybindings",
            Self::Accessibility => "Accessibility",
        }
    }

    /// Next tab (wraps).
    pub fn next(self) -> Self {
        let all = Self::ALL;
        let idx = all.iter().position(|&t| t == self).unwrap_or(0);
        all[(idx + 1) % all.len()]
    }

    /// Previous tab (wraps).
    pub fn prev(self) -> Self {
        let all = Self::ALL;
        let idx = all.iter().position(|&t| t == self).unwrap_or(0);
        if idx == 0 {
            all[all.len() - 1]
        } else {
            all[idx - 1]
        }
    }
}

/// A single editable field in the settings panel.
#[derive(Debug, Clone)]
pub enum SettingsField {
    /// Single-line text input (font family, window title, colors, etc.)
    Text {
        label: &'static str,
        value: String,
        placeholder: &'static str,
    },
    /// Numeric input with bounds and step.
    Number {
        label: &'static str,
        value: f32,
        min: f32,
        max: f32,
        step: f32,
    },
    /// Boolean toggle.
    Toggle { label: &'static str, value: bool },
    /// Dropdown with string options.
    Dropdown {
        label: &'static str,
        options: Vec<String>,
        selected: usize,
    },
    /// Keybinding editor: modifier checkboxes + key text input.
    Keybinding {
        label: &'static str,
        action: String,
        ctrl: bool,
        shift: bool,
        alt: bool,
        win: bool,
        key: String,
        /// Whether this binding is in edit mode.
        editing: bool,
        /// Sub-field focus: 0=row-level, 1=Ctrl, 2=Shift, 3=Alt, 4=Win, 5=Key.
        sub_focus: u8,
    },
}

impl SettingsField {
    /// The display label for this field.
    pub fn label(&self) -> &'static str {
        match self {
            Self::Text { label, .. }
            | Self::Number { label, .. }
            | Self::Toggle { label, .. }
            | Self::Dropdown { label, .. }
            | Self::Keybinding { label, .. } => label,
        }
    }

    /// Human-readable value for display.
    pub fn display_value(&self) -> String {
        match self {
            Self::Text { value, .. } => value.clone(),
            Self::Number { value, .. } => format!("{value:.1}"),
            Self::Toggle { value, .. } => {
                if *value {
                    "ON".to_string()
                } else {
                    "OFF".to_string()
                }
            }
            Self::Dropdown {
                options, selected, ..
            } => options
                .get(*selected)
                .cloned()
                .unwrap_or_else(|| "—".to_string()),
            Self::Keybinding {
                ctrl,
                shift,
                alt,
                win,
                key,
                ..
            } => {
                let mut parts = Vec::new();
                if *ctrl {
                    parts.push("Ctrl");
                }
                if *shift {
                    parts.push("Shift");
                }
                if *alt {
                    parts.push("Alt");
                }
                if *win {
                    parts.push("Win");
                }
                if !key.is_empty() {
                    // Capitalize first letter for display
                    let display_key: String = key
                        .chars()
                        .enumerate()
                        .map(|(i, c)| if i == 0 { c.to_ascii_uppercase() } else { c })
                        .collect();
                    return if parts.is_empty() {
                        display_key
                    } else {
                        format!("{}+{}", parts.join("+"), display_key)
                    };
                }
                if parts.is_empty() {
                    "(unbound)".to_string()
                } else {
                    parts.join("+")
                }
            }
        }
    }
}

/// The full settings UI state.
pub struct SettingsUI {
    /// Whether the settings overlay is visible.
    pub visible: bool,
    /// Active tab.
    pub active_tab: SettingsTab,
    /// Fields for each tab, stored in tab order matching `SettingsTab::ALL`.
    tab_fields: Vec<Vec<SettingsField>>,
    /// Currently focused field index within the active tab.
    pub focused_field: usize,
    /// Whether the focused field is in edit mode (text input active).
    pub editing: bool,
    /// Draft config being modified.
    pub draft: Config,
    /// Whether any changes have been made.
    pub dirty: bool,
}

impl SettingsUI {
    /// Build the settings UI from the current configuration.
    pub fn from_config(config: &Config) -> Self {
        let draft = config.clone();
        let tab_fields = Self::build_all_fields(&draft);
        Self {
            visible: false,
            active_tab: SettingsTab::Font,
            tab_fields,
            focused_field: 0,
            editing: false,
            draft,
            dirty: false,
        }
    }

    /// Toggle settings visibility.
    pub fn toggle(&mut self) {
        self.visible = !self.visible;
        if self.visible {
            self.focused_field = 0;
            self.editing = false;
        }
    }

    /// Open the settings panel.
    pub fn open(&mut self) {
        self.visible = true;
        self.focused_field = 0;
        self.editing = false;
    }

    /// Close the settings panel (discards unsaved changes from draft).
    pub fn close(&mut self) {
        self.visible = false;
        self.editing = false;
    }

    /// Switch to the next tab.
    pub fn next_tab(&mut self) {
        self.active_tab = self.active_tab.next();
        self.focused_field = 0;
        self.editing = false;
    }

    /// Switch to the previous tab.
    pub fn prev_tab(&mut self) {
        self.active_tab = self.active_tab.prev();
        self.focused_field = 0;
        self.editing = false;
    }

    /// Move focus to the next field within the active tab.
    pub fn next_field(&mut self) {
        let count = self.active_fields().len();
        if count > 0 {
            self.focused_field = (self.focused_field + 1) % count;
        }
        self.editing = false;
    }

    /// Move focus to the previous field within the active tab.
    pub fn prev_field(&mut self) {
        let count = self.active_fields().len();
        if count > 0 {
            self.focused_field = if self.focused_field == 0 {
                count - 1
            } else {
                self.focused_field - 1
            };
        }
        self.editing = false;
    }

    /// Enter edit mode on the focused field (for text/number inputs).
    pub fn start_edit(&mut self) {
        let Some(field) = self.active_fields().get(self.focused_field) else {
            return;
        };
        match field {
            SettingsField::Text { .. } | SettingsField::Number { .. } => {
                self.editing = true;
            }
            SettingsField::Toggle { .. } => {
                self.toggle_focused();
            }
            SettingsField::Dropdown { .. } => {
                self.cycle_dropdown_forward();
            }
            SettingsField::Keybinding { editing, .. } => {
                if *editing {
                    // Already in edit mode — confirm and exit
                    self.confirm_keybinding_edit();
                } else {
                    // Enter keybinding edit mode with sub_focus on Ctrl
                    let tab_idx = self.tab_index();
                    let field_idx = self.focused_field;
                    if let Some(SettingsField::Keybinding {
                        editing, sub_focus, ..
                    }) = self
                        .tab_fields
                        .get_mut(tab_idx)
                        .and_then(|f| f.get_mut(field_idx))
                    {
                        *editing = true;
                        *sub_focus = 1;
                    }
                    self.editing = true;
                }
            }
        }
    }

    /// Cancel edit mode without applying text changes.
    pub fn cancel_edit(&mut self) {
        self.editing = false;
        // Also clear keybinding edit state
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Keybinding {
            editing, sub_focus, ..
        }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
        {
            *editing = false;
            *sub_focus = 0;
        }
    }

    /// Append a character to the currently editing text field.
    pub fn push_char(&mut self, ch: char) {
        if !self.editing {
            return;
        }
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(field) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
        {
            match field {
                SettingsField::Text { value, .. } => {
                    value.push(ch);
                    self.dirty = true;
                }
                SettingsField::Number {
                    value, min, max, ..
                } => {
                    // Allow typing digits and decimal point
                    let mut s = format!("{value:.1}");
                    s.push(ch);
                    if let Ok(n) = s.parse::<f32>() {
                        *value = n.clamp(*min, *max);
                        self.dirty = true;
                    }
                }
                SettingsField::Keybinding {
                    editing,
                    sub_focus,
                    key,
                    ..
                } => {
                    if *editing && *sub_focus == 5 {
                        // Replace the key text with the typed character
                        *key = ch.to_lowercase().to_string();
                        self.dirty = true;
                    }
                }
                _ => {}
            }
        }
    }

    /// Remove the last character from the currently editing text field.
    pub fn pop_char(&mut self) {
        if !self.editing {
            return;
        }
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(field) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
        {
            match field {
                SettingsField::Text { value, .. } => {
                    value.pop();
                    self.dirty = true;
                }
                SettingsField::Keybinding {
                    editing,
                    sub_focus,
                    key,
                    ..
                } => {
                    if *editing && *sub_focus == 5 {
                        key.clear();
                        self.dirty = true;
                    }
                }
                _ => {}
            }
        }
    }

    /// Confirm the current text edit and apply it to the draft config.
    pub fn confirm_edit(&mut self) {
        if !self.editing {
            return;
        }
        self.confirm_keybinding_edit();
        self.editing = false;
        self.apply_fields_to_draft();
    }

    /// Move to the next sub-field within a keybinding editor.
    pub fn keybinding_next_sub(&mut self) {
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Keybinding {
            editing, sub_focus, ..
        }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
            && *editing
        {
            *sub_focus = if *sub_focus >= 5 { 1 } else { *sub_focus + 1 };
        }
    }

    /// Move to the previous sub-field within a keybinding editor.
    pub fn keybinding_prev_sub(&mut self) {
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Keybinding {
            editing, sub_focus, ..
        }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
            && *editing
        {
            *sub_focus = if *sub_focus <= 1 { 5 } else { *sub_focus - 1 };
        }
    }

    /// Toggle the currently focused modifier checkbox in a keybinding editor.
    pub fn keybinding_toggle_modifier(&mut self) {
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Keybinding {
            editing,
            sub_focus,
            ctrl,
            shift,
            alt,
            win,
            ..
        }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
            && *editing
        {
            match *sub_focus {
                1 => *ctrl = !*ctrl,
                2 => *shift = !*shift,
                3 => *alt = !*alt,
                4 => *win = !*win,
                _ => {}
            }
            self.dirty = true;
        }
    }

    /// Get the sub-focus index of the currently focused keybinding field.
    /// Returns 0 if the focused field is not a keybinding or not in edit mode.
    pub fn keybinding_sub_focus(&self) -> u8 {
        if let Some(SettingsField::Keybinding {
            editing, sub_focus, ..
        }) = self.active_fields().get(self.focused_field)
            && *editing
        {
            return *sub_focus;
        }
        0
    }

    /// Whether the focused keybinding field is in edit mode.
    pub fn is_keybinding_editing(&self) -> bool {
        if let Some(SettingsField::Keybinding { editing, .. }) =
            self.active_fields().get(self.focused_field)
        {
            return *editing;
        }
        false
    }

    /// Get the fields for the currently active tab (immutable).
    pub fn active_fields(&self) -> &[SettingsField] {
        let idx = self.tab_index();
        self.tab_fields
            .get(idx)
            .map(|v| v.as_slice())
            .unwrap_or(&[])
    }

    /// Get the focused field.
    pub fn focused(&self) -> Option<&SettingsField> {
        self.active_fields().get(self.focused_field)
    }

    /// Number of fields in the active tab.
    pub fn field_count(&self) -> usize {
        self.active_fields().len()
    }

    /// Apply the current settings to produce a final Config.
    /// Call this when the user hits Save.
    pub fn save(&mut self) -> Config {
        self.apply_fields_to_draft();
        self.dirty = false;
        self.draft.clone()
    }

    /// Reset the settings UI from a fresh config (e.g., after cancel).
    pub fn reset(&mut self, config: &Config) {
        self.draft = config.clone();
        self.tab_fields = Self::build_all_fields(&self.draft);
        self.dirty = false;
        self.focused_field = 0;
        self.editing = false;
    }

    /// Get all tab labels with their active state.
    pub fn tab_bar(&self) -> Vec<(&'static str, bool)> {
        SettingsTab::ALL
            .iter()
            .map(|t| (t.label(), *t == self.active_tab))
            .collect()
    }

    // --- Private helpers ---

    /// Exit keybinding edit mode on the focused field (if applicable).
    fn confirm_keybinding_edit(&mut self) {
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Keybinding {
            editing, sub_focus, ..
        }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
        {
            *editing = false;
            *sub_focus = 0;
        }
    }

    fn tab_index(&self) -> usize {
        SettingsTab::ALL
            .iter()
            .position(|&t| t == self.active_tab)
            .unwrap_or(0)
    }

    fn toggle_focused(&mut self) {
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Toggle { value, .. }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
        {
            *value = !*value;
            self.dirty = true;
            self.apply_fields_to_draft();
        }
    }

    fn cycle_dropdown_forward(&mut self) {
        let tab_idx = self.tab_index();
        let field_idx = self.focused_field;
        if let Some(SettingsField::Dropdown {
            options, selected, ..
        }) = self
            .tab_fields
            .get_mut(tab_idx)
            .and_then(|f| f.get_mut(field_idx))
        {
            if options.is_empty() {
                return;
            }
            *selected = (*selected + 1) % options.len();
            self.dirty = true;
            self.apply_fields_to_draft();
        }
    }

    /// Build field lists for all tabs from a Config.
    fn build_all_fields(config: &Config) -> Vec<Vec<SettingsField>> {
        vec![
            Self::build_font_fields(config),
            Self::build_window_fields(config),
            Self::build_terminal_fields(config),
            Self::build_colors_fields(config),
            Self::build_profiles_fields(config),
            Self::build_keybindings_fields(config),
            Self::build_accessibility_fields(config),
        ]
    }

    fn build_font_fields(config: &Config) -> Vec<SettingsField> {
        vec![
            SettingsField::Text {
                label: "Font Family",
                value: config.font.family.clone(),
                placeholder: "Cascadia Code",
            },
            SettingsField::Number {
                label: "Font Size",
                value: config.font.size,
                min: 6.0,
                max: 72.0,
                step: 1.0,
            },
            SettingsField::Number {
                label: "Line Height",
                value: config.font.line_height,
                min: 0.5,
                max: 3.0,
                step: 0.1,
            },
            SettingsField::Text {
                label: "Fallback Fonts",
                value: config.font.fallback_fonts.join(", "),
                placeholder: "Segoe UI Emoji, Segoe UI Symbol",
            },
        ]
    }

    fn build_window_fields(config: &Config) -> Vec<SettingsField> {
        vec![
            SettingsField::Text {
                label: "Window Title",
                value: config.window.title.clone(),
                placeholder: "Wixen Terminal",
            },
            SettingsField::Number {
                label: "Width",
                value: config.window.width as f32,
                min: 320.0,
                max: 7680.0,
                step: 10.0,
            },
            SettingsField::Number {
                label: "Height",
                value: config.window.height as f32,
                min: 240.0,
                max: 4320.0,
                step: 10.0,
            },
            SettingsField::Dropdown {
                label: "Renderer",
                options: vec![
                    "auto".to_string(),
                    "gpu".to_string(),
                    "software".to_string(),
                ],
                selected: match config.window.renderer.as_str() {
                    "gpu" => 1,
                    "software" => 2,
                    _ => 0,
                },
            },
            SettingsField::Number {
                label: "Opacity",
                value: config.window.opacity,
                min: 0.0,
                max: 1.0,
                step: 0.05,
            },
            SettingsField::Dropdown {
                label: "Background Style",
                options: vec![
                    "opaque".to_string(),
                    "acrylic".to_string(),
                    "mica".to_string(),
                ],
                selected: match config.window.background {
                    BackgroundStyle::Acrylic => 1,
                    BackgroundStyle::Mica => 2,
                    BackgroundStyle::Opaque => 0,
                },
            },
            SettingsField::Toggle {
                label: "Dark Title Bar",
                value: config.window.dark_title_bar,
            },
            SettingsField::Dropdown {
                label: "Scrollbar",
                options: vec![
                    "auto".to_string(),
                    "always".to_string(),
                    "never".to_string(),
                ],
                selected: match config.window.scrollbar {
                    wixen_config::ScrollbarMode::Always => 1,
                    wixen_config::ScrollbarMode::Never => 2,
                    wixen_config::ScrollbarMode::Auto => 0,
                },
            },
            SettingsField::Dropdown {
                label: "Tab Bar",
                options: vec![
                    "auto-hide".to_string(),
                    "always".to_string(),
                    "never".to_string(),
                ],
                selected: match config.window.tab_bar {
                    wixen_config::TabBarMode::Always => 1,
                    wixen_config::TabBarMode::Never => 2,
                    wixen_config::TabBarMode::AutoHide => 0,
                },
            },
        ]
    }

    fn build_terminal_fields(config: &Config) -> Vec<SettingsField> {
        vec![
            SettingsField::Number {
                label: "Scrollback Lines",
                value: config.terminal.scrollback_lines as f32,
                min: 0.0,
                max: 1_000_000.0,
                step: 1000.0,
            },
            SettingsField::Dropdown {
                label: "Cursor Style",
                options: vec![
                    "block".to_string(),
                    "underline".to_string(),
                    "bar".to_string(),
                ],
                selected: match config.terminal.cursor_style.as_str() {
                    "underline" => 1,
                    "bar" => 2,
                    _ => 0,
                },
            },
            SettingsField::Toggle {
                label: "Cursor Blink",
                value: config.terminal.cursor_blink,
            },
            SettingsField::Number {
                label: "Blink Interval (ms)",
                value: config.terminal.cursor_blink_ms as f32,
                min: 100.0,
                max: 2000.0,
                step: 50.0,
            },
            SettingsField::Dropdown {
                label: "Bell Style",
                options: vec![
                    "audible".to_string(),
                    "visual".to_string(),
                    "both".to_string(),
                    "none".to_string(),
                ],
                selected: match config.terminal.bell {
                    BellStyle::Audible => 0,
                    BellStyle::Visual => 1,
                    BellStyle::Both => 2,
                    BellStyle::None => 3,
                },
            },
        ]
    }

    fn build_colors_fields(config: &Config) -> Vec<SettingsField> {
        let mut fields = vec![
            SettingsField::Text {
                label: "Theme",
                value: config
                    .colors
                    .theme
                    .clone()
                    .unwrap_or_else(|| "(none)".to_string()),
                placeholder: "dracula, catppuccin-mocha, ...",
            },
            SettingsField::Text {
                label: "Foreground",
                value: config.colors.foreground.clone(),
                placeholder: "#d9d9d9",
            },
            SettingsField::Text {
                label: "Background",
                value: config.colors.background.clone(),
                placeholder: "#0d0d14",
            },
            SettingsField::Text {
                label: "Cursor Color",
                value: config.colors.cursor.clone(),
                placeholder: "#cccccc",
            },
            SettingsField::Text {
                label: "Selection Background",
                value: config.colors.selection_bg.clone(),
                placeholder: "#264f78",
            },
        ];

        // Palette entries 0-15
        for (i, &palette_label) in PALETTE_LABELS.iter().enumerate() {
            let value = config.colors.palette.get(i).cloned().unwrap_or_default();
            fields.push(SettingsField::Text {
                label: palette_label,
                value,
                placeholder: "",
            });
        }

        fields
    }

    fn build_profiles_fields(config: &Config) -> Vec<SettingsField> {
        let profiles = config.resolved_profiles();
        let mut fields = Vec::new();
        for (i, profile) in profiles.iter().enumerate() {
            fields.push(SettingsField::Text {
                label: PROFILE_NAME_LABELS[i.min(PROFILE_NAME_LABELS.len() - 1)],
                value: profile.name.clone(),
                placeholder: "Profile Name",
            });
            fields.push(SettingsField::Text {
                label: PROFILE_PROGRAM_LABELS[i.min(PROFILE_PROGRAM_LABELS.len() - 1)],
                value: profile.program.clone(),
                placeholder: "pwsh.exe",
            });
            fields.push(SettingsField::Text {
                label: PROFILE_ARGS_LABELS[i.min(PROFILE_ARGS_LABELS.len() - 1)],
                value: profile.args.join(" "),
                placeholder: "",
            });
            fields.push(SettingsField::Toggle {
                label: PROFILE_DEFAULT_LABELS[i.min(PROFILE_DEFAULT_LABELS.len() - 1)],
                value: profile.is_default,
            });
        }
        fields
    }

    fn build_keybindings_fields(config: &Config) -> Vec<SettingsField> {
        config
            .keybindings
            .bindings
            .iter()
            .map(|binding| {
                let (ctrl, shift, alt, win, key) = parse_chord(&binding.chord);
                let label = action_label(&binding.action);
                SettingsField::Keybinding {
                    label,
                    action: binding.action.clone(),
                    ctrl,
                    shift,
                    alt,
                    win,
                    key,
                    editing: false,
                    sub_focus: 0,
                }
            })
            .collect()
    }

    fn build_accessibility_fields(config: &Config) -> Vec<SettingsField> {
        let a = &config.accessibility;
        vec![
            SettingsField::Dropdown {
                label: "Screen Reader Output",
                options: vec![
                    "auto".to_string(),
                    "all".to_string(),
                    "commands-only".to_string(),
                    "errors-only".to_string(),
                    "silent".to_string(),
                ],
                selected: match a.screen_reader_output {
                    ScreenReaderVerbosity::Auto => 0,
                    ScreenReaderVerbosity::All => 1,
                    ScreenReaderVerbosity::CommandsOnly => 2,
                    ScreenReaderVerbosity::ErrorsOnly => 3,
                    ScreenReaderVerbosity::Silent => 4,
                },
            },
            SettingsField::Toggle {
                label: "Announce Command Complete",
                value: a.announce_command_complete,
            },
            SettingsField::Toggle {
                label: "Announce Exit Codes",
                value: a.announce_exit_codes,
            },
            SettingsField::Dropdown {
                label: "Live Region Politeness",
                options: vec!["polite".to_string(), "assertive".to_string()],
                selected: if a.live_region_politeness == "assertive" {
                    1
                } else {
                    0
                },
            },
            SettingsField::Dropdown {
                label: "Prompt Detection",
                options: vec![
                    "auto".to_string(),
                    "shell-integration".to_string(),
                    "heuristic".to_string(),
                    "disabled".to_string(),
                ],
                selected: match a.prompt_detection {
                    PromptDetection::Auto => 0,
                    PromptDetection::ShellIntegration => 1,
                    PromptDetection::Heuristic => 2,
                    PromptDetection::Disabled => 3,
                },
            },
            SettingsField::Dropdown {
                label: "High Contrast",
                options: vec!["auto".to_string(), "on".to_string(), "off".to_string()],
                selected: match a.high_contrast.as_str() {
                    "on" => 1,
                    "off" => 2,
                    _ => 0,
                },
            },
            SettingsField::Number {
                label: "Min Contrast Ratio",
                value: a.min_contrast_ratio,
                min: 0.0,
                max: 21.0,
                step: 0.5,
            },
            SettingsField::Dropdown {
                label: "Reduced Motion",
                options: vec!["auto".to_string(), "on".to_string(), "off".to_string()],
                selected: match a.reduced_motion.as_str() {
                    "on" => 1,
                    "off" => 2,
                    _ => 0,
                },
            },
            SettingsField::Toggle {
                label: "Announce Images",
                value: a.announce_images,
            },
            SettingsField::Toggle {
                label: "Announce Pane Position",
                value: a.announce_pane_position,
            },
            SettingsField::Toggle {
                label: "Announce Tab Details",
                value: a.announce_tab_details,
            },
            SettingsField::Toggle {
                label: "Audio Feedback",
                value: a.audio_feedback,
            },
            SettingsField::Toggle {
                label: "Audio Progress",
                value: a.audio_progress,
            },
            SettingsField::Toggle {
                label: "Audio Errors",
                value: a.audio_errors,
            },
            SettingsField::Toggle {
                label: "Audio Command Complete",
                value: a.audio_command_complete,
            },
        ]
    }

    /// Write current field values back into the draft Config.
    fn apply_fields_to_draft(&mut self) {
        self.apply_font_fields();
        self.apply_window_fields();
        self.apply_terminal_fields();
        self.apply_colors_fields();
        self.apply_profiles_fields();
        self.apply_keybindings_fields();
        self.apply_accessibility_fields();
    }

    fn apply_font_fields(&mut self) {
        let Some(fields) = self.tab_fields.first() else {
            return;
        };
        if let Some(SettingsField::Text { value, .. }) = fields.first() {
            self.draft.font.family = value.clone();
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(1) {
            self.draft.font.size = *value;
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(2) {
            self.draft.font.line_height = *value;
        }
        if let Some(SettingsField::Text { value, .. }) = fields.get(3) {
            self.draft.font.fallback_fonts = value
                .split(',')
                .map(|s| s.trim().to_string())
                .filter(|s| !s.is_empty())
                .collect();
        }
    }

    fn apply_window_fields(&mut self) {
        let Some(fields) = self.tab_fields.get(1) else {
            return;
        };
        if let Some(SettingsField::Text { value, .. }) = fields.first() {
            self.draft.window.title = value.clone();
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(1) {
            self.draft.window.width = *value as u32;
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(2) {
            self.draft.window.height = *value as u32;
        }
        if let Some(SettingsField::Dropdown {
            options, selected, ..
        }) = fields.get(3)
        {
            self.draft.window.renderer = options.get(*selected).cloned().unwrap_or_default();
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(4) {
            self.draft.window.opacity = *value;
        }
        if let Some(SettingsField::Dropdown { selected, .. }) = fields.get(5) {
            self.draft.window.background = match selected {
                1 => BackgroundStyle::Acrylic,
                2 => BackgroundStyle::Mica,
                _ => BackgroundStyle::Opaque,
            };
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(6) {
            self.draft.window.dark_title_bar = *value;
        }
        if let Some(SettingsField::Dropdown { selected, .. }) = fields.get(7) {
            self.draft.window.scrollbar = match selected {
                1 => wixen_config::ScrollbarMode::Always,
                2 => wixen_config::ScrollbarMode::Never,
                _ => wixen_config::ScrollbarMode::Auto,
            };
        }
        if let Some(SettingsField::Dropdown { selected, .. }) = fields.get(8) {
            self.draft.window.tab_bar = match selected {
                1 => wixen_config::TabBarMode::Always,
                2 => wixen_config::TabBarMode::Never,
                _ => wixen_config::TabBarMode::AutoHide,
            };
        }
    }

    fn apply_terminal_fields(&mut self) {
        let Some(fields) = self.tab_fields.get(2) else {
            return;
        };
        if let Some(SettingsField::Number { value, .. }) = fields.first() {
            self.draft.terminal.scrollback_lines = *value as usize;
        }
        if let Some(SettingsField::Dropdown {
            options, selected, ..
        }) = fields.get(1)
        {
            self.draft.terminal.cursor_style = options.get(*selected).cloned().unwrap_or_default();
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(2) {
            self.draft.terminal.cursor_blink = *value;
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(3) {
            self.draft.terminal.cursor_blink_ms = *value as u64;
        }
        if let Some(SettingsField::Dropdown { selected, .. }) = fields.get(4) {
            self.draft.terminal.bell = match selected {
                0 => BellStyle::Audible,
                1 => BellStyle::Visual,
                2 => BellStyle::Both,
                _ => BellStyle::None,
            };
        }
    }

    fn apply_colors_fields(&mut self) {
        let Some(fields) = self.tab_fields.get(3) else {
            return;
        };
        if let Some(SettingsField::Text { value, .. }) = fields.first() {
            self.draft.colors.theme = if value == "(none)" || value.is_empty() {
                None
            } else {
                Some(value.clone())
            };
        }
        if let Some(SettingsField::Text { value, .. }) = fields.get(1) {
            self.draft.colors.foreground = value.clone();
        }
        if let Some(SettingsField::Text { value, .. }) = fields.get(2) {
            self.draft.colors.background = value.clone();
        }
        if let Some(SettingsField::Text { value, .. }) = fields.get(3) {
            self.draft.colors.cursor = value.clone();
        }
        if let Some(SettingsField::Text { value, .. }) = fields.get(4) {
            self.draft.colors.selection_bg = value.clone();
        }
        // Palette entries (fields 5..21)
        self.draft.colors.palette.clear();
        for i in 0..16 {
            if let Some(SettingsField::Text { value, .. }) = fields.get(5 + i)
                && !value.is_empty()
            {
                while self.draft.colors.palette.len() <= i {
                    self.draft.colors.palette.push(String::new());
                }
                self.draft.colors.palette[i] = value.clone();
            }
        }
        // Trim trailing empty entries
        while self
            .draft
            .colors
            .palette
            .last()
            .is_some_and(|s| s.is_empty())
        {
            self.draft.colors.palette.pop();
        }
    }

    fn apply_profiles_fields(&mut self) {
        let Some(fields) = self.tab_fields.get(4) else {
            return;
        };
        let mut profiles = Vec::new();
        for chunk in fields.chunks(4) {
            let name = if let Some(SettingsField::Text { value, .. }) = chunk.first() {
                value.clone()
            } else {
                "Default".to_string()
            };
            let program = if let Some(SettingsField::Text { value, .. }) = chunk.get(1) {
                value.clone()
            } else {
                String::new()
            };
            let args_str = if let Some(SettingsField::Text { value, .. }) = chunk.get(2) {
                value.clone()
            } else {
                String::new()
            };
            let is_default = if let Some(SettingsField::Toggle { value, .. }) = chunk.get(3) {
                *value
            } else {
                false
            };
            profiles.push(Profile {
                name,
                program,
                args: args_str.split_whitespace().map(|s| s.to_string()).collect(),
                working_directory: String::new(),
                is_default,
            });
        }
        self.draft.profiles = profiles;
    }

    fn apply_keybindings_fields(&mut self) {
        let Some(fields) = self.tab_fields.get(5) else {
            return;
        };
        let mut bindings = Vec::new();
        for field in fields {
            if let SettingsField::Keybinding {
                action,
                ctrl,
                shift,
                alt,
                win,
                key,
                ..
            } = field
            {
                let mut parts = Vec::new();
                if *ctrl {
                    parts.push("ctrl");
                }
                if *shift {
                    parts.push("shift");
                }
                if *alt {
                    parts.push("alt");
                }
                if *win {
                    parts.push("win");
                }
                let chord = if key.is_empty() {
                    parts.join("+")
                } else if parts.is_empty() {
                    key.clone()
                } else {
                    format!("{}+{}", parts.join("+"), key)
                };
                let chord = normalize_chord(&chord);
                bindings.push(Keybinding::simple(&chord, action));
            }
        }
        self.draft.keybindings.bindings = bindings;
    }

    fn apply_accessibility_fields(&mut self) {
        let Some(fields) = self.tab_fields.get(6) else {
            return;
        };
        if let Some(SettingsField::Dropdown { selected, .. }) = fields.first() {
            self.draft.accessibility.screen_reader_output = match selected {
                1 => ScreenReaderVerbosity::All,
                2 => ScreenReaderVerbosity::CommandsOnly,
                3 => ScreenReaderVerbosity::ErrorsOnly,
                4 => ScreenReaderVerbosity::Silent,
                _ => ScreenReaderVerbosity::Auto,
            };
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(1) {
            self.draft.accessibility.announce_command_complete = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(2) {
            self.draft.accessibility.announce_exit_codes = *value;
        }
        if let Some(SettingsField::Dropdown {
            options, selected, ..
        }) = fields.get(3)
        {
            self.draft.accessibility.live_region_politeness =
                options.get(*selected).cloned().unwrap_or_default();
        }
        if let Some(SettingsField::Dropdown { selected, .. }) = fields.get(4) {
            self.draft.accessibility.prompt_detection = match selected {
                1 => PromptDetection::ShellIntegration,
                2 => PromptDetection::Heuristic,
                3 => PromptDetection::Disabled,
                _ => PromptDetection::Auto,
            };
        }
        if let Some(SettingsField::Dropdown {
            options, selected, ..
        }) = fields.get(5)
        {
            self.draft.accessibility.high_contrast =
                options.get(*selected).cloned().unwrap_or_default();
        }
        if let Some(SettingsField::Number { value, .. }) = fields.get(6) {
            self.draft.accessibility.min_contrast_ratio = *value;
        }
        if let Some(SettingsField::Dropdown {
            options, selected, ..
        }) = fields.get(7)
        {
            self.draft.accessibility.reduced_motion =
                options.get(*selected).cloned().unwrap_or_default();
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(8) {
            self.draft.accessibility.announce_images = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(9) {
            self.draft.accessibility.announce_pane_position = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(10) {
            self.draft.accessibility.announce_tab_details = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(11) {
            self.draft.accessibility.audio_feedback = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(12) {
            self.draft.accessibility.audio_progress = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(13) {
            self.draft.accessibility.audio_errors = *value;
        }
        if let Some(SettingsField::Toggle { value, .. }) = fields.get(14) {
            self.draft.accessibility.audio_command_complete = *value;
        }
    }
}

impl Default for SettingsUI {
    fn default() -> Self {
        Self::from_config(&Config::default())
    }
}

// --- Static label arrays for profile fields (up to 8 profiles) ---

/// Map an action identifier to a human-readable label.
fn action_label(action: &str) -> &'static str {
    match action {
        "new_tab" => "New Tab",
        "close_tab" => "Close Tab",
        "next_tab" => "Next Tab",
        "prev_tab" => "Previous Tab",
        "split_horizontal" => "Split Horizontal",
        "split_vertical" => "Split Vertical",
        "copy" => "Copy",
        "paste" => "Paste",
        "find" => "Find",
        "command_palette" => "Command Palette",
        "settings" => "Settings",
        "toggle_fullscreen" => "Toggle Fullscreen",
        _ => "Unknown Action",
    }
}

static PALETTE_LABELS: &[&str] = &[
    "Palette 0 (Black)",
    "Palette 1 (Red)",
    "Palette 2 (Green)",
    "Palette 3 (Yellow)",
    "Palette 4 (Blue)",
    "Palette 5 (Magenta)",
    "Palette 6 (Cyan)",
    "Palette 7 (White)",
    "Palette 8 (Bright Black)",
    "Palette 9 (Bright Red)",
    "Palette 10 (Bright Green)",
    "Palette 11 (Bright Yellow)",
    "Palette 12 (Bright Blue)",
    "Palette 13 (Bright Magenta)",
    "Palette 14 (Bright Cyan)",
    "Palette 15 (Bright White)",
];

static PROFILE_NAME_LABELS: &[&str] = &[
    "Profile 1 Name",
    "Profile 2 Name",
    "Profile 3 Name",
    "Profile 4 Name",
    "Profile 5 Name",
    "Profile 6 Name",
    "Profile 7 Name",
    "Profile 8 Name",
];

static PROFILE_PROGRAM_LABELS: &[&str] = &[
    "Profile 1 Program",
    "Profile 2 Program",
    "Profile 3 Program",
    "Profile 4 Program",
    "Profile 5 Program",
    "Profile 6 Program",
    "Profile 7 Program",
    "Profile 8 Program",
];

static PROFILE_ARGS_LABELS: &[&str] = &[
    "Profile 1 Args",
    "Profile 2 Args",
    "Profile 3 Args",
    "Profile 4 Args",
    "Profile 5 Args",
    "Profile 6 Args",
    "Profile 7 Args",
    "Profile 8 Args",
];

static PROFILE_DEFAULT_LABELS: &[&str] = &[
    "Profile 1 Default",
    "Profile 2 Default",
    "Profile 3 Default",
    "Profile 4 Default",
    "Profile 5 Default",
    "Profile 6 Default",
    "Profile 7 Default",
    "Profile 8 Default",
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_settings_from_default_config() {
        let config = Config::default();
        let ui = SettingsUI::from_config(&config);
        assert!(!ui.visible);
        assert_eq!(ui.active_tab, SettingsTab::Font);
        assert!(!ui.dirty);
        // Font tab should have 4 fields
        assert_eq!(ui.active_fields().len(), 4);
    }

    #[test]
    fn test_settings_tab_navigation() {
        let mut ui = SettingsUI::default();
        assert_eq!(ui.active_tab, SettingsTab::Font);

        ui.next_tab();
        assert_eq!(ui.active_tab, SettingsTab::Window);

        ui.next_tab();
        assert_eq!(ui.active_tab, SettingsTab::Terminal);

        ui.prev_tab();
        assert_eq!(ui.active_tab, SettingsTab::Window);

        // Wrap forward from Accessibility → Font
        ui.active_tab = SettingsTab::Accessibility;
        ui.next_tab();
        assert_eq!(ui.active_tab, SettingsTab::Font);

        // Wrap backward from Font → Accessibility
        ui.prev_tab();
        assert_eq!(ui.active_tab, SettingsTab::Accessibility);
    }

    #[test]
    fn test_settings_field_navigation() {
        let mut ui = SettingsUI::default();
        ui.open();
        assert_eq!(ui.focused_field, 0);

        ui.next_field();
        assert_eq!(ui.focused_field, 1);

        ui.next_field();
        assert_eq!(ui.focused_field, 2);

        ui.prev_field();
        assert_eq!(ui.focused_field, 1);

        // Wrap around: go back from 0 to last
        ui.focused_field = 0;
        ui.prev_field();
        assert_eq!(ui.focused_field, ui.field_count() - 1);
    }

    #[test]
    fn test_settings_text_edit() {
        let mut ui = SettingsUI::default();
        ui.open();
        // Focus Font Family (field 0)
        assert_eq!(ui.focused_field, 0);

        ui.start_edit();
        assert!(ui.editing);

        // Clear and type a new font name
        // Pop existing characters first
        let initial_len = match ui.active_fields().first() {
            Some(SettingsField::Text { value, .. }) => value.len(),
            _ => 0,
        };
        for _ in 0..initial_len {
            ui.pop_char();
        }
        for ch in "Consolas".chars() {
            ui.push_char(ch);
        }

        ui.confirm_edit();
        assert!(!ui.editing);
        assert!(ui.dirty);
        assert_eq!(ui.draft.font.family, "Consolas");
    }

    #[test]
    fn test_settings_toggle() {
        let mut ui = SettingsUI::default();
        ui.open();
        // Navigate to Window tab
        ui.next_tab();
        assert_eq!(ui.active_tab, SettingsTab::Window);

        // Dark Title Bar is the last field (index 6)
        ui.focused_field = 6;
        let was_on = match ui.active_fields().get(6) {
            Some(SettingsField::Toggle { value, .. }) => *value,
            _ => panic!("Expected toggle field"),
        };
        assert!(was_on); // default is true

        ui.start_edit(); // toggles immediately
        let is_on = match ui.active_fields().get(6) {
            Some(SettingsField::Toggle { value, .. }) => *value,
            _ => panic!("Expected toggle field"),
        };
        assert!(!is_on);
        assert!(ui.dirty);
        assert!(!ui.draft.window.dark_title_bar);
    }

    #[test]
    fn test_settings_dropdown_cycle() {
        let mut ui = SettingsUI::default();
        ui.open();
        // Navigate to Terminal tab
        ui.active_tab = SettingsTab::Terminal;

        // Cursor Style is field index 1 (dropdown: block, underline, bar)
        ui.focused_field = 1;
        let initial = match ui.active_fields().get(1) {
            Some(SettingsField::Dropdown { selected, .. }) => *selected,
            _ => panic!("Expected dropdown"),
        };
        assert_eq!(initial, 0); // "block"

        ui.start_edit(); // cycles forward
        let after = match ui.active_fields().get(1) {
            Some(SettingsField::Dropdown { selected, .. }) => *selected,
            _ => panic!("Expected dropdown"),
        };
        assert_eq!(after, 1); // "underline"
        assert_eq!(ui.draft.terminal.cursor_style, "underline");
    }

    #[test]
    fn test_settings_save_roundtrip() {
        let config = Config::default();
        let mut ui = SettingsUI::from_config(&config);
        ui.open();

        // Modify font size via Number field
        ui.active_tab = SettingsTab::Font;
        ui.focused_field = 1; // Font Size
        // Directly modify the field for test clarity
        let tab_idx = ui.tab_index();
        if let Some(SettingsField::Number { value, .. }) =
            ui.tab_fields.get_mut(tab_idx).and_then(|f| f.get_mut(1))
        {
            *value = 18.0;
        }
        ui.dirty = true;

        let saved = ui.save();
        assert_eq!(saved.font.size, 18.0);
        assert!(!ui.dirty);
        // Other fields should remain at defaults
        assert_eq!(saved.font.family, "Cascadia Code");
        assert_eq!(saved.window.title, "Wixen Terminal");
    }

    #[test]
    fn test_settings_reset() {
        let config = Config::default();
        let mut ui = SettingsUI::from_config(&config);
        ui.open();
        ui.dirty = true;

        let fresh = Config::default();
        ui.reset(&fresh);
        assert!(!ui.dirty);
        assert_eq!(ui.focused_field, 0);
    }

    #[test]
    fn test_settings_colors_tab_field_count() {
        let ui = SettingsUI::default();
        // Colors: theme + fg + bg + cursor + selection_bg + 16 palette = 21
        let colors_idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Colors)
            .unwrap();
        assert_eq!(ui.tab_fields[colors_idx].len(), 21);
    }

    #[test]
    fn test_settings_tab_bar() {
        let ui = SettingsUI::default();
        let bar = ui.tab_bar();
        assert_eq!(bar.len(), 7);
        assert_eq!(bar[0], ("Font", true));
        assert_eq!(bar[1], ("Window", false));
        assert_eq!(bar[5], ("Keybindings", false));
        assert_eq!(bar[6], ("Accessibility", false));
    }

    #[test]
    fn test_settings_profiles_from_config() {
        let toml = r#"
[[profiles]]
name = "PowerShell"
program = "pwsh.exe"
args = ["-NoLogo"]
is_default = true

[[profiles]]
name = "CMD"
program = "cmd.exe"
"#;
        let config: Config = toml::from_str(toml).unwrap();
        let ui = SettingsUI::from_config(&config);

        let profiles_idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Profiles)
            .unwrap();
        // 2 profiles × 4 fields = 8
        assert_eq!(ui.tab_fields[profiles_idx].len(), 8);
    }

    #[test]
    fn test_settings_display_values() {
        let field_text = SettingsField::Text {
            label: "Test",
            value: "hello".to_string(),
            placeholder: "",
        };
        assert_eq!(field_text.display_value(), "hello");

        let field_num = SettingsField::Number {
            label: "Size",
            value: 14.0,
            min: 1.0,
            max: 100.0,
            step: 1.0,
        };
        assert_eq!(field_num.display_value(), "14.0");

        let field_toggle = SettingsField::Toggle {
            label: "Blink",
            value: true,
        };
        assert_eq!(field_toggle.display_value(), "ON");

        let field_dropdown = SettingsField::Dropdown {
            label: "Style",
            options: vec!["block".to_string(), "bar".to_string()],
            selected: 1,
        };
        assert_eq!(field_dropdown.display_value(), "bar");
    }

    #[test]
    fn test_keybinding_field_from_config() {
        let config = Config::default();
        let ui = SettingsUI::from_config(&config);
        // Switch to Keybindings tab (index 5)
        let kb_idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Keybindings)
            .unwrap();
        let fields = &ui.tab_fields[kb_idx];
        assert!(fields.len() >= 40, "Expected 40+ keybinding fields");

        // First binding should be New Tab → Ctrl+Shift+T
        if let SettingsField::Keybinding {
            label,
            action,
            ctrl,
            shift,
            alt,
            key,
            ..
        } = &fields[0]
        {
            assert_eq!(*label, "New Tab");
            assert_eq!(action, "new_tab");
            assert!(*ctrl);
            assert!(*shift);
            assert!(!*alt);
            assert_eq!(key, "t");
        } else {
            panic!("Expected Keybinding field");
        }
    }

    #[test]
    fn test_keybinding_display_value() {
        let field = SettingsField::Keybinding {
            label: "Copy",
            action: "copy".to_string(),
            ctrl: true,
            shift: true,
            alt: false,
            win: false,
            key: "c".to_string(),
            editing: false,
            sub_focus: 0,
        };
        assert_eq!(field.display_value(), "Ctrl+Shift+C");

        let field_f11 = SettingsField::Keybinding {
            label: "Fullscreen",
            action: "toggle_fullscreen".to_string(),
            ctrl: false,
            shift: false,
            alt: false,
            win: false,
            key: "f11".to_string(),
            editing: false,
            sub_focus: 0,
        };
        assert_eq!(field_f11.display_value(), "F11");

        let field_unbound = SettingsField::Keybinding {
            label: "Test",
            action: "test".to_string(),
            ctrl: false,
            shift: false,
            alt: false,
            win: false,
            key: String::new(),
            editing: false,
            sub_focus: 0,
        };
        assert_eq!(field_unbound.display_value(), "(unbound)");
    }

    #[test]
    fn test_keybinding_toggle_modifier() {
        let mut ui = SettingsUI::default();
        ui.open();
        ui.active_tab = SettingsTab::Keybindings;
        ui.focused_field = 0;

        // Enter edit mode
        ui.start_edit();
        assert!(ui.is_keybinding_editing());
        assert_eq!(ui.keybinding_sub_focus(), 1); // on Ctrl

        // Ctrl should be true initially for new_tab (ctrl+shift+t)
        if let Some(SettingsField::Keybinding { ctrl, .. }) = ui.active_fields().first() {
            assert!(*ctrl);
        }

        // Toggle Ctrl off
        ui.keybinding_toggle_modifier();
        if let Some(SettingsField::Keybinding { ctrl, .. }) = ui.active_fields().first() {
            assert!(!*ctrl);
        }

        // Toggle Ctrl back on
        ui.keybinding_toggle_modifier();
        if let Some(SettingsField::Keybinding { ctrl, .. }) = ui.active_fields().first() {
            assert!(*ctrl);
        }
    }

    #[test]
    fn test_keybinding_sub_focus_navigation() {
        let mut ui = SettingsUI::default();
        ui.open();
        ui.active_tab = SettingsTab::Keybindings;
        ui.focused_field = 0;

        ui.start_edit();
        assert_eq!(ui.keybinding_sub_focus(), 1); // Ctrl

        ui.keybinding_next_sub();
        assert_eq!(ui.keybinding_sub_focus(), 2); // Shift

        ui.keybinding_next_sub();
        assert_eq!(ui.keybinding_sub_focus(), 3); // Alt

        ui.keybinding_next_sub();
        assert_eq!(ui.keybinding_sub_focus(), 4); // Win

        ui.keybinding_next_sub();
        assert_eq!(ui.keybinding_sub_focus(), 5); // Key

        // Wrap around
        ui.keybinding_next_sub();
        assert_eq!(ui.keybinding_sub_focus(), 1); // back to Ctrl

        // Go backward
        ui.keybinding_prev_sub();
        assert_eq!(ui.keybinding_sub_focus(), 5); // Key
    }

    #[test]
    fn test_keybinding_set_key() {
        let mut ui = SettingsUI::default();
        ui.open();
        ui.active_tab = SettingsTab::Keybindings;
        ui.focused_field = 0;

        // Enter edit mode and navigate to Key sub-field
        ui.start_edit();
        // Navigate to sub_focus 5 (Key)
        for _ in 0..4 {
            ui.keybinding_next_sub();
        }
        assert_eq!(ui.keybinding_sub_focus(), 5);

        // Set key to 'k'
        ui.push_char('k');
        if let Some(SettingsField::Keybinding { key, .. }) = ui.active_fields().first() {
            assert_eq!(key, "k");
        }
        assert!(ui.dirty);
    }

    #[test]
    fn test_keybinding_apply_to_draft() {
        let mut ui = SettingsUI::default();
        ui.open();
        ui.active_tab = SettingsTab::Keybindings;
        ui.focused_field = 0;

        // Enter edit and toggle off Shift, change key to 'n'
        ui.start_edit();
        // sub_focus 1 = Ctrl (leave on)
        ui.keybinding_next_sub(); // → 2 = Shift
        ui.keybinding_toggle_modifier(); // toggle Shift off

        // Navigate to Key
        ui.keybinding_next_sub(); // → 3 Alt
        ui.keybinding_next_sub(); // → 4 Win
        ui.keybinding_next_sub(); // → 5 Key
        ui.push_char('n');

        ui.confirm_edit();

        // Draft should now have ctrl+n for new_tab
        let binding = &ui.draft.keybindings.bindings[0];
        assert_eq!(binding.action, "new_tab");
        assert_eq!(binding.chord, "ctrl+n");
    }

    #[test]
    fn test_keybinding_edit_enter_exit() {
        let mut ui = SettingsUI::default();
        ui.open();
        ui.active_tab = SettingsTab::Keybindings;
        ui.focused_field = 0;

        assert!(!ui.is_keybinding_editing());

        // Enter edit
        ui.start_edit();
        assert!(ui.is_keybinding_editing());
        assert!(ui.editing);

        // Cancel
        ui.cancel_edit();
        assert!(!ui.is_keybinding_editing());
        assert!(!ui.editing);
    }

    #[test]
    fn test_keybinding_field_count() {
        let config = Config::default();
        let ui = SettingsUI::from_config(&config);
        let kb_idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Keybindings)
            .unwrap();
        assert!(
            ui.tab_fields[kb_idx].len() >= 40,
            "Expected at least 40 keybinding fields, got {}",
            ui.tab_fields[kb_idx].len()
        );
    }

    #[test]
    fn test_accessibility_tab_exists_in_all() {
        assert!(SettingsTab::ALL.contains(&SettingsTab::Accessibility));
    }

    #[test]
    fn test_accessibility_tab_label() {
        assert_eq!(SettingsTab::Accessibility.label(), "Accessibility");
    }

    #[test]
    fn test_accessibility_tab_has_expected_field_count() {
        let ui = SettingsUI::default();
        let idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Accessibility)
            .unwrap();
        assert_eq!(ui.tab_fields[idx].len(), 15);
    }

    #[test]
    fn test_accessibility_tab_screen_reader_dropdown_options() {
        let ui = SettingsUI::default();
        let idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Accessibility)
            .unwrap();
        let field = &ui.tab_fields[idx][0];
        if let SettingsField::Dropdown { options, .. } = field {
            assert_eq!(
                options,
                &[
                    "auto".to_string(),
                    "all".to_string(),
                    "commands-only".to_string(),
                    "errors-only".to_string(),
                    "silent".to_string(),
                ]
            );
        } else {
            panic!("Expected Dropdown for Screen Reader Output");
        }
    }

    #[test]
    fn test_accessibility_toggle_audio_feedback() {
        let mut ui = SettingsUI::default();
        ui.open();
        ui.active_tab = SettingsTab::Accessibility;

        // audio_feedback is field index 11 (0-indexed), default is false
        ui.focused_field = 11;
        let was = match ui.active_fields().get(11) {
            Some(SettingsField::Toggle { value, .. }) => *value,
            _ => panic!("Expected toggle for Audio Feedback"),
        };
        assert!(!was);

        ui.start_edit(); // toggles immediately
        let now = match ui.active_fields().get(11) {
            Some(SettingsField::Toggle { value, .. }) => *value,
            _ => panic!("Expected toggle for Audio Feedback"),
        };
        assert!(now);
        assert!(ui.dirty);
        assert!(ui.draft.accessibility.audio_feedback);
    }

    #[test]
    fn test_accessibility_min_contrast_bounds() {
        let ui = SettingsUI::default();
        let idx = SettingsTab::ALL
            .iter()
            .position(|&t| t == SettingsTab::Accessibility)
            .unwrap();
        // Min Contrast Ratio is field index 6
        let field = &ui.tab_fields[idx][6];
        if let SettingsField::Number { min, max, .. } = field {
            assert_eq!(*min, 0.0);
            assert_eq!(*max, 21.0);
        } else {
            panic!("Expected Number for Min Contrast Ratio");
        }
    }
}
