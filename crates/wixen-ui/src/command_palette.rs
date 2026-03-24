//! Command palette — fuzzy-searchable action list (Ctrl+Shift+P).
//!
//! The command palette provides quick access to all terminal actions:
//! tab management, pane splitting, settings, theme switching, etc.
//! It uses a simple subsequence fuzzy matcher to filter commands as the
//! user types.

/// A single command entry in the palette.
#[derive(Debug, Clone)]
pub struct PaletteEntry {
    /// Unique action identifier (e.g., "new_tab", "split_horizontal").
    pub id: String,
    /// Human-readable label shown in the palette.
    pub label: String,
    /// Optional keyboard shortcut hint (e.g., "Ctrl+Shift+T").
    pub shortcut: Option<String>,
    /// Category for grouping (e.g., "Tabs", "Panes", "Settings").
    pub category: String,
}

impl PaletteEntry {
    /// Convenience constructor for static built-in entries.
    fn builtin(id: &str, label: &str, shortcut: Option<&str>, category: &str) -> Self {
        Self {
            id: id.to_string(),
            label: label.to_string(),
            shortcut: shortcut.map(|s| s.to_string()),
            category: category.to_string(),
        }
    }
}

/// Create the built-in command palette entries.
fn builtin_commands() -> Vec<PaletteEntry> {
    vec![
        // --- Tabs ---
        PaletteEntry::builtin("new_tab", "New Tab", Some("Ctrl+Shift+T"), "Tabs"),
        PaletteEntry::builtin("close_tab", "Close Tab", Some("Ctrl+Shift+W"), "Tabs"),
        PaletteEntry::builtin("next_tab", "Next Tab", Some("Ctrl+Tab"), "Tabs"),
        PaletteEntry::builtin("prev_tab", "Previous Tab", Some("Ctrl+Shift+Tab"), "Tabs"),
        // --- Panes ---
        PaletteEntry::builtin(
            "split_horizontal",
            "Split Pane Horizontally",
            Some("Alt+Shift+Plus"),
            "Panes",
        ),
        PaletteEntry::builtin(
            "split_vertical",
            "Split Pane Vertically",
            Some("Alt+Shift+Minus"),
            "Panes",
        ),
        PaletteEntry::builtin("close_pane", "Close Pane", Some("Ctrl+Shift+W"), "Panes"),
        PaletteEntry::builtin(
            "focus_next_pane",
            "Focus Next Pane",
            Some("Alt+Down"),
            "Panes",
        ),
        PaletteEntry::builtin(
            "focus_prev_pane",
            "Focus Previous Pane",
            Some("Alt+Up"),
            "Panes",
        ),
        // --- Clipboard ---
        PaletteEntry::builtin("copy", "Copy Selection", Some("Ctrl+Shift+C"), "Clipboard"),
        PaletteEntry::builtin(
            "paste",
            "Paste from Clipboard",
            Some("Ctrl+Shift+V"),
            "Clipboard",
        ),
        // --- Search ---
        PaletteEntry::builtin("find", "Find in Terminal", Some("Ctrl+Shift+F"), "Search"),
        // --- View ---
        PaletteEntry::builtin("zoom_in", "Increase Font Size", Some("Ctrl+Plus"), "View"),
        PaletteEntry::builtin("zoom_out", "Decrease Font Size", Some("Ctrl+Minus"), "View"),
        PaletteEntry::builtin("zoom_reset", "Reset Font Size", Some("Ctrl+0"), "View"),
        PaletteEntry::builtin(
            "toggle_fullscreen",
            "Toggle Fullscreen",
            Some("F11"),
            "View",
        ),
        // --- Settings ---
        PaletteEntry::builtin(
            "open_settings",
            "Open Settings",
            Some("Ctrl+Comma"),
            "Settings",
        ),
        PaletteEntry::builtin(
            "open_config_file",
            "Open Configuration File",
            None,
            "Settings",
        ),
        PaletteEntry::builtin("reload_config", "Reload Configuration", None, "Settings"),
        // --- Window ---
        PaletteEntry::builtin("new_window", "New Window", None, "Window"),
        PaletteEntry::builtin(
            "scroll_up_page",
            "Scroll Up One Page",
            Some("Shift+PageUp"),
            "View",
        ),
        PaletteEntry::builtin(
            "scroll_down_page",
            "Scroll Down One Page",
            Some("Shift+PageDown"),
            "View",
        ),
        PaletteEntry::builtin("scroll_to_top", "Scroll to Top", Some("Ctrl+Home"), "View"),
        PaletteEntry::builtin(
            "scroll_to_bottom",
            "Scroll to Bottom",
            Some("Ctrl+End"),
            "View",
        ),
        // --- Command Navigation ---
        PaletteEntry::builtin(
            "jump_to_previous_prompt",
            "Jump to Previous Prompt",
            Some("Ctrl+Shift+Up"),
            "Navigation",
        ),
        PaletteEntry::builtin(
            "jump_to_next_prompt",
            "Jump to Next Prompt",
            Some("Ctrl+Shift+Down"),
            "Navigation",
        ),
        PaletteEntry::builtin(
            "copy_last_output",
            "Copy Last Command Output",
            None,
            "Navigation",
        ),
        PaletteEntry::builtin(
            "rerun_last_command",
            "Rerun Last Command",
            None,
            "Navigation",
        ),
        PaletteEntry::builtin("restart_shell", "Restart Shell", None, "Shell"),
        PaletteEntry::builtin("rename_tab", "Rename Tab", None, "Tabs"),
        // --- Edit ---
        PaletteEntry::builtin("select_all", "Select All", Some("Ctrl+Shift+A"), "Edit"),
        PaletteEntry::builtin("clear_scrollback", "Clear Scrollback", None, "Terminal"),
        PaletteEntry::builtin("reset_terminal", "Reset Terminal", None, "Terminal"),
        // --- Tab management ---
        PaletteEntry::builtin("duplicate_tab", "Duplicate Tab", None, "Tabs"),
        PaletteEntry::builtin("move_tab_left", "Move Tab Left", None, "Tabs"),
        PaletteEntry::builtin("move_tab_right", "Move Tab Right", None, "Tabs"),
        PaletteEntry::builtin("close_other_tabs", "Close Other Tabs", None, "Tabs"),
        PaletteEntry::builtin(
            "close_tabs_to_right",
            "Close Tabs to the Right",
            None,
            "Tabs",
        ),
        PaletteEntry::builtin("close_tabs_to_left", "Close Tabs to the Left", None, "Tabs"),
        // --- Clipboard ---
        PaletteEntry::builtin("copy_last_command", "Copy Last Command", None, "Clipboard"),
        PaletteEntry::builtin("copy_last_output", "Copy Last Output", None, "Clipboard"),
        // --- View ---
        PaletteEntry::builtin("toggle_tab_bar", "Toggle Tab Bar", None, "View"),
        PaletteEntry::builtin("clear_terminal", "Clear Terminal", None, "Terminal"),
        PaletteEntry::builtin(
            "toggle_always_on_top",
            "Toggle Always on Top",
            None,
            "Window",
        ),
        PaletteEntry::builtin("focus_last_tab", "Switch to Last Tab", None, "Tabs"),
        PaletteEntry::builtin(
            "toggle_broadcast_input",
            "Toggle Broadcast Input",
            None,
            "Panes",
        ),
        PaletteEntry::builtin(
            "scroll_to_selection",
            "Scroll to Selection",
            None,
            "Navigation",
        ),
        PaletteEntry::builtin("copy_cwd", "Copy Working Directory", None, "Clipboard"),
        PaletteEntry::builtin(
            "open_cwd_in_explorer",
            "Open in Explorer",
            None,
            "Navigation",
        ),
        PaletteEntry::builtin("toggle_bell", "Toggle Bell Style", None, "Settings"),
        PaletteEntry::builtin(
            "toggle_cursor_blink",
            "Toggle Cursor Blink",
            None,
            "Settings",
        ),
        PaletteEntry::builtin("cycle_cursor_style", "Cycle Cursor Style", None, "Settings"),
        PaletteEntry::builtin("focus_pane_left", "Focus Pane Left", None, "Panes"),
        PaletteEntry::builtin("focus_pane_right", "Focus Pane Right", None, "Panes"),
        PaletteEntry::builtin("focus_pane_up", "Focus Pane Up", None, "Panes"),
        PaletteEntry::builtin("focus_pane_down", "Focus Pane Down", None, "Panes"),
        PaletteEntry::builtin("resize_pane_grow", "Grow Active Pane", None, "Panes"),
        PaletteEntry::builtin("resize_pane_shrink", "Shrink Active Pane", None, "Panes"),
        PaletteEntry::builtin("toggle_word_wrap", "Toggle Word Wrap", None, "View"),
        PaletteEntry::builtin("increase_opacity", "Increase Opacity", None, "Window"),
        PaletteEntry::builtin("decrease_opacity", "Decrease Opacity", None, "Window"),
        PaletteEntry::builtin("reset_opacity", "Reset Opacity", None, "Window"),
        PaletteEntry::builtin("close_all_tabs", "Close All Tabs", None, "Tabs"),
        PaletteEntry::builtin(
            "export_buffer_text",
            "Export Terminal Buffer to Clipboard",
            None,
            "Clipboard",
        ),
        PaletteEntry::builtin("scroll_to_cursor", "Scroll to Cursor", None, "Navigation"),
        PaletteEntry::builtin("find_next", "Find Next Match", Some("F3"), "Search"),
        PaletteEntry::builtin(
            "find_previous",
            "Find Previous Match",
            Some("Shift+F3"),
            "Search",
        ),
        // --- Help ---
        PaletteEntry::builtin("open_help", "Open User Guide", Some("F1"), "Help"),
        // --- System ---
        PaletteEntry::builtin(
            "install_context_menu",
            "Install Explorer Context Menu",
            None,
            "System",
        ),
        PaletteEntry::builtin(
            "uninstall_context_menu",
            "Uninstall Explorer Context Menu",
            None,
            "System",
        ),
    ]
}

/// Number of built-in commands (for tests).
pub const BUILTIN_COUNT: usize = 70;

/// The palette can operate in different modes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PaletteMode {
    /// Normal command search / filter mode.
    Commands,
    /// Text input prompt (e.g., "Rename Tab").
    /// The query text is the user's input; confirm returns `PaletteResult::Input`.
    Input,
}

/// Result from confirming the palette.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PaletteResult {
    /// A command action ID was selected.
    Action(String),
    /// A text input was confirmed (from Input mode).
    Input(String),
}

/// State of the command palette overlay.
pub struct CommandPalette {
    /// Whether the palette is currently visible.
    pub visible: bool,
    /// Current search query typed by the user.
    pub query: String,
    /// Filtered results (indices into the command list).
    pub filtered: Vec<usize>,
    /// Currently highlighted result index (within `filtered`).
    pub selected: usize,
    /// All registered commands (builtins + dynamic profile/SSH entries).
    commands: Vec<PaletteEntry>,
    /// Index where dynamic profile entries start (everything before is builtin).
    profile_start: usize,
    /// Index where SSH target entries start (everything before is builtin+profile).
    ssh_start: usize,
    /// Current palette mode.
    pub mode: PaletteMode,
    /// Prompt text shown when in Input mode (e.g., "Enter new tab name:").
    pub input_prompt: String,
}

impl CommandPalette {
    /// Create a new command palette pre-loaded with built-in commands.
    pub fn new() -> Self {
        let commands = builtin_commands();
        let filtered: Vec<usize> = (0..commands.len()).collect();
        let profile_start = commands.len();
        let ssh_start = profile_start;
        Self {
            visible: false,
            query: String::new(),
            filtered,
            selected: 0,
            commands,
            profile_start,
            ssh_start,
            mode: PaletteMode::Commands,
            input_prompt: String::new(),
        }
    }

    /// Open the command palette in normal command mode.
    pub fn open(&mut self) {
        self.visible = true;
        self.query.clear();
        self.selected = 0;
        self.filtered = (0..self.commands.len()).collect();
        self.mode = PaletteMode::Commands;
        self.input_prompt.clear();
    }

    /// Open the palette in text input mode with a prompt.
    pub fn open_input(&mut self, prompt: &str) {
        self.visible = true;
        self.query.clear();
        self.selected = 0;
        self.filtered.clear();
        self.mode = PaletteMode::Input;
        self.input_prompt = prompt.to_string();
    }

    /// Close the command palette.
    pub fn close(&mut self) {
        self.visible = false;
        self.query.clear();
        self.mode = PaletteMode::Commands;
        self.input_prompt.clear();
    }

    /// Toggle the command palette visibility.
    pub fn toggle(&mut self) {
        if self.visible {
            self.close();
        } else {
            self.open();
        }
    }

    /// Update the search query and re-filter.
    pub fn set_query(&mut self, query: &str) {
        self.query = query.to_string();
        self.filter();
        self.selected = 0;
    }

    /// Append a character to the query.
    pub fn push_char(&mut self, ch: char) {
        self.query.push(ch);
        self.filter();
        self.selected = 0;
    }

    /// Remove the last character from the query.
    pub fn pop_char(&mut self) {
        self.query.pop();
        self.filter();
        self.selected = 0;
    }

    /// Move selection up.
    pub fn select_prev(&mut self) {
        if !self.filtered.is_empty() {
            self.selected = self
                .selected
                .checked_sub(1)
                .unwrap_or(self.filtered.len() - 1);
        }
    }

    /// Move selection down.
    pub fn select_next(&mut self) {
        if !self.filtered.is_empty() {
            self.selected = (self.selected + 1) % self.filtered.len();
        }
    }

    /// Confirm the current selection. Returns the action or input text.
    pub fn confirm(&mut self) -> Option<PaletteResult> {
        let result = match self.mode {
            PaletteMode::Commands => self
                .filtered
                .get(self.selected)
                .map(|&idx| PaletteResult::Action(self.commands[idx].id.clone())),
            PaletteMode::Input => Some(PaletteResult::Input(self.query.clone())),
        };
        self.close();
        result
    }

    /// Get the currently filtered entries.
    pub fn entries(&self) -> Vec<&PaletteEntry> {
        self.filtered
            .iter()
            .map(|&idx| &self.commands[idx])
            .collect()
    }

    /// Get the currently selected entry.
    pub fn selected_entry(&self) -> Option<&PaletteEntry> {
        self.filtered
            .get(self.selected)
            .map(|&idx| &self.commands[idx])
    }

    /// Add a custom command (e.g., from Lua scripts).
    pub fn add_command(&mut self, entry: PaletteEntry) {
        self.commands.push(entry);
        if self.visible {
            self.filter();
        }
    }

    /// Set profile entries in the palette. Replaces any previous profile entries.
    ///
    /// Each profile gets a "New Tab: {name}" entry with action `new_tab_profile_{index}`
    /// and a shortcut hint like "Ctrl+Shift+1".
    pub fn set_profiles(&mut self, profiles: &[(String, Option<String>)]) {
        // Remove old profile + SSH entries (profiles come first, SSH after)
        self.commands.truncate(self.profile_start);

        // Add new profile entries
        for (i, (name, shortcut)) in profiles.iter().enumerate() {
            self.commands.push(PaletteEntry {
                id: format!("new_tab_profile_{i}"),
                label: format!("New Tab: {name}"),
                shortcut: shortcut.clone(),
                category: "Profiles".to_string(),
            });
        }

        // SSH entries start after profiles
        self.ssh_start = self.commands.len();

        // Always refresh the filtered list so entries() returns the full set
        self.filter();
    }

    /// Set SSH target entries in the palette. Replaces any previous SSH entries.
    ///
    /// Each SSH target gets an "SSH: {name}" entry with action `ssh_{index}`.
    pub fn set_ssh_targets(&mut self, targets: &[String]) {
        // Remove old SSH entries (keep everything up to ssh_start)
        self.commands.truncate(self.ssh_start);

        // Add new SSH entries
        for (i, name) in targets.iter().enumerate() {
            self.commands.push(PaletteEntry {
                id: format!("ssh_{i}"),
                label: format!("SSH: {name}"),
                shortcut: None,
                category: "SSH".to_string(),
            });
        }

        // Refresh filtered list
        self.filter();
    }

    /// Load SSH targets from [`wixen_config::ssh::SshTarget`] structs into the palette.
    ///
    /// Replaces any previous SSH entries. Each target produces an entry with
    /// `id = "ssh_{name}"`, a label showing `name (user@host:port)`, and
    /// category `"SSH"`.
    pub fn load_ssh_targets(&mut self, targets: &[wixen_config::ssh::SshTarget]) {
        self.commands.truncate(self.ssh_start);

        for target in targets {
            let user_host = if target.user.is_empty() {
                format!("{}:{}", target.host, target.port)
            } else {
                format!("{}@{}:{}", target.user, target.host, target.port)
            };
            self.commands.push(PaletteEntry {
                id: format!("ssh_{}", target.name),
                label: format!("{} ({})", target.name, user_host),
                shortcut: None,
                category: "SSH".to_string(),
            });
        }

        self.filter();
    }

    /// Number of registered commands.
    pub fn command_count(&self) -> usize {
        self.commands.len()
    }

    /// Re-filter commands based on the current query.
    fn filter(&mut self) {
        if self.query.is_empty() {
            self.filtered = (0..self.commands.len()).collect();
        } else {
            let query_lower = self.query.to_lowercase();
            self.filtered = self
                .commands
                .iter()
                .enumerate()
                .filter(|(_, cmd)| {
                    fuzzy_match(&cmd.label.to_lowercase(), &query_lower)
                        || fuzzy_match(&cmd.id.to_lowercase(), &query_lower)
                        || fuzzy_match(&cmd.category.to_lowercase(), &query_lower)
                })
                .map(|(i, _)| i)
                .collect();
        }
    }
}

impl Default for CommandPalette {
    fn default() -> Self {
        Self::new()
    }
}

/// Simple subsequence fuzzy matching: checks if all characters of `pattern`
/// appear in `text` in order (case already lowered by caller).
fn fuzzy_match(text: &str, pattern: &str) -> bool {
    let mut pattern_chars = pattern.chars();
    let mut current = pattern_chars.next();
    for ch in text.chars() {
        if let Some(p) = current {
            if ch == p {
                current = pattern_chars.next();
            }
        } else {
            return true;
        }
    }
    current.is_none()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fuzzy_match_basic() {
        assert!(fuzzy_match("new tab", "nt"));
        assert!(fuzzy_match("new tab", "new"));
        assert!(fuzzy_match("new tab", "new tab"));
        assert!(!fuzzy_match("new tab", "old"));
        assert!(fuzzy_match("split pane horizontally", "sph"));
    }

    #[test]
    fn test_fuzzy_match_empty_pattern() {
        assert!(fuzzy_match("anything", ""));
    }

    #[test]
    fn test_palette_open_close() {
        let mut p = CommandPalette::new();
        assert!(!p.visible);
        p.open();
        assert!(p.visible);
        assert!(p.query.is_empty());
        assert_eq!(p.filtered.len(), p.command_count());
        p.close();
        assert!(!p.visible);
    }

    #[test]
    fn test_palette_filter() {
        let mut p = CommandPalette::new();
        p.open();
        p.set_query("tab");
        // Should find "New Tab", "Close Tab", "Next Tab", "Previous Tab", etc.
        assert!(p.filtered.len() >= 4);
        for &idx in &p.filtered {
            let entry = &p.commands[idx];
            let combined =
                format!("{} {} {}", entry.label, entry.id, entry.category).to_lowercase();
            assert!(
                fuzzy_match(&combined, "tab"),
                "Expected fuzzy 'tab' in: {}",
                combined
            );
        }
    }

    #[test]
    fn test_palette_navigation() {
        let mut p = CommandPalette::new();
        p.open();
        assert_eq!(p.selected, 0);
        p.select_next();
        assert_eq!(p.selected, 1);
        p.select_prev();
        assert_eq!(p.selected, 0);
        // Wrap around
        p.select_prev();
        assert_eq!(p.selected, p.filtered.len() - 1);
    }

    #[test]
    fn test_palette_confirm() {
        let mut p = CommandPalette::new();
        p.open();
        let result = p.confirm().unwrap();
        assert_eq!(result, PaletteResult::Action("new_tab".to_string()));
        assert!(!p.visible);
    }

    #[test]
    fn test_palette_push_pop_char() {
        let mut p = CommandPalette::new();
        p.open();
        let total = p.filtered.len();
        p.push_char('z');
        p.push_char('o');
        p.push_char('o');
        // "zoo" should match zoom commands
        assert!(p.filtered.len() < total);
        p.pop_char();
        p.pop_char();
        p.pop_char();
        assert_eq!(p.filtered.len(), total); // Back to showing all
    }

    #[test]
    fn test_builtin_command_count() {
        let p = CommandPalette::new();
        assert_eq!(p.command_count(), BUILTIN_COUNT);
    }

    #[test]
    fn test_palette_toggle() {
        let mut p = CommandPalette::new();
        p.toggle();
        assert!(p.visible);
        p.toggle();
        assert!(!p.visible);
    }

    #[test]
    fn test_profile_palette_entries() {
        let mut p = CommandPalette::new();
        let profiles = vec![
            ("PowerShell".to_string(), Some("Ctrl+Shift+1".to_string())),
            ("cmd.exe".to_string(), Some("Ctrl+Shift+2".to_string())),
            ("Git Bash".to_string(), None),
        ];
        p.set_profiles(&profiles);
        assert_eq!(p.command_count(), BUILTIN_COUNT + 3);

        // Verify profile entries
        let entries = p.entries();
        let profile_entries: Vec<_> = entries
            .iter()
            .filter(|e| e.category == "Profiles")
            .collect();
        assert_eq!(profile_entries.len(), 3);
        assert_eq!(profile_entries[0].label, "New Tab: PowerShell");
        assert_eq!(profile_entries[0].id, "new_tab_profile_0");
        assert_eq!(profile_entries[0].shortcut.as_deref(), Some("Ctrl+Shift+1"));
        assert_eq!(profile_entries[1].label, "New Tab: cmd.exe");
        assert_eq!(profile_entries[2].label, "New Tab: Git Bash");
        assert!(profile_entries[2].shortcut.is_none());
    }

    #[test]
    fn test_profile_reload_updates_palette() {
        let mut p = CommandPalette::new();
        let profiles_v1 = vec![("PowerShell".to_string(), None)];
        p.set_profiles(&profiles_v1);
        assert_eq!(p.command_count(), BUILTIN_COUNT + 1);

        // Reload with different profiles
        let profiles_v2 = vec![("pwsh".to_string(), None), ("cmd".to_string(), None)];
        p.set_profiles(&profiles_v2);
        assert_eq!(p.command_count(), BUILTIN_COUNT + 2);

        // Old profile entry should be gone
        let entries = p.entries();
        let profile_entries: Vec<_> = entries
            .iter()
            .filter(|e| e.category == "Profiles")
            .collect();
        assert_eq!(profile_entries.len(), 2);
        assert_eq!(profile_entries[0].label, "New Tab: pwsh");
        assert_eq!(profile_entries[1].label, "New Tab: cmd");
    }

    #[test]
    fn test_profile_action_dispatch() {
        let mut p = CommandPalette::new();
        let profiles = vec![("PowerShell".to_string(), Some("Ctrl+Shift+1".to_string()))];
        p.set_profiles(&profiles);

        // Navigate to the profile entry and confirm
        p.open();
        p.set_query("powershell");
        assert!(!p.filtered.is_empty());
        let result = p.confirm().unwrap();
        assert_eq!(
            result,
            PaletteResult::Action("new_tab_profile_0".to_string())
        );
    }

    #[test]
    fn test_ssh_palette_entries() {
        let mut p = CommandPalette::new();
        let targets = vec![
            "Production".to_string(),
            "Staging".to_string(),
            "Dev".to_string(),
        ];
        p.set_ssh_targets(&targets);

        let entries = p.entries();
        let ssh_entries: Vec<_> = entries.iter().filter(|e| e.category == "SSH").collect();
        assert_eq!(ssh_entries.len(), 3);
        assert_eq!(ssh_entries[0].label, "SSH: Production");
        assert_eq!(ssh_entries[0].id, "ssh_0");
        assert_eq!(ssh_entries[1].label, "SSH: Staging");
        assert_eq!(ssh_entries[1].id, "ssh_1");
        assert_eq!(ssh_entries[2].label, "SSH: Dev");
        assert_eq!(ssh_entries[2].id, "ssh_2");
    }

    #[test]
    fn test_ssh_targets_reload_replaces() {
        let mut p = CommandPalette::new();
        p.set_ssh_targets(&["OldServer".to_string()]);
        let entries = p.entries();
        assert!(entries.iter().any(|e| e.label == "SSH: OldServer"));

        // Reload with different targets
        p.set_ssh_targets(&["NewServer".to_string(), "Backup".to_string()]);
        let entries = p.entries();
        let ssh_entries: Vec<_> = entries.iter().filter(|e| e.category == "SSH").collect();
        assert_eq!(ssh_entries.len(), 2);
        assert!(!entries.iter().any(|e| e.label == "SSH: OldServer"));
        assert!(entries.iter().any(|e| e.label == "SSH: NewServer"));
    }

    #[test]
    fn test_ssh_and_profiles_coexist() {
        let mut p = CommandPalette::new();
        let profiles = vec![("PowerShell".to_string(), None)];
        p.set_profiles(&profiles);
        p.set_ssh_targets(&["Production".to_string()]);

        let entries = p.entries();
        assert!(entries.iter().any(|e| e.label == "New Tab: PowerShell"));
        assert!(entries.iter().any(|e| e.label == "SSH: Production"));
    }

    #[test]
    fn test_ssh_action_dispatch() {
        let mut p = CommandPalette::new();
        p.set_ssh_targets(&["MyServer".to_string()]);

        p.open();
        p.set_query("ssh myserver");
        assert!(!p.filtered.is_empty());
        let result = p.confirm().unwrap();
        assert_eq!(result, PaletteResult::Action("ssh_0".to_string()));
    }

    #[test]
    fn test_restart_shell_command_exists() {
        let p = CommandPalette::new();
        let entries = p.entries();
        let restart = entries.iter().find(|e| e.id == "restart_shell");
        assert!(restart.is_some(), "restart_shell command should exist");
        assert_eq!(restart.unwrap().category, "Shell");
    }

    // ── Tier L: Input mode tests ──

    #[test]
    fn test_palette_input_mode() {
        let mut p = CommandPalette::new();
        p.open_input("Enter new tab name:");
        assert!(p.visible);
        assert_eq!(p.mode, PaletteMode::Input);
        assert_eq!(p.input_prompt, "Enter new tab name:");
        assert!(p.query.is_empty());
    }

    #[test]
    fn test_palette_input_confirm() {
        let mut p = CommandPalette::new();
        p.open_input("Enter new tab name:");
        p.push_char('M');
        p.push_char('y');
        p.push_char(' ');
        p.push_char('T');
        p.push_char('a');
        p.push_char('b');
        let result = p.confirm().unwrap();
        assert_eq!(result, PaletteResult::Input("My Tab".to_string()));
        assert!(!p.visible);
        assert_eq!(p.mode, PaletteMode::Commands); // reset after close
    }

    #[test]
    fn test_palette_input_escape_closes() {
        let mut p = CommandPalette::new();
        p.open_input("Enter new tab name:");
        p.push_char('x');
        p.close();
        assert!(!p.visible);
        assert_eq!(p.mode, PaletteMode::Commands);
        assert!(p.input_prompt.is_empty());
    }

    #[test]
    fn test_palette_input_empty_confirm() {
        let mut p = CommandPalette::new();
        p.open_input("Enter new tab name:");
        let result = p.confirm().unwrap();
        assert_eq!(result, PaletteResult::Input(String::new()));
    }

    // ── Task 1: load_ssh_targets ──

    #[test]
    fn test_load_ssh_targets_produces_expected_entries() {
        use wixen_config::ssh::SshTarget;

        let mut p = CommandPalette::new();
        let targets = vec![
            SshTarget {
                name: "prod".into(),
                host: "prod.example.com".into(),
                port: 22,
                user: "deploy".into(),
                ..Default::default()
            },
            SshTarget {
                name: "staging".into(),
                host: "staging.local".into(),
                port: 2222,
                user: String::new(),
                ..Default::default()
            },
        ];

        p.load_ssh_targets(&targets);

        let entries = p.entries();
        let ssh: Vec<_> = entries.iter().filter(|e| e.category == "SSH").collect();
        assert_eq!(ssh.len(), 2);

        assert_eq!(ssh[0].id, "ssh_prod");
        assert_eq!(ssh[0].label, "prod (deploy@prod.example.com:22)");
        assert_eq!(ssh[0].category, "SSH");

        assert_eq!(ssh[1].id, "ssh_staging");
        assert_eq!(ssh[1].label, "staging (staging.local:2222)");
        assert_eq!(ssh[1].category, "SSH");
    }

    // ── Task 2: Explorer context menu entries ──

    #[test]
    fn test_context_menu_entries_in_default_palette() {
        let p = CommandPalette::new();
        let entries = p.entries();

        let install = entries
            .iter()
            .find(|e| e.id == "install_context_menu")
            .expect("install_context_menu should exist");
        assert_eq!(install.label, "Install Explorer Context Menu");
        assert_eq!(install.category, "System");

        let uninstall = entries
            .iter()
            .find(|e| e.id == "uninstall_context_menu")
            .expect("uninstall_context_menu should exist");
        assert_eq!(uninstall.label, "Uninstall Explorer Context Menu");
        assert_eq!(uninstall.category, "System");
    }
}
