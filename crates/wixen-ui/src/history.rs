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
}
