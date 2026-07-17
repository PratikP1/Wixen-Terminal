//! IGridProvider / IGridItemProvider for TUI apps.
//!
//! When the terminal is in alternate-screen mode (e.g., vim, htop, less),
//! the grid is exposed as a UIA table: each cell is a grid item with
//! row/column coordinates. This lets screen readers announce "Row 5, Column 3: x"
//! and navigate by row/column in TUI layouts.

use std::sync::Arc;

use parking_lot::RwLock;
use windows::Win32::UI::Accessibility::*;
use windows::core::*;

use crate::provider::TerminalA11yState;
use crate::tree::{A11yNode, NodeId};

/// Shared grid dimensions + cell text snapshot, updated by the UI layer.
#[derive(Debug, Clone)]
pub struct GridSnapshot {
    pub rows: usize,
    pub cols: usize,
    /// Row-major cell text: `cells[row * cols + col]`.
    pub cells: Vec<String>,
}

impl GridSnapshot {
    pub fn new(cols: usize, rows: usize) -> Self {
        Self {
            rows,
            cols,
            cells: vec![String::new(); cols * rows],
        }
    }

    pub fn cell_text(&self, col: usize, row: usize) -> &str {
        self.cells
            .get(row * self.cols + col)
            .map(|s| s.as_str())
            .unwrap_or("")
    }
}

impl Default for GridSnapshot {
    fn default() -> Self {
        Self::new(80, 24)
    }
}

/// UIA IGridProvider implementation for the terminal grid.
///
/// Exposes the terminal as a rows × cols grid. Each cell is accessed
/// via `GetItem(row, col)` which returns an `IGridItemProvider`.
#[implement(IGridProvider)]
pub struct TerminalGridProvider {
    state: Arc<RwLock<TerminalA11yState>>,
}

impl TerminalGridProvider {
    pub fn new(state: Arc<RwLock<TerminalA11yState>>) -> Self {
        Self { state }
    }

    pub fn into_interface(self) -> IGridProvider {
        self.into()
    }
}

impl IGridProvider_Impl for TerminalGridProvider_Impl {
    fn GetItem(&self, row: i32, column: i32) -> Result<IRawElementProviderSimple> {
        let state = self.state.read();
        let grid = &state.grid_snapshot;
        let r = row as usize;
        let c = column as usize;
        if r >= grid.rows || c >= grid.cols {
            return Err(Error::from_hresult(HRESULT(0x80070057u32 as i32))); // E_INVALIDARG
        }
        let item = GridItemProvider::new(Arc::clone(&self.state), r, c);
        let simple: IRawElementProviderSimple = item.into();
        Ok(simple)
    }

    fn RowCount(&self) -> Result<i32> {
        let state = self.state.read();
        Ok(state.grid_snapshot.rows as i32)
    }

    fn ColumnCount(&self) -> Result<i32> {
        let state = self.state.read();
        Ok(state.grid_snapshot.cols as i32)
    }
}

/// UIA IGridItemProvider for a single terminal cell.
#[implement(IRawElementProviderSimple, IGridItemProvider)]
pub struct GridItemProvider {
    state: Arc<RwLock<TerminalA11yState>>,
    row: usize,
    col: usize,
}

impl GridItemProvider {
    pub fn new(state: Arc<RwLock<TerminalA11yState>>, row: usize, col: usize) -> Self {
        Self { state, row, col }
    }
}

// --- IRawElementProviderSimple for grid items ---

impl IRawElementProviderSimple_Impl for GridItemProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        #[allow(non_upper_case_globals)]
        match pattern_id {
            UIA_GridItemPatternId => {
                let item: IGridItemProvider =
                    windows_core::ComObjectInterface::<IGridItemProvider>::as_interface_ref(self)
                        .to_owned();
                item.cast()
            }
            _ => Err(Error::empty()),
        }
    }

    #[allow(non_upper_case_globals)]
    fn GetPropertyValue(
        &self,
        property_id: UIA_PROPERTY_ID,
    ) -> Result<windows::Win32::System::Variant::VARIANT> {
        use windows::Win32::System::Variant::VARIANT;
        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_DataItemControlTypeId.0)),
            UIA_NamePropertyId => {
                let state = self.state.read();
                let text = state.grid_snapshot.cell_text(self.col, self.row);
                Ok(VARIANT::from(BSTR::from(text)))
            }
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from(&format!(
                "Cell_{}_{}",
                self.row, self.col
            )))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

// --- IGridItemProvider for grid items ---

impl IGridItemProvider_Impl for GridItemProvider_Impl {
    fn Row(&self) -> Result<i32> {
        Ok(self.row as i32)
    }

    fn Column(&self) -> Result<i32> {
        Ok(self.col as i32)
    }

    fn RowSpan(&self) -> Result<i32> {
        Ok(1)
    }

    fn ColumnSpan(&self) -> Result<i32> {
        Ok(1)
    }

    fn ContainingGrid(&self) -> Result<IRawElementProviderSimple> {
        // The containing grid is the terminal root — we'd need a reference.
        // For now, return empty; screen readers can fall back to tree navigation.
        Err(Error::empty())
    }
}

// --- Table node grid provider ------------------------------------------------
//
// A `Table` node in the accessibility tree (detected in command output) is
// exposed as its own UIA grid, distinct from the full-screen TUI grid above.
// Cell text comes from `A11yNode::cell_text` via the shared tree, looked up by
// the table's `NodeId`.

/// Read a table node's (rows, cols) from the shared tree, if it is a Table.
pub(crate) fn table_dimensions(
    state: &RwLock<TerminalA11yState>,
    node_id: NodeId,
) -> Option<(usize, usize)> {
    let state = state.read();
    match state.tree.find_node(node_id)? {
        A11yNode::Table { rows, cols, .. } => Some((*rows, *cols)),
        _ => None,
    }
}

/// Read a table cell's text from the shared tree (row 0 is the header row).
pub(crate) fn table_cell_text(
    state: &RwLock<TerminalA11yState>,
    node_id: NodeId,
    row: usize,
    col: usize,
) -> Option<String> {
    let state = state.read();
    state
        .tree
        .find_node(node_id)?
        .cell_text(row, col)
        .map(str::to_owned)
}

/// UIA IGridProvider backed by a `Table` node in the accessibility tree.
#[implement(IGridProvider)]
pub struct TableGridProvider {
    state: Arc<RwLock<TerminalA11yState>>,
    node_id: NodeId,
}

impl TableGridProvider {
    pub fn new(state: Arc<RwLock<TerminalA11yState>>, node_id: NodeId) -> Self {
        Self { state, node_id }
    }
}

impl IGridProvider_Impl for TableGridProvider_Impl {
    fn GetItem(&self, row: i32, column: i32) -> Result<IRawElementProviderSimple> {
        if row < 0 || column < 0 {
            return Err(Error::from_hresult(HRESULT(0x80070057u32 as i32))); // E_INVALIDARG
        }
        let (rows, cols) = table_dimensions(&self.state, self.node_id).ok_or_else(Error::empty)?;
        let (r, c) = (row as usize, column as usize);
        if r >= rows || c >= cols {
            return Err(Error::from_hresult(HRESULT(0x80070057u32 as i32))); // E_INVALIDARG
        }
        let item = TableCellProvider::new(Arc::clone(&self.state), self.node_id, r, c);
        Ok(item.into())
    }

    fn RowCount(&self) -> Result<i32> {
        let (rows, _) = table_dimensions(&self.state, self.node_id).ok_or_else(Error::empty)?;
        Ok(rows as i32)
    }

    fn ColumnCount(&self) -> Result<i32> {
        let (_, cols) = table_dimensions(&self.state, self.node_id).ok_or_else(Error::empty)?;
        Ok(cols as i32)
    }
}

/// UIA IGridItemProvider for a single cell of a `Table` node.
#[implement(IRawElementProviderSimple, IGridItemProvider)]
pub struct TableCellProvider {
    state: Arc<RwLock<TerminalA11yState>>,
    node_id: NodeId,
    row: usize,
    col: usize,
}

impl TableCellProvider {
    pub fn new(
        state: Arc<RwLock<TerminalA11yState>>,
        node_id: NodeId,
        row: usize,
        col: usize,
    ) -> Self {
        Self {
            state,
            node_id,
            row,
            col,
        }
    }
}

impl IRawElementProviderSimple_Impl for TableCellProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        #[allow(non_upper_case_globals)]
        match pattern_id {
            UIA_GridItemPatternId => {
                let item: IGridItemProvider =
                    windows_core::ComObjectInterface::<IGridItemProvider>::as_interface_ref(self)
                        .to_owned();
                item.cast()
            }
            _ => Err(Error::empty()),
        }
    }

    #[allow(non_upper_case_globals)]
    fn GetPropertyValue(
        &self,
        property_id: UIA_PROPERTY_ID,
    ) -> Result<windows::Win32::System::Variant::VARIANT> {
        use windows::Win32::System::Variant::VARIANT;
        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_DataItemControlTypeId.0)),
            UIA_NamePropertyId => {
                let text = table_cell_text(&self.state, self.node_id, self.row, self.col)
                    .unwrap_or_default();
                Ok(VARIANT::from(BSTR::from(&text)))
            }
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from(&format!(
                "TableCell_{}_{}_{}",
                self.node_id.0, self.row, self.col
            )))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

impl IGridItemProvider_Impl for TableCellProvider_Impl {
    fn Row(&self) -> Result<i32> {
        Ok(self.row as i32)
    }

    fn Column(&self) -> Result<i32> {
        Ok(self.col as i32)
    }

    fn RowSpan(&self) -> Result<i32> {
        Ok(1)
    }

    fn ColumnSpan(&self) -> Result<i32> {
        Ok(1)
    }

    fn ContainingGrid(&self) -> Result<IRawElementProviderSimple> {
        // The containing grid is the Table fragment; screen readers can fall
        // back to tree navigation to reach it.
        Err(Error::empty())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_grid_snapshot_cell_text() {
        let mut snap = GridSnapshot::new(4, 3);
        snap.cells[0] = "A".to_string(); // row 0, col 0
        snap.cells[5] = "B".to_string(); // row 1, col 1
        snap.cells[11] = "C".to_string(); // row 2, col 3

        assert_eq!(snap.cell_text(0, 0), "A");
        assert_eq!(snap.cell_text(1, 1), "B");
        assert_eq!(snap.cell_text(3, 2), "C");
        assert_eq!(snap.cell_text(2, 0), "");
    }

    #[test]
    fn test_grid_snapshot_default() {
        let snap = GridSnapshot::default();
        assert_eq!(snap.rows, 24);
        assert_eq!(snap.cols, 80);
        assert_eq!(snap.cells.len(), 80 * 24);
    }

    #[test]
    fn test_grid_snapshot_out_of_bounds() {
        let snap = GridSnapshot::new(3, 2);
        // Out of bounds should return ""
        assert_eq!(snap.cell_text(5, 5), "");
    }

    use crate::provider::TerminalA11yState;
    use crate::tree::A11yNode;
    use wixen_shell_integ::{BlockState, CommandBlock, RowRange};

    const TABULAR_OUTPUT: &str = "A     B     C\n1     2     3\n4     5     6";

    /// Build shared state whose tree contains one Table node, and return its id.
    fn state_with_table() -> (Arc<RwLock<TerminalA11yState>>, crate::tree::NodeId) {
        let state = Arc::new(RwLock::new(TerminalA11yState::new()));
        let blocks = vec![CommandBlock {
            id: 1,
            prompt: None,
            input: None,
            output: Some(RowRange::new(2, 4)),
            exit_code: Some(0),
            cwd: None,
            command_text: Some("wsl -l -v".to_string()),
            state: BlockState::Completed,
            started_at: None,
            output_line_count: 0,
        }];
        let table_id = {
            let mut s = state.write();
            s.tree.rebuild(&blocks, |_, _| TABULAR_OUTPUT.to_string());
            let output = s.tree.root.children()[0].children().last().unwrap();
            output
                .children()
                .iter()
                .find(|n| matches!(n, A11yNode::Table { .. }))
                .unwrap()
                .id()
        };
        (state, table_id)
    }

    #[test]
    fn test_table_dimensions_from_node() {
        let (state, table_id) = state_with_table();
        assert_eq!(table_dimensions(&state, table_id), Some((3, 3)));
    }

    #[test]
    fn test_table_cell_text_from_node() {
        let (state, table_id) = state_with_table();
        assert_eq!(
            table_cell_text(&state, table_id, 0, 0).as_deref(),
            Some("A")
        );
        assert_eq!(
            table_cell_text(&state, table_id, 0, 2).as_deref(),
            Some("C")
        );
        assert_eq!(
            table_cell_text(&state, table_id, 1, 1).as_deref(),
            Some("2")
        );
        assert_eq!(
            table_cell_text(&state, table_id, 2, 2).as_deref(),
            Some("6")
        );
        // Out of bounds
        assert_eq!(table_cell_text(&state, table_id, 3, 0), None);
    }

    #[test]
    fn test_table_grid_provider_row_and_column_count() {
        let (state, table_id) = state_with_table();
        let grid: IGridProvider = TableGridProvider::new(state, table_id).into();
        unsafe {
            assert_eq!(grid.RowCount().unwrap(), 3);
            assert_eq!(grid.ColumnCount().unwrap(), 3);
        }
    }

    #[test]
    fn test_table_grid_get_item_coordinates() {
        let (state, table_id) = state_with_table();
        let grid: IGridProvider = TableGridProvider::new(state, table_id).into();
        unsafe {
            let item = grid.GetItem(1, 2).unwrap();
            let gi: IGridItemProvider = item.cast().unwrap();
            assert_eq!(gi.Row().unwrap(), 1);
            assert_eq!(gi.Column().unwrap(), 2);
        }
    }

    #[test]
    fn test_table_grid_get_item_out_of_bounds_errors() {
        let (state, table_id) = state_with_table();
        let grid: IGridProvider = TableGridProvider::new(state, table_id).into();
        unsafe {
            assert!(grid.GetItem(3, 0).is_err());
            assert!(grid.GetItem(0, 3).is_err());
        }
    }
}
