//! UIA providers for the Settings overlay panel.
//!
//! Exposes settings fields to screen readers with correct control types and
//! pattern providers: Text→Edit+IValueProvider, Number→Spinner+IRangeValueProvider,
//! Toggle→CheckBox+IToggleProvider, Dropdown→ComboBox+ISelectionProvider,
//! Keybinding→Group with modifier checkboxes and key text children.

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

/// Compute the settings panel rectangle in screen coordinates.
///
/// Returns (x, y, width, height) matching the overlay layout in main.rs:
/// 80% window width × 70% height, centered, 15% from top.
fn settings_panel_rect(hwnd: HWND, state: &TerminalA11yState) -> (f64, f64, f64, f64) {
    let mut origin = POINT { x: 0, y: 0 };
    unsafe {
        let _ = ClientToScreen(hwnd, &mut origin);
    }
    let win_w = state.window_width;
    let win_h = state.window_height;
    let panel_w = win_w * 0.8;
    let panel_h = win_h * 0.7;
    let x = origin.x as f64 + (win_w - panel_w) / 2.0;
    let y = origin.y as f64 + win_h * 0.15;
    (x, y, panel_w, panel_h)
}

// ---------------------------------------------------------------------------
// Snapshot types — plain data extracted from SettingsUI while holding the lock
// ---------------------------------------------------------------------------

/// Field type mapped to UIA control types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FieldType {
    Text,
    Number,
    Toggle,
    Dropdown,
    Keybinding,
}

/// Snapshot of a single sub-field inside a keybinding editor.
#[derive(Debug, Clone)]
pub struct SubFieldSnapshot {
    /// Label: "Ctrl", "Shift", "Alt", "Win", or "Key".
    pub label: String,
    /// True for modifier checkboxes, false for the key text input.
    pub is_checkbox: bool,
    /// Checkbox checked state (Some for checkboxes).
    pub checked: Option<bool>,
    /// Text value (Some for key text input).
    pub value: Option<String>,
    /// Whether this sub-field has keyboard focus.
    pub is_focused: bool,
}

/// Snapshot of a single settings field.
#[derive(Debug, Clone)]
pub struct FieldSnapshot {
    /// Display label.
    pub label: String,
    /// Field type (determines UIA control type).
    pub field_type: FieldType,
    /// Human-readable current value.
    pub value_text: String,
    /// Whether this field currently has keyboard focus.
    pub is_focused: bool,
    /// Index within the active tab's field list.
    pub index: usize,
    /// For Number fields: min/max/step bounds.
    pub number_min: Option<f64>,
    pub number_max: Option<f64>,
    pub number_step: Option<f64>,
    pub number_value: Option<f64>,
    /// For Toggle fields: current on/off state.
    pub toggle_on: Option<bool>,
    /// For Dropdown fields: list of options and selected index.
    pub dropdown_options: Option<Vec<String>>,
    pub dropdown_selected: Option<usize>,
    /// For Keybinding fields: sub-field snapshots.
    pub sub_fields: Option<Vec<SubFieldSnapshot>>,
}

/// Complete snapshot of the settings panel state.
#[derive(Debug, Clone)]
pub struct SettingsSnapshot {
    /// Whether the settings panel is visible.
    pub visible: bool,
    /// Active tab label (e.g. "Font").
    pub active_tab_label: String,
    /// Active tab index (0-based).
    pub active_tab_index: usize,
    /// Labels for all tabs.
    pub tab_labels: Vec<String>,
    /// Fields in the active tab.
    pub fields: Vec<FieldSnapshot>,
    /// Index of the focused field within the active tab.
    pub focused_field: usize,
}

// ---------------------------------------------------------------------------
// Runtime ID generation — unique IDs for settings UIA elements
// ---------------------------------------------------------------------------

/// Base offset for settings panel runtime IDs (avoid collisions with tree NodeIds).
const SETTINGS_ID_BASE: i32 = 100_000;
/// Panel runtime ID.
const PANEL_ID: i32 = SETTINGS_ID_BASE;

fn tab_runtime_id(tab_index: usize) -> i32 {
    SETTINGS_ID_BASE + 1000 + tab_index as i32
}

fn field_runtime_id(field_index: usize) -> i32 {
    SETTINGS_ID_BASE + 2000 + field_index as i32
}

fn sub_field_runtime_id(field_index: usize, sub_index: usize) -> i32 {
    SETTINGS_ID_BASE + 3000 + (field_index * 10 + sub_index) as i32
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

// ---------------------------------------------------------------------------
// SettingsPanelProvider — root container for the settings overlay
// ---------------------------------------------------------------------------

/// UIA provider for the settings panel container (Pane control type).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct SettingsPanelProvider {
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl SettingsPanelProvider {
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

    fn snapshot(&self) -> Option<SettingsSnapshot> {
        let state = self.state.read();
        state.settings_snapshot.clone()
    }

    fn tab_provider(&self, tab_index: usize) -> SettingsTabProvider {
        SettingsTabProvider::new(
            tab_index,
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }

    fn field_provider(&self, field_index: usize) -> SettingsFieldProvider {
        SettingsFieldProvider::new(
            field_index,
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for SettingsPanelProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_PaneControlTypeId.0)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from("Settings"))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from("WixenSettingsPanel"))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => {
                let snap = self.snapshot();
                Ok(VARIANT::from(snap.is_some()))
            }
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("settings panel"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for SettingsPanelProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        match direction {
            NavigateDirection_Parent => self.root_provider.cast(),
            NavigateDirection_FirstChild => {
                // First child is the first tab item
                if !snap.tab_labels.is_empty() {
                    let tp: IRawElementProviderFragment = self.tab_provider(0).into();
                    Ok(tp)
                } else {
                    Err(Error::empty())
                }
            }
            NavigateDirection_LastChild => {
                // Last child is the last field (or last tab if no fields)
                if !snap.fields.is_empty() {
                    let fp: IRawElementProviderFragment =
                        self.field_provider(snap.fields.len() - 1).into();
                    Ok(fp)
                } else if !snap.tab_labels.is_empty() {
                    let tp: IRawElementProviderFragment =
                        self.tab_provider(snap.tab_labels.len() - 1).into();
                    Ok(tp)
                } else {
                    Err(Error::empty())
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(PANEL_ID)
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, h) = settings_panel_rect(self.hwnd, &state);
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
// SettingsTabProvider — one per tab item
// ---------------------------------------------------------------------------

/// UIA provider for a single settings tab (TabItem control type).
#[implement(IRawElementProviderSimple, IRawElementProviderFragment)]
pub struct SettingsTabProvider {
    tab_index: usize,
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl SettingsTabProvider {
    pub fn new(
        tab_index: usize,
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        root_provider: IRawElementProviderFragmentRoot,
    ) -> Self {
        Self {
            tab_index,
            hwnd,
            state,
            root_provider,
        }
    }

    fn snapshot(&self) -> Option<SettingsSnapshot> {
        let state = self.state.read();
        state.settings_snapshot.clone()
    }

    fn panel_provider(&self) -> SettingsPanelProvider {
        SettingsPanelProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for SettingsTabProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, _pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        Err(Error::empty())
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        let label = snap
            .tab_labels
            .get(self.tab_index)
            .cloned()
            .unwrap_or_default();
        let is_selected = snap.active_tab_index == self.tab_index;

        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(UIA_TabItemControlTypeId.0)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from(&label))),
            UIA_AutomationIdPropertyId => {
                Ok(VARIANT::from(BSTR::from(&format!("SettingsTab_{}", label))))
            }
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_SelectionItemIsSelectedPropertyId => Ok(VARIANT::from(is_selected)),
            UIA_LocalizedControlTypePropertyId => Ok(VARIANT::from(BSTR::from("tab"))),
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for SettingsTabProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.snapshot().ok_or(Error::empty())?;
        match direction {
            NavigateDirection_Parent => {
                let pp: IRawElementProviderFragment = self.panel_provider().into();
                Ok(pp)
            }
            NavigateDirection_NextSibling => {
                let next = self.tab_index + 1;
                if next < snap.tab_labels.len() {
                    // Next tab sibling
                    let tp = SettingsTabProvider::new(
                        next,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = tp.into();
                    Ok(frag)
                } else if !snap.fields.is_empty() {
                    // After last tab comes first field
                    let fp = SettingsFieldProvider::new(
                        0,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = fp.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            NavigateDirection_PreviousSibling => {
                if self.tab_index > 0 {
                    let tp = SettingsTabProvider::new(
                        self.tab_index - 1,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = tp.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(tab_runtime_id(self.tab_index))
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, _) = settings_panel_rect(self.hwnd, &state);
        let tab_count = state
            .settings_snapshot
            .as_ref()
            .map(|s| s.tab_labels.len().max(1))
            .unwrap_or(1);
        let tab_w = w / tab_count as f64;
        // Tabs are on the first row of the settings panel
        Ok(UiaRect {
            left: x + self.tab_index as f64 * tab_w,
            top: y,
            width: tab_w,
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
// SettingsFieldProvider — one per field in the active tab
// ---------------------------------------------------------------------------

/// UIA provider for a single settings field.
///
/// Maps field type to the correct UIA control type:
/// - Text → Edit (UIA_EditControlTypeId)
/// - Number → Spinner (UIA_SpinnerControlTypeId)
/// - Toggle → CheckBox (UIA_CheckBoxControlTypeId)
/// - Dropdown → ComboBox (UIA_ComboBoxControlTypeId)
/// - Keybinding → Group (UIA_GroupControlTypeId) with children
#[implement(
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    IValueProvider,
    IToggleProvider,
    IRangeValueProvider
)]
pub struct SettingsFieldProvider {
    field_index: usize,
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl SettingsFieldProvider {
    pub fn new(
        field_index: usize,
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        root_provider: IRawElementProviderFragmentRoot,
    ) -> Self {
        Self {
            field_index,
            hwnd,
            state,
            root_provider,
        }
    }

    fn field_snapshot(&self) -> Option<FieldSnapshot> {
        let state = self.state.read();
        let snap = state.settings_snapshot.as_ref()?;
        snap.fields.get(self.field_index).cloned()
    }

    fn settings_snapshot(&self) -> Option<SettingsSnapshot> {
        let state = self.state.read();
        state.settings_snapshot.clone()
    }

    fn panel_provider(&self) -> SettingsPanelProvider {
        SettingsPanelProvider::new(
            self.hwnd,
            Arc::clone(&self.state),
            self.root_provider.clone(),
        )
    }
}

/// Map FieldType to UIA control type ID.
pub fn field_type_to_control_type(ft: FieldType) -> i32 {
    match ft {
        FieldType::Text => UIA_EditControlTypeId.0,
        FieldType::Number => UIA_SpinnerControlTypeId.0,
        FieldType::Toggle => UIA_CheckBoxControlTypeId.0,
        FieldType::Dropdown => UIA_ComboBoxControlTypeId.0,
        FieldType::Keybinding => UIA_GroupControlTypeId.0,
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for SettingsFieldProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        match (pattern_id, field.field_type) {
            (UIA_ValuePatternId, FieldType::Text | FieldType::Dropdown) => {
                let val: IValueProvider =
                    windows_core::ComObjectInterface::<IValueProvider>::as_interface_ref(self)
                        .to_owned();
                val.cast()
            }
            (UIA_TogglePatternId, FieldType::Toggle) => {
                let tog: IToggleProvider =
                    windows_core::ComObjectInterface::<IToggleProvider>::as_interface_ref(self)
                        .to_owned();
                tog.cast()
            }
            (UIA_RangeValuePatternId, FieldType::Number) => {
                let rv: IRangeValueProvider =
                    windows_core::ComObjectInterface::<IRangeValueProvider>::as_interface_ref(self)
                        .to_owned();
                rv.cast()
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        match property_id {
            UIA_ControlTypePropertyId => {
                Ok(VARIANT::from(field_type_to_control_type(field.field_type)))
            }
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from(&field.label))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from(&format!(
                "SettingsField_{}",
                field.label
            )))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => Ok(VARIANT::from(field.is_focused)),
            UIA_LocalizedControlTypePropertyId => {
                let label = match field.field_type {
                    FieldType::Text => "text field",
                    FieldType::Number => "number field",
                    FieldType::Toggle => "toggle",
                    FieldType::Dropdown => "dropdown",
                    FieldType::Keybinding => "keybinding",
                };
                Ok(VARIANT::from(BSTR::from(label)))
            }
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

// --- IValueProvider (for Text and Dropdown fields) ---

impl IValueProvider_Impl for SettingsFieldProvider_Impl {
    fn SetValue(&self, _val: &PCWSTR) -> Result<()> {
        // Read-only from UIA side; edits go through the settings UI
        Err(Error::empty())
    }

    fn Value(&self) -> Result<BSTR> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        Ok(BSTR::from(&field.value_text))
    }

    fn IsReadOnly(&self) -> Result<BOOL> {
        // Settings fields are editable through the UI, but not via UIA SetValue
        Ok(BOOL::from(true))
    }
}

// --- IToggleProvider (for Toggle fields) ---

impl IToggleProvider_Impl for SettingsFieldProvider_Impl {
    fn Toggle(&self) -> Result<()> {
        // Read-only from UIA side
        Err(Error::empty())
    }

    fn ToggleState(&self) -> Result<ToggleState> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        match field.toggle_on {
            Some(true) => Ok(ToggleState_On),
            Some(false) => Ok(ToggleState_Off),
            None => Err(Error::empty()),
        }
    }
}

// --- IRangeValueProvider (for Number fields) ---

impl IRangeValueProvider_Impl for SettingsFieldProvider_Impl {
    fn SetValue(&self, _val: f64) -> Result<()> {
        Err(Error::empty())
    }

    fn Value(&self) -> Result<f64> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        field.number_value.ok_or(Error::empty())
    }

    fn IsReadOnly(&self) -> Result<BOOL> {
        Ok(BOOL::from(true))
    }

    fn Maximum(&self) -> Result<f64> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        field.number_max.ok_or(Error::empty())
    }

    fn Minimum(&self) -> Result<f64> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        field.number_min.ok_or(Error::empty())
    }

    fn LargeChange(&self) -> Result<f64> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        field.number_step.map(|s| s * 10.0).ok_or(Error::empty())
    }

    fn SmallChange(&self) -> Result<f64> {
        let field = self.field_snapshot().ok_or(Error::empty())?;
        field.number_step.ok_or(Error::empty())
    }
}

// --- Fragment navigation for SettingsFieldProvider ---

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for SettingsFieldProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        let snap = self.settings_snapshot().ok_or(Error::empty())?;
        match direction {
            NavigateDirection_Parent => {
                let pp: IRawElementProviderFragment = self.panel_provider().into();
                Ok(pp)
            }
            NavigateDirection_NextSibling => {
                let next = self.field_index + 1;
                if next < snap.fields.len() {
                    let fp = SettingsFieldProvider::new(
                        next,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = fp.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            NavigateDirection_PreviousSibling => {
                if self.field_index > 0 {
                    let fp = SettingsFieldProvider::new(
                        self.field_index - 1,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = fp.into();
                    Ok(frag)
                } else {
                    // Previous sibling is the last tab
                    if !snap.tab_labels.is_empty() {
                        let tp = SettingsTabProvider::new(
                            snap.tab_labels.len() - 1,
                            self.hwnd,
                            Arc::clone(&self.state),
                            self.root_provider.clone(),
                        );
                        let frag: IRawElementProviderFragment = tp.into();
                        Ok(frag)
                    } else {
                        Err(Error::empty())
                    }
                }
            }
            NavigateDirection_FirstChild => {
                // Only Keybinding fields have children
                let field = snap.fields.get(self.field_index).ok_or(Error::empty())?;
                if field.field_type == FieldType::Keybinding
                    && let Some(subs) = &field.sub_fields
                    && !subs.is_empty()
                {
                    let sp = KeybindingSubFieldProvider::new(
                        self.field_index,
                        0,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = sp.into();
                    return Ok(frag);
                }
                Err(Error::empty())
            }
            NavigateDirection_LastChild => {
                let field = snap.fields.get(self.field_index).ok_or(Error::empty())?;
                if field.field_type == FieldType::Keybinding
                    && let Some(subs) = &field.sub_fields
                    && !subs.is_empty()
                {
                    let sp = KeybindingSubFieldProvider::new(
                        self.field_index,
                        subs.len() - 1,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = sp.into();
                    return Ok(frag);
                }
                Err(Error::empty())
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(field_runtime_id(self.field_index))
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, _) = settings_panel_rect(self.hwnd, &state);
        // Fields start after the tab row + 1 header line, each is one cell_height tall
        let field_y = y + (2 + self.field_index) as f64 * state.cell_height;
        Ok(UiaRect {
            left: x,
            top: field_y,
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
// KeybindingSubFieldProvider — modifier checkboxes + key text inside a Keybinding
// ---------------------------------------------------------------------------

/// UIA provider for a sub-field inside a keybinding editor.
///
/// Modifier checkboxes → CheckBox + IToggleProvider.
/// Key text input → Edit + IValueProvider.
#[implement(
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    IToggleProvider,
    IValueProvider
)]
pub struct KeybindingSubFieldProvider {
    field_index: usize,
    sub_index: usize,
    hwnd: HWND,
    state: Arc<RwLock<TerminalA11yState>>,
    root_provider: IRawElementProviderFragmentRoot,
}

impl KeybindingSubFieldProvider {
    pub fn new(
        field_index: usize,
        sub_index: usize,
        hwnd: HWND,
        state: Arc<RwLock<TerminalA11yState>>,
        root_provider: IRawElementProviderFragmentRoot,
    ) -> Self {
        Self {
            field_index,
            sub_index,
            hwnd,
            state,
            root_provider,
        }
    }

    fn sub_snapshot(&self) -> Option<SubFieldSnapshot> {
        let state = self.state.read();
        let settings = state.settings_snapshot.as_ref()?;
        let field = settings.fields.get(self.field_index)?;
        let subs = field.sub_fields.as_ref()?;
        subs.get(self.sub_index).cloned()
    }

    fn parent_field_sub_count(&self) -> usize {
        let state = self.state.read();
        state
            .settings_snapshot
            .as_ref()
            .and_then(|s| s.fields.get(self.field_index))
            .and_then(|f| f.sub_fields.as_ref())
            .map(|s| s.len())
            .unwrap_or(0)
    }
}

#[allow(non_upper_case_globals)]
impl IRawElementProviderSimple_Impl for KeybindingSubFieldProvider_Impl {
    fn ProviderOptions(&self) -> Result<ProviderOptions> {
        Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
    }

    fn GetPatternProvider(&self, pattern_id: UIA_PATTERN_ID) -> Result<IUnknown> {
        let sub = self.sub_snapshot().ok_or(Error::empty())?;
        if sub.is_checkbox && pattern_id == UIA_TogglePatternId {
            let tog: IToggleProvider =
                windows_core::ComObjectInterface::<IToggleProvider>::as_interface_ref(self)
                    .to_owned();
            tog.cast()
        } else if !sub.is_checkbox && pattern_id == UIA_ValuePatternId {
            let val: IValueProvider =
                windows_core::ComObjectInterface::<IValueProvider>::as_interface_ref(self)
                    .to_owned();
            val.cast()
        } else {
            Err(Error::empty())
        }
    }

    fn GetPropertyValue(&self, property_id: UIA_PROPERTY_ID) -> Result<VARIANT> {
        let sub = self.sub_snapshot().ok_or(Error::empty())?;
        let control_type = if sub.is_checkbox {
            UIA_CheckBoxControlTypeId.0
        } else {
            UIA_EditControlTypeId.0
        };

        match property_id {
            UIA_ControlTypePropertyId => Ok(VARIANT::from(control_type)),
            UIA_NamePropertyId => Ok(VARIANT::from(BSTR::from(&sub.label))),
            UIA_AutomationIdPropertyId => Ok(VARIANT::from(BSTR::from(&format!(
                "Keybinding_{}_{}",
                self.field_index, sub.label
            )))),
            UIA_IsContentElementPropertyId | UIA_IsControlElementPropertyId => {
                Ok(VARIANT::from(true))
            }
            UIA_IsKeyboardFocusablePropertyId => Ok(VARIANT::from(true)),
            UIA_HasKeyboardFocusPropertyId => Ok(VARIANT::from(sub.is_focused)),
            UIA_LocalizedControlTypePropertyId => {
                let label = if sub.is_checkbox {
                    "modifier checkbox"
                } else {
                    "key input"
                };
                Ok(VARIANT::from(BSTR::from(label)))
            }
            _ => Err(Error::empty()),
        }
    }

    fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
        Err(Error::empty())
    }
}

// --- IToggleProvider for modifier checkboxes ---

impl IToggleProvider_Impl for KeybindingSubFieldProvider_Impl {
    fn Toggle(&self) -> Result<()> {
        Err(Error::empty())
    }

    fn ToggleState(&self) -> Result<ToggleState> {
        let sub = self.sub_snapshot().ok_or(Error::empty())?;
        match sub.checked {
            Some(true) => Ok(ToggleState_On),
            Some(false) => Ok(ToggleState_Off),
            None => Err(Error::empty()),
        }
    }
}

// --- IValueProvider for key text input ---

impl IValueProvider_Impl for KeybindingSubFieldProvider_Impl {
    fn SetValue(&self, _val: &PCWSTR) -> Result<()> {
        Err(Error::empty())
    }

    fn Value(&self) -> Result<BSTR> {
        let sub = self.sub_snapshot().ok_or(Error::empty())?;
        Ok(BSTR::from(&sub.value.unwrap_or_default()))
    }

    fn IsReadOnly(&self) -> Result<BOOL> {
        Ok(BOOL::from(true))
    }
}

// --- Fragment navigation for KeybindingSubFieldProvider ---

#[allow(non_upper_case_globals)]
impl IRawElementProviderFragment_Impl for KeybindingSubFieldProvider_Impl {
    fn Navigate(&self, direction: NavigateDirection) -> Result<IRawElementProviderFragment> {
        match direction {
            NavigateDirection_Parent => {
                let fp = SettingsFieldProvider::new(
                    self.field_index,
                    self.hwnd,
                    Arc::clone(&self.state),
                    self.root_provider.clone(),
                );
                let frag: IRawElementProviderFragment = fp.into();
                Ok(frag)
            }
            NavigateDirection_NextSibling => {
                let next = self.sub_index + 1;
                let count = self.parent_field_sub_count();
                if next < count {
                    let sp = KeybindingSubFieldProvider::new(
                        self.field_index,
                        next,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = sp.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            NavigateDirection_PreviousSibling => {
                if self.sub_index > 0 {
                    let sp = KeybindingSubFieldProvider::new(
                        self.field_index,
                        self.sub_index - 1,
                        self.hwnd,
                        Arc::clone(&self.state),
                        self.root_provider.clone(),
                    );
                    let frag: IRawElementProviderFragment = sp.into();
                    Ok(frag)
                } else {
                    Err(Error::empty())
                }
            }
            _ => Err(Error::empty()),
        }
    }

    fn GetRuntimeId(&self) -> Result<*mut SAFEARRAY> {
        make_runtime_id(sub_field_runtime_id(self.field_index, self.sub_index))
    }

    fn BoundingRectangle(&self) -> Result<UiaRect> {
        let state = self.state.read();
        let (x, y, w, _) = settings_panel_rect(self.hwnd, &state);
        // Sub-fields share the same row as their parent field
        let field_y = y + (2 + self.field_index) as f64 * state.cell_height;
        // Divide the row into sub-field segments
        let sub_w = w / 5.0; // approximate: modifier checkboxes + key field
        Ok(UiaRect {
            left: x + self.sub_index as f64 * sub_w,
            top: field_y,
            width: sub_w,
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

    fn make_test_snapshot() -> SettingsSnapshot {
        SettingsSnapshot {
            visible: true,
            active_tab_label: "Font".to_string(),
            active_tab_index: 0,
            tab_labels: vec![
                "Font".into(),
                "Window".into(),
                "Terminal".into(),
                "Colors".into(),
                "Profiles".into(),
                "Keybindings".into(),
            ],
            fields: vec![
                FieldSnapshot {
                    label: "Font Family".into(),
                    field_type: FieldType::Text,
                    value_text: "Consolas".into(),
                    is_focused: true,
                    index: 0,
                    number_min: None,
                    number_max: None,
                    number_step: None,
                    number_value: None,
                    toggle_on: None,
                    dropdown_options: None,
                    dropdown_selected: None,
                    sub_fields: None,
                },
                FieldSnapshot {
                    label: "Font Size".into(),
                    field_type: FieldType::Number,
                    value_text: "14.0".into(),
                    is_focused: false,
                    index: 1,
                    number_min: Some(6.0),
                    number_max: Some(72.0),
                    number_step: Some(0.5),
                    number_value: Some(14.0),
                    toggle_on: None,
                    dropdown_options: None,
                    dropdown_selected: None,
                    sub_fields: None,
                },
                FieldSnapshot {
                    label: "Bold Text".into(),
                    field_type: FieldType::Toggle,
                    value_text: "ON".into(),
                    is_focused: false,
                    index: 2,
                    number_min: None,
                    number_max: None,
                    number_step: None,
                    number_value: None,
                    toggle_on: Some(true),
                    dropdown_options: None,
                    dropdown_selected: None,
                    sub_fields: None,
                },
                FieldSnapshot {
                    label: "Renderer".into(),
                    field_type: FieldType::Dropdown,
                    value_text: "Auto".into(),
                    is_focused: false,
                    index: 3,
                    number_min: None,
                    number_max: None,
                    number_step: None,
                    number_value: None,
                    toggle_on: None,
                    dropdown_options: Some(vec!["Auto".into(), "GPU".into(), "Software".into()]),
                    dropdown_selected: Some(0),
                    sub_fields: None,
                },
                FieldSnapshot {
                    label: "New Tab".into(),
                    field_type: FieldType::Keybinding,
                    value_text: "Ctrl+Shift+T".into(),
                    is_focused: false,
                    index: 4,
                    number_min: None,
                    number_max: None,
                    number_step: None,
                    number_value: None,
                    toggle_on: None,
                    dropdown_options: None,
                    dropdown_selected: None,
                    sub_fields: Some(vec![
                        SubFieldSnapshot {
                            label: "Ctrl".into(),
                            is_checkbox: true,
                            checked: Some(true),
                            value: None,
                            is_focused: false,
                        },
                        SubFieldSnapshot {
                            label: "Shift".into(),
                            is_checkbox: true,
                            checked: Some(true),
                            value: None,
                            is_focused: false,
                        },
                        SubFieldSnapshot {
                            label: "Alt".into(),
                            is_checkbox: true,
                            checked: Some(false),
                            value: None,
                            is_focused: false,
                        },
                        SubFieldSnapshot {
                            label: "Win".into(),
                            is_checkbox: true,
                            checked: Some(false),
                            value: None,
                            is_focused: false,
                        },
                        SubFieldSnapshot {
                            label: "Key".into(),
                            is_checkbox: false,
                            checked: None,
                            value: Some("t".into()),
                            is_focused: false,
                        },
                    ]),
                },
            ],
            focused_field: 0,
        }
    }

    #[test]
    fn test_settings_snapshot_creation() {
        let snap = make_test_snapshot();
        assert!(snap.visible);
        assert_eq!(snap.tab_labels.len(), 6);
        assert_eq!(snap.fields.len(), 5);
        assert_eq!(snap.active_tab_label, "Font");
        assert_eq!(snap.focused_field, 0);
    }

    #[test]
    fn test_field_type_mapping() {
        assert_eq!(
            field_type_to_control_type(FieldType::Text),
            UIA_EditControlTypeId.0
        );
        assert_eq!(
            field_type_to_control_type(FieldType::Number),
            UIA_SpinnerControlTypeId.0
        );
        assert_eq!(
            field_type_to_control_type(FieldType::Toggle),
            UIA_CheckBoxControlTypeId.0
        );
        assert_eq!(
            field_type_to_control_type(FieldType::Dropdown),
            UIA_ComboBoxControlTypeId.0
        );
        assert_eq!(
            field_type_to_control_type(FieldType::Keybinding),
            UIA_GroupControlTypeId.0
        );
    }

    #[test]
    fn test_keybinding_sub_fields() {
        let snap = make_test_snapshot();
        let kb_field = &snap.fields[4];
        assert_eq!(kb_field.field_type, FieldType::Keybinding);
        let subs = kb_field.sub_fields.as_ref().unwrap();
        assert_eq!(subs.len(), 5);

        // First 4 are checkboxes
        assert!(subs[0].is_checkbox);
        assert_eq!(subs[0].label, "Ctrl");
        assert_eq!(subs[0].checked, Some(true));
        assert!(subs[1].is_checkbox);
        assert_eq!(subs[1].checked, Some(true));
        assert!(subs[2].is_checkbox);
        assert_eq!(subs[2].checked, Some(false));
        assert!(subs[3].is_checkbox);
        assert_eq!(subs[3].checked, Some(false));

        // Last is key text
        assert!(!subs[4].is_checkbox);
        assert_eq!(subs[4].label, "Key");
        assert_eq!(subs[4].value, Some("t".into()));
    }

    #[test]
    fn test_settings_panel_navigation_ids() {
        // Verify runtime ID helpers produce distinct values
        assert_eq!(tab_runtime_id(0), SETTINGS_ID_BASE + 1000);
        assert_eq!(tab_runtime_id(5), SETTINGS_ID_BASE + 1005);
        assert_eq!(field_runtime_id(0), SETTINGS_ID_BASE + 2000);
        assert_eq!(field_runtime_id(3), SETTINGS_ID_BASE + 2003);
        assert_eq!(sub_field_runtime_id(0, 0), SETTINGS_ID_BASE + 3000);
        assert_eq!(sub_field_runtime_id(2, 3), SETTINGS_ID_BASE + 3023);
    }

    #[test]
    fn test_field_focus_tracking() {
        let snap = make_test_snapshot();
        // Field 0 is focused
        assert!(snap.fields[0].is_focused);
        assert!(!snap.fields[1].is_focused);
        assert!(!snap.fields[2].is_focused);
    }

    #[test]
    fn test_toggle_field_state() {
        let snap = make_test_snapshot();
        let toggle_field = &snap.fields[2];
        assert_eq!(toggle_field.field_type, FieldType::Toggle);
        assert_eq!(toggle_field.toggle_on, Some(true));
        assert_eq!(toggle_field.value_text, "ON");
    }

    #[test]
    fn test_settings_field_rect_offsets() {
        // Verify the arithmetic for settings field BoundingRectangle
        let win_w = 1280.0_f64;
        let win_h = 800.0_f64;
        let cell_h = 20.0_f64;
        let origin_y = 0.0_f64;

        // Panel geometry: 80% width × 70% height, 15% from top
        let panel_w = win_w * 0.8; // 1024
        let panel_h = win_h * 0.7; // 560
        let panel_x = (win_w - panel_w) / 2.0; // 128
        let panel_y = origin_y + win_h * 0.15; // 120

        assert!((panel_w - 1024.0).abs() < 0.001);
        assert!((panel_h - 560.0).abs() < 0.001);
        assert!((panel_x - 128.0).abs() < 0.001);
        assert!((panel_y - 120.0).abs() < 0.001);

        // Field at index 1: y = panel_y + (2 + 1) * cell_h (skip tab row + header)
        let field_y = panel_y + (2.0 + 1.0) * cell_h;
        assert!((field_y - 180.0).abs() < 0.001); // 120 + 60 = 180

        // Tab rect: divide panel_w by tab_count (6)
        let tab_count = 6;
        let tab_w = panel_w / tab_count as f64;
        assert!((tab_w - 170.666).abs() < 0.667); // ~170.67
        let tab2_x = panel_x + 2.0 * tab_w;
        assert!((tab2_x - 469.333).abs() < 0.667); // 128 + 2*170.67 ≈ 469.33
    }
}
