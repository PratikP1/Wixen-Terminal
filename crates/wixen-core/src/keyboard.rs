//! Keyboard encoding — maps virtual key codes + modifiers to xterm escape sequences.
//!
//! Replaces the old `vk_to_sequence()` function with proper modifier encoding.
//! Uses the xterm modifier parameter: `mod = 1 + shift + 2*alt + 4*ctrl`.

// ---------------------------------------------------------------------------
// Kitty keyboard protocol (progressive enhancement)
// ---------------------------------------------------------------------------

/// Bitfield flags for Kitty keyboard protocol progressive enhancement levels.
///
/// These flags are sent via `CSI > flags u` (push) to enable enhanced key
/// reporting. Multiple flags can be combined with bitwise OR.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KittyKeyboardFlags;

impl KittyKeyboardFlags {
    pub const DISAMBIGUATE_ESCAPE_CODES: u32 = 0b0001;
    pub const REPORT_EVENT_TYPES: u32 = 0b0010;
    pub const REPORT_ALTERNATE_KEYS: u32 = 0b0100;
    pub const REPORT_ALL_KEYS: u32 = 0b1000;
    pub const REPORT_ASSOCIATED_TEXT: u32 = 0b1_0000;
}

/// Manages a stack of Kitty keyboard flag values for push/pop mode.
///
/// Applications push flags via `CSI > flags u` and pop via `CSI < u`.
/// The active flags are always the top of the stack.
#[derive(Debug, Clone, Default)]
pub struct KittyKeyboardState {
    stack: Vec<u32>,
}

impl KittyKeyboardState {
    /// Push a new set of flags onto the stack.
    pub fn push(&mut self, flags: u32) {
        self.stack.push(flags);
    }

    /// Pop the top flags from the stack. No-op if empty.
    pub fn pop(&mut self) {
        self.stack.pop();
    }

    /// Return the current (top-of-stack) flags, or 0 if the stack is empty.
    pub fn current_flags(&self) -> u32 {
        self.stack.last().copied().unwrap_or(0)
    }

    /// Whether Kitty keyboard mode is active (any non-zero flags on the stack).
    pub fn is_enabled(&self) -> bool {
        self.current_flags() != 0
    }
}

/// Encode a key event into Kitty protocol `CSI ... u` format.
///
/// - `key_code`: Unicode codepoint of the key (e.g. 97 for 'a').
/// - `modifiers`: xterm-style modifier value (1 = none, 2 = shift, etc.).
/// - `event_type`: 1 = press, 2 = repeat, 3 = release.
/// - `flags`: active `KittyKeyboardFlags` (used to decide which fields to include).
///
/// Encoding rules:
/// - Format: `CSI codepoint ; [modifiers[:event-type]] u`
/// - Omit the modifiers field entirely when modifiers == 1 and event_type == 1.
/// - Omit event_type when it is 1 (press).
pub fn encode_kitty_key(key_code: u32, modifiers: u32, event_type: u8, flags: u32) -> String {
    let has_event_types = flags & KittyKeyboardFlags::REPORT_EVENT_TYPES != 0;

    let include_event_type = has_event_types && event_type != 1;
    let include_modifiers = modifiers != 1 || include_event_type;

    let mut seq = format!("\x1b[{key_code}");

    if include_modifiers {
        seq.push(';');
        seq.push_str(&modifiers.to_string());
        if include_event_type {
            seq.push(':');
            seq.push_str(&event_type.to_string());
        }
    }

    seq.push('u');
    seq
}

/// Encode a key press into a terminal escape sequence.
///
/// `vk` is the Windows virtual key code. `shift`, `ctrl`, `alt` indicate modifier state.
/// `app_cursor` is true when DECCKM (application cursor keys) mode is active.
///
/// Returns `None` for keys that don't map to a terminal sequence (e.g. Shift alone).
pub fn encode_key(
    vk: u16,
    shift: bool,
    ctrl: bool,
    alt: bool,
    app_cursor: bool,
) -> Option<Vec<u8>> {
    // Ctrl+letter: send the control character directly
    if ctrl && !shift && !alt && (0x41..=0x5A).contains(&vk) {
        let ch = (vk as u8) - 0x40; // Ctrl+A = 0x01, Ctrl+Z = 0x1A
        return Some(vec![ch]);
    }

    // Alt+letter: ESC prefix + lowercase letter
    if alt && !ctrl && !shift && (0x41..=0x5A).contains(&vk) {
        let ch = (vk as u8) + 0x20; // lowercase
        return Some(vec![0x1b, ch]);
    }

    let modifier = modifier_param(shift, ctrl, alt);

    // Simple keys (no modifier parameterization)
    if modifier == 1 {
        return unmodified_key(vk, app_cursor);
    }

    // Modified keys — use parameterized forms
    modified_key(vk, modifier)
}

/// Compute the xterm modifier parameter: 1 + shift + 2*alt + 4*ctrl.
fn modifier_param(shift: bool, ctrl: bool, alt: bool) -> u8 {
    1 + (shift as u8) + (alt as u8) * 2 + (ctrl as u8) * 4
}

/// Unmodified (plain) key sequences.
fn unmodified_key(vk: u16, app_cursor: bool) -> Option<Vec<u8>> {
    let seq: &[u8] = match vk {
        0x0D => b"\r",   // Return
        0x08 => b"\x7f", // Backspace
        0x09 => b"\t",   // Tab
        0x1B => b"\x1b", // Escape
        // Arrow keys — application mode uses SS3, normal uses CSI
        0x26 if app_cursor => b"\x1bOA", // Up
        0x28 if app_cursor => b"\x1bOB", // Down
        0x27 if app_cursor => b"\x1bOC", // Right
        0x25 if app_cursor => b"\x1bOD", // Left
        0x26 => b"\x1b[A",               // Up
        0x28 => b"\x1b[B",               // Down
        0x27 => b"\x1b[C",               // Right
        0x25 => b"\x1b[D",               // Left
        // Navigation
        0x24 => b"\x1b[H",  // Home
        0x23 => b"\x1b[F",  // End
        0x2D => b"\x1b[2~", // Insert
        0x2E => b"\x1b[3~", // Delete
        0x21 => b"\x1b[5~", // Page Up
        0x22 => b"\x1b[6~", // Page Down
        // Function keys
        0x70 => b"\x1bOP",   // F1
        0x71 => b"\x1bOQ",   // F2
        0x72 => b"\x1bOR",   // F3
        0x73 => b"\x1bOS",   // F4
        0x74 => b"\x1b[15~", // F5
        0x75 => b"\x1b[17~", // F6
        0x76 => b"\x1b[18~", // F7
        0x77 => b"\x1b[19~", // F8
        0x78 => b"\x1b[20~", // F9
        0x79 => b"\x1b[21~", // F10
        0x7A => b"\x1b[23~", // F11
        0x7B => b"\x1b[24~", // F12
        _ => return None,
    };
    Some(seq.to_vec())
}

/// Modified key sequences — arrows, navigation, and function keys with modifier parameter.
fn modified_key(vk: u16, modifier: u8) -> Option<Vec<u8>> {
    // Arrow keys: CSI 1;{mod}{letter}
    let arrow_suffix = match vk {
        0x26 => Some(b'A'), // Up
        0x28 => Some(b'B'), // Down
        0x27 => Some(b'C'), // Right
        0x25 => Some(b'D'), // Left
        _ => None,
    };
    if let Some(suffix) = arrow_suffix {
        return Some(format!("\x1b[1;{modifier}{}", suffix as char).into_bytes());
    }

    // Home/End: CSI 1;{mod}H/F
    match vk {
        0x24 => return Some(format!("\x1b[1;{modifier}H").into_bytes()), // Home
        0x23 => return Some(format!("\x1b[1;{modifier}F").into_bytes()), // End
        _ => {}
    }

    // Tilde keys: CSI {num};{mod}~
    let tilde_num = match vk {
        0x2D => Some(2), // Insert
        0x2E => Some(3), // Delete
        0x21 => Some(5), // Page Up
        0x22 => Some(6), // Page Down
        _ => None,
    };
    if let Some(num) = tilde_num {
        return Some(format!("\x1b[{num};{modifier}~").into_bytes());
    }

    // F1-F4: CSI 1;{mod}P/Q/R/S (modified form uses CSI, not SS3)
    let f1_4_suffix = match vk {
        0x70 => Some(b'P'), // F1
        0x71 => Some(b'Q'), // F2
        0x72 => Some(b'R'), // F3
        0x73 => Some(b'S'), // F4
        _ => None,
    };
    if let Some(suffix) = f1_4_suffix {
        return Some(format!("\x1b[1;{modifier}{}", suffix as char).into_bytes());
    }

    // F5-F12: CSI {num};{mod}~
    let fn_num = match vk {
        0x74 => Some(15), // F5
        0x75 => Some(17), // F6
        0x76 => Some(18), // F7
        0x77 => Some(19), // F8
        0x78 => Some(20), // F9
        0x79 => Some(21), // F10
        0x7A => Some(23), // F11
        0x7B => Some(24), // F12
        _ => None,
    };
    if let Some(num) = fn_num {
        return Some(format!("\x1b[{num};{modifier}~").into_bytes());
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_unmodified_arrow() {
        assert_eq!(
            encode_key(0x26, false, false, false, false),
            Some(b"\x1b[A".to_vec())
        );
    }

    #[test]
    fn test_app_cursor_arrow() {
        assert_eq!(
            encode_key(0x26, false, false, false, true),
            Some(b"\x1bOA".to_vec())
        );
    }

    #[test]
    fn test_ctrl_arrow() {
        // Ctrl+Right → \x1b[1;5C
        assert_eq!(
            encode_key(0x27, false, true, false, false),
            Some(b"\x1b[1;5C".to_vec())
        );
    }

    #[test]
    fn test_shift_home() {
        // Shift+Home → \x1b[1;2H
        assert_eq!(
            encode_key(0x24, true, false, false, false),
            Some(b"\x1b[1;2H".to_vec())
        );
    }

    #[test]
    fn test_f5_plain() {
        assert_eq!(
            encode_key(0x74, false, false, false, false),
            Some(b"\x1b[15~".to_vec())
        );
    }

    #[test]
    fn test_ctrl_shift_f5() {
        // Ctrl+Shift+F5 → mod=6, \x1b[15;6~
        assert_eq!(
            encode_key(0x74, true, true, false, false),
            Some(b"\x1b[15;6~".to_vec())
        );
    }

    #[test]
    fn test_ctrl_a() {
        assert_eq!(
            encode_key(0x41, false, true, false, false),
            Some(vec![0x01])
        );
    }

    #[test]
    fn test_ctrl_z() {
        assert_eq!(
            encode_key(0x5A, false, true, false, false),
            Some(vec![0x1A])
        );
    }

    #[test]
    fn test_alt_a() {
        assert_eq!(
            encode_key(0x41, false, false, true, false),
            Some(vec![0x1b, b'a'])
        );
    }

    #[test]
    fn test_return() {
        assert_eq!(
            encode_key(0x0D, false, false, false, false),
            Some(b"\r".to_vec())
        );
    }

    #[test]
    fn test_backspace() {
        assert_eq!(
            encode_key(0x08, false, false, false, false),
            Some(b"\x7f".to_vec())
        );
    }

    #[test]
    fn test_f1_unmodified() {
        assert_eq!(
            encode_key(0x70, false, false, false, false),
            Some(b"\x1bOP".to_vec())
        );
    }

    #[test]
    fn test_f1_ctrl() {
        // Ctrl+F1 → \x1b[1;5P
        assert_eq!(
            encode_key(0x70, false, true, false, false),
            Some(b"\x1b[1;5P".to_vec())
        );
    }

    #[test]
    fn test_ctrl_shift_alt_up() {
        // mod = 1 + 1 + 2 + 4 = 8
        assert_eq!(
            encode_key(0x26, true, true, true, false),
            Some(b"\x1b[1;8A".to_vec())
        );
    }

    #[test]
    fn test_page_up_ctrl() {
        // Ctrl+PgUp → \x1b[5;5~
        assert_eq!(
            encode_key(0x21, false, true, false, false),
            Some(b"\x1b[5;5~".to_vec())
        );
    }

    #[test]
    fn test_delete_shift() {
        // Shift+Delete → \x1b[3;2~
        assert_eq!(
            encode_key(0x2E, true, false, false, false),
            Some(b"\x1b[3;2~".to_vec())
        );
    }

    #[test]
    fn test_unknown_key_returns_none() {
        // Some random VK that isn't mapped
        assert_eq!(encode_key(0xFF, false, false, false, false), None);
    }

    // -----------------------------------------------------------------------
    // Kitty keyboard protocol tests
    // -----------------------------------------------------------------------

    mod kitty_keyboard {
        use super::super::*;

        // -- KittyKeyboardState stack tests --

        #[test]
        fn current_flags_returns_zero_when_empty() {
            let state = KittyKeyboardState::default();
            assert_eq!(state.current_flags(), 0);
        }

        #[test]
        fn is_enabled_returns_false_when_empty() {
            let state = KittyKeyboardState::default();
            assert!(!state.is_enabled());
        }

        #[test]
        fn push_sets_current_flags() {
            let mut state = KittyKeyboardState::default();
            state.push(KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES);
            assert_eq!(state.current_flags(), 1);
            assert!(state.is_enabled());
        }

        #[test]
        fn pop_reverts_to_previous_flags() {
            let mut state = KittyKeyboardState::default();
            state.push(KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES);
            state.push(
                KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES
                    | KittyKeyboardFlags::REPORT_EVENT_TYPES,
            );
            assert_eq!(state.current_flags(), 0b0011);
            state.pop();
            assert_eq!(state.current_flags(), 0b0001);
        }

        #[test]
        fn pop_on_empty_is_noop() {
            let mut state = KittyKeyboardState::default();
            state.pop(); // should not panic
            assert_eq!(state.current_flags(), 0);
        }

        #[test]
        fn multiple_push_pop_operations() {
            let mut state = KittyKeyboardState::default();
            state.push(0b0001);
            state.push(0b0011);
            state.push(0b0111);
            assert_eq!(state.current_flags(), 0b0111);
            state.pop();
            assert_eq!(state.current_flags(), 0b0011);
            state.pop();
            assert_eq!(state.current_flags(), 0b0001);
            state.pop();
            assert_eq!(state.current_flags(), 0);
            assert!(!state.is_enabled());
        }

        // -- encode_kitty_key tests --

        #[test]
        fn basic_key_no_modifiers() {
            // 'a' (97), no modifiers, press — omit modifier field entirely
            let result = encode_kitty_key(97, 1, 1, KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES);
            assert_eq!(result, "\x1b[97u");
        }

        #[test]
        fn key_with_shift_modifier() {
            // 'A' = Shift+a: codepoint 97, modifier 2 (shift)
            let result = encode_kitty_key(97, 2, 1, KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES);
            assert_eq!(result, "\x1b[97;2u");
        }

        #[test]
        fn key_release_event() {
            // 'a' release: event_type=3, modifier=1, REPORT_EVENT_TYPES flag on
            let flags = KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES
                | KittyKeyboardFlags::REPORT_EVENT_TYPES;
            let result = encode_kitty_key(97, 1, 3, flags);
            assert_eq!(result, "\x1b[97;1:3u");
        }

        #[test]
        fn event_type_omitted_without_flag() {
            // Even with event_type=3, if REPORT_EVENT_TYPES is not set, omit it
            let result = encode_kitty_key(97, 1, 3, KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES);
            assert_eq!(result, "\x1b[97u");
        }

        #[test]
        fn modifier_and_release_event() {
            // Shift+a release: modifier=2, event_type=3
            let flags = KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES
                | KittyKeyboardFlags::REPORT_EVENT_TYPES;
            let result = encode_kitty_key(97, 2, 3, flags);
            assert_eq!(result, "\x1b[97;2:3u");
        }

        #[test]
        fn repeat_event() {
            // 'a' repeat: event_type=2
            let flags = KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES
                | KittyKeyboardFlags::REPORT_EVENT_TYPES;
            let result = encode_kitty_key(97, 1, 2, flags);
            assert_eq!(result, "\x1b[97;1:2u");
        }

        #[test]
        fn ctrl_modifier() {
            // Ctrl+a: modifier = 1 + 4(ctrl) = 5
            let result = encode_kitty_key(97, 5, 1, KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES);
            assert_eq!(result, "\x1b[97;5u");
        }

        #[test]
        fn flags_bitfield_constants() {
            assert_eq!(KittyKeyboardFlags::DISAMBIGUATE_ESCAPE_CODES, 0b0_0001);
            assert_eq!(KittyKeyboardFlags::REPORT_EVENT_TYPES, 0b0_0010);
            assert_eq!(KittyKeyboardFlags::REPORT_ALTERNATE_KEYS, 0b0_0100);
            assert_eq!(KittyKeyboardFlags::REPORT_ALL_KEYS, 0b0_1000);
            assert_eq!(KittyKeyboardFlags::REPORT_ASSOCIATED_TEXT, 0b1_0000);
        }
    }
}
