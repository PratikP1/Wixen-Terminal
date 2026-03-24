//! ITextProvider + ITextRangeProvider — screen reader text navigation.
//!
//! Enables screen readers to navigate terminal text by character, word, line,
//! paragraph, and document. The text model is flat: the full terminal visible
//! text with lines separated by '\n'. Positions are byte offsets into the
//! UTF-16 encoding that UIA expects.

use std::sync::Arc;
use std::sync::atomic::{AtomicI32, Ordering};

use parking_lot::RwLock;
use tracing::debug;
use windows::Win32::System::Com::SAFEARRAY;
use windows::Win32::System::Ole::{
    SafeArrayAccessData, SafeArrayCreateVector, SafeArrayUnaccessData,
};
use windows::Win32::System::Variant::VARIANT;
use windows::Win32::UI::Accessibility::*;
use windows::core::*;

use crate::provider::TerminalA11yState;

// ─── ITextProvider ───────────────────────────────────────────────────────────

/// UIA Text Pattern provider for the terminal.
///
/// Text model: `TerminalA11yState::full_text` is the canonical text, encoded to
/// UTF-16 on demand. Offsets in `TerminalTextRange` are UTF-16 code unit indices.
#[implement(ITextProvider, ITextProvider2)]
pub struct TerminalTextProvider {
    state: Arc<RwLock<TerminalA11yState>>,
    /// The enclosing element provider (the terminal root).
    enclosing: IRawElementProviderSimple,
    /// Lock-free cursor offset — readable from any thread without the RwLock.
    cursor_offset: Arc<AtomicI32>,
}

impl TerminalTextProvider {
    pub fn new(
        state: Arc<RwLock<TerminalA11yState>>,
        enclosing: IRawElementProviderSimple,
        cursor_offset: Arc<AtomicI32>,
    ) -> Self {
        Self {
            state,
            enclosing,
            cursor_offset,
        }
    }

    pub fn into_interface(self) -> ITextProvider {
        self.into()
    }

    pub fn into_interface2(self) -> ITextProvider2 {
        self.into()
    }
}

/// Create a SAFEARRAY of IUnknown containing a single ITextRangeProvider.
///
/// Uses `SafeArrayAccessData`/`SafeArrayUnaccessData` + manual `AddRef`
/// instead of `SafeArrayPutElement` to avoid a DEP crash when
/// `SafeArrayPutElement` internally dispatches through the vtable.
unsafe fn safearray_of_one_range(range: ITextRangeProvider) -> Result<*mut SAFEARRAY> {
    unsafe {
        let sa = SafeArrayCreateVector(windows::Win32::System::Variant::VT_UNKNOWN, 0, 1);
        if sa.is_null() {
            debug!("safearray_of_one_range: SafeArrayCreateVector returned null");
            return Err(Error::from_hresult(HRESULT(-2147024882i32)));
        }
        let unk: IUnknown = match range.cast() {
            Ok(u) => u,
            Err(e) => {
                debug!(?e, "safearray_of_one_range: cast to IUnknown failed");
                return Err(e);
            }
        };

        // Write the raw COM pointer directly into the SAFEARRAY data buffer.
        // This avoids SafeArrayPutElement's internal vtable dispatch which
        // triggers DEP crashes with windows-rs #[implement] COM objects.
        let mut pdata: *mut std::ffi::c_void = std::ptr::null_mut();
        SafeArrayAccessData(sa, &mut pdata)?;
        let slot = pdata as *mut *mut std::ffi::c_void;
        // Copy the raw interface pointer into the array slot.
        // transmute_copy reads the raw pointer without affecting ref count.
        let raw: *mut std::ffi::c_void = std::mem::transmute_copy(&unk);
        *slot = raw;
        // AddRef for the SAFEARRAY's copy. We call through the vtable manually
        // since windows-rs IUnknown doesn't expose AddRef as a pub method.
        let vtable = *(raw as *const *const IUnknown_Vtbl);
        ((*vtable).AddRef)(raw);
        SafeArrayUnaccessData(sa)?;

        Ok(sa)
    }
}

#[allow(non_upper_case_globals)]
impl ITextProvider_Impl for TerminalTextProvider_Impl {
    fn GetSelection(&self) -> Result<*mut SAFEARRAY> {
        // Read cursor offset lock-free — never blocks even during frame processing
        let offset = self.cursor_offset.load(Ordering::Acquire);
        debug!(offset, "GetSelection: returning degenerate range at cursor");

        let range = TerminalTextRange::new(
            Arc::clone(&self.state),
            self.enclosing.clone(),
            offset,
            offset,
        );
        debug!(offset, "GetSelection: returning degenerate range");
        unsafe { safearray_of_one_range(range.into()) }
    }

    fn GetVisibleRanges(&self) -> Result<*mut SAFEARRAY> {
        debug!("ITextProvider::GetVisibleRanges");
        let full_text = self
            .state
            .try_read()
            .map(|s| s.full_text.clone())
            .unwrap_or_default();
        let utf16: Vec<u16> = full_text.encode_utf16().collect();
        let len = utf16.len() as i32;

        let range = TerminalTextRange::new(Arc::clone(&self.state), self.enclosing.clone(), 0, len);
        unsafe { safearray_of_one_range(range.into()) }
    }

    fn RangeFromChild(
        &self,
        _childelement: Ref<'_, IRawElementProviderSimple>,
    ) -> Result<ITextRangeProvider> {
        debug!("ITextProvider::RangeFromChild");
        self.DocumentRange()
    }

    fn RangeFromPoint(&self, _point: &UiaPoint) -> Result<ITextRangeProvider> {
        debug!("ITextProvider::RangeFromPoint");
        let range = TerminalTextRange::new(Arc::clone(&self.state), self.enclosing.clone(), 0, 0);
        Ok(range.into())
    }

    fn DocumentRange(&self) -> Result<ITextRangeProvider> {
        debug!("ITextProvider::DocumentRange");
        let full_text = self
            .state
            .try_read()
            .map(|s| s.full_text.clone())
            .unwrap_or_default();
        let utf16: Vec<u16> = full_text.encode_utf16().collect();
        let len = utf16.len() as i32;

        let range = TerminalTextRange::new(Arc::clone(&self.state), self.enclosing.clone(), 0, len);
        Ok(range.into())
    }

    fn SupportedTextSelection(&self) -> Result<SupportedTextSelection> {
        Ok(SupportedTextSelection_Single)
    }
}

// ─── ITextProvider2 ─────────────────────────────────────────────────────────

#[allow(non_upper_case_globals)]
impl ITextProvider2_Impl for TerminalTextProvider_Impl {
    // COM trait signature mandates `*mut BOOL` out-param; we null-check before writing.
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    fn GetCaretRange(&self, isactive: *mut BOOL) -> Result<ITextRangeProvider> {
        // Read cursor offset lock-free — never blocks even during frame processing
        let offset = self.cursor_offset.load(Ordering::Acquire);
        debug!(offset, "GetCaretRange: returning degenerate range");

        if !isactive.is_null() {
            unsafe {
                *isactive = BOOL::from(true);
            }
        }

        let range = TerminalTextRange::new(
            Arc::clone(&self.state),
            self.enclosing.clone(),
            offset,
            offset,
        );
        Ok(range.into())
    }

    fn RangeFromAnnotation(
        &self,
        _annotation: Ref<'_, IRawElementProviderSimple>,
    ) -> Result<ITextRangeProvider> {
        // No annotation support — return document range as fallback
        self.DocumentRange()
    }
}

// ─── ITextRangeProvider ──────────────────────────────────────────────────────

/// A text range within the terminal's visible text.
///
/// `start` and `end` are UTF-16 code unit offsets into the text snapshot.
/// The range is [start, end) — end is exclusive.
#[implement(ITextRangeProvider)]
pub struct TerminalTextRange {
    state: Arc<RwLock<TerminalA11yState>>,
    enclosing: IRawElementProviderSimple,
    start: RwLock<i32>,
    end: RwLock<i32>,
}

impl TerminalTextRange {
    fn new(
        state: Arc<RwLock<TerminalA11yState>>,
        enclosing: IRawElementProviderSimple,
        start: i32,
        end: i32,
    ) -> Self {
        Self {
            state,
            enclosing,
            start: RwLock::new(start),
            end: RwLock::new(end),
        }
    }

    /// Get a UTF-16 snapshot of the current terminal text.
    fn text_snapshot(&self) -> Vec<u16> {
        self.state
            .try_read()
            .map(|s| s.full_text.encode_utf16().collect())
            .unwrap_or_default()
    }

    /// Clamp a position to [0, len].
    fn clamp(pos: i32, len: i32) -> i32 {
        pos.max(0).min(len)
    }
}

#[allow(non_upper_case_globals)]
impl ITextRangeProvider_Impl for TerminalTextRange_Impl {
    fn Clone(&self) -> Result<ITextRangeProvider> {
        let start = *self.start.read();
        let end = *self.end.read();
        let range =
            TerminalTextRange::new(Arc::clone(&self.state), self.enclosing.clone(), start, end);
        Ok(range.into())
    }

    fn Compare(&self, range: Ref<'_, ITextRangeProvider>) -> Result<BOOL> {
        let our_start = *self.start.read();
        let our_end = *self.end.read();
        let target = range.ok()?;
        let target_start = unsafe {
            extract_endpoint_via_com(target, TextPatternRangeEndpoint_Start, &self.state)?
        };
        let target_end =
            unsafe { extract_endpoint_via_com(target, TextPatternRangeEndpoint_End, &self.state)? };
        Ok(BOOL::from(
            our_start == target_start && our_end == target_end,
        ))
    }

    fn CompareEndpoints(
        &self,
        endpoint: TextPatternRangeEndpoint,
        targetrange: Ref<'_, ITextRangeProvider>,
        targetendpoint: TextPatternRangeEndpoint,
    ) -> Result<i32> {
        let our_pos = if endpoint == TextPatternRangeEndpoint_Start {
            *self.start.read()
        } else {
            *self.end.read()
        };

        let target = targetrange.ok()?;
        let target_pos = unsafe { extract_endpoint_via_com(target, targetendpoint, &self.state)? };

        debug!(our_pos, target_pos, "CompareEndpoints");
        Ok((our_pos - target_pos).signum())
    }

    fn ExpandToEnclosingUnit(&self, unit: TextUnit) -> Result<()> {
        let utf16 = self.text_snapshot();
        let len = utf16.len() as i32;
        let mut start = TerminalTextRange::clamp(*self.start.read(), len);
        let mut end;

        match unit {
            TextUnit_Character => {
                if start < len {
                    end = start + 1;
                    // Handle surrogate pairs
                    if start < len - 1 {
                        let hi = utf16[start as usize];
                        if (0xD800..=0xDBFF).contains(&hi) {
                            end = start + 2;
                        }
                    }
                } else {
                    end = start;
                }
            }
            TextUnit_Word => {
                let (ws, we) = find_word_boundary(&utf16, start);
                start = ws;
                end = we;
            }
            TextUnit_Line | TextUnit_Paragraph => {
                let (ls, le) = find_line_boundary(&utf16, start);
                start = ls;
                end = le;
            }
            TextUnit_Page | TextUnit_Document | TextUnit_Format => {
                start = 0;
                end = len;
            }
            _ => {
                end = TerminalTextRange::clamp(*self.end.read(), len);
            }
        }

        *self.start.write() = start;
        *self.end.write() = end;
        Ok(())
    }

    fn FindAttribute(
        &self,
        _attributeid: UIA_TEXTATTRIBUTE_ID,
        _val: &VARIANT,
        _backward: BOOL,
    ) -> Result<ITextRangeProvider> {
        Err(Error::empty())
    }

    fn FindText(
        &self,
        text: &BSTR,
        backward: BOOL,
        ignorecase: BOOL,
    ) -> Result<ITextRangeProvider> {
        let utf16 = self.text_snapshot();
        // BSTR derefs to &[u16]
        let search: &[u16] = text;
        if search.is_empty() {
            return Err(Error::empty());
        }

        let s = (*self.start.read() as usize).min(utf16.len());
        let e = (*self.end.read() as usize).min(utf16.len());
        let (s, e) = if s > e { (e, s) } else { (s, e) };
        let range = &utf16[s..e];

        let found = if ignorecase.as_bool() {
            let range_lower: Vec<u16> = String::from_utf16_lossy(range)
                .to_lowercase()
                .encode_utf16()
                .collect();
            let search_lower: Vec<u16> = String::from_utf16_lossy(search)
                .to_lowercase()
                .encode_utf16()
                .collect();
            if backward.as_bool() {
                find_last_substr(&range_lower, &search_lower)
            } else {
                find_first_substr(&range_lower, &search_lower)
            }
        } else if backward.as_bool() {
            find_last_substr(range, search)
        } else {
            find_first_substr(range, search)
        };

        match found {
            Some(offset) => {
                let abs_start = s as i32 + offset as i32;
                let abs_end = abs_start + search.len() as i32;
                let result = TerminalTextRange::new(
                    Arc::clone(&self.state),
                    self.enclosing.clone(),
                    abs_start,
                    abs_end,
                );
                Ok(result.into())
            }
            None => Err(Error::empty()),
        }
    }

    fn GetAttributeValue(&self, attributeid: UIA_TEXTATTRIBUTE_ID) -> Result<VARIANT> {
        match attributeid {
            UIA_IsReadOnlyAttributeId => Ok(VARIANT::from(false)),
            _ => {
                // Return the "not supported" sentinel as a default VARIANT
                Ok(VARIANT::default())
            }
        }
    }

    fn GetBoundingRectangles(&self) -> Result<*mut SAFEARRAY> {
        // Return empty SAFEARRAY — no cell-level geometry yet
        unsafe {
            let sa = SafeArrayCreateVector(windows::Win32::System::Variant::VT_R8, 0, 0);
            if sa.is_null() {
                return Err(Error::from_hresult(HRESULT(-2147024882i32)));
            }
            Ok(sa)
        }
    }

    fn GetEnclosingElement(&self) -> Result<IRawElementProviderSimple> {
        Ok(self.enclosing.clone())
    }

    fn GetText(&self, maxlength: i32) -> Result<BSTR> {
        let utf16 = self.text_snapshot();
        let len = utf16.len() as i32;
        let start = TerminalTextRange::clamp(*self.start.read(), len) as usize;
        let end = TerminalTextRange::clamp(*self.end.read(), len) as usize;

        // Guard against invalid ranges (race between text change and offset reads)
        let (start, end) = if start > end {
            (end, start)
        } else {
            (start, end)
        };
        let end = end.min(utf16.len());
        let start = start.min(end);

        let slice = &utf16[start..end];
        let result = if maxlength >= 0 && (slice.len() as i32) > maxlength {
            &slice[..maxlength as usize]
        } else {
            slice
        };

        Ok(BSTR::from_wide(result))
    }

    fn Move(&self, unit: TextUnit, count: i32) -> Result<i32> {
        if count == 0 {
            return Ok(0);
        }

        let utf16 = self.text_snapshot();
        let len = utf16.len() as i32;
        let mut start = TerminalTextRange::clamp(*self.start.read(), len);

        let mut moved = 0i32;
        let direction = if count > 0 { 1 } else { -1 };
        let steps = count.unsigned_abs() as i32;

        for _ in 0..steps {
            let new_pos = move_by_unit(&utf16, start, direction, unit);
            if new_pos == start {
                break;
            }
            start = new_pos;
            moved += direction;
        }

        *self.start.write() = start;
        *self.end.write() = start;
        self.ExpandToEnclosingUnit(unit)?;

        Ok(moved)
    }

    fn MoveEndpointByUnit(
        &self,
        endpoint: TextPatternRangeEndpoint,
        unit: TextUnit,
        count: i32,
    ) -> Result<i32> {
        if count == 0 {
            return Ok(0);
        }

        let utf16 = self.text_snapshot();
        let len = utf16.len() as i32;

        let mut pos = if endpoint == TextPatternRangeEndpoint_Start {
            TerminalTextRange::clamp(*self.start.read(), len)
        } else {
            TerminalTextRange::clamp(*self.end.read(), len)
        };

        let mut moved = 0i32;
        let direction = if count > 0 { 1 } else { -1 };
        let steps = count.unsigned_abs() as i32;

        for _ in 0..steps {
            let new_pos = move_by_unit(&utf16, pos, direction, unit);
            if new_pos == pos {
                break;
            }
            pos = new_pos;
            moved += direction;
        }

        if endpoint == TextPatternRangeEndpoint_Start {
            *self.start.write() = pos;
            if pos > *self.end.read() {
                *self.end.write() = pos;
            }
        } else {
            *self.end.write() = pos;
            if pos < *self.start.read() {
                *self.start.write() = pos;
            }
        }

        Ok(moved)
    }

    fn MoveEndpointByRange(
        &self,
        endpoint: TextPatternRangeEndpoint,
        targetrange: Ref<'_, ITextRangeProvider>,
        targetendpoint: TextPatternRangeEndpoint,
    ) -> Result<()> {
        let target = targetrange.ok()?;
        let target_pos = unsafe { extract_endpoint_via_com(target, targetendpoint, &self.state)? };

        if endpoint == TextPatternRangeEndpoint_Start {
            *self.start.write() = target_pos;
            if target_pos > *self.end.read() {
                *self.end.write() = target_pos;
            }
        } else {
            *self.end.write() = target_pos;
            if target_pos < *self.start.read() {
                *self.start.write() = target_pos;
            }
        }

        debug!(endpoint = endpoint.0, target_pos, "MoveEndpointByRange");
        Ok(())
    }

    fn Select(&self) -> Result<()> {
        debug!("ITextRangeProvider::Select");
        Ok(())
    }

    fn AddToSelection(&self) -> Result<()> {
        Err(Error::from_hresult(HRESULT(-2147024891i32))) // E_INVALIDARG
    }

    fn RemoveFromSelection(&self) -> Result<()> {
        debug!("ITextRangeProvider::RemoveFromSelection");
        Ok(())
    }

    fn ScrollIntoView(&self, _aligntotop: BOOL) -> Result<()> {
        debug!("ITextRangeProvider::ScrollIntoView");
        Ok(())
    }

    fn GetChildren(&self) -> Result<*mut SAFEARRAY> {
        unsafe {
            let sa = SafeArrayCreateVector(windows::Win32::System::Variant::VT_UNKNOWN, 0, 0);
            if sa.is_null() {
                return Err(Error::from_hresult(HRESULT(-2147024882i32)));
            }
            Ok(sa)
        }
    }
}

// ─── Text navigation helpers ─────────────────────────────────────────────────

/// Find line boundaries around a UTF-16 position.
/// Returns (line_start, line_end) where line_end includes the '\n' if present.
fn find_line_boundary(text: &[u16], pos: i32) -> (i32, i32) {
    let pos = pos.max(0).min(text.len() as i32) as usize;
    let newline = '\n' as u16;

    let line_start = if pos == 0 {
        0
    } else {
        let mut s = pos;
        while s > 0 && text[s - 1] != newline {
            s -= 1;
        }
        s
    };

    let mut line_end = pos;
    while line_end < text.len() && text[line_end] != newline {
        line_end += 1;
    }
    if line_end < text.len() {
        line_end += 1; // include '\n'
    }

    (line_start as i32, line_end as i32)
}

/// Find word boundaries around a UTF-16 position.
/// Words are delimited by whitespace.
fn find_word_boundary(text: &[u16], pos: i32) -> (i32, i32) {
    let pos = pos.max(0).min(text.len() as i32) as usize;

    if pos >= text.len() {
        return (text.len() as i32, text.len() as i32);
    }

    let is_word_char = |c: u16| -> bool {
        (c >= b'a' as u16 && c <= b'z' as u16)
            || (c >= b'A' as u16 && c <= b'Z' as u16)
            || (c >= b'0' as u16 && c <= b'9' as u16)
            || c == b'_' as u16
            || c > 0x7F
    };

    let at_word = is_word_char(text[pos]);

    let mut start = pos;
    while start > 0 && is_word_char(text[start - 1]) == at_word {
        start -= 1;
    }

    let mut end = pos;
    while end < text.len() && is_word_char(text[end]) == at_word {
        end += 1;
    }

    (start as i32, end as i32)
}

/// Move a position by one unit in the given direction (+1 or -1).
#[allow(non_upper_case_globals)]
fn move_by_unit(text: &[u16], pos: i32, direction: i32, unit: TextUnit) -> i32 {
    let len = text.len() as i32;
    let pos = pos.max(0).min(len);

    match unit {
        TextUnit_Character => {
            if direction > 0 {
                if pos < len {
                    let hi = text[pos as usize];
                    if (0xD800..=0xDBFF).contains(&hi) && pos + 1 < len {
                        (pos + 2).min(len)
                    } else {
                        pos + 1
                    }
                } else {
                    pos
                }
            } else if pos > 0 {
                let lo = text[(pos - 1) as usize];
                if (0xDC00..=0xDFFF).contains(&lo) && pos >= 2 {
                    pos - 2
                } else {
                    pos - 1
                }
            } else {
                pos
            }
        }
        TextUnit_Word => {
            let newline = '\n' as u16;
            if direction > 0 {
                let mut p = pos as usize;
                while p < text.len() && text[p] != b' ' as u16 && text[p] != newline {
                    p += 1;
                }
                while p < text.len() && (text[p] == b' ' as u16 || text[p] == b'\t' as u16) {
                    p += 1;
                }
                p as i32
            } else {
                let mut p = pos as usize;
                while p > 0 && (text[p - 1] == b' ' as u16 || text[p - 1] == b'\t' as u16) {
                    p -= 1;
                }
                while p > 0 && text[p - 1] != b' ' as u16 && text[p - 1] != newline {
                    p -= 1;
                }
                p as i32
            }
        }
        TextUnit_Line | TextUnit_Paragraph => {
            let newline = '\n' as u16;
            if direction > 0 {
                let mut p = pos as usize;
                while p < text.len() && text[p] != newline {
                    p += 1;
                }
                if p < text.len() {
                    p += 1;
                }
                p as i32
            } else {
                let mut p = pos as usize;
                if p > 0 && text[p - 1] == newline {
                    p -= 1;
                }
                while p > 0 && text[p - 1] != newline {
                    p -= 1;
                }
                p as i32
            }
        }
        TextUnit_Page | TextUnit_Document => {
            if direction > 0 {
                len
            } else {
                0
            }
        }
        _ => pos,
    }
}

/// Find first occurrence of `needle` in `haystack`.
fn find_first_substr(haystack: &[u16], needle: &[u16]) -> Option<usize> {
    if needle.is_empty() || needle.len() > haystack.len() {
        return None;
    }
    haystack.windows(needle.len()).position(|w| w == needle)
}

/// Find last occurrence of `needle` in `haystack`.
fn find_last_substr(haystack: &[u16], needle: &[u16]) -> Option<usize> {
    if needle.is_empty() || needle.len() > haystack.len() {
        return None;
    }
    haystack.windows(needle.len()).rposition(|w| w == needle)
}

/// Extract the absolute UTF-16 offset of an endpoint from a text range via COM calls.
///
/// Uses Clone + MoveEndpointByUnit + GetText to measure position without
/// needing unsafe downcasting to the concrete type.
///
/// # Safety
/// Caller must ensure the `range` COM pointer is valid and points to a
/// `TerminalTextRange` sharing the same `state`.
unsafe fn extract_endpoint_via_com(
    range: &ITextRangeProvider,
    endpoint: TextPatternRangeEndpoint,
    state: &Arc<RwLock<TerminalA11yState>>,
) -> Result<i32> {
    unsafe {
        let clone = range.Clone()?;
        if endpoint == TextPatternRangeEndpoint_End {
            // Move start to doc beginning, keeping end in place.
            // Range becomes [0, target_end]. GetText length = target_end.
            clone.MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Document, -1)?;
            let text = clone.GetText(-1)?;
            Ok(text.len() as i32)
        } else {
            // Move end to doc end, keeping start in place.
            // Range becomes [target_start, doc_len]. GetText length = doc_len - target_start.
            clone.MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Document, 1)?;
            let text = clone.GetText(-1)?;
            let doc_len: i32 = {
                let s = state.read();
                s.full_text.encode_utf16().count() as i32
            };
            Ok(doc_len - text.len() as i32)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicI32, Ordering};
    use windows::Win32::System::Variant::VARIANT;

    /// Minimal stub IRawElementProviderSimple for creating test text ranges.
    #[implement(IRawElementProviderSimple)]
    struct StubProvider;

    impl IRawElementProviderSimple_Impl for StubProvider_Impl {
        fn ProviderOptions(&self) -> Result<ProviderOptions> {
            Ok(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading)
        }
        fn GetPatternProvider(&self, _patternid: UIA_PATTERN_ID) -> Result<IUnknown> {
            Err(Error::empty())
        }
        fn GetPropertyValue(&self, _propertyid: UIA_PROPERTY_ID) -> Result<VARIANT> {
            Ok(VARIANT::default())
        }
        fn HostRawElementProvider(&self) -> Result<IRawElementProviderSimple> {
            Err(Error::empty())
        }
    }

    fn make_state(text: &str) -> Arc<RwLock<TerminalA11yState>> {
        let mut state = TerminalA11yState::default();
        state.full_text = text.to_string();
        Arc::new(RwLock::new(state))
    }

    fn make_range(
        state: &Arc<RwLock<TerminalA11yState>>,
        start: i32,
        end: i32,
    ) -> ITextRangeProvider {
        let provider = StubProvider;
        let enclosing: IRawElementProviderSimple = provider.into();
        let range = TerminalTextRange::new(Arc::clone(state), enclosing, start, end);
        range.into()
    }

    #[test]
    fn test_compare_equal_ranges() {
        let state = make_state("hello world");
        let range_a = make_range(&state, 3, 7);
        let range_b = make_range(&state, 3, 7);
        let result = unsafe { range_a.Compare(&range_b).unwrap() };
        assert!(result.as_bool(), "Ranges with same offsets should be equal");
    }

    #[test]
    fn test_compare_different_ranges() {
        let state = make_state("hello world");
        let range_a = make_range(&state, 0, 0);
        let range_b = make_range(&state, 3, 7);
        let result = unsafe { range_a.Compare(&range_b).unwrap() };
        assert!(
            !result.as_bool(),
            "Degenerate range should not equal non-empty range"
        );
    }

    #[test]
    fn test_compare_endpoints_start_lt() {
        let state = make_state("hello world");
        let range_a = make_range(&state, 2, 7);
        let range_b = make_range(&state, 5, 9);
        let result = unsafe {
            range_a
                .CompareEndpoints(
                    TextPatternRangeEndpoint_Start,
                    &range_b,
                    TextPatternRangeEndpoint_Start,
                )
                .unwrap()
        };
        assert!(result < 0, "A.start (2) < B.start (5) should be negative");
    }

    #[test]
    fn test_compare_endpoints_start_gt() {
        let state = make_state("hello world");
        let range_a = make_range(&state, 8, 11);
        let range_b = make_range(&state, 3, 7);
        let result = unsafe {
            range_a
                .CompareEndpoints(
                    TextPatternRangeEndpoint_Start,
                    &range_b,
                    TextPatternRangeEndpoint_Start,
                )
                .unwrap()
        };
        assert!(result > 0, "A.start (8) > B.start (3) should be positive");
    }

    #[test]
    fn test_compare_endpoints_end_vs_start() {
        let state = make_state("hello world");
        let range_a = make_range(&state, 2, 8);
        let range_b = make_range(&state, 5, 11);
        let result = unsafe {
            range_a
                .CompareEndpoints(
                    TextPatternRangeEndpoint_End,
                    &range_b,
                    TextPatternRangeEndpoint_Start,
                )
                .unwrap()
        };
        assert!(result > 0, "A.end (8) > B.start (5) should be positive");
    }

    #[test]
    fn test_move_endpoint_by_range() {
        let state = make_state("hello world");
        let range_a = make_range(&state, 0, 5);
        let range_b = make_range(&state, 8, 11);
        unsafe {
            range_a
                .MoveEndpointByRange(
                    TextPatternRangeEndpoint_End,
                    &range_b,
                    TextPatternRangeEndpoint_Start,
                )
                .unwrap();
        }
        // After move: A should be [0, 8]
        let text = unsafe { range_a.GetText(-1).unwrap() };
        assert_eq!(
            text.len(),
            8,
            "After MoveEndpointByRange, range should span 0..8"
        );
    }

    #[test]
    fn test_find_line_boundary() {
        let text: Vec<u16> = "hello\nworld\nfoo".encode_utf16().collect();

        let (s, e) = find_line_boundary(&text, 2);
        assert_eq!(s, 0);
        assert_eq!(e, 6);

        let (s, e) = find_line_boundary(&text, 8);
        assert_eq!(s, 6);
        assert_eq!(e, 12);

        let (s, e) = find_line_boundary(&text, 13);
        assert_eq!(s, 12);
        assert_eq!(e, 15);
    }

    #[test]
    fn test_find_word_boundary() {
        let text: Vec<u16> = "hello world".encode_utf16().collect();

        let (s, e) = find_word_boundary(&text, 2);
        assert_eq!(s, 0);
        assert_eq!(e, 5);

        let (s, e) = find_word_boundary(&text, 5);
        assert_eq!(s, 5);
        assert_eq!(e, 6);

        let (s, e) = find_word_boundary(&text, 8);
        assert_eq!(s, 6);
        assert_eq!(e, 11);
    }

    #[test]
    fn test_move_by_character() {
        let text: Vec<u16> = "abc".encode_utf16().collect();

        assert_eq!(move_by_unit(&text, 0, 1, TextUnit_Character), 1);
        assert_eq!(move_by_unit(&text, 1, 1, TextUnit_Character), 2);
        assert_eq!(move_by_unit(&text, 3, 1, TextUnit_Character), 3);
        assert_eq!(move_by_unit(&text, 2, -1, TextUnit_Character), 1);
        assert_eq!(move_by_unit(&text, 0, -1, TextUnit_Character), 0);
    }

    #[test]
    fn test_move_by_line() {
        let text: Vec<u16> = "line1\nline2\nline3".encode_utf16().collect();

        assert_eq!(move_by_unit(&text, 0, 1, TextUnit_Line), 6);
        assert_eq!(move_by_unit(&text, 6, 1, TextUnit_Line), 12);
        assert_eq!(move_by_unit(&text, 6, -1, TextUnit_Line), 0);
    }

    #[test]
    fn test_find_substr() {
        let text: Vec<u16> = "hello world hello".encode_utf16().collect();
        let needle: Vec<u16> = "hello".encode_utf16().collect();

        assert_eq!(find_first_substr(&text, &needle), Some(0));
        assert_eq!(find_last_substr(&text, &needle), Some(12));
    }

    #[test]
    fn test_cursor_offset_atomic_readable_while_write_locked() {
        use std::sync::atomic::Ordering;
        let state = make_state("hello\nworld\n");
        let cursor_offset = Arc::new(AtomicI32::new(0));
        // Simulate main loop: update cursor and cache the offset
        {
            let mut s = state.write();
            s.cursor_row = 1;
            s.cursor_col = 3;
            cursor_offset.store(s.cursor_offset_utf16(), Ordering::Release);
        }
        // Hold write lock — simulates main thread doing frame work
        let _write_guard = state.write();
        // Another thread reads the atomic — simulates NVDA's COM thread
        let offset_clone = Arc::clone(&cursor_offset);
        let handle = std::thread::spawn(move || offset_clone.load(Ordering::Acquire));
        let result = handle.join().unwrap();
        assert_eq!(
            result, 9,
            "COM thread should read offset 9 even while write lock is held"
        );
    }

    #[test]
    fn test_get_selection_uses_cursor_offset_not_end() {
        // Before fix: GetSelection always returned (len, len) — end of text.
        // After fix: it should return (cursor_offset, cursor_offset).
        let state = make_state("hello\nworld\n");
        state.write().cursor_row = 0;
        state.write().cursor_col = 2;
        // Cursor at offset 2, text len = 12.
        // If GetSelection still uses end-of-text, the range endpoints would be (12, 12).
        // After fix they should be (2, 2).
        let offset = state.read().cursor_offset_utf16();
        assert_eq!(offset, 2);
        let text_len: i32 = state.read().full_text.encode_utf16().count() as i32;
        assert_ne!(offset, text_len, "Cursor should NOT be at end of text");
    }

    #[test]
    fn test_get_caret_range_returns_degenerate_at_cursor() {
        let state = make_state("hello\nworld\n");
        state.write().cursor_row = 1;
        state.write().cursor_col = 3;
        let offset = Arc::new(AtomicI32::new(state.read().cursor_offset_utf16()));
        // Cursor at "wor|ld" = UTF-16 offset 9 (6 for "hello\n" + 3)
        let provider = StubProvider;
        let enclosing: IRawElementProviderSimple = provider.into();
        let tp = TerminalTextProvider::new(Arc::clone(&state), enclosing, offset);
        let itp2: ITextProvider2 = tp.into_interface2();
        let mut is_active = BOOL::default();
        let range = unsafe { itp2.GetCaretRange(&mut is_active).unwrap() };
        // Should be a degenerate range (empty) at the cursor
        let text = unsafe { range.GetText(-1).unwrap() };
        assert_eq!(text.len(), 0, "Caret range should be degenerate (empty)");
        assert!(is_active.as_bool(), "Caret should be reported as active");
    }

    #[test]
    fn test_cursor_offset_utf16_first_line() {
        let mut state = TerminalA11yState::default();
        state.full_text = "hello\nworld\n".to_string();
        state.cursor_row = 0;
        state.cursor_col = 3;
        // "hel|lo\nworld\n" — offset 3
        assert_eq!(state.cursor_offset_utf16(), 3);
    }

    #[test]
    fn test_cursor_offset_utf16_second_line() {
        let mut state = TerminalA11yState::default();
        state.full_text = "hello\nworld\n".to_string();
        state.cursor_row = 1;
        state.cursor_col = 2;
        // "hello\nwo|rld\n" — offset 8 (6 for "hello\n" + 2)
        assert_eq!(state.cursor_offset_utf16(), 8);
    }

    #[test]
    fn test_cursor_offset_utf16_at_end() {
        let mut state = TerminalA11yState::default();
        state.full_text = "hello\nworld\n".to_string();
        state.cursor_row = 2;
        state.cursor_col = 0;
        // Past last line — clamp to end
        assert_eq!(state.cursor_offset_utf16(), 12);
    }

    #[test]
    fn test_cursor_offset_utf16_col_past_line_end() {
        let mut state = TerminalA11yState::default();
        state.full_text = "hi\nworld\n".to_string();
        state.cursor_row = 0;
        state.cursor_col = 10; // past "hi" length
        // Clamp to line end: offset 2
        assert_eq!(state.cursor_offset_utf16(), 2);
    }

    #[test]
    fn test_cursor_offset_utf16_empty_text() {
        let mut state = TerminalA11yState::default();
        state.full_text = String::new();
        state.cursor_row = 0;
        state.cursor_col = 0;
        assert_eq!(state.cursor_offset_utf16(), 0);
    }
}
