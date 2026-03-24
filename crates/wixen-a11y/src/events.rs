//! UIA event raising and output throttling.
//!
//! Terminal output can be thousands of lines/second. We batch UIA events
//! into 100ms windows to prevent screen reader floods.

use std::time::{Duration, Instant};

use tracing::debug;
use windows::Win32::UI::Accessibility::*;
use windows::core::*;

/// Output throttling state for UIA event batching.
pub struct EventThrottler {
    /// Time of last TextChanged event
    last_text_changed: Instant,
    /// Minimum interval between TextChanged events
    debounce_interval: Duration,
    /// Accumulated text since last announcement
    pending_text: String,
    /// Lines received since last announcement
    lines_since_announce: usize,
    /// Whether we're in "streaming mode" (rapid output detected)
    streaming_mode: bool,
    /// Lines/second threshold for entering streaming mode
    streaming_threshold: usize,
    /// Time when streaming was last detected
    streaming_start: Option<Instant>,
}

impl EventThrottler {
    pub fn new() -> Self {
        Self {
            last_text_changed: Instant::now(),
            debounce_interval: Duration::from_millis(100),
            pending_text: String::new(),
            lines_since_announce: 0,
            streaming_mode: false,
            streaming_threshold: 10,
            streaming_start: None,
        }
    }

    /// Record new output lines. Returns true if a UIA event should be raised.
    pub fn on_output(&mut self, text: &str) -> bool {
        self.pending_text.push_str(text);
        self.lines_since_announce += text.lines().count().max(1);

        let elapsed = self.last_text_changed.elapsed();
        elapsed >= self.debounce_interval
    }

    /// Take the pending text and reset the timer.
    pub fn take_pending(&mut self) -> Option<String> {
        if self.pending_text.is_empty() {
            return None;
        }

        let text = std::mem::take(&mut self.pending_text);
        self.last_text_changed = Instant::now();

        // Check for streaming mode
        if self.lines_since_announce > self.streaming_threshold {
            if !self.streaming_mode {
                self.streaming_mode = true;
                self.streaming_start = Some(Instant::now());
                debug!(
                    "Entering streaming mode ({} lines/batch)",
                    self.lines_since_announce
                );
            }
        } else if self.streaming_mode {
            self.streaming_mode = false;
            self.streaming_start = None;
            debug!("Exiting streaming mode");
        }

        self.lines_since_announce = 0;
        Some(text)
    }

    /// Whether we're in streaming mode (rapid output).
    pub fn is_streaming(&self) -> bool {
        self.streaming_mode
    }

    /// Set the debounce interval from a config value in milliseconds.
    /// Clamped to 50–1000ms.
    pub fn set_debounce_ms(&mut self, ms: u32) {
        let clamped = ms.clamp(50, 1000);
        self.debounce_interval = Duration::from_millis(clamped as u64);
    }

    /// Check if we have pending text that needs announcing.
    pub fn has_pending(&self) -> bool {
        !self.pending_text.is_empty()
    }
}

impl Default for EventThrottler {
    fn default() -> Self {
        Self::new()
    }
}

/// Strip VT escape sequences and C0 control characters from text so screen
/// readers get clean output.
///
/// Handles CSI (`\x1b[...X`), OSC (`\x1b]...(\x07|\x1b\\)`), lone ESC sequences,
/// and C0 controls (`\x00-\x1F`). Preserves `\n` (line delimiter) and `\t`
/// (formatting). Strips `\r` so `\r\n` becomes `\n`.
pub fn strip_vt_escapes(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    let bytes = input.as_bytes();
    let len = bytes.len();
    let mut i = 0;

    while i < len {
        let b = bytes[i];
        if b == 0x1b && i + 1 < len {
            match bytes[i + 1] {
                b'[' => {
                    // CSI: skip until final byte (0x40..=0x7E)
                    i += 2;
                    while i < len && !(0x40..=0x7E).contains(&bytes[i]) {
                        i += 1;
                    }
                    if i < len {
                        i += 1; // skip final byte
                    }
                }
                b']' => {
                    // OSC: skip until BEL (0x07) or ST (\x1b\\)
                    i += 2;
                    while i < len {
                        if bytes[i] == 0x07 {
                            i += 1;
                            break;
                        }
                        if bytes[i] == 0x1b && i + 1 < len && bytes[i + 1] == b'\\' {
                            i += 2;
                            break;
                        }
                        i += 1;
                    }
                }
                _ => {
                    // Other ESC sequence (e.g. ESC c, ESC 7, ESC 8) — skip 2 bytes
                    i += 2;
                }
            }
        } else if b == b'\n' || b == b'\t' {
            // Preserve newlines and tabs
            out.push(b as char);
            i += 1;
        } else if b < 0x20 {
            // Strip all other C0 controls (BS, BEL, CR, etc.)
            i += 1;
        } else {
            out.push(b as char);
            i += 1;
        }
    }

    out
}

/// Raise a UIA TextChanged event on the provider.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_text_changed(provider: &IRawElementProviderSimple) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            let _ = UiaRaiseAutomationEvent(provider, UIA_Text_TextChangedEventId);
        }
    }
}

/// Raise a UIA LiveRegionChanged event for new output.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_live_region_changed(provider: &IRawElementProviderSimple) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            let _ = UiaRaiseAutomationEvent(provider, UIA_LiveRegionChangedEventId);
        }
    }
}

/// Raise a UIA notification event with the given text.
///
/// Uses `NotificationProcessing_MostRecent` for rapid output so only the
/// latest batch is spoken.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_notification(
    provider: &IRawElementProviderSimple,
    text: &str,
    activity_id: &str,
    is_streaming: bool,
) {
    unsafe {
        if !UiaClientsAreListening().as_bool() {
            return;
        }

        let processing = if is_streaming {
            NotificationProcessing_MostRecent
        } else {
            NotificationProcessing_All
        };

        let _ = UiaRaiseNotificationEvent(
            provider,
            NotificationKind_ItemAdded,
            processing,
            &BSTR::from(text),
            &BSTR::from(activity_id),
        );
    }
}

/// Raise a notification for command completion.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_command_complete(
    provider: &IRawElementProviderSimple,
    command: &str,
    exit_code: i32,
) {
    unsafe {
        if !UiaClientsAreListening().as_bool() {
            return;
        }

        let msg = if exit_code == 0 {
            format!("Command succeeded: {}", command)
        } else {
            format!("Command failed (exit {}): {}", exit_code, command)
        };

        let _ = UiaRaiseNotificationEvent(
            provider,
            NotificationKind_ActionCompleted,
            NotificationProcessing_ImportantAll,
            &BSTR::from(&msg),
            &BSTR::from("command-complete"),
        );
    }
}

/// Raise a UIA structure changed event.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_structure_changed(
    provider: &IRawElementProviderSimple,
    child_runtime_id: &mut [i32],
) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            let _ = UiaRaiseStructureChangedEvent(
                provider,
                StructureChangeType_ChildAdded,
                child_runtime_id.as_mut_ptr(),
                child_runtime_id.len() as i32,
            );
        }
    }
}

/// Raise a notification for a pane mode change (zoom, read-only, broadcast).
///
/// Screen readers will announce the mode change so users know their input
/// state has changed.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_mode_change(provider: &IRawElementProviderSimple, mode: &str, enabled: bool) {
    unsafe {
        if !UiaClientsAreListening().as_bool() {
            return;
        }

        let msg = if enabled {
            format!("{mode} enabled")
        } else {
            format!("{mode} disabled")
        };

        let _ = UiaRaiseNotificationEvent(
            provider,
            NotificationKind_ActionCompleted,
            NotificationProcessing_ImportantAll,
            &BSTR::from(&msg),
            &BSTR::from("mode-change"),
        );
    }
}

/// Raise a notification for an inline image placed in the terminal.
///
/// Includes the image protocol, dimensions, and filename (if available)
/// so screen reader users know an image was displayed.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_image_placed(
    provider: &IRawElementProviderSimple,
    protocol: &str,
    width: u32,
    height: u32,
    filename: Option<&str>,
) {
    unsafe {
        if !UiaClientsAreListening().as_bool() {
            return;
        }

        let msg = match filename {
            Some(name) => format!("{protocol} image: {name} ({width}\u{00d7}{height} pixels)"),
            None => format!("{protocol} image: {width}\u{00d7}{height} pixels"),
        };

        let _ = UiaRaiseNotificationEvent(
            provider,
            NotificationKind_ItemAdded,
            NotificationProcessing_All,
            &BSTR::from(&msg),
            &BSTR::from("image-placed"),
        );
    }
}

/// Raise a UIA TextSelectionChanged event.
///
/// Tells screen readers the caret/selection moved so they re-query
/// `GetCaretRange()` / `GetSelection()`.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_text_selection_changed(provider: &IRawElementProviderSimple) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            let _ = UiaRaiseAutomationEvent(provider, UIA_Text_TextSelectionChangedEventId);
        }
    }
}

/// Raise a UIA focus changed event.
///
/// # Safety
/// Must be called on the UI thread.
pub unsafe fn raise_focus_changed(provider: &IRawElementProviderSimple) {
    unsafe {
        if UiaClientsAreListening().as_bool() {
            let _ = UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_throttler_set_debounce_ms() {
        let mut throttler = EventThrottler::new();
        throttler.set_debounce_ms(250);
        assert_eq!(throttler.debounce_interval, Duration::from_millis(250));
    }

    #[test]
    fn test_throttler_set_debounce_ms_clamps_low() {
        let mut throttler = EventThrottler::new();
        throttler.set_debounce_ms(10);
        assert_eq!(throttler.debounce_interval, Duration::from_millis(50));
    }

    #[test]
    fn test_throttler_set_debounce_ms_clamps_high() {
        let mut throttler = EventThrottler::new();
        throttler.set_debounce_ms(5000);
        assert_eq!(throttler.debounce_interval, Duration::from_millis(1000));
    }

    #[test]
    fn test_throttler_debounce() {
        let mut throttler = EventThrottler::new();

        // First output — should be ready immediately (timer starts at Instant::now)
        // But the debounce check is: elapsed >= 100ms
        // Since we just created it, elapsed is ~0, so first call returns false
        assert!(!throttler.on_output("line 1\n"));

        // Simulate waiting 150ms
        throttler.last_text_changed = Instant::now() - Duration::from_millis(150);
        assert!(throttler.on_output("line 2\n"));

        let text = throttler.take_pending().unwrap();
        assert!(text.contains("line 1"));
        assert!(text.contains("line 2"));
    }

    #[test]
    fn test_strip_vt_escapes_csi() {
        assert_eq!(strip_vt_escapes("\x1b[?25lhello\x1b[?25h"), "hello");
    }

    #[test]
    fn test_strip_vt_escapes_osc() {
        assert_eq!(
            strip_vt_escapes("\x1b]0;window title\x07some text"),
            "some text"
        );
        assert_eq!(strip_vt_escapes("\x1b]9001;CmdNotFound;ls\x1b\\"), "");
    }

    #[test]
    fn test_strip_vt_escapes_mixed() {
        // \r is stripped, \n preserved
        assert_eq!(
            strip_vt_escapes(
                "\x1b[?25l\x1b[2;1HC:\\Users\\prati>\r\nC:\\Users\\prati>\x1b]0;cmd.exe\x07\x1b[?25h"
            ),
            "C:\\Users\\prati>\nC:\\Users\\prati>"
        );
    }

    #[test]
    fn test_strip_vt_escapes_erase_lines() {
        // \r stripped, \n preserved
        assert_eq!(strip_vt_escapes("\r\n\x1b[K\r\n\x1b[K\r\n\x1b[K"), "\n\n\n");
    }

    #[test]
    fn test_strip_vt_escapes_plain_text() {
        assert_eq!(strip_vt_escapes("hello world"), "hello world");
    }

    #[test]
    fn test_strip_vt_escapes_empty() {
        assert_eq!(strip_vt_escapes(""), "");
    }

    #[test]
    fn test_strip_vt_escapes_control_chars() {
        // Backspace, BEL, and other C0 controls should be stripped
        assert_eq!(strip_vt_escapes("\x08"), "");
        assert_eq!(strip_vt_escapes("\x08 \x08"), " ");
        assert_eq!(strip_vt_escapes("\x07hello\x08"), "hello");
    }

    #[test]
    fn test_strip_vt_escapes_preserves_newlines() {
        // \n must survive — it's our line delimiter
        assert_eq!(strip_vt_escapes("line1\nline2\n"), "line1\nline2\n");
    }

    #[test]
    fn test_strip_vt_escapes_strips_carriage_return() {
        // \r is a control char used for cursor positioning, not meaningful for speech
        assert_eq!(strip_vt_escapes("hello\r\nworld"), "hello\nworld");
    }

    #[test]
    fn test_strip_vt_escapes_tabs_preserved() {
        // \t is useful for formatting in terminal output
        assert_eq!(strip_vt_escapes("col1\tcol2"), "col1\tcol2");
    }

    #[test]
    fn test_strip_vt_escapes_mixed_controls_and_escapes() {
        // Real arrow key echo: CSI sequences + backspace
        assert_eq!(strip_vt_escapes("\x1b[C\x08\x1b[K"), "");
    }

    #[test]
    fn test_streaming_detection() {
        let mut throttler = EventThrottler::new();
        throttler.last_text_changed = Instant::now() - Duration::from_millis(150);

        // Send lots of lines
        for i in 0..20 {
            throttler.on_output(&format!("line {}\n", i));
        }

        throttler.take_pending();
        assert!(throttler.is_streaming());
    }
}
