//! Session state persistence — saves/restores open tabs across launches.
//!
//! The session file is a small JSON document stored alongside the config file.
//! It captures which tabs were open (and which profile each used) so the
//! terminal can restore them on next launch.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use tracing::info;

/// A saved tab entry.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SavedTab {
    /// Tab title at time of save.
    pub title: String,
    /// Profile name used to spawn the tab (empty = default).
    pub profile: String,
    /// Working directory at time of save (empty = unknown).
    pub working_directory: String,
}

/// Serializable session state.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct SessionState {
    /// Saved tabs in order (left to right).
    pub tabs: Vec<SavedTab>,
    /// Index of the active tab at time of save.
    pub active_tab_index: usize,
}

impl SessionState {
    /// Create a new empty session state.
    pub fn new() -> Self {
        Self::default()
    }

    /// Load session state from a JSON file. Returns `None` if the file
    /// doesn't exist or can't be parsed.
    pub fn load(path: &Path) -> Option<Self> {
        let content = std::fs::read_to_string(path).ok()?;
        let state: SessionState = serde_json::from_str(&content).ok()?;
        if state.tabs.is_empty() {
            return None;
        }
        info!(tabs = state.tabs.len(), path = %path.display(), "Session restored");
        Some(state)
    }

    /// Save session state to a JSON file.
    pub fn save(&self, path: &Path) -> Result<(), std::io::Error> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let json = serde_json::to_string_pretty(self).map_err(std::io::Error::other)?;
        std::fs::write(path, json)?;
        info!(tabs = self.tabs.len(), path = %path.display(), "Session saved");
        Ok(())
    }

    /// Get the default session file path (sibling of config file).
    pub fn default_path(config_path: &Path) -> PathBuf {
        config_path.with_file_name("session.json")
    }

    /// Whether this session has any tabs worth restoring.
    pub fn is_empty(&self) -> bool {
        self.tabs.is_empty()
    }

    /// Check whether session restore is enabled given the config value.
    ///
    /// Returns `true` for `"always"`, `false` for `"never"` (or any unrecognized value).
    /// The `"ask"` mode also returns `true` — the UI layer is responsible for
    /// prompting the user before calling `load()`.
    pub fn should_restore(mode: &str) -> bool {
        matches!(mode, "always" | "ask")
    }

    /// Check whether session state should be saved on exit.
    ///
    /// Saving is enabled for both `"always"` and `"ask"` modes so that there
    /// is something to restore on next launch.
    pub fn should_save(mode: &str) -> bool {
        matches!(mode, "always" | "ask")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_session_state_default_empty() {
        let state = SessionState::new();
        assert!(state.is_empty());
        assert_eq!(state.active_tab_index, 0);
    }

    #[test]
    fn test_session_save_and_load() {
        let dir = std::env::temp_dir().join("wixen_session_test_save_load");
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("session.json");

        let state = SessionState {
            tabs: vec![
                SavedTab {
                    title: "PowerShell".into(),
                    profile: "PowerShell".into(),
                    working_directory: "C:\\Users".into(),
                },
                SavedTab {
                    title: "cmd".into(),
                    profile: "Command Prompt".into(),
                    working_directory: String::new(),
                },
            ],
            active_tab_index: 1,
        };

        state.save(&path).unwrap();
        let loaded = SessionState::load(&path).unwrap();
        assert_eq!(loaded, state);
        assert_eq!(loaded.tabs.len(), 2);
        assert_eq!(loaded.active_tab_index, 1);
        assert_eq!(loaded.tabs[0].title, "PowerShell");

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_session_load_missing_file() {
        let result = SessionState::load(Path::new("/nonexistent/session.json"));
        assert!(result.is_none());
    }

    #[test]
    fn test_session_load_empty_tabs() {
        let dir = std::env::temp_dir().join("wixen_session_test_empty");
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("session.json");

        let state = SessionState {
            tabs: vec![],
            active_tab_index: 0,
        };
        state.save(&path).unwrap();

        // Empty tabs → returns None (nothing to restore)
        let loaded = SessionState::load(&path);
        assert!(loaded.is_none());

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_session_load_corrupt_json() {
        let dir = std::env::temp_dir().join("wixen_session_test_corrupt");
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("session.json");

        std::fs::write(&path, "not valid json {{{").unwrap();
        let result = SessionState::load(&path);
        assert!(result.is_none());

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_session_default_path() {
        let config = Path::new("C:\\Users\\test\\.config\\wixen\\config.toml");
        let session = SessionState::default_path(config);
        assert_eq!(
            session,
            Path::new("C:\\Users\\test\\.config\\wixen\\session.json")
        );
    }

    #[test]
    fn test_should_restore_modes() {
        assert!(!SessionState::should_restore("never"));
        assert!(SessionState::should_restore("always"));
        assert!(SessionState::should_restore("ask"));
        assert!(!SessionState::should_restore("")); // unknown defaults to no
        assert!(!SessionState::should_restore("bogus"));
    }

    #[test]
    fn test_should_save_modes() {
        assert!(!SessionState::should_save("never"));
        assert!(SessionState::should_save("always"));
        assert!(SessionState::should_save("ask"));
        assert!(!SessionState::should_save(""));
    }

    #[test]
    fn test_session_serialize_roundtrip() {
        let state = SessionState {
            tabs: vec![SavedTab {
                title: "Tab 1".into(),
                profile: "Default".into(),
                working_directory: String::new(),
            }],
            active_tab_index: 0,
        };
        let json = serde_json::to_string(&state).unwrap();
        let parsed: SessionState = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed, state);
    }
}
