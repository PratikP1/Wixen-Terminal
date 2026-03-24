//! UIA provider implementation — the COM interface that screen readers call.
//!
//! Implements IRawElementProviderSimple, Fragment, and FragmentRoot for the
//! terminal window. This is the bridge between the accessibility tree and UIA.

use std::sync::Arc;
use std::sync::atomic::AtomicI32;

use parking_lot::RwLock;
use tracing::debug;
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::Gdi::ClientToScreen;
use windows::Win32::System::Com::SAFEARRAY;
use windows::Win32::System::Ole::{SafeArrayCreateVector, SafeArrayPutElement};
use windows::Win32::System::Variant::VARIANT;
use windows::Win32::UI::Accessibility::*;
use windows::Win32::UI::Input::KeyboardAndMouse::SetFocus;
use windows::Win32::UI::WindowsAndMessaging::GetClientRect;
use windows::core::*;
use windows_core::ComObjectInterface;

use crate::fragment::ChildFragmentProvider;
use crate::grid_provider::{GridSnapshot, TerminalGridProvider};
use crate::palette_provider::{PalettePanelProvider, PaletteSnapshot};
use crate::settings_provider::{SettingsPanelProvider, SettingsSnapshot};
use crate::text_provider::TerminalTextProvider;
use crate::tree::AccessibilityTree;
use windows::Win32::UI::Accessibility::ITextProvider2;

/// Shared terminal state that the UIA provider reads.
pub struct TerminalA11yState {
    /// The accessibility tree
    pub tree: AccessibilityTree,
    /// Full text content of the terminal (for ITextProvider)
    pub full_text: String,
    /// Whether the terminal has focus
    pub has_focus: bool,
    /// Window title
    pub title: String,
    /// Grid snapshot for IGridProvider (TUI mode)
    pub grid_snapshot: GridSnapshot,
    /// Whether alternate screen is active (enables grid pattern)
    pub alt_screen_active: bool,
    /// Settings panel state snapshot for UIA (None when settings are closed).
    pub settings_snapshot: Option<SettingsSnapshot>,
    /// Command palette state snapshot for UIA (None when palette is closed).
    pub palette_snapshot: Option<PaletteSnapshot>,

    // --- Cursor position (set by main loop each frame) ---
    /// Cursor row (0-based, relative to viewport top).
    pub cursor_row: usize,
    /// Cursor column (0-based).
    pub cursor_col: usize,

    // --- Layout metrics for BoundingRectangle computation ---
    /// Cell width in pixels (set by main loop each frame).
    pub cell_width: f64,
    /// Cell height in pixels (set by main loop each frame).
    pub cell_height: f64,
    /// Tab bar height in pixels (0 when hidden).
    pub tab_bar_height: f64,
    /// Window client area width in pixels.
    pub window_width: f64,
    /// Window client area height in pixels.
    pub window_height: f64,
}

impl TerminalA11yState {
    pub fn new() -> Self {
        Self {
            tree: AccessibilityTree::new(),
            full_text: String::new(),
            has_focus: false,
            title: "Wixen Terminal".to_string(),
            grid_snapshot: GridSnapshot::default(),
            alt_screen_active: false,
            settings_snapshot: None,
            palette_snapshot: None,
            cursor_row: 0,
            cursor_col: 0,
            cell_width: 0.0,
            cell_height: 0.0,
            tab_bar_height: 0.0,
            window_width: 0.0,
            window_height: 0.0,
        }
    }

    /// Compute the UTF-16 code unit offset of the cursor within `full_text`.
    ///
    /// Walks lines of `full_text` to find the row, then advances by `cursor_col`
    /// characters (clamped to line length). Returns the offset clamped to text length.
    pub fn cursor_offset_utf16(&self) -> i32 {
        let utf16: Vec<u16> = self.full_text.encode_utf16().collect();
        let newline = '\n' as u16;

        let mut line_start = 0usize;
        let mut current_row = 0usize;

        // Walk to the target row
        while current_row < self.cursor_row && line_start < utf16.len() {
            if utf16[line_start] == newline {
                current_row += 1;
            }
            line_start += 1;
        }

        if line_start > utf16.len() {
            return utf16.len() as i32;
        }

        // Find end of current line
        let mut line_end = line_start;
        while line_end < utf16.len() && utf16[line_end] != newline {
            line_end += 1;
        }

        let line_len = line_end - line_start;
        let col = self.cursor_col.min(line_len);
        (line_start + col) as i32
    }
}

impl Default for TerminalA11yState {
    fn default() -> Self {
        Self::new()
    }
}

/// The root UIA provider for the terminal window.
///
/// This is a COM object that UIA clients (screen readers) call to query
/// the terminal's accessibility properties.
#[implement(
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    IRawElementProviderFragmentRoot
)]
pub struct TerminalProvider {
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    /// Lock-free cursor offset shared with text providers.
    cursor_offset: Arc<AtomicI32>,
}

impl TerminalProvider {
    pub fn new(
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        cursor_offset: Arc<AtomicI32>,
    ) -> Self {
        Self {
            hwnd,
            state,
            cursor_offset,
        }
    }

    /// Create a COM object from this provider.
    pub fn into_interface(self) -> IRawElementProviderSimple {
        self.into()
    }
}

// --- IRawElementProviderSimple implementation ---

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for TerminalProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        debug!(pattern_id = pattern_id.0, "GetPatternProvider called");
        match pattern_id {
            UIA_TextPatternId => {
                let enclosing: IRawElementProviderSimple =
                    ComObjectInterface::<IRawElementProviderSimple>::as_interface_ref(self)
                        .to_owned();
                let text_provider = TerminalTextProvider::new(
                    Arc::clone(&self.state),
                    enclosing,
                    Arc::clone(&self.cursor_offset),
                );
                let itext: ITextProvider = text_provider.into_interface();
                itext.cast()
            }
            UIA_TextPattern2Id => {
                let enclosing: IRawElementProviderSimple =
                    ComObjectInterface::<IRawElementProviderSimple>::as_interface_ref(self)
                        .to_owned();
                let text_provider = TerminalTextProvider::new(
                    Arc::clone(&self.state),
                    enclosing,
                    Arc::clone(&self.cursor_offset),
                );
                let itext2: ITextProvider2 = text_provider.into_interface2();
                itext2.cast()
            }
            UIA_GridPatternId => {
                // Expose grid pattern when alternate screen is active (TUI apps)
                let alt_active = self.state.read().alt_screen_active;
                if alt_active {
                    let grid = TerminalGridProvider::new(Arc::clone(&self.state));
                    let igrid: IGridProvider = grid.into_interface();
                    igrid.cast()
                } else {
                    Err(Error::empty())
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let state = self.state.read();

        match property_id {
            UIA_ControlTypePropertyId => {
                // Document control type — most appropriate for terminal text
                Ok(VARIANT::from(UIA_DocumentControlTypeId.0))
            }
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from(&state.title))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from("WixenTerminalView"))),
            UIA_ClassNamePropertyId => Ok(VARIANT::from(BSTR::from("WixenTerminalWindow"))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => Ok(VARIANT::from(state.has_focus)),
            UIA_NativeWindowHandlePropertyId => Ok(VARIANT::from(self.hwnd.0 as i32)),
            UIA_IsTextPatternAvailablePropertyId => Ok(VARIANT::from(true)),
            UIA_IsTextPattern2AvailablePropertyId => Ok(VARIANT::from(true)),
            UIA_IsGridPatternAvailablePropertyId => Ok(VARIANT::from(state.alt_screen_active)),
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("terminal"))),
            UIA_LiveSettingPropertyId => {
                // Polite live region — new output is announced when idle
                Ok(VARIANT::from(Polite.0))
            }
            _ => {
                // Return empty variant for unhandled properties (UIA convention)
                Ok(VARIANT::default())
            }
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        unsafe { UiaHostProviderFromHwnd(self.hwnd) }
    }
}

// --- IRawElementProviderFragment implementation ---

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for TerminalProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        debug!(direction = direction.0, "Fragment Navigate called");
        // Root has no siblings or parent in UIA
        match direction {
            NavigateDirection_Parent => Err(Error::empty()),
            NavigateDirection_FirstChild | NavigateDirection_LastChild => {
                // When palette is visible, it takes priority as the first child
                let (palette_visible, settings_visible) = {
                    let state = self.state.read();
                    let pv = state.palette_snapshot.as_ref().is_some_and(|s| s.visible);
                    let sv = state.settings_snapshot.as_ref().is_some_and(|s| s.visible);
                    (pv, sv)
                };

                if palette_visible {
                    let root =
                        ComObjectInterface::<IRawElementProviderFragmentRoot>::as_interface_ref(
                            self,
                        )
                        .to_owned();
                    let panel = PalettePanelProvider::new(self.hwnd, Arc::clone(&self.state), root);
                    let frag: IRawElementProviderFragment = panel.into();
                    return Ok(frag);
                }

                if settings_visible {
                    let root =
                        ComObjectInterface::<IRawElementProviderFragmentRoot>::as_interface_ref(
                            self,
                        )
                        .to_owned();
                    let panel =
                        SettingsPanelProvider::new(self.hwnd, Arc::clone(&self.state), root);
                    let frag: IRawElementProviderFragment = panel.into();
                    return Ok(frag);
                }

                let child_id = {
                    let state = self.state.read();
                    let children = state.tree.root.children();
                    if direction == NavigateDirection_FirstChild {
                        children.first().map(|n| n.id())
                    } else {
                        children.last().map(|n| n.id())
                    }
                };
                match child_id {
                    Some(id) => {
                        let root = ComObjectInterface::<IRawElementProviderFragmentRoot>::as_interface_ref(self).to_owned();
                        Ok(
                            ChildFragmentProvider::new(
                                id,
                                self.hwnd,
                                Arc::clone(&self.state),
                                root,
                            )
                            .into(),
                        )
                    }
                    None => Err(Error::empty()),
                }
            }
            NavigateDirection_NextSibling | NavigateDirection_PreviousSibling => {
                Err(Error::empty())
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        // Root provider runtime ID: [UiaAppendRuntimeId, 1]
        let runtime_id: [i32; 2] = [UiaAppendRuntimeId as i32, 1];
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

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let mut rect = RECT::default();
        unsafe {
            let _ = GetClientRect(self.hwnd, &mut rect);
            let mut point = POINT {
                x: rect.left,
                y: rect.top,
            };
            let _ = ClientToScreen(self.hwnd, &mut point);
            Ok(UiaRect {
                left: point.x as f64,
                top: point.y as f64,
                width: (rect.right - rect.left) as f64,
                height: (rect.bottom - rect.top) as f64,
            })
        }
    }

    fn GetEmbeddedFragmentRoots(&self) -> Result<*mut SAFEARRAY> {
        // No embedded fragment roots
        Err(Error::empty())
    }

    fn SetFocus(&self) -> Result<()> {
        unsafe {
            let _ = SetFocus(Some(self.hwnd));
        }
        Ok(())
    }

    fn FragmentRoot(&self) -> Result<IRawElementProviderFragmentRoot> {
        // The root provider IS the fragment root — get interface via ComObjectInterface
        Ok(
            ComObjectInterface::<IRawElementProviderFragmentRoot>::as_interface_ref(self)
                .to_owned(),
        )
    }
}

// --- IRawElementProviderFragmentRoot implementation ---

impl IRawElementProviderFragmentRoot_Impl for TerminalProvider_Impl {
    fn ElementProviderFromPoint(&self, _x: f64, _y: f64) -> Result<IRawElementProviderFragment> {
        // Return self — we don't support sub-element hit testing yet
        Ok(ComObjectInterface::<IRawElementProviderFragment>::as_interface_ref(self).to_owned())
    }

    fn GetFocus(&self) -> Result<IRawElementProviderFragment> {
        // When palette is visible, return the search box provider
        let palette_visible = {
            let state = self.state.read();
            state.palette_snapshot.as_ref().is_some_and(|s| s.visible)
        };
        if palette_visible {
            let root =
                ComObjectInterface::<IRawElementProviderFragmentRoot>::as_interface_ref(self)
                    .to_owned();
            use crate::palette_provider::PaletteSearchProvider;
            let sp = PaletteSearchProvider::new(self.hwnd, Arc::clone(&self.state), root);
            let frag: IRawElementProviderFragment = sp.into();
            return Ok(frag);
        }

        // When settings are visible, return the focused field provider
        let focused_field = {
            let state = self.state.read();
            state
                .settings_snapshot
                .as_ref()
                .filter(|s| s.visible)
                .map(|s| s.focused_field)
        };
        if let Some(field_idx) = focused_field {
            let root =
                ComObjectInterface::<IRawElementProviderFragmentRoot>::as_interface_ref(self)
                    .to_owned();
            use crate::settings_provider::SettingsFieldProvider;
            let fp =
                SettingsFieldProvider::new(field_idx, self.hwnd, Arc::clone(&self.state), root);
            let frag: IRawElementProviderFragment = fp.into();
            return Ok(frag);
        }
        // The terminal view itself has focus
        Ok(ComObjectInterface::<IRawElementProviderFragment>::as_interface_ref(self).to_owned())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn provider_options_includes_server_side_and_com_threading() {
        let state = Arc::new(RwLock::new(TerminalA11yState::new()));
        let cursor_offset = Arc::new(AtomicI32::new(0));
        let provider = TerminalProvider::new(HWND::default(), state, cursor_offset);
        let iface: IRawElementProviderSimple = provider.into();
        let opts = unsafe { iface.ProviderOptions().unwrap() };
        assert!(
            opts & ProviderOptions_ServerSideProvider != ProviderOptions(0),
            "Must be server-side provider"
        );
        assert!(
            opts & ProviderOptions_UseComThreading != ProviderOptions(0),
            "Must use COM threading — windows-rs #[implement] vtables crash with \
             DEP violation when called directly from MTA threads"
        );
    }
}
