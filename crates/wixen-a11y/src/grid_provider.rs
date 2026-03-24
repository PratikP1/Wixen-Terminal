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
}
