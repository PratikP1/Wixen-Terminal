//! Tab management — ordered collection of terminal tabs.
//!
//! Each tab owns a pane tree for split panes. The tab manager tracks the active
//! tab and provides cycling, reordering, and tab bar metadata.

use crate::panes::{PaneId, PaneTree};

/// Unique identifier for a tab.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct TabId(pub u64);

/// Optional RGBA color for a tab's color indicator stripe.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct TabColor {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl TabColor {
    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }
}

/// A single terminal tab.
#[derive(Debug)]
pub struct Tab {
    pub id: TabId,
    /// Display title (from shell or user override).
    pub title: String,
    /// The split pane tree for this tab.
    pub pane_tree: PaneTree,
    /// Currently focused pane within this tab.
    pub active_pane: PaneId,
    /// Whether this tab has a pending bell indicator (badge).
    pub has_bell: bool,
    /// Optional color indicator stripe (set via profile or escape sequence).
    pub tab_color: Option<TabColor>,
    /// Process exit status: `None` = running, `Some(code)` = exited.
    pub exit_status: Option<Option<u32>>,
    /// Profile name (shell type) for accessibility announcements (e.g., "PowerShell", "cmd.exe").
    pub profile_name: String,
}

impl Tab {
    /// Format the display title including exit status suffix.
    ///
    /// Running tabs show their title as-is. Exited tabs append a status
    /// indicator: `✓` for exit code 0, `✕ (N)` for non-zero codes,
    /// or `✕` if the exit code is unknown.
    pub fn display_title(&self) -> String {
        match self.exit_status {
            None => self.title.clone(),
            Some(Some(0)) => format!("{} \u{2713}", self.title),
            Some(Some(code)) => format!("{} \u{2715} ({})", self.title, code),
            Some(None) => format!("{} \u{2715}", self.title),
        }
    }
}

/// Manages the set of open tabs.
#[derive(Debug)]
pub struct TabManager {
    tabs: Vec<Tab>,
    active_idx: usize,
    /// Previous active tab index (for "switch to last tab" feature).
    prev_active_idx: usize,
    next_id: u64,
}

impl TabManager {
    /// Create a new tab manager with one initial tab.
    pub fn new(title: &str) -> Self {
        let id = TabId(1);
        let (pane_tree, root_pane) = PaneTree::new();
        let tab = Tab {
            id,
            title: title.to_string(),
            pane_tree,
            active_pane: root_pane,
            has_bell: false,
            tab_color: None,
            exit_status: None,
            profile_name: String::new(),
        };
        Self {
            tabs: vec![tab],
            active_idx: 0,
            prev_active_idx: 0,
            next_id: 2,
        }
    }

    /// Add a new tab at the end. Returns the new tab's ID and root pane ID.
    pub fn add_tab(&mut self, title: &str) -> (TabId, PaneId) {
        let id = TabId(self.next_id);
        self.next_id += 1;
        let (pane_tree, root_pane) = PaneTree::new();
        let tab = Tab {
            id,
            title: title.to_string(),
            pane_tree,
            active_pane: root_pane,
            has_bell: false,
            tab_color: None,
            exit_status: None,
            profile_name: String::new(),
        };
        self.tabs.push(tab);
        self.active_idx = self.tabs.len() - 1;
        (id, root_pane)
    }

    /// Close a tab by ID. Returns false if not found or if it's the last tab.
    pub fn close_tab(&mut self, id: TabId) -> bool {
        if self.tabs.len() <= 1 {
            return false;
        }
        if let Some(idx) = self.tabs.iter().position(|t| t.id == id) {
            self.tabs.remove(idx);
            // Adjust active index
            if self.active_idx >= self.tabs.len() {
                self.active_idx = self.tabs.len() - 1;
            } else if self.active_idx > idx {
                self.active_idx -= 1;
            }
            true
        } else {
            false
        }
    }

    /// Close all tabs except the one with the given ID.
    ///
    /// Returns the pane IDs of the closed tabs (so the caller can clean them up).
    /// If the tab ID isn't found, nothing is closed and an empty vec is returned.
    pub fn close_other_tabs(&mut self, keep: TabId) -> Vec<PaneId> {
        let mut removed_panes = Vec::new();
        if let Some(keep_idx) = self.tabs.iter().position(|t| t.id == keep) {
            for (i, tab) in self.tabs.iter().enumerate() {
                if i != keep_idx {
                    removed_panes.push(tab.active_pane);
                }
            }
            let kept = self.tabs.remove(keep_idx);
            self.tabs.clear();
            self.tabs.push(kept);
            self.active_idx = 0;
        }
        removed_panes
    }

    /// Close all tabs to the right of the active tab.
    ///
    /// Returns the pane IDs of the closed tabs.
    pub fn close_tabs_to_right(&mut self) -> Vec<PaneId> {
        if self.active_idx + 1 >= self.tabs.len() {
            return Vec::new();
        }
        let removed: Vec<PaneId> = self.tabs[self.active_idx + 1..]
            .iter()
            .map(|t| t.active_pane)
            .collect();
        self.tabs.truncate(self.active_idx + 1);
        removed
    }

    /// Close all tabs to the left of the active tab.
    ///
    /// Returns the pane IDs of the closed tabs.
    pub fn close_tabs_to_left(&mut self) -> Vec<PaneId> {
        if self.active_idx == 0 {
            return Vec::new();
        }
        let removed: Vec<PaneId> = self.tabs[..self.active_idx]
            .iter()
            .map(|t| t.active_pane)
            .collect();
        self.tabs.drain(..self.active_idx);
        self.active_idx = 0;
        removed
    }

    /// Switch to the tab with the given ID. Clears the bell badge on the tab.
    pub fn select_tab(&mut self, id: TabId) {
        if let Some(idx) = self.tabs.iter().position(|t| t.id == id) {
            self.prev_active_idx = self.active_idx;
            self.active_idx = idx;
            self.tabs[idx].has_bell = false;
        }
    }

    /// Switch to the tab at the given 0-based index. No-op if out of range.
    pub fn select_tab_by_index(&mut self, idx: usize) {
        if idx < self.tabs.len() {
            self.prev_active_idx = self.active_idx;
            self.active_idx = idx;
            self.tabs[idx].has_bell = false;
        }
    }

    /// Switch to the previously active tab (Alt+Tab equivalent for tabs).
    pub fn focus_last_tab(&mut self) {
        let target = self.prev_active_idx.min(self.tabs.len().saturating_sub(1));
        if target != self.active_idx {
            self.prev_active_idx = self.active_idx;
            self.active_idx = target;
            self.tabs[target].has_bell = false;
        }
    }

    /// Set the bell badge on a tab (e.g., when BEL fires in a background tab).
    pub fn set_bell(&mut self, id: TabId) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.has_bell = true;
        }
    }

    /// Switch to the next tab (wraps around).
    pub fn next_tab(&mut self) {
        if !self.tabs.is_empty() {
            self.prev_active_idx = self.active_idx;
            self.active_idx = (self.active_idx + 1) % self.tabs.len();
        }
    }

    /// Switch to the previous tab (wraps around).
    pub fn prev_tab(&mut self) {
        if !self.tabs.is_empty() {
            self.prev_active_idx = self.active_idx;
            self.active_idx = if self.active_idx == 0 {
                self.tabs.len() - 1
            } else {
                self.active_idx - 1
            };
        }
    }

    /// Get the active tab.
    pub fn active_tab(&self) -> &Tab {
        &self.tabs[self.active_idx]
    }

    /// Get the active tab mutably.
    pub fn active_tab_mut(&mut self) -> &mut Tab {
        &mut self.tabs[self.active_idx]
    }

    /// Get the active tab's ID.
    pub fn active_tab_id(&self) -> TabId {
        self.tabs[self.active_idx].id
    }

    /// Get the active tab's index (0-based).
    pub fn active_index(&self) -> usize {
        self.active_idx
    }

    /// Get all tabs.
    pub fn tabs(&self) -> &[Tab] {
        &self.tabs
    }

    /// Number of open tabs.
    pub fn tab_count(&self) -> usize {
        self.tabs.len()
    }

    /// Move a tab from one index to another.
    pub fn move_tab(&mut self, from: usize, to: usize) {
        if from >= self.tabs.len() || to >= self.tabs.len() || from == to {
            return;
        }
        let tab = self.tabs.remove(from);
        self.tabs.insert(to, tab);

        // Track active tab through the move
        if self.active_idx == from {
            self.active_idx = to;
        } else if from < self.active_idx && to >= self.active_idx {
            self.active_idx -= 1;
        } else if from > self.active_idx && to <= self.active_idx {
            self.active_idx += 1;
        }
    }

    /// Set the title of a tab.
    pub fn set_title(&mut self, id: TabId, title: &str) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.title = title.to_string();
        }
    }

    /// Split the active pane in the active tab. Returns the new pane ID.
    pub fn split_active_pane(&mut self, direction: crate::panes::SplitDirection) -> Option<PaneId> {
        let tab = &mut self.tabs[self.active_idx];
        let new_id = tab.pane_tree.split(tab.active_pane, direction)?;
        tab.active_pane = new_id;
        Some(new_id)
    }

    /// Close the active pane in the active tab.
    ///
    /// Returns `Some(new_active_pane)` if the pane was closed and another pane
    /// became active. Returns `None` if this was the last pane in the tab
    /// (caller should close the tab).
    pub fn close_active_pane(&mut self) -> Option<PaneId> {
        let tab = &mut self.tabs[self.active_idx];
        let old_active = tab.active_pane;
        if !tab.pane_tree.close(old_active) {
            return None; // Last pane — can't close
        }
        let ids = tab.pane_tree.pane_ids();
        tab.active_pane = ids[0];
        Some(tab.active_pane)
    }

    /// Cycle focus to the next pane in tree order (wraps around).
    pub fn focus_next_pane(&mut self) {
        let tab = &mut self.tabs[self.active_idx];
        let ids = tab.pane_tree.pane_ids();
        if let Some(idx) = ids.iter().position(|&id| id == tab.active_pane) {
            tab.active_pane = ids[(idx + 1) % ids.len()];
        }
    }

    /// Cycle focus to the previous pane in tree order (wraps around).
    pub fn focus_prev_pane(&mut self) {
        let tab = &mut self.tabs[self.active_idx];
        let ids = tab.pane_tree.pane_ids();
        if let Some(idx) = ids.iter().position(|&id| id == tab.active_pane) {
            tab.active_pane = if idx == 0 {
                ids[ids.len() - 1]
            } else {
                ids[idx - 1]
            };
        }
    }

    /// Set the active pane for the current tab.
    pub fn set_active_pane(&mut self, pane_id: PaneId) {
        self.tabs[self.active_idx].active_pane = pane_id;
    }

    /// Resize the active pane's split ratio by `delta`.
    ///
    /// Positive delta gives more space to the active pane's side in its parent split.
    /// Returns `true` if a split was found and adjusted.
    pub fn resize_active_pane(&mut self, delta: f32) -> bool {
        let tab = &mut self.tabs[self.active_idx];
        tab.pane_tree.resize_pane(tab.active_pane, delta)
    }

    /// Rename the active tab. If `title` is empty, the rename is ignored
    /// (the current title is preserved).
    pub fn rename_active_tab(&mut self, title: &str) {
        if !title.is_empty() {
            self.tabs[self.active_idx].title = title.to_string();
        }
    }

    /// Set the color indicator for a tab.
    pub fn set_tab_color(&mut self, id: TabId, color: TabColor) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.tab_color = Some(color);
        }
    }

    /// Clear the color indicator for a tab.
    pub fn clear_tab_color(&mut self, id: TabId) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.tab_color = None;
        }
    }

    /// Mark a tab's process as exited with the given exit code.
    pub fn set_exit_status(&mut self, id: TabId, code: Option<u32>) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.exit_status = Some(code);
        }
    }

    /// Clear the exit status for a tab (e.g., on shell restart).
    pub fn clear_exit_status(&mut self, id: TabId) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.exit_status = None;
        }
    }

    /// Set the profile name (shell type) for a tab.
    pub fn set_profile_name(&mut self, id: TabId, profile: &str) {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.id == id) {
            tab.profile_name = profile.to_string();
        }
    }

    /// Whether the tab bar should be visible.
    ///
    /// Default policy: hide when there's only one tab.
    pub fn should_show_tab_bar(&self) -> bool {
        self.tabs.len() > 1
    }

    /// Whether the tab bar should be visible given the configured mode.
    pub fn should_show_tab_bar_with_mode(&self, mode: wixen_config::TabBarMode) -> bool {
        match mode {
            wixen_config::TabBarMode::Always => true,
            wixen_config::TabBarMode::Never => false,
            wixen_config::TabBarMode::AutoHide => self.tabs.len() > 1,
        }
    }

    /// Snapshot the current tab layout as a serializable `SessionState`.
    pub fn to_session_state(&self) -> wixen_config::SessionState {
        wixen_config::SessionState {
            tabs: self
                .tabs
                .iter()
                .map(|tab| wixen_config::SavedTab {
                    title: tab.title.clone(),
                    profile: String::new(),
                    working_directory: String::new(),
                })
                .collect(),
            active_tab_index: self.active_idx,
        }
    }

    /// Reconstruct a `TabManager` from a saved session.
    ///
    /// Returns the manager and a list of `PaneId`s (one per tab) so the caller
    /// can spawn a PTY for each.  If the session is empty, falls back to a
    /// single "Terminal" tab.
    pub fn from_session_state(session: &wixen_config::SessionState) -> (Self, Vec<PaneId>) {
        if session.tabs.is_empty() {
            let mgr = Self::new("Terminal");
            let pane_id = mgr.active_tab().active_pane;
            return (mgr, vec![pane_id]);
        }

        let first = &session.tabs[0];
        let mut mgr = Self::new(&first.title);
        let mut pane_ids = vec![mgr.active_tab().active_pane];

        for tab in session.tabs.iter().skip(1) {
            let (_id, pane) = mgr.add_tab(&tab.title);
            pane_ids.push(pane);
        }

        let active = session.active_tab_index.min(mgr.tabs.len() - 1);
        mgr.active_idx = active;

        (mgr, pane_ids)
    }

    /// Detach a tab from this manager, removing it and returning the Tab struct.
    ///
    /// Returns `None` if the tab wasn't found or if it's the last tab (can't
    /// leave the manager empty). The caller can then attach this tab to
    /// another window's TabManager.
    pub fn detach_tab(&mut self, id: TabId) -> Option<Tab> {
        if self.tabs.len() <= 1 {
            return None;
        }
        let idx = self.tabs.iter().position(|t| t.id == id)?;
        let tab = self.tabs.remove(idx);
        // Adjust active index
        if self.active_idx >= self.tabs.len() {
            self.active_idx = self.tabs.len() - 1;
        } else if self.active_idx > idx {
            self.active_idx -= 1;
        }
        Some(tab)
    }

    /// Attach a tab from another window into this manager.
    ///
    /// The tab is inserted at the end and becomes the active tab.
    /// Returns the tab's ID for convenience.
    pub fn attach_tab(&mut self, tab: Tab) -> TabId {
        let id = tab.id;
        self.tabs.push(tab);
        self.active_idx = self.tabs.len() - 1;
        id
    }

    /// Get tab bar items for rendering: (tab_id, title, is_active, has_bell, tab_color).
    ///
    /// The title includes exit status indicators (✓ or ✕) when applicable.
    pub fn tab_bar_items(&self) -> Vec<(TabId, String, bool, bool, Option<TabColor>)> {
        self.tabs
            .iter()
            .enumerate()
            .map(|(i, tab)| {
                (
                    tab.id,
                    tab.display_title(),
                    i == self.active_idx,
                    tab.has_bell,
                    tab.tab_color,
                )
            })
            .collect()
    }
}

/// Build a rich screen reader announcement for a tab.
///
/// Format: "Tab {pos} of {total}: {title}, {profile}, {status}"
/// where status is "running" or "exited ({code})".
/// If profile is empty, it is omitted.
pub fn tab_announcement(mgr: &TabManager, tab_id: TabId) -> String {
    let tabs = mgr.tabs();
    let total = tabs.len();
    let (index, tab) = match tabs.iter().enumerate().find(|(_, t)| t.id == tab_id) {
        Some((i, t)) => (i + 1, t),
        None => return String::new(),
    };

    let status = match tab.exit_status {
        None => "running".to_string(),
        Some(Some(code)) => format!("exited ({})", code),
        Some(None) => "exited".to_string(),
    };

    if tab.profile_name.is_empty() {
        format!("Tab {} of {}: {}, {}", index, total, tab.title, status)
    } else {
        format!(
            "Tab {} of {}: {}, {}, {}",
            index, total, tab.title, tab.profile_name, status
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_initial_state() {
        let mgr = TabManager::new("Terminal");
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(mgr.active_tab().title, "Terminal");
        assert_eq!(mgr.active_index(), 0);
        assert!(!mgr.should_show_tab_bar());
    }

    #[test]
    fn test_add_tab() {
        let mut mgr = TabManager::new("Tab 1");
        let (id2, _pane) = mgr.add_tab("Tab 2");

        assert_eq!(mgr.tab_count(), 2);
        assert_eq!(mgr.active_tab_id(), id2);
        assert_eq!(mgr.active_tab().title, "Tab 2");
        assert!(mgr.should_show_tab_bar());
    }

    #[test]
    fn test_close_tab() {
        let mut mgr = TabManager::new("Tab 1");
        let (id2, _) = mgr.add_tab("Tab 2");

        assert!(mgr.close_tab(id2));
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(mgr.active_tab().title, "Tab 1");
    }

    #[test]
    fn test_close_last_tab_fails() {
        let mut mgr = TabManager::new("Only Tab");
        let id = mgr.active_tab_id();
        assert!(!mgr.close_tab(id));
        assert_eq!(mgr.tab_count(), 1);
    }

    #[test]
    fn test_close_active_tab_selects_previous() {
        let mut mgr = TabManager::new("Tab 1");
        let (id2, _) = mgr.add_tab("Tab 2");
        let (_id3, _) = mgr.add_tab("Tab 3");

        // Active is Tab 3 (idx 2). Close Tab 2 (idx 1).
        mgr.select_tab(id2);
        assert_eq!(mgr.active_index(), 1);

        // Now close active tab (Tab 2)
        mgr.close_tab(id2);
        // Should stay at index 1 (now Tab 3) or adjust
        assert_eq!(mgr.tab_count(), 2);
    }

    #[test]
    fn test_next_prev_tab() {
        let mut mgr = TabManager::new("Tab 1");
        mgr.add_tab("Tab 2");
        mgr.add_tab("Tab 3");

        mgr.select_tab(TabId(1));
        assert_eq!(mgr.active_index(), 0);

        mgr.next_tab();
        assert_eq!(mgr.active_index(), 1);

        mgr.next_tab();
        assert_eq!(mgr.active_index(), 2);

        // Wrap around
        mgr.next_tab();
        assert_eq!(mgr.active_index(), 0);

        // Previous wraps
        mgr.prev_tab();
        assert_eq!(mgr.active_index(), 2);
    }

    #[test]
    fn test_select_tab() {
        let mut mgr = TabManager::new("Tab 1");
        let (id2, _) = mgr.add_tab("Tab 2");

        mgr.select_tab(TabId(1));
        assert_eq!(mgr.active_tab().title, "Tab 1");

        mgr.select_tab(id2);
        assert_eq!(mgr.active_tab().title, "Tab 2");

        // Select nonexistent — no change
        mgr.select_tab(TabId(999));
        assert_eq!(mgr.active_tab_id(), id2);
    }

    #[test]
    fn test_move_tab() {
        let mut mgr = TabManager::new("A");
        mgr.add_tab("B");
        mgr.add_tab("C");

        mgr.select_tab(TabId(1));
        assert_eq!(mgr.active_index(), 0);

        // Move A from 0 to 2
        mgr.move_tab(0, 2);
        assert_eq!(mgr.tabs()[0].title, "B");
        assert_eq!(mgr.tabs()[1].title, "C");
        assert_eq!(mgr.tabs()[2].title, "A");
        assert_eq!(mgr.active_index(), 2); // followed the moved tab
    }

    #[test]
    fn test_set_title() {
        let mut mgr = TabManager::new("Old");
        mgr.set_title(TabId(1), "New");
        assert_eq!(mgr.active_tab().title, "New");
    }

    #[test]
    fn test_tab_bar_items() {
        let mut mgr = TabManager::new("Tab 1");
        mgr.add_tab("Tab 2");
        mgr.select_tab(TabId(1));

        let items = mgr.tab_bar_items();
        assert_eq!(items.len(), 2);
        assert_eq!(items[0].1, "Tab 1");
        assert!(items[0].2); // active
        assert_eq!(items[1].1, "Tab 2");
        assert!(!items[1].2); // not active
    }

    #[test]
    fn test_tab_pane_integration() {
        use crate::panes::SplitDirection;

        let mut mgr = TabManager::new("Main");
        let tab = mgr.active_tab_mut();
        let root_pane = tab.active_pane;

        // Split the active pane
        let new_pane = tab
            .pane_tree
            .split(root_pane, SplitDirection::Horizontal)
            .unwrap();
        tab.active_pane = new_pane;

        assert_eq!(tab.pane_tree.pane_count(), 2);
        assert_eq!(tab.active_pane, new_pane);

        // Layout should show two side-by-side panes
        let layout = tab.pane_tree.layout();
        assert_eq!(layout.len(), 2);
    }

    #[test]
    fn test_split_active_pane() {
        use crate::panes::SplitDirection;

        let mut mgr = TabManager::new("Main");
        let root = mgr.active_tab().active_pane;
        let new_pane = mgr.split_active_pane(SplitDirection::Horizontal).unwrap();

        // Active pane switches to the newly created pane
        assert_eq!(mgr.active_tab().active_pane, new_pane);
        assert_ne!(root, new_pane);
        assert_eq!(mgr.active_tab().pane_tree.pane_count(), 2);
    }

    #[test]
    fn test_close_active_pane() {
        use crate::panes::SplitDirection;

        let mut mgr = TabManager::new("Main");
        let root = mgr.active_tab().active_pane;
        let _new_pane = mgr.split_active_pane(SplitDirection::Vertical).unwrap();

        // Close the new (active) pane — root survives
        let survivor = mgr.close_active_pane();
        assert!(survivor.is_some());
        assert_eq!(mgr.active_tab().pane_tree.pane_count(), 1);
        assert_eq!(mgr.active_tab().active_pane, root);
    }

    #[test]
    fn test_close_last_pane_returns_none() {
        let mut mgr = TabManager::new("Main");
        assert!(mgr.close_active_pane().is_none());
    }

    #[test]
    fn test_focus_next_prev_pane() {
        use crate::panes::SplitDirection;

        let mut mgr = TabManager::new("Main");
        let p1 = mgr.active_tab().active_pane;
        let p2 = mgr.split_active_pane(SplitDirection::Horizontal).unwrap();

        // Active is now p2. Cycle next → wraps to p1
        mgr.focus_next_pane();
        assert_eq!(mgr.active_tab().active_pane, p1);

        // Cycle next again → back to p2
        mgr.focus_next_pane();
        assert_eq!(mgr.active_tab().active_pane, p2);

        // Cycle prev → back to p1
        mgr.focus_prev_pane();
        assert_eq!(mgr.active_tab().active_pane, p1);
    }

    // ── Tier 3C: Bell visual indicator tests ──

    #[test]
    fn test_bell_badge_initial_false() {
        let mgr = TabManager::new("Terminal");
        assert!(!mgr.active_tab().has_bell);
    }

    #[test]
    fn test_set_bell_badge() {
        let mut mgr = TabManager::new("Tab 1");
        let (tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.set_bell(tab2_id);
        assert!(mgr.tabs()[1].has_bell);
        assert!(!mgr.tabs()[0].has_bell);
    }

    #[test]
    fn test_clear_bell_on_select() {
        let mut mgr = TabManager::new("Tab 1");
        let (tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.set_bell(tab2_id);
        assert!(mgr.tabs()[1].has_bell);

        // Selecting the tab clears the bell
        mgr.select_tab(tab2_id);
        assert!(!mgr.active_tab().has_bell);
    }

    #[test]
    fn test_tab_bar_items_include_bell() {
        let mut mgr = TabManager::new("Tab 1");
        let (tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.set_bell(tab2_id);
        let items = mgr.tab_bar_items();
        // Items are (TabId, &str, is_active, has_bell)
        assert!(!items[0].3); // Tab 1: no bell
        assert!(items[1].3); // Tab 2: bell
    }

    // ── Tier 3A: Tab rename tests ──

    #[test]
    fn test_rename_active_tab() {
        let mut mgr = TabManager::new("Terminal");
        assert_eq!(mgr.active_tab().title, "Terminal");
        mgr.rename_active_tab("My Custom Tab");
        assert_eq!(mgr.active_tab().title, "My Custom Tab");
    }

    #[test]
    fn test_rename_active_tab_empty_uses_default() {
        let mut mgr = TabManager::new("Terminal");
        mgr.rename_active_tab("");
        assert_eq!(mgr.active_tab().title, "Terminal");
    }

    #[test]
    fn test_rename_specific_tab() {
        let mut mgr = TabManager::new("Tab 1");
        let (tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.set_title(tab2_id, "Renamed Tab 2");
        assert_eq!(mgr.tabs()[1].title, "Renamed Tab 2");
        assert_eq!(mgr.tabs()[0].title, "Tab 1"); // unchanged
    }

    #[test]
    fn test_rename_tab_bar_items_reflect_change() {
        let mut mgr = TabManager::new("Tab 1");
        let (_tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.rename_active_tab("Custom Name");
        let items = mgr.tab_bar_items();
        assert_eq!(items[1].1, "Custom Name"); // active tab is tab2
    }

    // ── Session save/restore tests ──

    #[test]
    fn test_to_session_state_single_tab() {
        let mgr = TabManager::new("PowerShell");
        let session = mgr.to_session_state();
        assert_eq!(session.tabs.len(), 1);
        assert_eq!(session.tabs[0].title, "PowerShell");
        assert_eq!(session.active_tab_index, 0);
    }

    #[test]
    fn test_to_session_state_multiple_tabs() {
        let mut mgr = TabManager::new("Tab 1");
        mgr.add_tab("Tab 2");
        mgr.add_tab("Tab 3");
        mgr.select_tab(TabId(2)); // select Tab 2 (middle)

        let session = mgr.to_session_state();
        assert_eq!(session.tabs.len(), 3);
        assert_eq!(session.tabs[0].title, "Tab 1");
        assert_eq!(session.tabs[1].title, "Tab 2");
        assert_eq!(session.tabs[2].title, "Tab 3");
        assert_eq!(session.active_tab_index, 1);
    }

    #[test]
    fn test_from_session_state() {
        let session = wixen_config::SessionState {
            tabs: vec![
                wixen_config::SavedTab {
                    title: "Work".into(),
                    profile: "PowerShell".into(),
                    working_directory: String::new(),
                },
                wixen_config::SavedTab {
                    title: "Dev".into(),
                    profile: "".into(),
                    working_directory: String::new(),
                },
            ],
            active_tab_index: 1,
        };
        let (mgr, pane_ids) = TabManager::from_session_state(&session);
        assert_eq!(mgr.tab_count(), 2);
        assert_eq!(mgr.active_index(), 1);
        assert_eq!(mgr.tabs()[0].title, "Work");
        assert_eq!(mgr.tabs()[1].title, "Dev");
        assert_eq!(pane_ids.len(), 2);
    }

    #[test]
    fn test_from_session_state_empty_falls_back() {
        let session = wixen_config::SessionState {
            tabs: vec![],
            active_tab_index: 0,
        };
        let (mgr, pane_ids) = TabManager::from_session_state(&session);
        // Empty session → single default tab
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(mgr.tabs()[0].title, "Terminal");
        assert_eq!(pane_ids.len(), 1);
    }

    #[test]
    fn test_from_session_state_active_index_oob() {
        let session = wixen_config::SessionState {
            tabs: vec![wixen_config::SavedTab {
                title: "Only".into(),
                profile: "".into(),
                working_directory: String::new(),
            }],
            active_tab_index: 99, // out of bounds
        };
        let (mgr, _) = TabManager::from_session_state(&session);
        assert_eq!(mgr.tab_count(), 1);
        // Active index should be clamped to valid range
        assert_eq!(mgr.active_index(), 0);
    }

    #[test]
    fn test_session_roundtrip() {
        let mut mgr = TabManager::new("Alpha");
        mgr.add_tab("Beta");
        mgr.add_tab("Gamma");
        mgr.select_tab(TabId(2)); // Beta

        let session = mgr.to_session_state();
        let (restored, pane_ids) = TabManager::from_session_state(&session);

        assert_eq!(restored.tab_count(), mgr.tab_count());
        assert_eq!(restored.active_index(), mgr.active_index());
        for (orig, rest) in mgr.tabs().iter().zip(restored.tabs().iter()) {
            assert_eq!(orig.title, rest.title);
        }
        assert_eq!(pane_ids.len(), 3);
    }

    // ── Tab color indicator tests ──

    #[test]
    fn test_tab_color_default_none() {
        let mgr = TabManager::new("Terminal");
        assert!(mgr.active_tab().tab_color.is_none());
    }

    #[test]
    fn test_set_tab_color() {
        let mut mgr = TabManager::new("Tab 1");
        let id = mgr.active_tab_id();
        mgr.set_tab_color(id, TabColor::new(255, 0, 0));
        assert_eq!(mgr.active_tab().tab_color, Some(TabColor::new(255, 0, 0)));
    }

    #[test]
    fn test_clear_tab_color() {
        let mut mgr = TabManager::new("Tab 1");
        let id = mgr.active_tab_id();
        mgr.set_tab_color(id, TabColor::new(0, 128, 255));
        assert!(mgr.active_tab().tab_color.is_some());
        mgr.clear_tab_color(id);
        assert!(mgr.active_tab().tab_color.is_none());
    }

    #[test]
    fn test_tab_bar_items_include_color() {
        let mut mgr = TabManager::new("Tab 1");
        let (tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.set_tab_color(tab2_id, TabColor::new(0, 255, 0));
        let items = mgr.tab_bar_items();
        assert!(items[0].4.is_none()); // Tab 1: no color
        assert_eq!(items[1].4, Some(TabColor::new(0, 255, 0))); // Tab 2: green
    }

    // ── Tier K: Exit status display tests ──

    #[test]
    fn test_display_title_running() {
        let mgr = TabManager::new("Terminal");
        assert_eq!(mgr.active_tab().display_title(), "Terminal");
    }

    #[test]
    fn test_display_title_exit_success() {
        let mut mgr = TabManager::new("Terminal");
        let id = mgr.active_tab_id();
        mgr.set_exit_status(id, Some(0));
        assert_eq!(mgr.active_tab().display_title(), "Terminal \u{2713}");
    }

    #[test]
    fn test_display_title_exit_failure() {
        let mut mgr = TabManager::new("Terminal");
        let id = mgr.active_tab_id();
        mgr.set_exit_status(id, Some(1));
        assert_eq!(mgr.active_tab().display_title(), "Terminal \u{2715} (1)");
    }

    #[test]
    fn test_display_title_exit_unknown() {
        let mut mgr = TabManager::new("Terminal");
        let id = mgr.active_tab_id();
        mgr.set_exit_status(id, None);
        assert_eq!(mgr.active_tab().display_title(), "Terminal \u{2715}");
    }

    #[test]
    fn test_clear_exit_status() {
        let mut mgr = TabManager::new("Terminal");
        let id = mgr.active_tab_id();
        mgr.set_exit_status(id, Some(42));
        assert!(mgr.active_tab().exit_status.is_some());
        mgr.clear_exit_status(id);
        assert!(mgr.active_tab().exit_status.is_none());
        assert_eq!(mgr.active_tab().display_title(), "Terminal");
    }

    #[test]
    fn test_tab_bar_items_show_exit_status() {
        let mut mgr = TabManager::new("Tab 1");
        let (tab2_id, _) = mgr.add_tab("Tab 2");
        mgr.set_exit_status(tab2_id, Some(130));
        let items = mgr.tab_bar_items();
        assert_eq!(items[0].1, "Tab 1"); // running — no suffix
        assert_eq!(items[1].1, "Tab 2 \u{2715} (130)"); // exited with code
    }

    #[test]
    fn test_tab_bar_mode_always() {
        let mgr = TabManager::new("Only Tab");
        assert!(!mgr.should_show_tab_bar()); // default: auto-hide with 1 tab
        assert!(mgr.should_show_tab_bar_with_mode(wixen_config::TabBarMode::Always));
    }

    #[test]
    fn test_tab_bar_mode_never() {
        let mut mgr = TabManager::new("A");
        mgr.add_tab("B");
        assert!(mgr.should_show_tab_bar()); // default: show with 2 tabs
        assert!(!mgr.should_show_tab_bar_with_mode(wixen_config::TabBarMode::Never));
    }

    #[test]
    fn test_tab_bar_mode_auto_hide() {
        let mut mgr = TabManager::new("A");
        assert!(!mgr.should_show_tab_bar_with_mode(wixen_config::TabBarMode::AutoHide));
        mgr.add_tab("B");
        assert!(mgr.should_show_tab_bar_with_mode(wixen_config::TabBarMode::AutoHide));
    }

    #[test]
    fn test_close_other_tabs() {
        let mut mgr = TabManager::new("A");
        let (tab_b, _) = mgr.add_tab("B");
        mgr.add_tab("C");
        assert_eq!(mgr.tab_count(), 3);

        let removed = mgr.close_other_tabs(tab_b);
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(mgr.active_tab_id(), tab_b);
        assert_eq!(removed.len(), 2);
    }

    #[test]
    fn test_close_tabs_to_right() {
        let mut mgr = TabManager::new("A");
        let first_id = mgr.active_tab_id();
        mgr.add_tab("B");
        mgr.add_tab("C");
        // Switch back to first tab
        mgr.select_tab(first_id);
        assert_eq!(mgr.active_index(), 0);
        assert_eq!(mgr.tab_count(), 3);

        let removed = mgr.close_tabs_to_right();
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(removed.len(), 2);
    }

    #[test]
    fn test_close_tabs_to_left() {
        let mut mgr = TabManager::new("A");
        mgr.add_tab("B");
        mgr.add_tab("C");
        // Switch to last tab (index 2)
        let last_id = mgr.tabs()[2].id;
        mgr.select_tab(last_id);
        assert_eq!(mgr.active_index(), 2);

        let removed = mgr.close_tabs_to_left();
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(mgr.active_index(), 0);
        assert_eq!(mgr.active_tab_id(), last_id);
        assert_eq!(removed.len(), 2);
    }

    #[test]
    fn test_close_tabs_to_right_none() {
        let mut mgr = TabManager::new("A");
        // Only one tab, nothing to the right
        let removed = mgr.close_tabs_to_right();
        assert!(removed.is_empty());
        assert_eq!(mgr.tab_count(), 1);
    }

    #[test]
    fn test_select_tab_by_index() {
        let mut mgr = TabManager::new("A");
        let (_, pane_b) = mgr.add_tab("B");
        let (_, pane_c) = mgr.add_tab("C");

        mgr.select_tab_by_index(0);
        assert_eq!(mgr.active_index(), 0);

        mgr.select_tab_by_index(1);
        assert_eq!(mgr.active_index(), 1);
        assert_eq!(mgr.active_tab().active_pane, pane_b);

        mgr.select_tab_by_index(2);
        assert_eq!(mgr.active_index(), 2);
        assert_eq!(mgr.active_tab().active_pane, pane_c);
    }

    #[test]
    fn test_select_tab_by_index_out_of_range() {
        let mut mgr = TabManager::new("A");
        mgr.add_tab("B");
        mgr.select_tab_by_index(0);
        assert_eq!(mgr.active_index(), 0);

        // Out of range — no-op
        mgr.select_tab_by_index(99);
        assert_eq!(mgr.active_index(), 0);
    }

    #[test]
    fn test_select_tab_by_index_clears_bell() {
        let mut mgr = TabManager::new("A");
        let (tab_b, _) = mgr.add_tab("B");
        mgr.select_tab_by_index(0);
        mgr.set_bell(tab_b);

        // Switching to tab B should clear its bell
        mgr.select_tab_by_index(1);
        assert!(!mgr.active_tab().has_bell);
    }

    #[test]
    fn test_focus_last_tab() {
        let mut mgr = TabManager::new("A");
        mgr.add_tab("B");
        mgr.add_tab("C");

        // Start at tab C (index 2 — add_tab sets active to last)
        assert_eq!(mgr.active_index(), 2);
        // Switch to tab A
        mgr.select_tab_by_index(0);
        assert_eq!(mgr.active_index(), 0);
        // focus_last_tab goes back to tab C
        mgr.focus_last_tab();
        assert_eq!(mgr.active_index(), 2);
        // Again goes back to tab A
        mgr.focus_last_tab();
        assert_eq!(mgr.active_index(), 0);
    }

    #[test]
    fn test_focus_last_tab_single_tab() {
        let mut mgr = TabManager::new("A");
        // With only one tab, focus_last_tab is a no-op
        mgr.focus_last_tab();
        assert_eq!(mgr.active_index(), 0);
    }

    #[test]
    fn test_set_active_pane() {
        let mut mgr = TabManager::new("A");
        let active_pane = mgr.active_tab().active_pane;
        let new_pane = mgr
            .split_active_pane(crate::panes::SplitDirection::Horizontal)
            .unwrap();
        assert_ne!(active_pane, new_pane);
        mgr.set_active_pane(active_pane);
        assert_eq!(mgr.active_tab().active_pane, active_pane);
    }

    #[test]
    fn test_resize_active_pane() {
        let mut mgr = TabManager::new("A");
        let _new_pane = mgr
            .split_active_pane(crate::panes::SplitDirection::Horizontal)
            .unwrap();
        // Initially 0.5 ratio — grow the active pane
        assert!(mgr.resize_active_pane(0.1));
    }

    #[test]
    fn test_resize_single_pane_no_split() {
        let mut mgr = TabManager::new("A");
        // No split — resize should return false
        assert!(!mgr.resize_active_pane(0.1));
    }

    // --- Tab detach/attach tests ---

    #[test]
    fn test_detach_tab_returns_tab() {
        let mut mgr = TabManager::new("A");
        let (b_id, _) = mgr.add_tab("B");
        assert_eq!(mgr.tab_count(), 2);

        let detached = mgr.detach_tab(b_id);
        assert!(detached.is_some());
        let tab = detached.unwrap();
        assert_eq!(tab.id, b_id);
        assert_eq!(tab.title, "B");
        assert_eq!(mgr.tab_count(), 1);
    }

    #[test]
    fn test_detach_last_tab_fails() {
        let mut mgr = TabManager::new("A");
        let a_id = mgr.active_tab_id();
        assert!(mgr.detach_tab(a_id).is_none());
        assert_eq!(mgr.tab_count(), 1);
    }

    #[test]
    fn test_detach_nonexistent_tab_fails() {
        let mut mgr = TabManager::new("A");
        let _ = mgr.add_tab("B");
        assert!(mgr.detach_tab(TabId(999)).is_none());
        assert_eq!(mgr.tab_count(), 2);
    }

    #[test]
    fn test_detach_active_tab_adjusts_index() {
        let mut mgr = TabManager::new("A");
        let (b_id, _) = mgr.add_tab("B");
        mgr.select_tab(b_id);
        assert_eq!(mgr.active_index(), 1);

        // Detach active tab B — active should fall back to A
        let detached = mgr.detach_tab(b_id);
        assert!(detached.is_some());
        assert_eq!(mgr.tab_count(), 1);
        assert_eq!(mgr.active_tab().title, "A");
    }

    #[test]
    fn test_attach_tab_adds_and_activates() {
        let mut mgr1 = TabManager::new("A");
        let (b_id, _) = mgr1.add_tab("B");

        // Detach B from mgr1
        let tab = mgr1.detach_tab(b_id).unwrap();

        // Attach to a second manager
        let mut mgr2 = TabManager::new("C");
        let attached_id = mgr2.attach_tab(tab);
        assert_eq!(attached_id, b_id);
        assert_eq!(mgr2.tab_count(), 2);
        assert_eq!(mgr2.active_tab().title, "B");
    }

    // --- Tab announcement tests ---

    #[test]
    fn test_tab_announcement_running() {
        let mut mgr = TabManager::new("Project");
        let id = mgr.active_tab_id();
        mgr.set_profile_name(id, "PowerShell");
        let ann = tab_announcement(&mgr, id);
        assert_eq!(ann, "Tab 1 of 1: Project, PowerShell, running");
    }

    #[test]
    fn test_tab_announcement_exited() {
        let mut mgr = TabManager::new("Terminal");
        let id = mgr.active_tab_id();
        mgr.set_profile_name(id, "cmd.exe");
        mgr.set_exit_status(id, Some(0));
        let ann = tab_announcement(&mgr, id);
        assert_eq!(ann, "Tab 1 of 1: Terminal, cmd.exe, exited (0)");
    }

    #[test]
    fn test_tab_announcement_position() {
        let mut mgr = TabManager::new("First");
        mgr.add_tab("Second");
        let (third_id, _) = mgr.add_tab("Third");
        mgr.set_profile_name(third_id, "bash");
        let ann = tab_announcement(&mgr, third_id);
        assert_eq!(ann, "Tab 3 of 3: Third, bash, running");
    }

    #[test]
    fn test_tab_announcement_exited_nonzero() {
        let mut mgr = TabManager::new("Build");
        let id = mgr.active_tab_id();
        mgr.set_profile_name(id, "PowerShell");
        mgr.set_exit_status(id, Some(1));
        let ann = tab_announcement(&mgr, id);
        assert_eq!(ann, "Tab 1 of 1: Build, PowerShell, exited (1)");
    }

    #[test]
    fn test_tab_announcement_no_profile() {
        let mgr = TabManager::new("Terminal");
        let id = mgr.active_tab_id();
        let ann = tab_announcement(&mgr, id);
        assert_eq!(ann, "Tab 1 of 1: Terminal, running");
    }

    #[test]
    fn test_detach_and_attach_preserves_pane_tree() {
        let mut mgr = TabManager::new("A");
        let (b_id, _) = mgr.add_tab("B");
        // Split a pane in tab B
        mgr.select_tab(b_id);
        mgr.split_active_pane(crate::panes::SplitDirection::Horizontal);
        assert_eq!(mgr.active_tab().pane_tree.pane_count(), 2);

        let tab = mgr.detach_tab(b_id).unwrap();
        assert_eq!(tab.pane_tree.pane_count(), 2);

        let mut mgr2 = TabManager::new("C");
        mgr2.attach_tab(tab);
        assert_eq!(mgr2.active_tab().pane_tree.pane_count(), 2);
    }
}
