//! Text selection model for the terminal.

/// A point in the terminal grid (column, row).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct GridPoint {
    pub col: usize,
    pub row: usize,
}

impl GridPoint {
    pub fn new(col: usize, row: usize) -> Self {
        Self { col, row }
    }
}

impl PartialOrd for GridPoint {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for GridPoint {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.row.cmp(&other.row).then(self.col.cmp(&other.col))
    }
}

/// Selection type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SelectionType {
    /// Character-level selection (click and drag)
    Normal,
    /// Word selection (double-click)
    Word,
    /// Line selection (triple-click)
    Line,
    /// Rectangular / block selection (Alt+click and drag)
    Block,
}

/// Active text selection state.
#[derive(Debug, Clone)]
pub struct Selection {
    /// The anchor point (where the selection started)
    pub anchor: GridPoint,
    /// The moving end of the selection
    pub end: GridPoint,
    /// Selection type
    pub sel_type: SelectionType,
}

impl Selection {
    pub fn new(point: GridPoint, sel_type: SelectionType) -> Self {
        Self {
            anchor: point,
            end: point,
            sel_type,
        }
    }

    /// Get the selection range as (start, end) where start <= end.
    pub fn ordered(&self) -> (GridPoint, GridPoint) {
        if self.anchor <= self.end {
            (self.anchor, self.end)
        } else {
            (self.end, self.anchor)
        }
    }

    /// Check if a cell (col, row) is within this selection.
    pub fn contains(&self, col: usize, row: usize) -> bool {
        let (start, end) = self.ordered();
        let point = GridPoint::new(col, row);

        match self.sel_type {
            SelectionType::Normal => point >= start && point <= end,
            SelectionType::Word => point >= start && point <= end,
            SelectionType::Line => row >= start.row && row <= end.row,
            SelectionType::Block => {
                let min_col = self.anchor.col.min(self.end.col);
                let max_col = self.anchor.col.max(self.end.col);
                row >= start.row && row <= end.row && col >= min_col && col <= max_col
            }
        }
    }

    /// Number of rows spanned by this selection.
    pub fn row_count(&self) -> usize {
        let (start, end) = self.ordered();
        end.row - start.row + 1
    }

    /// Approximate character count (for single-line: col difference, multi-line: estimate).
    pub fn approximate_char_count(&self, cols: usize) -> usize {
        let (start, end) = self.ordered();
        if start.row == end.row {
            end.col.saturating_sub(start.col) + 1
        } else {
            let first_line = cols.saturating_sub(start.col);
            let middle = (end.row - start.row).saturating_sub(1) * cols;
            let last_line = end.col + 1;
            first_line + middle + last_line
        }
    }
}

/// Produce a screen-reader-friendly description of a selection.
///
/// Examples:
/// - "Selected 15 characters"
/// - "Selected 3 lines"
/// - "Block selected 4 columns, 3 rows"
/// - "No selection"
pub fn selection_description(selection: Option<&Selection>, cols: usize) -> String {
    let Some(sel) = selection else {
        return "No selection".to_string();
    };

    let rows = sel.row_count();

    match sel.sel_type {
        SelectionType::Block => {
            let (_start, _end) = sel.ordered();
            let block_cols = sel.anchor.col.abs_diff(sel.end.col) + 1;
            format!("Block selected {block_cols} columns, {rows} rows")
        }
        SelectionType::Line => {
            if rows == 1 {
                "Selected 1 line".to_string()
            } else {
                format!("Selected {rows} lines")
            }
        }
        _ => {
            let chars = sel.approximate_char_count(cols);
            if rows == 1 {
                if chars == 1 {
                    "Selected 1 character".to_string()
                } else {
                    format!("Selected {chars} characters")
                }
            } else {
                format!("Selected {chars} characters across {rows} lines")
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_block_selection_contains() {
        // Block selection from (1, 2) to (3, 5) — rectangular region
        let mut sel = Selection::new(GridPoint::new(1, 2), SelectionType::Block);
        sel.end = GridPoint::new(3, 5);

        // Inside the rectangle
        assert!(sel.contains(1, 2));
        assert!(sel.contains(2, 3));
        assert!(sel.contains(3, 5));
        assert!(sel.contains(1, 4));

        // Outside — row out of range
        assert!(!sel.contains(2, 1));
        assert!(!sel.contains(2, 6));

        // Outside — col out of range
        assert!(!sel.contains(0, 3));
        assert!(!sel.contains(4, 3));
    }

    #[test]
    fn test_block_selection_inverted() {
        // Anchor at bottom-right, end at top-left
        let mut sel = Selection::new(GridPoint::new(5, 8), SelectionType::Block);
        sel.end = GridPoint::new(2, 3);

        // Should still work — block is (cols 2..5, rows 3..8)
        assert!(sel.contains(3, 5));
        assert!(sel.contains(2, 3));
        assert!(sel.contains(5, 8));

        // Outside
        assert!(!sel.contains(1, 5));
        assert!(!sel.contains(6, 5));
        assert!(!sel.contains(3, 2));
        assert!(!sel.contains(3, 9));
    }

    #[test]
    fn test_block_selection_single_row() {
        let mut sel = Selection::new(GridPoint::new(2, 5), SelectionType::Block);
        sel.end = GridPoint::new(7, 5);

        assert!(sel.contains(2, 5));
        assert!(sel.contains(4, 5));
        assert!(sel.contains(7, 5));
        assert!(!sel.contains(1, 5));
        assert!(!sel.contains(8, 5));
        assert!(!sel.contains(4, 4));
        assert!(!sel.contains(4, 6));
    }

    #[test]
    fn test_normal_selection_contains() {
        let mut sel = Selection::new(GridPoint::new(3, 1), SelectionType::Normal);
        sel.end = GridPoint::new(5, 3);

        // In range
        assert!(sel.contains(3, 1));
        assert!(sel.contains(0, 2)); // middle row, any col
        assert!(sel.contains(5, 3));

        // Out of range
        assert!(!sel.contains(2, 1)); // before start on first row
        assert!(!sel.contains(6, 3)); // after end on last row
    }

    #[test]
    fn test_line_selection_contains() {
        let mut sel = Selection::new(GridPoint::new(10, 2), SelectionType::Line);
        sel.end = GridPoint::new(3, 4);

        // Any column within row range should be selected
        assert!(sel.contains(0, 2));
        assert!(sel.contains(100, 3));
        assert!(sel.contains(0, 4));

        // Out of row range
        assert!(!sel.contains(10, 1));
        assert!(!sel.contains(3, 5));
    }

    // --- Selection description tests ---

    #[test]
    fn test_description_no_selection() {
        assert_eq!(selection_description(None, 80), "No selection");
    }

    #[test]
    fn test_description_single_char() {
        let sel = Selection::new(GridPoint::new(5, 0), SelectionType::Normal);
        assert_eq!(
            selection_description(Some(&sel), 80),
            "Selected 1 character"
        );
    }

    #[test]
    fn test_description_single_line_chars() {
        let mut sel = Selection::new(GridPoint::new(0, 0), SelectionType::Normal);
        sel.end = GridPoint::new(14, 0);
        assert_eq!(
            selection_description(Some(&sel), 80),
            "Selected 15 characters"
        );
    }

    #[test]
    fn test_description_multi_line() {
        let mut sel = Selection::new(GridPoint::new(10, 0), SelectionType::Normal);
        sel.end = GridPoint::new(20, 2);
        let desc = selection_description(Some(&sel), 80);
        assert!(desc.contains("across 3 lines"), "Got: {desc}");
    }

    #[test]
    fn test_description_line_selection() {
        let mut sel = Selection::new(GridPoint::new(0, 3), SelectionType::Line);
        sel.end = GridPoint::new(0, 5);
        assert_eq!(selection_description(Some(&sel), 80), "Selected 3 lines");
    }

    #[test]
    fn test_description_single_line_selection() {
        let sel = Selection::new(GridPoint::new(0, 3), SelectionType::Line);
        assert_eq!(selection_description(Some(&sel), 80), "Selected 1 line");
    }

    #[test]
    fn test_description_block_selection() {
        let mut sel = Selection::new(GridPoint::new(2, 1), SelectionType::Block);
        sel.end = GridPoint::new(5, 4);
        assert_eq!(
            selection_description(Some(&sel), 80),
            "Block selected 4 columns, 4 rows"
        );
    }
}
