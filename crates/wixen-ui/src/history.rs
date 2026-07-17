//! Command history browser — data model for browsing previously executed commands.
//!
//! Stores completed commands (newest first) and supports case-insensitive search
//! for the command history overlay. Provides accessible labels for screen readers.

use std::time::SystemTime;

/// A single entry in the command history.
#[derive(Debug, Clone)]
pub struct HistoryEntry {
    /// The command text that was executed.
    pub command: String,
    /// Exit code (None if unknown).
    pub exit_code: Option<i32>,
    /// Working directory at execution time.
    pub cwd: Option<String>,
    /// When the command was executed.
    pub timestamp: Option<SystemTime>,
    /// ID of the originating `CommandBlock`.
    pub block_id: u64,
}

/// Browsable command history, stored newest-first.
pub struct CommandHistory {
    entries: Vec<HistoryEntry>,
}

impl CommandHistory {
    /// Create an empty command history.
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
        }
    }

    /// Push a new entry to the front (newest first).
    ///
    /// Consecutive duplicate commands (same `command` text as the current newest)
    /// are silently deduplicated — only the latest occurrence is kept.
    pub fn push(&mut self, entry: HistoryEntry) {
        if let Some(front) = self.entries.first()
            && front.command == entry.command
        {
            self.entries[0] = entry;
            return;
        }
        self.entries.insert(0, entry);
    }

    /// Case-insensitive substring search across command text.
    ///
    /// An empty query returns all entries.
    pub fn search(&self, query: &str) -> Vec<&HistoryEntry> {
        if query.is_empty() {
            return self.entries.iter().collect();
        }
        let query_lower = query.to_lowercase();
        self.entries
            .iter()
            .filter(|e| e.command.to_lowercase().contains(&query_lower))
            .collect()
    }

    /// All entries (newest first).
    pub fn entries(&self) -> &[HistoryEntry] {
        &self.entries
    }

    /// Number of entries.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the history is empty.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Remove all entries.
    pub fn clear(&mut self) {
        self.entries.clear();
    }

    /// The first `n` entries (newest first). Returns fewer if history is shorter.
    pub fn most_recent(&self, n: usize) -> &[HistoryEntry] {
        let end = n.min(self.entries.len());
        &self.entries[..end]
    }
}

impl Default for CommandHistory {
    fn default() -> Self {
        Self::new()
    }
}

/// Format a history entry as a screen-reader-friendly label.
///
/// - Exit code 0 → `"command (succeeded)"`
/// - Exit code N → `"command (exit N)"`
/// - No exit code → just the command text
pub fn history_entry_label(entry: &HistoryEntry) -> String {
    match entry.exit_code {
        Some(0) => format!("{} (succeeded)", entry.command),
        Some(code) => format!("{} (exit {})", entry.command, code),
        None => entry.command.clone(),
    }
}

/// Overlay model for browsing and searching command history.
///
/// Mirrors [`crate::command_palette::CommandPalette`]: it owns the overlay's
/// visibility, the search query, the selection index, and a filtered snapshot of
/// matching entries (newest first). It is populated from a [`CommandHistory`] and
/// returns the chosen command text on confirm.
///
/// The snapshot is cloned from the history so the browser does not borrow it
/// between calls — callers pass `&CommandHistory` only when (re)populating.
pub struct HistoryBrowser {
    /// Whether the browser overlay is visible.
    pub visible: bool,
    /// Current search query.
    pub query: String,
    /// Highlighted index within the filtered snapshot.
    pub selected: usize,
    /// Filtered entries (newest first), cloned from the source history.
    filtered: Vec<HistoryEntry>,
}

impl HistoryBrowser {
    /// Create a hidden, empty history browser.
    pub fn new() -> Self {
        Self {
            visible: false,
            query: String::new(),
            selected: 0,
            filtered: Vec::new(),
        }
    }

    /// Open the browser over `history`, clearing any prior query and showing all
    /// entries with the newest selected.
    pub fn open(&mut self, history: &CommandHistory) {
        self.visible = true;
        self.query.clear();
        self.selected = 0;
        self.populate(history);
    }

    /// Close the browser and drop the filtered snapshot.
    pub fn close(&mut self) {
        self.visible = false;
        self.query.clear();
        self.selected = 0;
        self.filtered.clear();
    }

    /// Refresh the filtered snapshot from `history` using the current query.
    ///
    /// Selection is clamped so it always points at a valid entry (or 0 when the
    /// result is empty).
    pub fn populate(&mut self, history: &CommandHistory) {
        self.filtered = history.search(&self.query).into_iter().cloned().collect();
        if self.filtered.is_empty() {
            self.selected = 0;
        } else if self.selected >= self.filtered.len() {
            self.selected = self.filtered.len() - 1;
        }
    }

    /// Replace the query and re-filter against `history`, resetting the selection.
    pub fn set_query(&mut self, history: &CommandHistory, query: &str) {
        self.query = query.to_string();
        self.selected = 0;
        self.populate(history);
    }

    /// Append a character to the query and re-filter.
    pub fn push_char(&mut self, history: &CommandHistory, ch: char) {
        self.query.push(ch);
        self.selected = 0;
        self.populate(history);
    }

    /// Remove the last query character and re-filter.
    pub fn pop_char(&mut self, history: &CommandHistory) {
        self.query.pop();
        self.selected = 0;
        self.populate(history);
    }

    /// Move the selection to the next (older) entry, wrapping to the top.
    pub fn select_next(&mut self) {
        if !self.filtered.is_empty() {
            self.selected = (self.selected + 1) % self.filtered.len();
        }
    }

    /// Move the selection to the previous (newer) entry, wrapping to the bottom.
    pub fn select_prev(&mut self) {
        if !self.filtered.is_empty() {
            self.selected = self
                .selected
                .checked_sub(1)
                .unwrap_or(self.filtered.len() - 1);
        }
    }

    /// The filtered entries currently shown (newest first).
    pub fn entries(&self) -> &[HistoryEntry] {
        &self.filtered
    }

    /// The currently highlighted entry, if any.
    pub fn current(&self) -> Option<&HistoryEntry> {
        self.filtered.get(self.selected)
    }

    /// Confirm the selection: return the chosen command text and close the
    /// browser. Returns `None` when there is nothing selected.
    pub fn confirm(&mut self) -> Option<String> {
        let command = self.current().map(|e| e.command.clone());
        self.close();
        command
    }

    /// Screen-reader / overlay render lines — one accessible label per filtered
    /// entry, in display order (newest first).
    pub fn overlay_lines(&self) -> Vec<String> {
        self.filtered.iter().map(history_entry_label).collect()
    }
}

impl Default for HistoryBrowser {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_entry(cmd: &str, exit_code: Option<i32>, block_id: u64) -> HistoryEntry {
        HistoryEntry {
            command: cmd.to_string(),
            exit_code,
            cwd: None,
            timestamp: None,
            block_id,
        }
    }

    #[test]
    fn push_adds_entries_newest_first() {
        let mut h = CommandHistory::new();
        h.push(make_entry("first", Some(0), 1));
        h.push(make_entry("second", Some(0), 2));
        h.push(make_entry("third", Some(0), 3));

        assert_eq!(h.entries()[0].command, "third");
        assert_eq!(h.entries()[1].command, "second");
        assert_eq!(h.entries()[2].command, "first");
    }

    #[test]
    fn consecutive_duplicate_commands_deduplicated() {
        let mut h = CommandHistory::new();
        h.push(make_entry("ls", Some(0), 1));
        h.push(make_entry("ls", Some(1), 2));

        assert_eq!(h.len(), 1);
        // The latest push replaces the previous one
        assert_eq!(h.entries()[0].block_id, 2);
        assert_eq!(h.entries()[0].exit_code, Some(1));
    }

    #[test]
    fn search_returns_matching_entries() {
        let mut h = CommandHistory::new();
        h.push(make_entry("cargo build", Some(0), 1));
        h.push(make_entry("cargo test", Some(0), 2));
        h.push(make_entry("git status", Some(0), 3));

        let results = h.search("cargo");
        assert_eq!(results.len(), 2);
        assert!(results.iter().all(|e| e.command.contains("cargo")));
    }

    #[test]
    fn search_is_case_insensitive() {
        let mut h = CommandHistory::new();
        h.push(make_entry("Cargo Build", Some(0), 1));

        let results = h.search("cargo build");
        assert_eq!(results.len(), 1);

        let results = h.search("CARGO");
        assert_eq!(results.len(), 1);
    }

    #[test]
    fn search_empty_query_returns_all() {
        let mut h = CommandHistory::new();
        h.push(make_entry("a", Some(0), 1));
        h.push(make_entry("b", Some(0), 2));
        h.push(make_entry("c", Some(0), 3));

        let results = h.search("");
        assert_eq!(results.len(), 3);
    }

    #[test]
    fn most_recent_returns_first_n() {
        let mut h = CommandHistory::new();
        for i in 1..=5 {
            h.push(make_entry(&format!("cmd{i}"), Some(0), i));
        }

        let recent = h.most_recent(3);
        assert_eq!(recent.len(), 3);
        assert_eq!(recent[0].command, "cmd5");
        assert_eq!(recent[1].command, "cmd4");
        assert_eq!(recent[2].command, "cmd3");
    }

    #[test]
    fn clear_empties_history() {
        let mut h = CommandHistory::new();
        h.push(make_entry("a", Some(0), 1));
        h.push(make_entry("b", Some(0), 2));
        assert!(!h.is_empty());

        h.clear();
        assert!(h.is_empty());
        assert_eq!(h.len(), 0);
    }

    #[test]
    fn history_entry_label_succeeded() {
        let entry = make_entry("cargo test", Some(0), 1);
        assert_eq!(history_entry_label(&entry), "cargo test (succeeded)");
    }

    #[test]
    fn history_entry_label_exit_code() {
        let entry = make_entry("cargo build", Some(1), 1);
        assert_eq!(history_entry_label(&entry), "cargo build (exit 1)");
    }

    #[test]
    fn history_entry_label_no_exit_code() {
        let entry = make_entry("running", None, 1);
        assert_eq!(history_entry_label(&entry), "running");
    }

    // ── HistoryBrowser ──

    fn sample_history() -> CommandHistory {
        let mut h = CommandHistory::new();
        h.push(make_entry("cargo build", Some(0), 1));
        h.push(make_entry("cargo test", Some(1), 2));
        h.push(make_entry("git status", Some(0), 3));
        h
    }

    #[test]
    fn browser_open_shows_all_entries_newest_first() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        assert!(!b.visible);

        b.open(&history);

        assert!(b.visible);
        assert_eq!(b.entries().len(), 3);
        assert_eq!(b.entries()[0].command, "git status");
        assert_eq!(b.entries()[2].command, "cargo build");
        assert_eq!(b.selected, 0);
    }

    #[test]
    fn browser_filters_using_search() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);

        b.set_query(&history, "cargo");

        assert_eq!(b.entries().len(), 2);
        assert!(b.entries().iter().all(|e| e.command.contains("cargo")));
        assert_eq!(b.selected, 0);
    }

    #[test]
    fn browser_push_and_pop_char_refilter() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);

        b.push_char(&history, 'g');
        b.push_char(&history, 'i');
        b.push_char(&history, 't');
        assert_eq!(b.entries().len(), 1);
        assert_eq!(b.entries()[0].command, "git status");

        b.pop_char(&history);
        b.pop_char(&history);
        b.pop_char(&history);
        assert_eq!(b.entries().len(), 3);
    }

    #[test]
    fn browser_selection_moves_and_wraps() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);

        assert_eq!(b.selected, 0);
        b.select_next();
        assert_eq!(b.selected, 1);
        b.select_prev();
        assert_eq!(b.selected, 0);
        // Wrap backwards to the last entry.
        b.select_prev();
        assert_eq!(b.selected, 2);
        // Wrap forwards to the first entry.
        b.select_next();
        assert_eq!(b.selected, 0);
    }

    #[test]
    fn browser_empty_history_has_no_selection() {
        let history = CommandHistory::new();
        let mut b = HistoryBrowser::new();
        b.open(&history);

        assert!(b.entries().is_empty());
        assert!(b.current().is_none());
        // Movement on an empty list is a no-op, not a panic.
        b.select_next();
        b.select_prev();
        assert_eq!(b.selected, 0);
        assert!(b.confirm().is_none());
    }

    #[test]
    fn browser_confirm_returns_selected_command_and_closes() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);
        b.select_next(); // move to "cargo test"

        let chosen = b.confirm();

        assert_eq!(chosen.as_deref(), Some("cargo test"));
        assert!(!b.visible);
        assert!(b.entries().is_empty());
    }

    #[test]
    fn browser_current_tracks_selection() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);

        assert_eq!(b.current().map(|e| e.command.as_str()), Some("git status"));
        b.select_next();
        assert_eq!(b.current().map(|e| e.command.as_str()), Some("cargo test"));
    }

    #[test]
    fn browser_overlay_lines_use_entry_labels() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);

        let lines = b.overlay_lines();
        assert_eq!(
            lines,
            vec![
                "git status (succeeded)".to_string(),
                "cargo test (exit 1)".to_string(),
                "cargo build (succeeded)".to_string(),
            ]
        );
    }

    #[test]
    fn browser_populate_clamps_stale_selection() {
        let history = sample_history();
        let mut b = HistoryBrowser::new();
        b.open(&history);
        b.select_prev(); // selected = 2 (last)

        // Narrow to a single result: selection must clamp into range.
        b.set_query(&history, "git");
        assert_eq!(b.entries().len(), 1);
        assert_eq!(b.selected, 0);
    }
}
