//! UIA child fragment providers for command blocks and text regions.
//!
//! Each command block in the accessibility tree becomes a navigable UIA element,
//! letting screen readers walk the document structure: command blocks contain
//! prompt, input, and output text regions as children.

use std::sync::Arc;

use parking_lot::RwLock;
use tracing::debug;
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::Gdi::ClientToScreen;
use windows::Win32::System::Com::SAFEARRAY;
use windows::Win32::System::Ole::{SafeArrayCreateVector, SafeArrayPutElement};
use windows::Win32::System::Variant::VARIANT;
use windows::Win32::UI::Accessibility::*;
use windows::core::*;

use crate::provider::TerminalA11yState;
use crate::tree::{A11yNode, NodeId, SemanticRole};

// --- Snapshot helpers --------------------------------------------------------

/// Extracted node metadata for navigation (avoids holding the RwLock during COM calls).
#[derive(Debug)]
pub(crate) struct NodeSnapshot {
    pub kind: NodeKind,
    pub parent_id: NodeId,
    pub prev_sibling: Option<NodeId>,
    pub next_sibling: Option<NodeId>,
    pub first_child: Option<NodeId>,
    pub last_child: Option<NodeId>,
}

#[derive(Debug)]
pub(crate) enum NodeKind {
    CommandBlock {
        name: String,
        shell_block_id: u64,
        is_error: bool,
    },
    TextRegion {
        text: String,
        role: SemanticRole,
    },
}

/// Search the tree for `target` and return a snapshot of its metadata.
pub(crate) fn snapshot_node(root: &A11yNode, target: NodeId) -> Option<NodeSnapshot> {
    snapshot_recursive(root, target)
}

fn snapshot_recursive(parent: &A11yNode, target: NodeId) -> Option<NodeSnapshot> {
    let children = parent.children();
    for (i, child) in children.iter().enumerate() {
        if child.id() == target {
            let prev = if i > 0 {
                Some(children[i - 1].id())
            } else {
                None
            };
            let next = if i + 1 < children.len() {
                Some(children[i + 1].id())
            } else {
                None
            };
            let cc = child.children();

            let kind = match child {
                A11yNode::CommandBlock {
                    name,
                    shell_block_id,
                    is_error,
                    ..
                } => NodeKind::CommandBlock {
                    name: name.clone(),
                    shell_block_id: *shell_block_id,
                    is_error: *is_error,
                },
                A11yNode::TextRegion { text, role, .. } => NodeKind::TextRegion {
                    text: text.clone(),
                    role: *role,
                },
                // Document and LiveRegion are not exposed as child fragments
                _ => return None,
            };

            return Some(NodeSnapshot {
                kind,
                parent_id: parent.id(),
                prev_sibling: prev,
                next_sibling: next,
                first_child: cc.first().map(|n| n.id()),
                last_child: cc.last().map(|n| n.id()),
            });
        }
        if let Some(snap) = snapshot_recursive(child, target) {
            return Some(snap);
        }
    }
    None
}

// --- ChildFragmentProvider ---------------------------------------------------

/// UIA fragment provider for child elements (command blocks and text regions).
///
/// Each instance wraps a `NodeId` and reads the shared `AccessibilityTree` to
/// resolve properties and navigation on demand.
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct ChildFragmentProvider {
    node_id: NodeId,
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl ChildFragmentProvider {
    pub fn new(
        node_id: NodeId,
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        root_provider: IRawElementProviderFragmentRoot,
    ) -> Self {
        Self {
            node_id,
            hwnd,
            state,
            root_provider,
        }
    }

    /// Create a provider for another node in the same tree.
    fn provider_for(&self, id: NodeId) -> IRawElementProviderFragment {
        let p = ChildFragmentProvider::new(
            id,
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        );
        p.into()
    }

    /// Take a snapshot of this node from the shared tree.
    fn snapshot(&self) -> Option<NodeSnapshot> {
        let state = self.state.read();
        snapshot_node(&state.tree.root, self.node_id)
    }
}

// --- IRawElementProviderSimple -----------------------------------------------

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for ChildFragmentProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        // Child fragments don't expose patterns (text pattern is on root)
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let snap = self.snapshot().ok_or(Error::empty())?;

        match property_id {
            UIA_ControlTypePropertyId => {
                let type_id = match &snap.kind {
                    NodeKind::CommandBlock { .. } => UIA_GroupControlTypeId.0,
                    NodeKind::TextRegion { .. } => UIA_TextControlTypeId.0,
                };
                Ok(VARIANT::from(type_id))
            }
            UIA_NamePropertyId => {
                let name = match &snap.kind {
                    NodeKind::CommandBlock { name, .. } => name.clone(),
                    NodeKind::TextRegion { text, role, .. } => match role {
                        SemanticRole::Prompt => format!("Prompt: {}", text),
                        SemanticRole::CommandInput => format!("Command: {}", text),
                        SemanticRole::ErrorText => format!("Error: {}", text),
                        SemanticRole::Path => format!("Path: {}", text),
                        SemanticRole::Url => format!("Link: {}", text),
                        SemanticRole::StatusLine => format!("Status: {}", text),
                        SemanticRole::OutputText => text.clone(),
                    },
                };
                Ok(VARIANT::from(BSTR::from(&name)))
            }
            UIA_AutomationIdPropertyId => {
                let aid = match &snap.kind {
                    NodeKind::CommandBlock { shell_block_id, .. } => {
                        format!("CommandBlock_{}", shell_block_id)
                    }
                    NodeKind::TextRegion { role, .. } => {
                        format!("{:?}_{}", role, self.node_id.0)
                    }
                };
                Ok(VARIANT::from(BSTR::from(&aid)))
            }
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_LocalizedControlTypePropertyId => {
                let label = match &snap.kind {
                    NodeKind::CommandBlock { is_error, .. } => {
                        if *is_error {
                            "failed command"
                        } else {
                            "command"
                        }
                    }
                    NodeKind::TextRegion { role, .. } => match role {
                        SemanticRole::Prompt => "prompt",
                        SemanticRole::CommandInput => "command input",
                        SemanticRole::OutputText => "output",
                        SemanticRole::ErrorText => "error output",
                        SemanticRole::Path => "path",
                        SemanticRole::Url => "link",
                        SemanticRole::StatusLine => "status",
                    },
                };
                Ok(VARIANT::from(BSTR::from(label)))
            }
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        // Child fragments are virtual — not HWND-hosted
        Err(Error::empty())
    }
}

// --- IRawElementProviderFragment ---------------------------------------------

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for ChildFragmentProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        debug!(
            node_id = self.node_id.0,
            direction = direction.0,
            "ChildFragment Navigate"
        );
        let snap = self.snapshot().ok_or(Error::empty())?;

        match direction {
            NavigateDirection_Parent => {
                if snap.parent_id == NodeId(1) {
                    // Parent is the document root — cast root provider to fragment
                    self.root_provider.cast()
                } else {
                    // Parent is another child (e.g., command block for text regions)
                    Ok(self.provider_for(snap.parent_id))
                }
            }
            NavigateDirection_NextSibling => snap
                .next_sibling
                .map(|id| self.provider_for(id))
                .ok_or(Error::empty()),
            NavigateDirection_PreviousSibling => snap
                .prev_sibling
                .map(|id| self.provider_for(id))
                .ok_or(Error::empty()),
            NavigateDirection_FirstChild => snap
                .first_child
                .map(|id| self.provider_for(id))
                .ok_or(Error::empty()),
            NavigateDirection_LastChild => snap
                .last_child
                .map(|id| self.provider_for(id))
                .ok_or(Error::empty()),
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        let runtime_id: [i32; 2] = [UiaAppendRuntimeId as i32, self.node_id.0];
        unsafe {
            let sa = SafeArrayCreateVector(windows::Win32::System::Variant::VT_I4, 0, 2);
            if sa.is_null() {
                return Err(Error::from_hresult(HRESULT(-2147024882i32)));
            }
            for (i, val) in runtime_id.iter().enumerate() {
                SafeArrayPutElement(sa, &(i as i32), val as *const _ as *const _)?;
            }
            Ok(sa)
        }
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let cell_h = state.cell_height;
        let tab_bar_h = state.tab_bar_height;
        let win_w = state.window_width;

        // Find this node's row range
        let row_range = state
            .tree
            .find_node(self.node_id)
            .and_then(|node| node.row_range());

        let (start_row, end_row) = match row_range {
            Some((s, e)) => (s, e),
            None => {
                return Ok(UiaRect {
                    left: 0.0,
                    top: 0.0,
                    width: 0.0,
                    height: 0.0,
                });
            }
        };

        drop(state);

        // Get window origin in screen coordinates
        let mut origin = POINT { x: 0, y: 0 };
        unsafe {
            let _ = ClientToScreen(self.hwnd, &mut origin);
        }

        let y = origin.y as f64 + tab_bar_h + start_row as f64 * cell_h;
        let h = (end_row - start_row + 1) as f64 * cell_h;

        Ok(UiaRect {
            left: origin.x as f64,
            top: y,
            width: win_w,
            height: h,
        })
    }

    fn GetEmbeddedFragmentRoots(&self) -> Result<*mut SAFEARRAY> {
        Err(Error::empty())
    }

    fn SetFocus(&self) -> Result<()> {
        unsafe {
            let _ = windows::Win32::UI::Input::KeyboardAndMouse::SetFocus(Some(self.hwnd));
        }
        Ok(())
    }

    fn FragmentRoot(&self) -> Result<IRawElementProviderFragmentRoot> {
        Ok(self.root_provider.clone())
    }
}

// --- Tests -------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tree::AccessibilityTree;
    use wixen_shell_integ::{BlockState, CommandBlock, RowRange};

    fn build_test_tree() -> AccessibilityTree {
        let mut tree = AccessibilityTree::new();
        let blocks = vec![
            CommandBlock {
                id: 1,
                prompt: Some(RowRange::new(0, 0)),
                input: Some(RowRange::new(1, 1)),
                output: Some(RowRange::new(2, 4)),
                exit_code: Some(0),
                cwd: None,
                command_text: Some("ls".to_string()),
                state: BlockState::Completed,
                started_at: None,
                output_line_count: 0,
            },
            CommandBlock {
                id: 2,
                prompt: Some(RowRange::new(5, 5)),
                input: Some(RowRange::new(6, 6)),
                output: Some(RowRange::new(7, 9)),
                exit_code: Some(1),
                cwd: None,
                command_text: Some("cat missing".to_string()),
                state: BlockState::Completed,
                started_at: None,
                output_line_count: 0,
            },
        ];
        tree.rebuild(&blocks, |s, e| format!("rows {}-{}", s, e));
        tree
    }

    #[test]
    fn test_snapshot_command_block() {
        let tree = build_test_tree();
        let first_id = tree.root.children()[0].id();
        let snap = snapshot_node(&tree.root, first_id).unwrap();

        match &snap.kind {
            NodeKind::CommandBlock { name, is_error, .. } => {
                assert_eq!(name, "ls (succeeded)");
                assert!(!is_error);
            }
            _ => panic!("Expected CommandBlock"),
        }
        assert_eq!(snap.parent_id, NodeId(1));
        assert!(snap.prev_sibling.is_none());
        assert!(snap.next_sibling.is_some());
        assert!(snap.first_child.is_some());
        assert!(snap.last_child.is_some());
    }

    #[test]
    fn test_snapshot_text_region() {
        let tree = build_test_tree();
        let block = &tree.root.children()[0];
        let prompt_id = block.children()[0].id();
        let snap = snapshot_node(&tree.root, prompt_id).unwrap();

        match &snap.kind {
            NodeKind::TextRegion { role, text, .. } => {
                assert_eq!(*role, SemanticRole::Prompt);
                assert_eq!(text, "rows 0-0");
            }
            _ => panic!("Expected TextRegion"),
        }
        assert_eq!(snap.parent_id, block.id());
        assert!(snap.prev_sibling.is_none());
        assert!(snap.next_sibling.is_some());
    }

    #[test]
    fn test_snapshot_sibling_chain() {
        let tree = build_test_tree();
        let first_id = tree.root.children()[0].id();
        let second_id = tree.root.children()[1].id();

        let snap1 = snapshot_node(&tree.root, first_id).unwrap();
        assert_eq!(snap1.next_sibling, Some(second_id));
        assert!(snap1.prev_sibling.is_none());

        let snap2 = snapshot_node(&tree.root, second_id).unwrap();
        assert_eq!(snap2.prev_sibling, Some(first_id));
        assert!(snap2.next_sibling.is_none());
    }

    #[test]
    fn test_snapshot_error_block() {
        let tree = build_test_tree();
        let second_id = tree.root.children()[1].id();
        let snap = snapshot_node(&tree.root, second_id).unwrap();

        match &snap.kind {
            NodeKind::CommandBlock { is_error, name, .. } => {
                assert!(*is_error);
                assert!(name.contains("exit 1"));
            }
            _ => panic!("Expected CommandBlock"),
        }
    }

    #[test]
    fn test_snapshot_children_of_block() {
        let tree = build_test_tree();
        let block = &tree.root.children()[0];
        let block_id = block.id();
        let snap = snapshot_node(&tree.root, block_id).unwrap();

        // First child should be prompt, last should be output
        let first_child_id = snap.first_child.unwrap();
        let last_child_id = snap.last_child.unwrap();
        assert_ne!(first_child_id, last_child_id);

        let first_snap = snapshot_node(&tree.root, first_child_id).unwrap();
        assert!(matches!(
            first_snap.kind,
            NodeKind::TextRegion {
                role: SemanticRole::Prompt,
                ..
            }
        ));

        let last_snap = snapshot_node(&tree.root, last_child_id).unwrap();
        assert!(matches!(
            last_snap.kind,
            NodeKind::TextRegion {
                role: SemanticRole::OutputText,
                ..
            }
        ));
    }

    #[test]
    fn test_snapshot_not_found() {
        let tree = build_test_tree();
        assert!(snapshot_node(&tree.root, NodeId(9999)).is_none());
    }
}
