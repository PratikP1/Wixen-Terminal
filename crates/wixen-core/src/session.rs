//! Session state export/import for terminal persistence.
//!
//! Provides a serializable `SessionState` capturing the terminal grid content,
//! cursor position, and title. Used for restoring terminal sessions across restarts.

use serde::{Deserialize, Serialize};

/// Serializable terminal session state.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SessionState {
    /// Grid rows as text (one string per row, trailing spaces trimmed).
    pub grid_rows: Vec<String>,
    /// Scrollback text (most recent lines, text-only).
    pub scrollback_text: Vec<String>,
    /// Cursor column.
    pub cursor_col: usize,
    /// Cursor row.
    pub cursor_row: usize,
    /// Window/tab title.
    pub title: String,
    /// Working directory (from OSC 7).
    pub cwd: String,
    /// Number of columns at time of export.
    pub cols: usize,
    /// Number of rows at time of export.
    pub rows: usize,
}

/// Session restore mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
pub enum RestoreMode {
    /// Never restore sessions.
    #[default]
    Never,
    /// Always restore the last session on startup.
    Always,
    /// Ask the user whether to restore.
    Ask,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_session_state_serialize_roundtrip() {
        let state = SessionState {
            grid_rows: vec!["hello".to_string(), "world".to_string()],
            scrollback_text: vec!["old line 1".to_string()],
            cursor_col: 5,
            cursor_row: 1,
            title: "My Terminal".to_string(),
            cwd: "/home/user".to_string(),
            cols: 80,
            rows: 24,
        };
        let json = serde_json::to_string(&state).unwrap();
        let restored: SessionState = serde_json::from_str(&json).unwrap();
        assert_eq!(state, restored);
    }

    #[test]
    fn test_session_state_empty() {
        let state = SessionState {
            grid_rows: Vec::new(),
            scrollback_text: Vec::new(),
            cursor_col: 0,
            cursor_row: 0,
            title: String::new(),
            cwd: String::new(),
            cols: 80,
            rows: 24,
        };
        let json = serde_json::to_string(&state).unwrap();
        let restored: SessionState = serde_json::from_str(&json).unwrap();
        assert_eq!(state, restored);
    }

    #[test]
    fn test_restore_mode_default() {
        assert_eq!(RestoreMode::default(), RestoreMode::Never);
    }

    #[test]
    fn test_restore_mode_deserialize() {
        let modes = [
            ("\"never\"", RestoreMode::Never),
            ("\"always\"", RestoreMode::Always),
            ("\"ask\"", RestoreMode::Ask),
        ];
        for (json, expected) in modes {
            let mode: RestoreMode = serde_json::from_str(json).unwrap();
            assert_eq!(mode, expected);
        }
    }
}
