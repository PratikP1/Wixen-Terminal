//! UIA providers for the Command Palette overlay.
//!
//! Exposes palette state to screen readers: a search box (Edit + IValueProvider)
//! and a filtered list of command entries (ListItem per entry with selection state).

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
// Snapshot types — plain data extracted from CommandPalette state
// ---------------------------------------------------------------------------

/// Snapshot of a single command palette entry.
#[derive(Debug, Clone)]
pub struct PaletteEntrySnapshot {
    /// Human-readable label (e.g. "New Tab").
    pub label: String,
    /// Optional keyboard shortcut hint (e.g. "Ctrl+Shift+T").
    pub shortcut: Option<String>,
    /// Category for grouping (e.g. "Tabs").
    pub category: String,
    /// Whether this entry is currently selected/highlighted.
    pub is_selected: bool,
    /// Index within the filtered list.
    pub index: usize,
}

/// Complete snapshot of the command palette state.
#[derive(Debug, Clone)]
pub struct PaletteSnapshot {
    /// Whether the palette is currently visible.
    pub visible: bool,
    /// Current search query text.
    pub query: String,
    /// Filtered entries visible in the palette.
    pub entries: Vec<PaletteEntrySnapshot>,
    /// Index of the selected entry within `entries`.
    pub selected_index: usize,
}

// ---------------------------------------------------------------------------
// Runtime ID generation — unique IDs for palette UIA elements
// ---------------------------------------------------------------------------

/// Base offset for palette runtime IDs (avoid collisions with tree + settings).
const PALETTE_ID_BASE: i32 = 200_000;
/// Panel runtime ID.
const PALETTE_PANEL_ID: i32 = PALETTE_ID_BASE;
/// Search box runtime ID.
const PALETTE_SEARCH_ID: i32 = PALETTE_ID_BASE + 1;

fn palette_entry_runtime_id(index: usize) -> i32 {
    PALETTE_ID_BASE + 100 + index as i32
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

/// Compute the palette panel rectangle in screen coordinates.
///
/// Returns (x, y, width, total_height) matching the overlay layout in main.rs:
/// 60% window width centered, 15% from top.
fn palette_panel_rect(hwnd: HWND, state: &TerminalA11yState) -> (f64, f64, f64, f64) {
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
        .palette_snapshot
        .as_ref()
        .map(|s| s.entries.len().min(12))
        .unwrap_or(0);
    // 1 search box line + entries + 2 lines padding
    let total_lines = 1 + entry_count + 2;
    let panel_h = total_lines as f64 * cell_h;

    (x, y, panel_w, panel_h)
}

// ---------------------------------------------------------------------------
// PalettePanelProvider — root container for the palette overlay
// ---------------------------------------------------------------------------

/// UIA provider for the command palette container (Window control type).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct PalettePanelProvider {
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl PalettePanelProvider {
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

    fn snapshot(&self) -> Option<PaletteSnapshot> {
        let state = self.state.read();
        state.palette_snapshot.clone()
    }

    fn search_provider(&self) -> PaletteSearchProvider {
        PaletteSearchProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }

    fn entry_provider(&self, index: usize) -> PaletteEntryProvider {
        PaletteEntryProvider::new(
            index,
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for PalettePanelProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_WindowControlTypeId.0)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from("Command Palette"))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from("WixenCommandPalette"))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => {
                let snap = self.snapshot();
                Ok(VARIANT::from(snap.is_some()))
            }
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("command palette"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for PalettePanelProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        match direction {
            NavigateDirection_Parent => self.root_provider.cast(),
            NavigateDirection_FirstChild => {
                // First child is always the search box
                let sp: IRawElementProviderFragment = self.search_provider().into();
                Ok(sp)
            }
            NavigateDirection_LastChild => {
                // Last child: last entry, or search box if no entries
                if snap.entries.is_empty() {
                    let sp: IRawElementProviderFragment = self.search_provider().into();
                    Ok(sp)
                } else {
                    let ep: IRawElementProviderFragment =
                        self.entry_provider(snap.entries.len() - 1).into();
                    Ok(ep)
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(PALETTE_PANEL_ID)
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, h) = palette_panel_rect(self.hwnd, &state);
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
// PaletteSearchProvider — the search/filter text box
// ---------------------------------------------------------------------------

/// UIA provider for the palette search box (Edit + IValueProvider).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment, IValueProvider)]
pub struct PaletteSearchProvider {
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl PaletteSearchProvider {
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

    fn snapshot(&self) -> Option<PaletteSnapshot> {
        let state = self.state.read();
        state.palette_snapshot.clone()
    }

    fn panel_provider(&self) -> PalettePanelProvider {
        PalettePanelProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }

    fn entry_provider(&self, index: usize) -> PaletteEntryProvider {
        PaletteEntryProvider::new(
            index,
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for PaletteSearchProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        match pattern_id {
            UIA_ValuePatternId => {
                let val: IValueProvider =
                    windows_core::ComObjectInterface::<IValueProvider>::as_interface_ref(self)
                        .to_owned();
                val.cast()
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_EditControlTypeId.0)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from("Search commands"))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from("PaletteSearchBox"))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => {
                // Search box always has focus when palette is visible
                let snap = self.snapshot();
                Ok(VARIANT::from(snap.is_some_and(|s| s.visible)))
            }
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("search box"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

// --- IValueProvider for the search box ---

impl IValueProvider_Impl for PaletteSearchProvider_Impl {
    fn SetValue(&self, _val: &PCWSTR) -> Result<()> {
        // Read-only from UIA side; typing goes through key events
        Err(Error::empty())
    }

    fn Value(&self) -> Result<BSTR> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        Ok(BSTR::from(&snap.query))
    }

    fn IsReadOnly(&self) -> Result<BOOL> {
        Ok(BOOL::from(true))
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for PaletteSearchProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        match direction {
            NavigateDirection_Parent => {
                let pp: IRawElementProviderFragment = self.panel_provider().into();
                Ok(pp)
            }
            NavigateDirection_NextSibling => {
                // After search box comes first entry (if any)
                if snap.entries.is_empty() {
                    Err(Error::empty())
                } else {
                    let ep: IRawElementProviderFragment = self.entry_provider(0).into();
                    Ok(ep)
                }
            }
            NavigateDirection_PreviousSibling => {
                // Search box is the first child — no previous sibling
                Err(Error::empty())
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(PALETTE_SEARCH_ID)
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, _) = palette_panel_rect(self.hwnd, &state);
        // Search box is the first line of the palette
        Ok(UiaRect {
            left: x,
            top: y,
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
// PaletteEntryProvider — one per visible command entry
// ---------------------------------------------------------------------------

/// UIA provider for a single command palette entry (ListItem control type).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct PaletteEntryProvider {
    entry_index: usize,
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl PaletteEntryProvider {
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

    fn snapshot(&self) -> Option<PaletteSnapshot> {
        let state = self.state.read();
        state.palette_snapshot.clone()
    }

    fn entry_snapshot(&self) -> Option<PaletteEntrySnapshot> {
        let state = self.state.read();
        let snap = state.palette_snapshot.as_ref()?;
        snap.entries.get(self.entry_index).cloned()
    }

    fn panel_provider(&self) -> PalettePanelProvider {
        PalettePanelProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }

    fn search_provider(&self) -> PaletteSearchProvider {
        PaletteSearchProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for PaletteEntryProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let entry = self.entry_snapshot().ok_or(Error::empty())?;

        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_ListItemControlTypeId.0)),
            UIA_NamePropertyId => {
                // Build a descriptive name: "New Tab, Ctrl+Shift+T, Tabs"
                let mut name = entry.label.clone();
                if let Some(shortcut) = &entry.shortcut {
                    name.push_str(", ");
                    name.push_str(shortcut);
                }
                name.push_str(", ");
                name.push_str(&entry.category);
                Ok(VARIANT::from(BSTR::from(&name)))
            }
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from(&format!(
                "PaletteEntry_{}",
                self.entry_index
            )))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => Ok(VARIANT::from(entry.is_selected)),
            UIA_SelectionItemIsSelectedPropertyId => Ok(VARIANT::from(entry.is_selected)),
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("command"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for PaletteEntryProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        match direction {
            NavigateDirection_Parent => {
                let pp: IRawElementProviderFragment = self.panel_provider().into();
                Ok(pp)
            }
            NavigateDirection_NextSibling => {
                let next = self.entry_index + 1;
                if next < snap.entries.len() {
                    let ep = PaletteEntryProvider::new(
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
                    let ep = PaletteEntryProvider::new(
                        self.entry_index - 1,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = ep.into();
                    Ok(frag)
                } else {
                    // Previous sibling of first entry is the search box
                    let sp: IRawElementProviderFragment = self.search_provider().into();
                    Ok(sp)
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(palette_entry_runtime_id(self.entry_index))
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, _) = palette_panel_rect(self.hwnd, &state);
        // Entry is offset: 1 line for search box + entry_index lines below
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

    fn sample_snapshot() -> PaletteSnapshot {
        PaletteSnapshot {
            visible: true,
            query: "tab".to_string(),
            entries: vec![
                PaletteEntrySnapshot {
                    label: "New Tab".to_string(),
                    shortcut: Some("Ctrl+Shift+T".to_string()),
                    category: "Tabs".to_string(),
                    is_selected: true,
                    index: 0,
                },
                PaletteEntrySnapshot {
                    label: "Close Tab".to_string(),
                    shortcut: Some("Ctrl+Shift+W".to_string()),
                    category: "Tabs".to_string(),
                    is_selected: false,
                    index: 1,
                },
                PaletteEntrySnapshot {
                    label: "Next Tab".to_string(),
                    shortcut: Some("Ctrl+Tab".to_string()),
                    category: "Tabs".to_string(),
                    is_selected: false,
                    index: 2,
                },
            ],
            selected_index: 0,
        }
    }

    #[test]
    fn test_palette_snapshot_creation() {
        let snap = sample_snapshot();
        assert!(snap.visible);
        assert_eq!(snap.query, "tab");
        assert_eq!(snap.entries.len(), 3);
        assert!(snap.entries[0].is_selected);
        assert!(!snap.entries[1].is_selected);
    }

    #[test]
    fn test_palette_entry_snapshot_fields() {
        let snap = sample_snapshot();
        let entry = &snap.entries[0];
        assert_eq!(entry.label, "New Tab");
        assert_eq!(entry.shortcut.as_deref(), Some("Ctrl+Shift+T"));
        assert_eq!(entry.category, "Tabs");
        assert_eq!(entry.index, 0);
    }

    #[test]
    fn test_palette_entry_runtime_ids_unique() {
        let id0 = palette_entry_runtime_id(0);
        let id1 = palette_entry_runtime_id(1);
        let id2 = palette_entry_runtime_id(2);
        assert_ne!(id0, id1);
        assert_ne!(id1, id2);
        assert_ne!(id0, PALETTE_PANEL_ID);
        assert_ne!(id0, PALETTE_SEARCH_ID);
    }

    #[test]
    fn test_palette_snapshot_empty_entries() {
        let snap = PaletteSnapshot {
            visible: true,
            query: "zzzzz".to_string(),
            entries: vec![],
            selected_index: 0,
        };
        assert!(snap.entries.is_empty());
        assert!(snap.visible);
    }

    #[test]
    fn test_palette_selected_entry() {
        let snap = sample_snapshot();
        let selected = snap
            .entries
            .iter()
            .find(|e| e.is_selected)
            .expect("should have a selected entry");
        assert_eq!(selected.label, "New Tab");
        assert_eq!(snap.selected_index, 0);
    }

    #[test]
    fn test_palette_entry_rect_offsets() {
        // Verify the arithmetic for palette entry BoundingRectangle
        let win_w = 1000.0_f64;
        let win_h = 800.0_f64;
        let cell_h = 20.0_f64;
        let origin_y = 50.0_f64;

        // Panel geometry: 60% width, 15% from top
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
