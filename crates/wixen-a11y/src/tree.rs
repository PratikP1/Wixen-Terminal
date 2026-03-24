//! Virtual accessibility tree — the document model that UIA exposes.
//!
//! This tree is built from the terminal grid + shell integration data and
//! provides the structure that screen readers navigate.

use wixen_core::url::detect_safe_urls;
use wixen_shell_integ::{BlockState, CommandBlock};

/// Unique runtime identifier for each node in the accessibility tree.
/// UIA requires each element to have a unique runtime ID.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct NodeId(pub i32);

/// Semantic role for inline text spans.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SemanticRole {
    /// Shell prompt text (e.g., "C:\Users\foo>")
    Prompt,
    /// User-typed command input
    CommandInput,
    /// Normal command output
    OutputText,
    /// Error output (detected by exit code or stderr markers)
    ErrorText,
    /// File path
    Path,
    /// URL (detected via regex or OSC 8)
    Url,
    /// Status line (bottom of TUI apps)
    StatusLine,
}

/// A node in the accessibility tree.
#[derive(Debug, Clone)]
pub enum A11yNode {
    /// Root document — the entire terminal view.
    Document { id: NodeId, children: Vec<A11yNode> },
    /// A command block (prompt + input + output).
    CommandBlock {
        id: NodeId,
        /// The shell integration block ID this maps to
        shell_block_id: u64,
        /// Display name (e.g., "git status (exit 0)")
        name: String,
        /// Children: prompt, input, output regions
        children: Vec<A11yNode>,
        /// Whether the command failed
        is_error: bool,
        /// Current state
        state: BlockState,
    },
    /// A region of text with a semantic role.
    TextRegion {
        id: NodeId,
        /// The text content
        text: String,
        /// Semantic role
        role: SemanticRole,
        /// Start row in the terminal grid (absolute)
        start_row: usize,
        /// End row in the terminal grid (absolute, inclusive)
        end_row: usize,
        /// Child nodes (e.g., Hyperlink nodes detected within this region).
        children: Vec<A11yNode>,
    },
    /// Live region for active output.
    LiveRegion {
        id: NodeId,
        /// Current text in the live region
        text: String,
        /// Politeness level (1=polite, 2=assertive)
        politeness: u32,
    },
    /// A hyperlink detected in command output.
    Hyperlink {
        id: NodeId,
        /// The URL target.
        url: String,
        /// The display text (may equal the URL for plain-text URLs).
        text: String,
        /// Start row in the terminal grid (absolute).
        start_row: usize,
        /// End row in the terminal grid (absolute).
        end_row: usize,
    },
}

impl A11yNode {
    pub fn id(&self) -> NodeId {
        match self {
            A11yNode::Document { id, .. } => *id,
            A11yNode::CommandBlock { id, .. } => *id,
            A11yNode::TextRegion { id, .. } => *id,
            A11yNode::LiveRegion { id, .. } => *id,
            A11yNode::Hyperlink { id, .. } => *id,
        }
    }

    pub fn children(&self) -> &[A11yNode] {
        match self {
            A11yNode::Document { children, .. } => children,
            A11yNode::CommandBlock { children, .. } => children,
            A11yNode::TextRegion { children, .. } => children,
            A11yNode::LiveRegion { .. } => &[],
            A11yNode::Hyperlink { .. } => &[],
        }
    }

    /// Get the row range (start_row, end_row) for this node if applicable.
    ///
    /// For TextRegion: returns its own row span.
    /// For CommandBlock: returns the combined row span of all children.
    /// Returns None for Document and LiveRegion.
    pub fn row_range(&self) -> Option<(usize, usize)> {
        match self {
            A11yNode::TextRegion {
                start_row, end_row, ..
            } => Some((*start_row, *end_row)),
            A11yNode::CommandBlock { children, .. } => {
                let mut min_row = usize::MAX;
                let mut max_row = 0;
                for child in children {
                    if let Some((s, e)) = child.row_range() {
                        min_row = min_row.min(s);
                        max_row = max_row.max(e);
                    }
                }
                if min_row <= max_row {
                    Some((min_row, max_row))
                } else {
                    None
                }
            }
            A11yNode::Hyperlink {
                start_row, end_row, ..
            } => Some((*start_row, *end_row)),
            A11yNode::Document { .. } | A11yNode::LiveRegion { .. } => None,
        }
    }
}

/// The complete accessibility tree.
pub struct AccessibilityTree {
    /// Root node
    pub root: A11yNode,
    /// Next ID counter
    next_id: i32,
}

impl AccessibilityTree {
    pub fn new() -> Self {
        Self {
            root: A11yNode::Document {
                id: NodeId(1),
                children: Vec::new(),
            },
            next_id: 2,
        }
    }

    /// Allocate a new unique node ID.
    fn alloc_id(&mut self) -> NodeId {
        let id = NodeId(self.next_id);
        self.next_id += 1;
        id
    }

    /// Rebuild the tree from shell integration command blocks and terminal state.
    ///
    /// `extract_text` is a closure that extracts text from the terminal grid
    /// for a given row range (start_row, end_row inclusive).
    pub fn rebuild<F>(&mut self, blocks: &[CommandBlock], extract_text: F)
    where
        F: Fn(usize, usize) -> String,
    {
        let mut children = Vec::with_capacity(blocks.len() + 1);

        for block in blocks {
            let block_id = self.alloc_id();

            let mut block_children = Vec::new();

            // Prompt region
            if let Some(prompt_range) = block.prompt {
                let text = extract_text(prompt_range.start, prompt_range.end);
                if !text.is_empty() {
                    block_children.push(A11yNode::TextRegion {
                        id: self.alloc_id(),
                        text,
                        role: SemanticRole::Prompt,
                        start_row: prompt_range.start,
                        end_row: prompt_range.end,
                        children: Vec::new(),
                    });
                }
            }

            // Input region
            if let Some(input_range) = block.input {
                let text = extract_text(input_range.start, input_range.end);
                if !text.is_empty() {
                    block_children.push(A11yNode::TextRegion {
                        id: self.alloc_id(),
                        text,
                        role: SemanticRole::CommandInput,
                        start_row: input_range.start,
                        end_row: input_range.end,
                        children: Vec::new(),
                    });
                }
            }

            // Output region
            if let Some(output_range) = block.output {
                let text = extract_text(output_range.start, output_range.end);
                if !text.is_empty() {
                    let is_error = block.exit_code.is_some_and(|c| c != 0);

                    // Scan for URLs in each line of output
                    let mut hyperlinks = Vec::new();
                    for (line_offset, line) in text.lines().enumerate() {
                        let row = output_range.start + line_offset;
                        for url_match in detect_safe_urls(line) {
                            hyperlinks.push(A11yNode::Hyperlink {
                                id: self.alloc_id(),
                                url: url_match.url.clone(),
                                text: url_match.url,
                                start_row: row,
                                end_row: row,
                            });
                        }
                    }

                    block_children.push(A11yNode::TextRegion {
                        id: self.alloc_id(),
                        text,
                        role: if is_error {
                            SemanticRole::ErrorText
                        } else {
                            SemanticRole::OutputText
                        },
                        start_row: output_range.start,
                        end_row: output_range.end,
                        children: hyperlinks,
                    });
                }
            }

            let is_error = block.exit_code.is_some_and(|c| c != 0);
            let name = match (&block.command_text, block.exit_code) {
                (Some(cmd), Some(code)) => {
                    if code == 0 {
                        format!("{} (succeeded)", cmd)
                    } else {
                        format!("{} (exit {})", cmd, code)
                    }
                }
                (Some(cmd), None) => format!("{} (running)", cmd),
                (None, _) => match block.state {
                    BlockState::PromptActive => "Prompt".to_string(),
                    BlockState::InputActive => "Typing command".to_string(),
                    BlockState::Executing => "Command running".to_string(),
                    BlockState::Completed => "Command completed".to_string(),
                },
            };

            children.push(A11yNode::CommandBlock {
                id: block_id,
                shell_block_id: block.id,
                name,
                children: block_children,
                is_error,
                state: block.state,
            });
        }

        self.root = A11yNode::Document {
            id: NodeId(1),
            children,
        };
    }

    /// Find a node by its runtime ID.
    pub fn find_node(&self, id: NodeId) -> Option<&A11yNode> {
        find_node_recursive(&self.root, id)
    }

    /// Get the total number of nodes.
    pub fn node_count(&self) -> usize {
        count_nodes(&self.root)
    }
}

impl Default for AccessibilityTree {
    fn default() -> Self {
        Self::new()
    }
}

fn find_node_recursive(node: &A11yNode, target: NodeId) -> Option<&A11yNode> {
    if node.id() == target {
        return Some(node);
    }
    for child in node.children() {
        if let Some(found) = find_node_recursive(child, target) {
            return Some(found);
        }
    }
    None
}

fn count_nodes(node: &A11yNode) -> usize {
    1 + node.children().iter().map(count_nodes).sum::<usize>()
}

#[cfg(test)]
mod tests {
    use super::*;
    use wixen_shell_integ::RowRange;

    #[test]
    fn test_rebuild_tree() {
        let blocks = vec![
            CommandBlock {
                id: 1,
                prompt: Some(RowRange::new(0, 0)),
                input: Some(RowRange::new(1, 1)),
                output: Some(RowRange::new(2, 5)),
                exit_code: Some(0),
                cwd: None,
                command_text: Some("ls -la".to_string()),
                state: BlockState::Completed,
                started_at: None,
                output_line_count: 0,
            },
            CommandBlock {
                id: 2,
                prompt: Some(RowRange::new(6, 6)),
                input: Some(RowRange::new(7, 7)),
                output: Some(RowRange::new(8, 8)),
                exit_code: Some(1),
                cwd: None,
                command_text: Some("cat missing.txt".to_string()),
                state: BlockState::Completed,
                started_at: None,
                output_line_count: 0,
            },
        ];

        let mut tree = AccessibilityTree::new();
        tree.rebuild(&blocks, |start, end| format!("[rows {}-{}]", start, end));

        // Root + 2 command blocks
        assert_eq!(tree.root.children().len(), 2);

        // First block: succeeded
        if let A11yNode::CommandBlock {
            name,
            is_error,
            children,
            ..
        } = &tree.root.children()[0]
        {
            assert_eq!(name, "ls -la (succeeded)");
            assert!(!is_error);
            assert_eq!(children.len(), 3); // prompt, input, output
        } else {
            panic!("Expected CommandBlock");
        }

        // Second block: failed
        if let A11yNode::CommandBlock {
            name,
            is_error,
            children,
            ..
        } = &tree.root.children()[1]
        {
            assert_eq!(name, "cat missing.txt (exit 1)");
            assert!(is_error);
            assert_eq!(children.len(), 3);
            // Output should be ErrorText
            if let A11yNode::TextRegion { role, .. } = &children[2] {
                assert_eq!(*role, SemanticRole::ErrorText);
            }
        } else {
            panic!("Expected CommandBlock");
        }
    }

    #[test]
    fn test_find_node() {
        let mut tree = AccessibilityTree::new();
        let blocks = vec![CommandBlock {
            id: 1,
            prompt: Some(RowRange::new(0, 0)),
            input: None,
            output: None,
            exit_code: None,
            cwd: None,
            command_text: None,
            state: BlockState::PromptActive,
            started_at: None,
            output_line_count: 0,
        }];
        tree.rebuild(&blocks, |_, _| "test".to_string());

        // Root is always NodeId(1)
        assert!(tree.find_node(NodeId(1)).is_some());
        // Should not find a non-existent node
        assert!(tree.find_node(NodeId(999)).is_none());
    }

    #[test]
    fn test_command_block_row_range() {
        let mut tree = AccessibilityTree::new();
        let blocks = vec![CommandBlock {
            id: 1,
            prompt: Some(RowRange::new(5, 5)),
            input: Some(RowRange::new(6, 6)),
            output: Some(RowRange::new(7, 12)),
            exit_code: Some(0),
            cwd: None,
            command_text: Some("build".to_string()),
            state: BlockState::Completed,
            started_at: None,
            output_line_count: 0,
        }];
        tree.rebuild(&blocks, |s, e| format!("rows {}-{}", s, e));

        // Command block aggregates min/max from children
        let block = &tree.root.children()[0];
        let (start, end) = block.row_range().unwrap();
        assert_eq!(start, 5);
        assert_eq!(end, 12);

        // Text region prompt has its own row range
        let prompt = &block.children()[0];
        let (ps, pe) = prompt.row_range().unwrap();
        assert_eq!(ps, 5);
        assert_eq!(pe, 5);

        // Output region
        let output = &block.children()[2];
        let (os, oe) = output.row_range().unwrap();
        assert_eq!(os, 7);
        assert_eq!(oe, 12);
    }

    #[test]
    fn test_row_range_none_for_document() {
        let tree = AccessibilityTree::new();
        // Document nodes have no meaningful row range
        assert!(tree.root.row_range().is_none());
    }

    #[test]
    fn test_bounding_rect_math() {
        // Verify the arithmetic used in BoundingRectangle implementations
        let cell_h = 18.0_f64;
        let tab_bar_h = 30.0_f64;
        let origin_y = 100.0_f64;
        let start_row = 5_usize;
        let end_row = 12_usize;

        let y = origin_y + tab_bar_h + start_row as f64 * cell_h;
        let h = (end_row - start_row + 1) as f64 * cell_h;

        assert!((y - 220.0).abs() < 0.001); // 100 + 30 + 5*18 = 220
        assert!((h - 144.0).abs() < 0.001); // 8 * 18 = 144
    }

    #[test]
    fn test_bounding_rect_at_row_zero() {
        // Command block starting at row 0 should be flush against tab bar
        let cell_h = 16.0_f64;
        let tab_bar_h = 32.0_f64;
        let origin_y = 200.0_f64;
        let start_row = 0_usize;
        let end_row = 2_usize;

        let y = origin_y + tab_bar_h + start_row as f64 * cell_h;
        let h = (end_row - start_row + 1) as f64 * cell_h;

        assert!((y - 232.0).abs() < 0.001); // 200 + 32 + 0 = 232
        assert!((h - 48.0).abs() < 0.001); // 3 * 16 = 48
    }

    #[test]
    fn test_hyperlink_node_from_url_in_output() {
        let blocks = vec![CommandBlock {
            id: 1,
            prompt: Some(RowRange::new(0, 0)),
            input: Some(RowRange::new(1, 1)),
            output: Some(RowRange::new(2, 3)),
            exit_code: Some(0),
            cwd: None,
            command_text: Some("echo".to_string()),
            state: BlockState::Completed,
            started_at: None,
            output_line_count: 0,
        }];

        let mut tree = AccessibilityTree::new();
        tree.rebuild(&blocks, |start, end| {
            if start == 2 && end == 3 {
                "Visit https://example.com for info\nMore text here".to_string()
            } else {
                format!("[rows {}-{}]", start, end)
            }
        });

        // Find the output TextRegion node — it should now have a Hyperlink child
        let cmd_block = &tree.root.children()[0];
        let output_node = cmd_block.children().last().unwrap();

        // The output node should have children (hyperlinks)
        let hyperlinks: Vec<&A11yNode> = output_node
            .children()
            .iter()
            .filter(|n| matches!(n, A11yNode::Hyperlink { .. }))
            .collect();
        assert_eq!(hyperlinks.len(), 1);

        if let A11yNode::Hyperlink {
            url,
            text,
            start_row,
            end_row,
            ..
        } = &hyperlinks[0]
        {
            assert_eq!(url, "https://example.com");
            assert_eq!(text, "https://example.com");
            assert_eq!(*start_row, 2);
            assert_eq!(*end_row, 2);
        } else {
            panic!("Expected Hyperlink node");
        }
    }

    #[test]
    fn test_hyperlink_node_id_and_row_range() {
        let node = A11yNode::Hyperlink {
            id: NodeId(42),
            url: "https://example.com".to_string(),
            text: "example".to_string(),
            start_row: 5,
            end_row: 5,
        };
        assert_eq!(node.id(), NodeId(42));
        assert_eq!(node.row_range(), Some((5, 5)));
        assert!(node.children().is_empty());
    }

    #[test]
    fn test_bounding_rect_single_row() {
        // Single-row block (start == end)
        let cell_h = 20.0_f64;
        let tab_bar_h = 0.0_f64;
        let origin_y = 0.0_f64;
        let start_row = 10_usize;
        let end_row = 10_usize;

        let y = origin_y + tab_bar_h + start_row as f64 * cell_h;
        let h = (end_row - start_row + 1) as f64 * cell_h;

        assert!((y - 200.0).abs() < 0.001); // 10 * 20 = 200
        assert!((h - 20.0).abs() < 0.001); // 1 * 20 = 20
    }
}
