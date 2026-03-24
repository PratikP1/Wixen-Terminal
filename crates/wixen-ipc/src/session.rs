//! Session persistence — save and restore terminal session metadata.
//!
//! Sessions track the tab layout, pane tree structure, and shell profiles
//! so they can be restored after a restart. Terminal content (scrollback)
//! is NOT persisted — only the structural layout.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use tracing::debug;

/// Metadata for a single terminal session (one window).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SessionInfo {
    /// Unique session identifier.
    pub session_id: String,
    /// Window title at time of save.
    pub window_title: String,
    /// Tab layout (ordered list of tab snapshots).
    pub tabs: Vec<TabSnapshot>,
    /// Index of the active tab.
    pub active_tab_idx: usize,
    /// Session creation timestamp (Unix epoch seconds).
    pub created_at: u64,
    /// Last save timestamp (Unix epoch seconds).
    pub saved_at: u64,
}

/// Snapshot of a single tab's state.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TabSnapshot {
    /// Tab display title.
    pub title: String,
    /// Pane tree layout.
    pub pane_layout: PaneLayout,
    /// Index of the active pane within the layout.
    pub active_pane_idx: usize,
}

/// Serializable pane tree layout.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum PaneLayout {
    /// Single pane (leaf node).
    Leaf {
        /// Profile name used to spawn this pane's shell.
        profile: Option<String>,
        /// Working directory at time of save.
        cwd: Option<String>,
    },
    /// Split node with two children.
    Split {
        /// Split direction.
        direction: SplitDir,
        /// Ratio of space allocated to the first child (0.0..1.0).
        ratio: f32,
        /// First child.
        first: Box<PaneLayout>,
        /// Second child.
        second: Box<PaneLayout>,
    },
}

/// Split direction for pane layouts.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum SplitDir {
    Horizontal,
    Vertical,
}

/// Persistent store for session files.
///
/// Sessions are saved as JSON files in a configurable directory.
pub struct SessionStore {
    dir: PathBuf,
}

impl SessionStore {
    /// Create a session store backed by the given directory.
    pub fn new(dir: PathBuf) -> Self {
        Self { dir }
    }

    /// Create a session store using the default directory.
    ///
    /// Default: `%APPDATA%\wixen\sessions\` (or `<portable_root>\sessions\`).
    pub fn default_dir() -> PathBuf {
        if let Some(appdata) = std::env::var_os("APPDATA") {
            PathBuf::from(appdata).join("wixen").join("sessions")
        } else {
            PathBuf::from("sessions")
        }
    }

    /// Ensure the session directory exists.
    pub fn ensure_dir(&self) -> std::io::Result<()> {
        std::fs::create_dir_all(&self.dir)
    }

    /// Save a session to disk.
    pub fn save(&self, session: &SessionInfo) -> std::io::Result<()> {
        self.ensure_dir()?;
        let path = self.session_path(&session.session_id);
        let json = serde_json::to_string_pretty(session)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?;
        std::fs::write(&path, json)?;
        debug!(session_id = %session.session_id, path = %path.display(), "Session saved");
        Ok(())
    }

    /// Load a session from disk.
    pub fn load(&self, session_id: &str) -> std::io::Result<SessionInfo> {
        let path = self.session_path(session_id);
        let json = std::fs::read_to_string(&path)?;
        let session: SessionInfo = serde_json::from_str(&json)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?;
        debug!(session_id, path = %path.display(), "Session loaded");
        Ok(session)
    }

    /// Delete a session from disk.
    pub fn delete(&self, session_id: &str) -> std::io::Result<()> {
        let path = self.session_path(session_id);
        if path.exists() {
            std::fs::remove_file(&path)?;
            debug!(session_id, "Session deleted");
        }
        Ok(())
    }

    /// List all saved session IDs.
    pub fn list(&self) -> std::io::Result<Vec<String>> {
        if !self.dir.exists() {
            return Ok(Vec::new());
        }
        let mut ids = Vec::new();
        for entry in std::fs::read_dir(&self.dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().is_some_and(|ext| ext == "json")
                && let Some(stem) = path.file_stem()
            {
                ids.push(stem.to_string_lossy().into_owned());
            }
        }
        ids.sort();
        Ok(ids)
    }

    /// Generate a new unique session ID.
    pub fn generate_id() -> String {
        use std::sync::atomic::{AtomicU64, Ordering};
        use std::time::{SystemTime, UNIX_EPOCH};
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let ts = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis();
        let seq = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        format!("session-{ts}-{pid}-{seq}")
    }

    fn session_path(&self, session_id: &str) -> PathBuf {
        self.dir.join(format!("{session_id}.json"))
    }
}

impl PaneLayout {
    /// Count the total number of leaf panes in this layout.
    pub fn pane_count(&self) -> usize {
        match self {
            PaneLayout::Leaf { .. } => 1,
            PaneLayout::Split { first, second, .. } => first.pane_count() + second.pane_count(),
        }
    }

    /// Create a simple single-pane layout.
    pub fn single(profile: Option<String>, cwd: Option<String>) -> Self {
        PaneLayout::Leaf { profile, cwd }
    }

    /// Create a horizontal split.
    pub fn hsplit(first: PaneLayout, second: PaneLayout, ratio: f32) -> Self {
        PaneLayout::Split {
            direction: SplitDir::Horizontal,
            ratio,
            first: Box::new(first),
            second: Box::new(second),
        }
    }

    /// Create a vertical split.
    pub fn vsplit(first: PaneLayout, second: PaneLayout, ratio: f32) -> Self {
        PaneLayout::Split {
            direction: SplitDir::Vertical,
            ratio,
            first: Box::new(first),
            second: Box::new(second),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn sample_session() -> SessionInfo {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        SessionInfo {
            session_id: "test-session-1".into(),
            window_title: "Wixen Terminal".into(),
            tabs: vec![
                TabSnapshot {
                    title: "PowerShell".into(),
                    pane_layout: PaneLayout::single(
                        Some("PowerShell".into()),
                        Some("C:\\Users\\Dev".into()),
                    ),
                    active_pane_idx: 0,
                },
                TabSnapshot {
                    title: "Cargo Build".into(),
                    pane_layout: PaneLayout::hsplit(
                        PaneLayout::single(Some("PowerShell".into()), None),
                        PaneLayout::single(Some("cmd".into()), None),
                        0.5,
                    ),
                    active_pane_idx: 0,
                },
            ],
            active_tab_idx: 0,
            created_at: now,
            saved_at: now,
        }
    }

    #[test]
    fn test_session_info_serialization() {
        let session = sample_session();
        let json = serde_json::to_string_pretty(&session).unwrap();
        let deserialized: SessionInfo = serde_json::from_str(&json).unwrap();
        assert_eq!(session, deserialized);
    }

    #[test]
    fn test_pane_layout_count_single() {
        let layout = PaneLayout::single(None, None);
        assert_eq!(layout.pane_count(), 1);
    }

    #[test]
    fn test_pane_layout_count_split() {
        let layout = PaneLayout::hsplit(
            PaneLayout::single(None, None),
            PaneLayout::single(None, None),
            0.5,
        );
        assert_eq!(layout.pane_count(), 2);
    }

    #[test]
    fn test_pane_layout_count_nested() {
        let layout = PaneLayout::hsplit(
            PaneLayout::single(None, None),
            PaneLayout::vsplit(
                PaneLayout::single(None, None),
                PaneLayout::single(None, None),
                0.5,
            ),
            0.6,
        );
        assert_eq!(layout.pane_count(), 3);
    }

    #[test]
    fn test_session_store_save_load() {
        let dir = std::env::temp_dir().join("wixen-test-sessions-save-load");
        let _ = std::fs::remove_dir_all(&dir);
        let store = SessionStore::new(dir.clone());

        let session = sample_session();
        store.save(&session).unwrap();

        let loaded = store.load("test-session-1").unwrap();
        assert_eq!(session, loaded);

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_session_store_list() {
        let dir = std::env::temp_dir().join("wixen-test-sessions-list");
        let _ = std::fs::remove_dir_all(&dir);
        let store = SessionStore::new(dir.clone());

        // Empty directory
        assert!(store.list().unwrap().is_empty());

        // Save two sessions
        let mut s1 = sample_session();
        s1.session_id = "alpha".into();
        store.save(&s1).unwrap();

        let mut s2 = sample_session();
        s2.session_id = "beta".into();
        store.save(&s2).unwrap();

        let ids = store.list().unwrap();
        assert_eq!(ids, vec!["alpha", "beta"]);

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_session_store_delete() {
        let dir = std::env::temp_dir().join("wixen-test-sessions-delete");
        let _ = std::fs::remove_dir_all(&dir);
        let store = SessionStore::new(dir.clone());

        let session = sample_session();
        store.save(&session).unwrap();
        assert_eq!(store.list().unwrap().len(), 1);

        store.delete("test-session-1").unwrap();
        assert!(store.list().unwrap().is_empty());

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_session_store_load_missing() {
        let dir = std::env::temp_dir().join("wixen-test-sessions-missing");
        let _ = std::fs::remove_dir_all(&dir);
        let store = SessionStore::new(dir.clone());
        store.ensure_dir().unwrap();

        let err = store.load("nonexistent").unwrap_err();
        assert_eq!(err.kind(), std::io::ErrorKind::NotFound);

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_generate_id_unique() {
        let id1 = SessionStore::generate_id();
        let id2 = SessionStore::generate_id();
        assert_ne!(id1, id2);
        assert!(id1.starts_with("session-"));
    }

    #[test]
    fn test_default_dir_has_sessions() {
        let dir = SessionStore::default_dir();
        let dir_str = dir.to_string_lossy();
        assert!(dir_str.contains("sessions"));
    }

    #[test]
    fn test_tab_snapshot_serialization() {
        let tab = TabSnapshot {
            title: "My Tab".into(),
            pane_layout: PaneLayout::vsplit(
                PaneLayout::single(Some("pwsh".into()), Some("C:\\".into())),
                PaneLayout::single(Some("cmd".into()), None),
                0.7,
            ),
            active_pane_idx: 1,
        };
        let json = serde_json::to_string(&tab).unwrap();
        let deserialized: TabSnapshot = serde_json::from_str(&json).unwrap();
        assert_eq!(tab, deserialized);
    }

    #[test]
    fn test_split_dir_serialization() {
        let h = serde_json::to_string(&SplitDir::Horizontal).unwrap();
        let v = serde_json::to_string(&SplitDir::Vertical).unwrap();
        assert_eq!(h, "\"Horizontal\"");
        assert_eq!(v, "\"Vertical\"");
    }
}
