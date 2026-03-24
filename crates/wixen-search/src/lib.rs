//! wixen-search: Terminal search with plain text and regex support.
//!
//! Searches across both the visible viewport and the scrollback buffer.
//! Supports incremental search with live match updating, case sensitivity,
//! regex mode, and forward/backward navigation.

use regex::Regex;
use tracing::debug;
use wixen_core::Terminal;
use wixen_core::cell::Cell;

/// A single search match in the terminal.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SearchMatch {
    /// Absolute row index (0 = first scrollback row, scrollback.len() = first grid row).
    pub row: usize,
    /// Start column (inclusive).
    pub col_start: usize,
    /// End column (exclusive).
    pub col_end: usize,
}

/// Search direction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SearchDirection {
    Forward,
    Backward,
}

/// Search options controlling match behavior.
#[derive(Debug, Clone)]
pub struct SearchOptions {
    /// Case-sensitive matching.
    pub case_sensitive: bool,
    /// Use regex pattern instead of plain text.
    pub regex: bool,
    /// Wrap around when reaching the end/beginning.
    pub wrap_around: bool,
}

impl Default for SearchOptions {
    fn default() -> Self {
        Self {
            case_sensitive: false,
            regex: false,
            wrap_around: true,
        }
    }
}

/// Terminal search engine.
///
/// Holds the current query, options, cached matches, and active match index.
pub struct SearchEngine {
    /// Current search query (raw input from user).
    query: String,
    /// Compiled regex (set when options.regex is true and query is valid).
    compiled_regex: Option<Regex>,
    /// Search options.
    options: SearchOptions,
    /// All matches found in the terminal.
    matches: Vec<SearchMatch>,
    /// Index of the currently focused match (None = no focus).
    active: Option<usize>,
}

impl SearchEngine {
    pub fn new() -> Self {
        Self {
            query: String::new(),
            compiled_regex: None,
            options: SearchOptions::default(),
            matches: Vec::new(),
            active: None,
        }
    }

    /// Start or update a search with the given query and options.
    ///
    /// Re-scans the terminal and populates matches. The active match is set
    /// to the first match at or after the cursor position.
    pub fn search(&mut self, terminal: &Terminal, query: &str, options: SearchOptions) {
        self.query = query.to_string();
        self.options = options;
        self.compiled_regex = None;
        self.matches.clear();
        self.active = None;

        if self.query.is_empty() {
            return;
        }

        // Compile regex if in regex mode
        if self.options.regex {
            let pattern = if self.options.case_sensitive {
                Regex::new(&self.query)
            } else {
                Regex::new(&format!("(?i){}", &self.query))
            };
            match pattern {
                Ok(re) => self.compiled_regex = Some(re),
                Err(_) => return, // Invalid regex — no matches
            }
        }

        // Scan all rows: scrollback first, then grid
        let scrollback_len = terminal.scrollback.len();
        let total_rows = scrollback_len + terminal.rows();

        for abs_row in 0..total_rows {
            let line = extract_row_text(terminal, abs_row, scrollback_len);
            self.find_matches_in_line(&line, abs_row);
        }

        // Set active to the first match at or after the cursor's absolute row
        if !self.matches.is_empty() {
            let cursor_abs_row = scrollback_len + terminal.grid.cursor.row;
            self.active = Some(
                self.matches
                    .iter()
                    .position(|m| m.row >= cursor_abs_row)
                    .unwrap_or(0),
            );
        }

        debug!(
            query = %self.query,
            matches = self.matches.len(),
            active = ?self.active,
            "Search completed"
        );
    }

    /// Find all matches in a single line and append to self.matches.
    fn find_matches_in_line(&mut self, line: &str, abs_row: usize) {
        if let Some(ref re) = self.compiled_regex {
            // Regex search
            for mat in re.find_iter(line) {
                let col_start = char_col_at_byte(line, mat.start());
                let col_end = char_col_at_byte(line, mat.end());
                self.matches.push(SearchMatch {
                    row: abs_row,
                    col_start,
                    col_end,
                });
            }
        } else {
            // Plain text search
            let (haystack, needle);
            if self.options.case_sensitive {
                haystack = line.to_string();
                needle = self.query.clone();
            } else {
                haystack = line.to_lowercase();
                needle = self.query.to_lowercase();
            }

            let mut start = 0;
            while let Some(pos) = haystack[start..].find(&needle) {
                let byte_start = start + pos;
                let byte_end = byte_start + needle.len();
                let col_start = char_col_at_byte(line, byte_start);
                let col_end = char_col_at_byte(line, byte_end);
                self.matches.push(SearchMatch {
                    row: abs_row,
                    col_start,
                    col_end,
                });
                // Advance past this match (by at least one char to avoid infinite loops)
                start = byte_start
                    + line[byte_start..]
                        .chars()
                        .next()
                        .map_or(1, |c| c.len_utf8());
            }
        }
    }

    /// Move to the next match in the given direction.
    /// Returns the new active match, if any.
    pub fn next_match(&mut self, direction: SearchDirection) -> Option<&SearchMatch> {
        if self.matches.is_empty() {
            return None;
        }

        let len = self.matches.len();
        let current = self.active.unwrap_or(0);

        let next = match direction {
            SearchDirection::Forward => {
                if current + 1 < len {
                    current + 1
                } else if self.options.wrap_around {
                    0
                } else {
                    current
                }
            }
            SearchDirection::Backward => {
                if current > 0 {
                    current - 1
                } else if self.options.wrap_around {
                    len - 1
                } else {
                    current
                }
            }
        };

        self.active = Some(next);
        self.matches.get(next)
    }

    /// Get the currently active match.
    pub fn active_match(&self) -> Option<&SearchMatch> {
        self.active.and_then(|i| self.matches.get(i))
    }

    /// Get the 1-based index of the active match and total count (e.g., "3 of 17").
    pub fn match_status(&self) -> Option<(usize, usize)> {
        self.active.map(|i| (i + 1, self.matches.len()))
    }

    /// Get all matches (for highlighting).
    pub fn all_matches(&self) -> &[SearchMatch] {
        &self.matches
    }

    /// Total number of matches.
    pub fn match_count(&self) -> usize {
        self.matches.len()
    }

    /// Whether a search is currently active (non-empty query).
    pub fn is_active(&self) -> bool {
        !self.query.is_empty()
    }

    /// Get the current query string.
    pub fn query(&self) -> &str {
        &self.query
    }

    /// Get a display string like "2/15" or "No results" for the search bar.
    pub fn status_text(&self) -> String {
        if self.query.is_empty() {
            return String::new();
        }
        if self.matches.is_empty() {
            return "No results".to_string();
        }
        match self.active {
            Some(idx) => format!("{}/{}", idx + 1, self.matches.len()),
            None => format!("0/{}", self.matches.len()),
        }
    }

    /// Clear the search state.
    pub fn clear(&mut self) {
        self.query.clear();
        self.compiled_regex = None;
        self.matches.clear();
        self.active = None;
    }

    /// Check whether a given cell is part of any match.
    ///
    /// `abs_row` is the absolute row (scrollback offset + grid row).
    /// Returns the match state for that cell position.
    pub fn cell_match_state(&self, abs_row: usize, col: usize) -> CellMatchState {
        for (i, m) in self.matches.iter().enumerate() {
            if m.row == abs_row && col >= m.col_start && col < m.col_end {
                return if self.active == Some(i) {
                    CellMatchState::Active
                } else {
                    CellMatchState::Highlighted
                };
            }
        }
        CellMatchState::None
    }
}

impl Default for SearchEngine {
    fn default() -> Self {
        Self::new()
    }
}

/// Match state for a single cell — used by the renderer for coloring.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CellMatchState {
    /// Cell is not part of any match.
    None,
    /// Cell is part of a match (but not the active one).
    Highlighted,
    /// Cell is part of the currently focused (active) match.
    Active,
}

/// Extract the text content of a row by absolute index.
///
/// Rows 0..scrollback_len are from the scrollback buffer,
/// rows scrollback_len.. are from the active grid.
fn extract_row_text(terminal: &Terminal, abs_row: usize, scrollback_len: usize) -> String {
    if abs_row < scrollback_len {
        // Scrollback row
        if let Some(cells) = terminal.scrollback.get(abs_row) {
            row_cells_to_string(cells)
        } else {
            String::new()
        }
    } else {
        // Grid row
        let grid_row = abs_row - scrollback_len;
        if let Some(row) = terminal.grid.row(grid_row) {
            row_cells_to_string(row)
        } else {
            String::new()
        }
    }
}

/// Convert a slice of cells to a string, trimming trailing whitespace.
fn row_cells_to_string(cells: &[Cell]) -> String {
    let mut s = String::new();
    for cell in cells {
        if cell.width > 0 {
            s.push_str(&cell.content);
        }
    }
    s.trim_end().to_string()
}

/// Convert a byte offset in a string to a column (character) index.
///
/// This is needed because terminal columns are character-based but string
/// searching returns byte offsets.
fn char_col_at_byte(s: &str, byte_offset: usize) -> usize {
    s[..byte_offset].chars().count()
}

#[cfg(test)]
mod tests {
    use super::*;
    use wixen_core::Terminal;

    fn make_terminal_with_text(lines: &[&str]) -> Terminal {
        let cols = lines
            .iter()
            .map(|l| l.chars().count())
            .max()
            .unwrap_or(80)
            .max(10);
        let rows = lines.len().max(1);
        let mut term = Terminal::new(cols, rows);
        for (row_idx, line) in lines.iter().enumerate() {
            for (col_idx, ch) in line.chars().enumerate() {
                if let Some(cell) = term.grid.cell_mut(col_idx, row_idx) {
                    cell.content.clear();
                    cell.content.push(ch);
                }
            }
        }
        term
    }

    #[test]
    fn test_plain_search_case_insensitive() {
        let term = make_terminal_with_text(&["Hello World", "hello again", "nothing here"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "hello", SearchOptions::default());
        assert_eq!(engine.match_count(), 2);
        assert_eq!(engine.matches[0].row, 0);
        assert_eq!(engine.matches[0].col_start, 0);
        assert_eq!(engine.matches[0].col_end, 5);
        assert_eq!(engine.matches[1].row, 1);
    }

    #[test]
    fn test_plain_search_case_sensitive() {
        let term = make_terminal_with_text(&["Hello World", "hello again"]);
        let mut engine = SearchEngine::new();
        engine.search(
            &term,
            "Hello",
            SearchOptions {
                case_sensitive: true,
                ..Default::default()
            },
        );
        assert_eq!(engine.match_count(), 1);
        assert_eq!(engine.matches[0].row, 0);
    }

    #[test]
    fn test_regex_search() {
        let term = make_terminal_with_text(&[
            "error: file not found",
            "warning: unused variable",
            "error: type mismatch",
        ]);
        let mut engine = SearchEngine::new();
        engine.search(
            &term,
            r"error:.*",
            SearchOptions {
                regex: true,
                ..Default::default()
            },
        );
        assert_eq!(engine.match_count(), 2);
        assert_eq!(engine.matches[0].row, 0);
        assert_eq!(engine.matches[1].row, 2);
    }

    #[test]
    fn test_next_match_wraps() {
        let term = make_terminal_with_text(&["aaa bbb aaa", "ccc aaa ddd"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "aaa", SearchOptions::default());
        assert_eq!(engine.match_count(), 3);

        // Navigate forward through all matches and wrap
        assert_eq!(engine.active_match().unwrap().row, 0);
        engine.next_match(SearchDirection::Forward);
        assert_eq!(engine.active_match().unwrap().col_start, 8); // second "aaa" in row 0
        engine.next_match(SearchDirection::Forward);
        assert_eq!(engine.active_match().unwrap().row, 1); // "aaa" in row 1
        engine.next_match(SearchDirection::Forward);
        assert_eq!(engine.active_match().unwrap().row, 0); // wrapped back to start
        assert_eq!(engine.active_match().unwrap().col_start, 0);
    }

    #[test]
    fn test_next_match_backward() {
        let term = make_terminal_with_text(&["foo bar foo"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "foo", SearchOptions::default());
        assert_eq!(engine.match_count(), 2);

        // Start at first match, go backward should wrap to last
        engine.next_match(SearchDirection::Backward);
        assert_eq!(engine.active_match().unwrap().col_start, 8); // last "foo"
    }

    #[test]
    fn test_no_wrap_around() {
        let term = make_terminal_with_text(&["abc def abc"]);
        let mut engine = SearchEngine::new();
        engine.search(
            &term,
            "abc",
            SearchOptions {
                wrap_around: false,
                ..Default::default()
            },
        );
        assert_eq!(engine.match_count(), 2);

        // Go forward to last match
        engine.next_match(SearchDirection::Forward);
        // Try to go forward again — should stay at last
        engine.next_match(SearchDirection::Forward);
        assert_eq!(engine.active_match().unwrap().col_start, 8);
    }

    #[test]
    fn test_match_status() {
        let term = make_terminal_with_text(&["test test test"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "test", SearchOptions::default());
        assert_eq!(engine.match_status(), Some((1, 3))); // "1 of 3"
        engine.next_match(SearchDirection::Forward);
        assert_eq!(engine.match_status(), Some((2, 3))); // "2 of 3"
    }

    #[test]
    fn test_cell_match_state() {
        let term = make_terminal_with_text(&["hello world"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "world", SearchOptions::default());
        assert_eq!(engine.match_count(), 1);

        // Active match is at cols 6..11
        assert_eq!(engine.cell_match_state(0, 5), CellMatchState::None);
        assert_eq!(engine.cell_match_state(0, 6), CellMatchState::Active);
        assert_eq!(engine.cell_match_state(0, 10), CellMatchState::Active);
        assert_eq!(engine.cell_match_state(0, 11), CellMatchState::None);
    }

    #[test]
    fn test_empty_query() {
        let term = make_terminal_with_text(&["hello"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "", SearchOptions::default());
        assert_eq!(engine.match_count(), 0);
        assert!(!engine.is_active());
    }

    #[test]
    fn test_invalid_regex() {
        let term = make_terminal_with_text(&["hello"]);
        let mut engine = SearchEngine::new();
        engine.search(
            &term,
            "[invalid",
            SearchOptions {
                regex: true,
                ..Default::default()
            },
        );
        assert_eq!(engine.match_count(), 0);
    }

    #[test]
    fn test_clear() {
        let term = make_terminal_with_text(&["hello world"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "hello", SearchOptions::default());
        assert_eq!(engine.match_count(), 1);

        engine.clear();
        assert_eq!(engine.match_count(), 0);
        assert!(!engine.is_active());
        assert!(engine.active_match().is_none());
    }

    #[test]
    fn test_multiple_matches_same_row() {
        let term = make_terminal_with_text(&["ababab"]);
        let mut engine = SearchEngine::new();
        engine.search(&term, "ab", SearchOptions::default());
        assert_eq!(engine.match_count(), 3);
        assert_eq!(engine.matches[0].col_start, 0);
        assert_eq!(engine.matches[1].col_start, 2);
        assert_eq!(engine.matches[2].col_start, 4);
    }

    #[test]
    fn test_status_text() {
        let term = make_terminal_with_text(&["hello world hello"]);
        let mut engine = SearchEngine::new();

        // Empty query
        assert_eq!(engine.status_text(), "");

        // With matches
        engine.search(&term, "hello", SearchOptions::default());
        assert_eq!(engine.status_text(), "1/2");

        engine.next_match(SearchDirection::Forward);
        assert_eq!(engine.status_text(), "2/2");

        // No results
        engine.search(&term, "zzz", SearchOptions::default());
        assert_eq!(engine.status_text(), "No results");
    }
}
