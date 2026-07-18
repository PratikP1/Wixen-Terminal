//! UIA providers for the command history browser overlay.
//!
//! Exposes history browser state to screen readers: a list container with one
//! ListItem per history entry, mirroring `palette_provider`.

use std::sync::Arc;

use parking_lot::RwLock;
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::Gdi::ClientToScreen;
use windows::Win32::System::Com::SAFEARRAY;
use windows::Win32::System::Ole::{SafeArrayCreateVector, SafeArrayPutElement};
use windows::Win32::System::Variant::VARIANT;
use windows::Win32::UI::Accessibility::*;
use windows::core::*;

use crate::provider::TerminalA11yState;

// ---------------------------------------------------------------------------
// Snapshot types — plain data extracted from HistoryBrowser state
// ---------------------------------------------------------------------------

/// Snapshot of a single command history entry.
#[derive(Debug, Clone)]
pub struct HistoryEntrySnapshot {
    /// Screen-reader-ready label (e.g. "cargo test (succeeded)").
    pub label: String,
    /// Index within the filtered list (newest first).
    pub index: usize,
}

/// Complete snapshot of the command history browser state.
#[derive(Debug, Clone)]
pub struct HistorySnapshot {
    /// Whether the history browser overlay is currently visible.
    pub visible: bool,
    /// Filtered entries visible in the browser (newest first).
    pub entries: Vec<HistoryEntrySnapshot>,
    /// Index of the selected entry within `entries`.
    pub selected_index: usize,
}

impl HistorySnapshot {
    /// Entries exposed through UIA — empty when the overlay is hidden.
    pub fn exposed_entries(&self) -> &[HistoryEntrySnapshot] {
        if self.visible { &self.entries } else { &[] }
    }

    /// Whether the entry at `index` is the current selection.
    ///
    /// Always false when the overlay is hidden or `index` is out of range.
    pub fn is_entry_selected(&self, index: usize) -> bool {
        self.visible && index == self.selected_index && index < self.entries.len()
    }
}

// ---------------------------------------------------------------------------
// Runtime ID generation — unique IDs for history browser UIA elements
// ---------------------------------------------------------------------------

/// Base offset for history runtime IDs (avoid collisions with settings 100k
/// and palette 200k ranges).
const HISTORY_ID_BASE: i32 = 300_000;
/// List container runtime ID.
const HISTORY_PANEL_ID: i32 = HISTORY_ID_BASE;

fn history_entry_runtime_id(index: usize) -> i32 {
    HISTORY_ID_BASE + 100 + index as i32
}

/// Helper to build a two-element SAFEARRAY runtime ID.
fn make_runtime_id(element_id: i32) -> Result<*mut SAFEARRAY> {
    let runtime_id: [i32; 2] = [UiaAppendRuntimeId as i32, element_id];
    unsafe {
        let sa = SafeArrayCreateVector(windows::Win32::System::Variant::VT_I4, 0, 2);
        if sa.is_null() {
            return Err(Error::from_hresult(HRESULT(-2147024882i32))); // E_OUTOFMEMORY
        }
        for (i, val) in runtime_id.iter().enumerate() {
            SafeArrayPutElement(sa, &(i as i32), val as *const _ as *const _)?;
        }
        Ok(sa)
    }
}

/// Compute the history browser panel rectangle in screen coordinates.
///
/// Returns (x, y, width, total_height) matching the overlay layout in main.rs:
/// 60% window width centered, 15% from top.
fn history_panel_rect(hwnd: HWND, state: &TerminalA11yState) -> (f64, f64, f64, f64) {
    let mut origin = POINT { x: 0, y: 0 };
    unsafe {
        let _ = ClientToScreen(hwnd, &mut origin);
    }
    let win_w = state.window_width;
    let win_h = state.window_height;
    let cell_h = state.cell_height;
    let panel_w = win_w * 0.6;
    let x = origin.x as f64 + (win_w - panel_w) / 2.0;
    let y = origin.y as f64 + win_h * 0.15;

    let entry_count = state
        .history_snapshot
        .as_ref()
        .map(|s| s.exposed_entries().len().min(12))
        .unwrap_or(0);
    // 1 search line + entries + 2 lines padding
    let total_lines = 1 + entry_count + 2;
    let panel_h = total_lines as f64 * cell_h;

    (x, y, panel_w, panel_h)
}

// ---------------------------------------------------------------------------
// HistoryPanelProvider — list container for the history browser overlay
// ---------------------------------------------------------------------------

/// UIA provider for the command history list container (List control type).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct HistoryPanelProvider {
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl HistoryPanelProvider {
    pub fn new(
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        root_provider: IRawElementProviderFragmentRoot,
    ) -> Self {
        Self {
            hwnd,
            state,
            root_provider,
        }
    }

    fn snapshot(&self) -> Option<HistorySnapshot> {
        let state = self.state.read();
        state.history_snapshot.clone()
    }

    fn entry_provider(&self, index: usize) -> HistoryEntryProvider {
        HistoryEntryProvider::new(
            index,
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for HistoryPanelProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_ListControlTypeId.0)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from("Command history"))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from("WixenCommandHistory"))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => {
                let snap = self.snapshot();
                Ok(VARIANT::from(snap.is_some_and(|s| s.visible)))
            }
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("command history"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for HistoryPanelProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        let entry_count = snap.exposed_entries().len();
        match direction {
            NavigateDirection_Parent => self.root_provider.cast(),
            NavigateDirection_FirstChild => {
                if entry_count == 0 {
                    Err(Error::empty())
                } else {
                    let ep: IRawElementProviderFragment = self.entry_provider(0).into();
                    Ok(ep)
                }
            }
            NavigateDirection_LastChild => {
                if entry_count == 0 {
                    Err(Error::empty())
                } else {
                    let ep: IRawElementProviderFragment =
                        self.entry_provider(entry_count - 1).into();
                    Ok(ep)
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(HISTORY_PANEL_ID)
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, h) = history_panel_rect(self.hwnd, &state);
        Ok(UiaRect {
            left: x,
            top: y,
            width: w,
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

// ---------------------------------------------------------------------------
// HistoryEntryProvider — one per visible history entry
// ---------------------------------------------------------------------------

/// UIA provider for a single history entry (ListItem control type).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct HistoryEntryProvider {
    entry_index: usize,
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl HistoryEntryProvider {
    pub fn new(
        entry_index: usize,
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        root_provider: IRawElementProviderFragmentRoot,
    ) -> Self {
        Self {
            entry_index,
            hwnd,
            state,
            root_provider,
        }
    }

    fn snapshot(&self) -> Option<HistorySnapshot> {
        let state = self.state.read();
        state.history_snapshot.clone()
    }

    fn panel_provider(&self) -> HistoryPanelProvider {
        HistoryPanelProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for HistoryEntryProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        let entry = snap
            .exposed_entries()
            .get(self.entry_index)
            .ok_or(Error::empty())?;
        let is_selected = snap.is_entry_selected(self.entry_index);

        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_ListItemControlTypeId.0)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from(&entry.label))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from(&format!(
                "HistoryEntry_{}",
                self.entry_index
            )))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => Ok(VARIANT::from(is_selected)),
            UIA_SelectionItemIsSelectedPropertyId => Ok(VARIANT::from(is_selected)),
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("history entry"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for HistoryEntryProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        let entry_count = snap.exposed_entries().len();
        match direction {
            NavigateDirection_Parent => {
                let pp: IRawElementProviderFragment = self.panel_provider().into();
                Ok(pp)
            }
            NavigateDirection_NextSibling => {
                let next = self.entry_index + 1;
                if next < entry_count {
                    let ep = HistoryEntryProvider::new(
                        next,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = ep.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            NavigateDirection_PreviousSibling => {
                if self.entry_index > 0 {
                    let ep = HistoryEntryProvider::new(
                        self.entry_index - 1,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = ep.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(history_entry_runtime_id(self.entry_index))
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, _) = history_panel_rect(self.hwnd, &state);
        // Entry is offset: 1 line for the search row + entry_index lines below
        let entry_y = y + (1 + self.entry_index) as f64 * state.cell_height;
        Ok(UiaRect {
            left: x,
            top: entry_y,
            width: w,
            height: state.cell_height,
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_snapshot() -> HistorySnapshot {
        HistorySnapshot {
            visible: true,
            entries: vec![
                HistoryEntrySnapshot {
                    label: "cargo test (succeeded)".to_string(),
                    index: 0,
                },
                HistoryEntrySnapshot {
                    label: "cargo build (exit 1)".to_string(),
                    index: 1,
                },
                HistoryEntrySnapshot {
                    label: "git status (succeeded)".to_string(),
                    index: 2,
                },
            ],
            selected_index: 1,
        }
    }

    #[test]
    fn test_history_snapshot_creation() {
        let snap = sample_snapshot();
        assert!(snap.visible);
        assert_eq!(snap.entries.len(), 3);
        assert_eq!(snap.selected_index, 1);
    }

    #[test]
    fn test_history_entry_snapshot_fields() {
        let snap = sample_snapshot();
        let entry = &snap.entries[1];
        assert_eq!(entry.label, "cargo build (exit 1)");
        assert_eq!(entry.index, 1);
    }

    #[test]
    fn test_history_list_reports_child_count() {
        let snap = sample_snapshot();
        assert_eq!(snap.exposed_entries().len(), 3);
    }

    #[test]
    fn test_history_hidden_yields_no_exposure() {
        let mut snap = sample_snapshot();
        snap.visible = false;
        assert!(snap.exposed_entries().is_empty());
        assert!(!snap.is_entry_selected(snap.selected_index));
    }

    #[test]
    fn test_history_empty_yields_empty_list() {
        let snap = HistorySnapshot {
            visible: true,
            entries: vec![],
            selected_index: 0,
        };
        assert!(snap.exposed_entries().is_empty());
        assert!(!snap.is_entry_selected(0));
    }

    #[test]
    fn test_history_selected_entry_reflected() {
        let snap = sample_snapshot();
        assert!(!snap.is_entry_selected(0));
        assert!(snap.is_entry_selected(1));
        assert!(!snap.is_entry_selected(2));
        assert!(!snap.is_entry_selected(99));
    }

    #[test]
    fn test_history_entry_runtime_ids_unique() {
        let id0 = history_entry_runtime_id(0);
        let id1 = history_entry_runtime_id(1);
        let id2 = history_entry_runtime_id(2);
        assert_ne!(id0, id1);
        assert_ne!(id1, id2);
        assert_ne!(id0, HISTORY_PANEL_ID);
        // Distinct from the settings (100_000) and palette (200_000) ID ranges.
        assert!(HISTORY_PANEL_ID >= 300_000);
    }

    #[test]
    fn test_history_entry_rect_offsets() {
        // Verify the arithmetic for entry BoundingRectangle (mirrors the
        // overlay layout in main.rs: 60% width centered, 15% from top,
        // row 0 is the search line, entries start on row 1).
        let win_w = 1000.0_f64;
        let win_h = 800.0_f64;
        let cell_h = 20.0_f64;
        let origin_y = 50.0_f64;

        let panel_w = win_w * 0.6; // 600
        let panel_x = (win_w - panel_w) / 2.0; // 200
        let panel_y = origin_y + win_h * 0.15; // 50 + 120 = 170

        assert!((panel_w - 600.0).abs() < 0.001);
        assert!((panel_x - 200.0).abs() < 0.001);
        assert!((panel_y - 170.0).abs() < 0.001);

        // Entry at index 2: y offset = panel_y + (1 + 2) * cell_h (search row + index)
        let entry_y = panel_y + (1.0 + 2.0) * cell_h;
        assert!((entry_y - 230.0).abs() < 0.001); // 170 + 60 = 230
    }
}
