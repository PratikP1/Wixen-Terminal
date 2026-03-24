//! Split pane tree — recursive binary splits for terminal panes.
//!
//! Each pane is a leaf in a binary tree. Splits divide a pane into two children
//! (horizontal = left|right, vertical = top/bottom). Layout is computed as
//! normalized [0,1] rectangles, then scaled to pixel coordinates externally.

/// Unique identifier for a pane.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PaneId(pub u64);

/// Direction of a split.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SplitDirection {
    /// Left | Right
    Horizontal,
    /// Top / Bottom
    Vertical,
}

/// A node in the binary split tree.
#[derive(Debug)]
pub enum PaneNode {
    /// A terminal pane (leaf node).
    Leaf { id: PaneId },
    /// A split into two children.
    Split {
        direction: SplitDirection,
        /// Fraction of space given to the first child (0.0..1.0).
        ratio: f32,
        first: Box<PaneNode>,
        second: Box<PaneNode>,
    },
}

/// A pane's computed layout in normalized coordinates.
#[derive(Debug, Clone, PartialEq)]
pub struct PaneRect {
    pub pane_id: PaneId,
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

/// Per-pane state flags that don't affect the tree structure.
#[derive(Debug, Clone, Default)]
pub struct PaneState {
    /// Pane is in read-only mode — input is blocked.
    pub read_only: bool,
    /// Pane is in broadcast-receive mode — input typed in the active pane
    /// is also sent to all broadcast-enabled panes.
    pub broadcast: bool,
}

/// The pane tree for a single tab.
#[derive(Debug)]
pub struct PaneTree {
    root: PaneNode,
    next_id: u64,
    /// Per-pane state, keyed by PaneId.
    states: std::collections::HashMap<PaneId, PaneState>,
    /// When Some, the given pane is zoomed (maximized to fill the entire tab).
    /// All other panes remain in the tree but are hidden from layout.
    zoomed_pane: Option<PaneId>,
}

impl PaneTree {
    /// Create a new tree with a single root pane. Returns the tree and root pane ID.
    pub fn new() -> (Self, PaneId) {
        let id = PaneId(1);
        let mut states = std::collections::HashMap::new();
        states.insert(id, PaneState::default());
        let tree = Self {
            root: PaneNode::Leaf { id },
            next_id: 2,
            states,
            zoomed_pane: None,
        };
        (tree, id)
    }

    fn alloc_id(&mut self) -> PaneId {
        let id = PaneId(self.next_id);
        self.next_id += 1;
        id
    }

    /// Split a pane into two. Returns the new pane's ID, or None if `target` wasn't found.
    /// Splitting a zoomed pane unzooms first.
    pub fn split(&mut self, target: PaneId, direction: SplitDirection) -> Option<PaneId> {
        // Unzoom if splitting the zoomed pane
        if self.zoomed_pane == Some(target) {
            self.zoomed_pane = None;
        }
        let new_id = self.alloc_id();
        if split_node(&mut self.root, target, direction, new_id) {
            self.states.insert(new_id, PaneState::default());
            Some(new_id)
        } else {
            None
        }
    }

    /// Close a pane. Its sibling takes its parent's place.
    /// Returns false if the pane wasn't found or is the last pane.
    pub fn close(&mut self, target: PaneId) -> bool {
        let closed = close_node(&mut self.root, target);
        if closed {
            self.states.remove(&target);
            // Unzoom if closing the zoomed pane
            if self.zoomed_pane == Some(target) {
                self.zoomed_pane = None;
            }
        }
        closed
    }

    /// Compute layout rectangles for all leaf panes in normalized [0,1] space.
    ///
    /// When a pane is zoomed, only that pane is returned with full [0,0,1,1] rect.
    pub fn layout(&self) -> Vec<PaneRect> {
        if let Some(zoomed) = self.zoomed_pane {
            return vec![PaneRect {
                pane_id: zoomed,
                x: 0.0,
                y: 0.0,
                width: 1.0,
                height: 1.0,
            }];
        }
        let mut rects = Vec::new();
        layout_recursive(&self.root, 0.0, 0.0, 1.0, 1.0, &mut rects);
        rects
    }

    /// Count the number of leaf panes.
    pub fn pane_count(&self) -> usize {
        count_leaves(&self.root)
    }

    /// Collect all leaf pane IDs in tree order.
    pub fn pane_ids(&self) -> Vec<PaneId> {
        let mut ids = Vec::new();
        collect_ids(&self.root, &mut ids);
        ids
    }

    /// Adjust the split ratio of the split containing `target` pane.
    ///
    /// `delta` is added to the ratio (positive = give more space to first child,
    /// negative = give more to second). The ratio is clamped to [0.1, 0.9].
    /// Returns `true` if a split was found and adjusted.
    pub fn resize_pane(&mut self, target: PaneId, delta: f32) -> bool {
        resize_pane_node(&mut self.root, target, delta)
    }

    /// Find the pane at the given normalized point (x, y in [0,1]).
    ///
    /// Returns `None` if the point is outside the [0,1] range.
    pub fn pane_at_point(&self, x: f32, y: f32) -> Option<PaneId> {
        if !(0.0..=1.0).contains(&x) || !(0.0..=1.0).contains(&y) {
            return None;
        }
        pane_at_point_recursive(&self.root, 0.0, 0.0, 1.0, 1.0, x, y)
    }

    /// Find the next pane in the given direction from `from`.
    ///
    /// Navigates by layout position: for Horizontal direction, finds the
    /// nearest pane to the right (forward=true) or left (forward=false).
    /// For Vertical, finds nearest below (forward) or above (backward).
    pub fn find_adjacent(
        &self,
        from: PaneId,
        direction: SplitDirection,
        forward: bool,
    ) -> Option<PaneId> {
        let rects = self.layout();
        let current = rects.iter().find(|r| r.pane_id == from)?;

        let candidates: Vec<&PaneRect> = rects
            .iter()
            .filter(|r| r.pane_id != from)
            .filter(|r| match direction {
                SplitDirection::Horizontal => {
                    // Overlapping vertical range
                    let overlap_v = r.y < current.y + current.height && r.y + r.height > current.y;
                    if forward {
                        overlap_v && r.x >= current.x + current.width - 0.001
                    } else {
                        overlap_v && r.x + r.width <= current.x + 0.001
                    }
                }
                SplitDirection::Vertical => {
                    // Overlapping horizontal range
                    let overlap_h = r.x < current.x + current.width && r.x + r.width > current.x;
                    if forward {
                        overlap_h && r.y >= current.y + current.height - 0.001
                    } else {
                        overlap_h && r.y + r.height <= current.y + 0.001
                    }
                }
            })
            .collect();

        // Pick the nearest candidate
        candidates
            .into_iter()
            .min_by(|a, b| {
                let dist_a = match direction {
                    SplitDirection::Horizontal => {
                        if forward {
                            a.x - (current.x + current.width)
                        } else {
                            current.x - (a.x + a.width)
                        }
                    }
                    SplitDirection::Vertical => {
                        if forward {
                            a.y - (current.y + current.height)
                        } else {
                            current.y - (a.y + a.height)
                        }
                    }
                };
                let dist_b = match direction {
                    SplitDirection::Horizontal => {
                        if forward {
                            b.x - (current.x + current.width)
                        } else {
                            current.x - (b.x + b.width)
                        }
                    }
                    SplitDirection::Vertical => {
                        if forward {
                            b.y - (current.y + current.height)
                        } else {
                            current.y - (b.y + b.height)
                        }
                    }
                };
                dist_a
                    .abs()
                    .partial_cmp(&dist_b.abs())
                    .unwrap_or(std::cmp::Ordering::Equal)
            })
            .map(|r| r.pane_id)
    }
    // --- Zoom ----------------------------------------------------------------

    /// Toggle zoom on a pane. When zoomed, `layout()` returns only this pane
    /// filling the full [0,1] space. Other panes remain in the tree.
    /// Returns the new zoom state (true = zoomed, false = unzoomed).
    pub fn toggle_zoom(&mut self, pane: PaneId) -> bool {
        if self.zoomed_pane == Some(pane) {
            self.zoomed_pane = None;
            false
        } else {
            self.zoomed_pane = Some(pane);
            true
        }
    }

    /// Whether any pane is currently zoomed.
    pub fn is_zoomed(&self) -> bool {
        self.zoomed_pane.is_some()
    }

    /// The currently zoomed pane, if any.
    pub fn zoomed_pane(&self) -> Option<PaneId> {
        self.zoomed_pane
    }

    // --- Read-only mode ------------------------------------------------------

    /// Toggle read-only mode on a pane. Returns the new state.
    pub fn toggle_read_only(&mut self, pane: PaneId) -> bool {
        let state = self.states.entry(pane).or_default();
        state.read_only = !state.read_only;
        state.read_only
    }

    /// Check if a pane is read-only.
    pub fn is_read_only(&self, pane: PaneId) -> bool {
        self.states.get(&pane).is_some_and(|s| s.read_only)
    }

    // --- Broadcast input -----------------------------------------------------

    /// Toggle broadcast mode on a pane. Returns the new state.
    pub fn toggle_broadcast(&mut self, pane: PaneId) -> bool {
        let state = self.states.entry(pane).or_default();
        state.broadcast = !state.broadcast;
        state.broadcast
    }

    /// Check if a pane is in broadcast mode.
    pub fn is_broadcast(&self, pane: PaneId) -> bool {
        self.states.get(&pane).is_some_and(|s| s.broadcast)
    }

    /// Get all pane IDs that are in broadcast mode.
    pub fn broadcast_panes(&self) -> Vec<PaneId> {
        self.states
            .iter()
            .filter(|(_, s)| s.broadcast)
            .map(|(id, _)| *id)
            .collect()
    }

    /// Get the state for a pane.
    pub fn pane_state(&self, pane: PaneId) -> Option<&PaneState> {
        self.states.get(&pane)
    }
}

// --- Tree mutation helpers ---------------------------------------------------

/// Replace a leaf node with a split containing the original and a new pane.
fn split_node(
    node: &mut PaneNode,
    target: PaneId,
    direction: SplitDirection,
    new_id: PaneId,
) -> bool {
    match node {
        PaneNode::Leaf { id } if *id == target => {
            let original = PaneNode::Leaf { id: *id };
            let new_pane = PaneNode::Leaf { id: new_id };
            *node = PaneNode::Split {
                direction,
                ratio: 0.5,
                first: Box::new(original),
                second: Box::new(new_pane),
            };
            true
        }
        PaneNode::Split { first, second, .. } => {
            split_node(first, target, direction, new_id)
                || split_node(second, target, direction, new_id)
        }
        _ => false,
    }
}

/// Remove a leaf node, replacing its parent split with the surviving sibling.
fn close_node(node: &mut PaneNode, target: PaneId) -> bool {
    match node {
        PaneNode::Leaf { .. } => false,
        PaneNode::Split { first, second, .. } => {
            // Check if first child is the target
            if matches!(**first, PaneNode::Leaf { id } if id == target) {
                // Replace this split with the second child
                let survivor = std::mem::replace(second.as_mut(), PaneNode::Leaf { id: PaneId(0) });
                *node = survivor;
                return true;
            }
            // Check if second child is the target
            if matches!(**second, PaneNode::Leaf { id } if id == target) {
                let survivor = std::mem::replace(first.as_mut(), PaneNode::Leaf { id: PaneId(0) });
                *node = survivor;
                return true;
            }
            // Recurse
            close_node(first, target) || close_node(second, target)
        }
    }
}

/// Adjust the split ratio of the parent split that contains `target` as a child.
fn resize_pane_node(node: &mut PaneNode, target: PaneId, delta: f32) -> bool {
    match node {
        PaneNode::Leaf { .. } => false,
        PaneNode::Split {
            ratio,
            first,
            second,
            ..
        } => {
            // Check if first child IS the target leaf
            let first_is_target = matches!(**first, PaneNode::Leaf { id } if id == target);
            // Check if second child IS the target leaf
            let second_is_target = matches!(**second, PaneNode::Leaf { id } if id == target);

            if first_is_target || second_is_target {
                // Adjust ratio — positive delta favors first child
                let adj = if first_is_target { delta } else { -delta };
                *ratio = (*ratio + adj).clamp(0.1, 0.9);
                return true;
            }
            // Recurse into children
            resize_pane_node(first, target, delta) || resize_pane_node(second, target, delta)
        }
    }
}

// --- Hit testing ------------------------------------------------------------

fn pane_at_point_recursive(
    node: &PaneNode,
    rx: f32,
    ry: f32,
    rw: f32,
    rh: f32,
    px: f32,
    py: f32,
) -> Option<PaneId> {
    match node {
        PaneNode::Leaf { id } => {
            if px >= rx && px <= rx + rw && py >= ry && py <= ry + rh {
                Some(*id)
            } else {
                None
            }
        }
        PaneNode::Split {
            direction,
            ratio,
            first,
            second,
        } => match direction {
            SplitDirection::Horizontal => {
                let w1 = rw * ratio;
                if px < rx + w1 {
                    pane_at_point_recursive(first, rx, ry, w1, rh, px, py)
                } else {
                    pane_at_point_recursive(second, rx + w1, ry, rw - w1, rh, px, py)
                }
            }
            SplitDirection::Vertical => {
                let h1 = rh * ratio;
                if py < ry + h1 {
                    pane_at_point_recursive(first, rx, ry, rw, h1, px, py)
                } else {
                    pane_at_point_recursive(second, rx, ry + h1, rw, rh - h1, px, py)
                }
            }
        },
    }
}

// --- Layout computation -----------------------------------------------------

fn layout_recursive(node: &PaneNode, x: f32, y: f32, w: f32, h: f32, out: &mut Vec<PaneRect>) {
    match node {
        PaneNode::Leaf { id } => {
            out.push(PaneRect {
                pane_id: *id,
                x,
                y,
                width: w,
                height: h,
            });
        }
        PaneNode::Split {
            direction,
            ratio,
            first,
            second,
        } => match direction {
            SplitDirection::Horizontal => {
                let w1 = w * ratio;
                let w2 = w - w1;
                layout_recursive(first, x, y, w1, h, out);
                layout_recursive(second, x + w1, y, w2, h, out);
            }
            SplitDirection::Vertical => {
                let h1 = h * ratio;
                let h2 = h - h1;
                layout_recursive(first, x, y, w, h1, out);
                layout_recursive(second, x, y + h1, w, h2, out);
            }
        },
    }
}

fn count_leaves(node: &PaneNode) -> usize {
    match node {
        PaneNode::Leaf { .. } => 1,
        PaneNode::Split { first, second, .. } => count_leaves(first) + count_leaves(second),
    }
}

fn collect_ids(node: &PaneNode, ids: &mut Vec<PaneId>) {
    match node {
        PaneNode::Leaf { id } => ids.push(*id),
        PaneNode::Split { first, second, .. } => {
            collect_ids(first, ids);
            collect_ids(second, ids);
        }
    }
}

/// Build a spatial label like "Left pane, 1 of 3" for screen reader announcements.
///
/// Determines spatial position (left/right/top/bottom/center) from the pane's
/// layout rect, and the numeric index from tree-order among all panes.
pub fn pane_position_label(tree: &PaneTree, pane_id: PaneId) -> String {
    let rects = tree.layout();
    let total = rects.len();
    let ids = tree.pane_ids();

    if total == 1 {
        return "Pane 1 of 1".to_string();
    }

    let index = ids.iter().position(|&id| id == pane_id).unwrap_or(0) + 1;

    let rect = match rects.iter().find(|r| r.pane_id == pane_id) {
        Some(r) => r,
        None => return format!("Pane {} of {}", index, total),
    };

    let mut parts = Vec::new();

    // Vertical position
    if rect.y < 0.4 && rect.height < 0.6 {
        parts.push("Top");
    } else if rect.y > 0.4 {
        parts.push("Bottom");
    }

    // Horizontal position
    if rect.x < 0.4 && rect.width < 0.6 {
        parts.push("left");
    } else if rect.x > 0.4 {
        parts.push("right");
    }

    let spatial = if parts.is_empty() {
        String::new()
    } else if parts.len() == 1 {
        // Capitalize first part if it's "left" or "right" standing alone
        let s = parts[0];
        let mut chars = s.chars();
        match chars.next() {
            Some(c) => format!("{}{}", c.to_uppercase(), chars.as_str()),
            None => String::new(),
        }
    } else {
        // "Top-left", "Bottom-right", etc.
        format!("{}-{}", parts[0], parts[1])
    };

    if spatial.is_empty() {
        format!("Pane {} of {}", index, total)
    } else {
        format!("{} pane, {} of {}", spatial, index, total)
    }
}

// --- Tests -------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_single_pane_layout() {
        let (tree, root_id) = PaneTree::new();
        let rects = tree.layout();
        assert_eq!(rects.len(), 1);
        assert_eq!(rects[0].pane_id, root_id);
        assert!((rects[0].x - 0.0).abs() < f32::EPSILON);
        assert!((rects[0].width - 1.0).abs() < f32::EPSILON);
        assert!((rects[0].height - 1.0).abs() < f32::EPSILON);
    }

    #[test]
    fn test_horizontal_split() {
        let (mut tree, root_id) = PaneTree::new();
        let new_id = tree.split(root_id, SplitDirection::Horizontal).unwrap();

        assert_eq!(tree.pane_count(), 2);
        let rects = tree.layout();
        assert_eq!(rects.len(), 2);

        let left = rects.iter().find(|r| r.pane_id == root_id).unwrap();
        let right = rects.iter().find(|r| r.pane_id == new_id).unwrap();

        assert!((left.width - 0.5).abs() < f32::EPSILON);
        assert!((right.width - 0.5).abs() < f32::EPSILON);
        assert!((left.x - 0.0).abs() < f32::EPSILON);
        assert!((right.x - 0.5).abs() < f32::EPSILON);
        assert!((left.height - 1.0).abs() < f32::EPSILON);
    }

    #[test]
    fn test_vertical_split() {
        let (mut tree, root_id) = PaneTree::new();
        let new_id = tree.split(root_id, SplitDirection::Vertical).unwrap();

        let rects = tree.layout();
        let top = rects.iter().find(|r| r.pane_id == root_id).unwrap();
        let bottom = rects.iter().find(|r| r.pane_id == new_id).unwrap();

        assert!((top.height - 0.5).abs() < f32::EPSILON);
        assert!((bottom.height - 0.5).abs() < f32::EPSILON);
        assert!((bottom.y - 0.5).abs() < f32::EPSILON);
    }

    #[test]
    fn test_nested_split() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();
        let p3 = tree.split(p2, SplitDirection::Vertical).unwrap();

        assert_eq!(tree.pane_count(), 3);

        let rects = tree.layout();
        let r1 = rects.iter().find(|r| r.pane_id == p1).unwrap();
        let r2 = rects.iter().find(|r| r.pane_id == p2).unwrap();
        let r3 = rects.iter().find(|r| r.pane_id == p3).unwrap();

        // p1 is left half
        assert!((r1.width - 0.5).abs() < f32::EPSILON);
        assert!((r1.height - 1.0).abs() < f32::EPSILON);

        // p2 is top-right quarter
        assert!((r2.width - 0.5).abs() < f32::EPSILON);
        assert!((r2.height - 0.5).abs() < f32::EPSILON);
        assert!((r2.x - 0.5).abs() < f32::EPSILON);

        // p3 is bottom-right quarter
        assert!((r3.x - 0.5).abs() < f32::EPSILON);
        assert!((r3.y - 0.5).abs() < f32::EPSILON);
        assert!((r3.width - 0.5).abs() < f32::EPSILON);
        assert!((r3.height - 0.5).abs() < f32::EPSILON);
    }

    #[test]
    fn test_close_pane() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        assert_eq!(tree.pane_count(), 2);
        assert!(tree.close(p2));
        assert_eq!(tree.pane_count(), 1);

        // Surviving pane takes full space
        let rects = tree.layout();
        assert_eq!(rects.len(), 1);
        assert_eq!(rects[0].pane_id, p1);
        assert!((rects[0].width - 1.0).abs() < f32::EPSILON);
    }

    #[test]
    fn test_close_first_child() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        assert!(tree.close(p1));
        assert_eq!(tree.pane_count(), 1);

        let rects = tree.layout();
        assert_eq!(rects[0].pane_id, p2);
        assert!((rects[0].width - 1.0).abs() < f32::EPSILON);
    }

    #[test]
    fn test_close_last_pane_fails() {
        let (mut tree, root_id) = PaneTree::new();
        assert!(!tree.close(root_id));
        assert_eq!(tree.pane_count(), 1);
    }

    #[test]
    fn test_close_nonexistent() {
        let (mut tree, _) = PaneTree::new();
        assert!(!tree.close(PaneId(999)));
    }

    #[test]
    fn test_split_nonexistent() {
        let (mut tree, _) = PaneTree::new();
        assert!(
            tree.split(PaneId(999), SplitDirection::Horizontal)
                .is_none()
        );
    }

    #[test]
    fn test_pane_ids_order() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();
        let p3 = tree.split(p2, SplitDirection::Vertical).unwrap();

        let ids = tree.pane_ids();
        assert_eq!(ids, vec![p1, p2, p3]);
    }

    #[test]
    fn test_find_adjacent_horizontal() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        assert_eq!(
            tree.find_adjacent(p1, SplitDirection::Horizontal, true),
            Some(p2)
        );
        assert_eq!(
            tree.find_adjacent(p2, SplitDirection::Horizontal, false),
            Some(p1)
        );
        assert_eq!(
            tree.find_adjacent(p1, SplitDirection::Horizontal, false),
            None
        );
    }

    #[test]
    fn test_resize_pane_grow() {
        let (mut tree, p1) = PaneTree::new();
        let _p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        // Initial ratio is 0.5, grow p1 by 0.1
        assert!(tree.resize_pane(p1, 0.1));
        let rects = tree.layout();
        let left = rects.iter().find(|r| r.pane_id == p1).unwrap();
        assert!((left.width - 0.6).abs() < 0.01);
    }

    #[test]
    fn test_resize_pane_shrink() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        // Shrink p2 by 0.1 — gives more space to p1
        assert!(tree.resize_pane(p2, 0.1));
        let rects = tree.layout();
        let right = rects.iter().find(|r| r.pane_id == p2).unwrap();
        // p2 is second child: -delta applied, so ratio goes from 0.5 to 0.4
        // p2 width = 1.0 - 0.4 = 0.6
        assert!((right.width - 0.6).abs() < 0.01);
    }

    #[test]
    fn test_resize_pane_clamps() {
        let (mut tree, p1) = PaneTree::new();
        let _p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        // Try to grow p1 far beyond 0.9
        for _ in 0..20 {
            tree.resize_pane(p1, 0.1);
        }
        let rects = tree.layout();
        let left = rects.iter().find(|r| r.pane_id == p1).unwrap();
        assert!((left.width - 0.9).abs() < 0.01);
    }

    #[test]
    fn test_resize_single_pane_no_op() {
        let (mut tree, root_id) = PaneTree::new();
        assert!(!tree.resize_pane(root_id, 0.1));
    }

    #[test]
    fn test_find_adjacent_vertical() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Vertical).unwrap();

        assert_eq!(
            tree.find_adjacent(p1, SplitDirection::Vertical, true),
            Some(p2)
        );
        assert_eq!(
            tree.find_adjacent(p2, SplitDirection::Vertical, false),
            Some(p1)
        );
    }

    // --- pane_at_point tests ---

    #[test]
    fn test_pane_at_point_single() {
        let (tree, root_id) = PaneTree::new();
        assert_eq!(tree.pane_at_point(0.5, 0.5), Some(root_id));
        assert_eq!(tree.pane_at_point(0.0, 0.0), Some(root_id));
        assert_eq!(tree.pane_at_point(0.99, 0.99), Some(root_id));
    }

    #[test]
    fn test_pane_at_point_horizontal_split() {
        let (mut tree, root_id) = PaneTree::new();
        let new_id = tree.split(root_id, SplitDirection::Horizontal).unwrap();
        // Left pane occupies [0, 0.5), right pane [0.5, 1.0)
        assert_eq!(tree.pane_at_point(0.25, 0.5), Some(root_id));
        assert_eq!(tree.pane_at_point(0.75, 0.5), Some(new_id));
    }

    #[test]
    fn test_pane_at_point_vertical_split() {
        let (mut tree, root_id) = PaneTree::new();
        let new_id = tree.split(root_id, SplitDirection::Vertical).unwrap();
        // Top pane occupies [0, 0.5), bottom pane [0.5, 1.0)
        assert_eq!(tree.pane_at_point(0.5, 0.25), Some(root_id));
        assert_eq!(tree.pane_at_point(0.5, 0.75), Some(new_id));
    }

    #[test]
    fn test_pane_at_point_nested() {
        let (mut tree, root_id) = PaneTree::new();
        // Horizontal split: root_id (left) | p2 (right)
        let p2 = tree.split(root_id, SplitDirection::Horizontal).unwrap();
        // Vertical split on p2: p2 (top-right) | p3 (bottom-right)
        let p3 = tree.split(p2, SplitDirection::Vertical).unwrap();
        // Point in top-left quadrant → root_id
        assert_eq!(tree.pane_at_point(0.25, 0.25), Some(root_id));
        // Point in top-right quadrant → p2
        assert_eq!(tree.pane_at_point(0.75, 0.25), Some(p2));
        // Point in bottom-right quadrant → p3
        assert_eq!(tree.pane_at_point(0.75, 0.75), Some(p3));
    }

    #[test]
    fn test_pane_at_point_out_of_bounds() {
        let (tree, _) = PaneTree::new();
        assert_eq!(tree.pane_at_point(-0.1, 0.5), None);
        assert_eq!(tree.pane_at_point(0.5, -0.1), None);
        assert_eq!(tree.pane_at_point(1.1, 0.5), None);
        assert_eq!(tree.pane_at_point(0.5, 1.1), None);
    }

    // --- Zoom tests ---

    #[test]
    fn test_zoom_single_pane_fills_layout() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        // Before zoom: two panes
        assert_eq!(tree.layout().len(), 2);

        // Zoom p1: layout returns only p1 at full size
        assert!(tree.toggle_zoom(p1));
        assert!(tree.is_zoomed());
        assert_eq!(tree.zoomed_pane(), Some(p1));
        let rects = tree.layout();
        assert_eq!(rects.len(), 1);
        assert_eq!(rects[0].pane_id, p1);
        assert!((rects[0].width - 1.0).abs() < f32::EPSILON);
        assert!((rects[0].height - 1.0).abs() < f32::EPSILON);

        // Tree structure preserved: pane_count still 2
        assert_eq!(tree.pane_count(), 2);

        // Unzoom: back to two panes
        assert!(!tree.toggle_zoom(p1));
        assert!(!tree.is_zoomed());
        assert_eq!(tree.layout().len(), 2);
        // p2 still exists
        let rects = tree.layout();
        assert!(rects.iter().any(|r| r.pane_id == p2));
    }

    #[test]
    fn test_zoom_different_pane_switches() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        tree.toggle_zoom(p1);
        assert_eq!(tree.zoomed_pane(), Some(p1));

        // Zooming a different pane switches the zoom target
        tree.toggle_zoom(p2);
        assert_eq!(tree.zoomed_pane(), Some(p2));
        let rects = tree.layout();
        assert_eq!(rects.len(), 1);
        assert_eq!(rects[0].pane_id, p2);
    }

    #[test]
    fn test_close_zoomed_pane_unzooms() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        tree.toggle_zoom(p2);
        assert!(tree.is_zoomed());

        tree.close(p2);
        assert!(!tree.is_zoomed());
        assert_eq!(tree.pane_count(), 1);
    }

    #[test]
    fn test_split_zoomed_pane_unzooms() {
        let (mut tree, p1) = PaneTree::new();

        tree.toggle_zoom(p1);
        assert!(tree.is_zoomed());

        // Splitting a zoomed pane unzooms it
        let _p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();
        assert!(!tree.is_zoomed());
        assert_eq!(tree.layout().len(), 2);
    }

    // --- Read-only tests ---

    #[test]
    fn test_read_only_default_off() {
        let (tree, p1) = PaneTree::new();
        assert!(!tree.is_read_only(p1));
    }

    #[test]
    fn test_toggle_read_only() {
        let (mut tree, p1) = PaneTree::new();
        assert!(tree.toggle_read_only(p1)); // on
        assert!(tree.is_read_only(p1));
        assert!(!tree.toggle_read_only(p1)); // off
        assert!(!tree.is_read_only(p1));
    }

    #[test]
    fn test_read_only_per_pane() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        tree.toggle_read_only(p1);
        assert!(tree.is_read_only(p1));
        assert!(!tree.is_read_only(p2));
    }

    #[test]
    fn test_close_pane_cleans_state() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        tree.toggle_read_only(p2);
        tree.toggle_broadcast(p2);
        tree.close(p2);

        // State cleaned up
        assert!(tree.pane_state(p2).is_none());
    }

    // --- Broadcast tests ---

    #[test]
    fn test_broadcast_default_off() {
        let (tree, p1) = PaneTree::new();
        assert!(!tree.is_broadcast(p1));
        assert!(tree.broadcast_panes().is_empty());
    }

    #[test]
    fn test_toggle_broadcast() {
        let (mut tree, p1) = PaneTree::new();
        assert!(tree.toggle_broadcast(p1)); // on
        assert!(tree.is_broadcast(p1));
        assert!(!tree.toggle_broadcast(p1)); // off
        assert!(!tree.is_broadcast(p1));
    }

    #[test]
    fn test_broadcast_panes_returns_enabled() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();
        let p3 = tree.split(p2, SplitDirection::Vertical).unwrap();

        tree.toggle_broadcast(p1);
        tree.toggle_broadcast(p3);

        let bc = tree.broadcast_panes();
        assert_eq!(bc.len(), 2);
        assert!(bc.contains(&p1));
        assert!(bc.contains(&p3));
        assert!(!bc.contains(&p2));
    }

    #[test]
    fn test_new_pane_inherits_default_state() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        let state = tree.pane_state(p2).unwrap();
        assert!(!state.read_only);
        assert!(!state.broadcast);
    }

    // --- Pane position label tests ---

    #[test]
    fn test_pane_position_label_single() {
        let (tree, _) = PaneTree::new();
        let label = pane_position_label(&tree, PaneId(1));
        assert_eq!(label, "Pane 1 of 1");
    }

    #[test]
    fn test_pane_position_label_horizontal_split() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();

        let label1 = pane_position_label(&tree, p1);
        assert_eq!(label1, "Left pane, 1 of 2");

        let label2 = pane_position_label(&tree, p2);
        assert_eq!(label2, "Right pane, 2 of 2");
    }

    #[test]
    fn test_pane_position_label_vertical_split() {
        let (mut tree, p1) = PaneTree::new();
        let p2 = tree.split(p1, SplitDirection::Vertical).unwrap();

        let label1 = pane_position_label(&tree, p1);
        assert_eq!(label1, "Top pane, 1 of 2");

        let label2 = pane_position_label(&tree, p2);
        assert_eq!(label2, "Bottom pane, 2 of 2");
    }

    #[test]
    fn test_pane_position_label_four_pane_grid() {
        let (mut tree, p1) = PaneTree::new();
        // Horizontal split: p1 (left) | p2 (right)
        let p2 = tree.split(p1, SplitDirection::Horizontal).unwrap();
        // Vertical split on p1: p1 (top-left) | p3 (bottom-left)
        let p3 = tree.split(p1, SplitDirection::Vertical).unwrap();
        // Vertical split on p2: p2 (top-right) | p4 (bottom-right)
        let p4 = tree.split(p2, SplitDirection::Vertical).unwrap();

        let label1 = pane_position_label(&tree, p1);
        assert_eq!(label1, "Top-left pane, 1 of 4");

        let label3 = pane_position_label(&tree, p3);
        assert_eq!(label3, "Bottom-left pane, 2 of 4");

        let label2 = pane_position_label(&tree, p2);
        assert_eq!(label2, "Top-right pane, 3 of 4");

        let label4 = pane_position_label(&tree, p4);
        assert_eq!(label4, "Bottom-right pane, 4 of 4");
    }
}
