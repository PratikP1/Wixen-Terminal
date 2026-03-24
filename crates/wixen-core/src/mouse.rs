//! Mouse event encoding for terminal mouse reporting.
//!
//! Supports both legacy X10/Normal encoding (`ESC [ M ...`) and SGR extended
//! encoding (`ESC [ < ... M/m`). The SGR format is preferred because it handles
//! coordinates beyond 223 and distinguishes press from release.

/// Mouse button identifiers (xterm encoding).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MouseButton {
    Left,
    Middle,
    Right,
    /// Release (legacy encoding only — SGR uses the original button + 'm')
    Release,
    WheelUp,
    WheelDown,
}

impl MouseButton {
    /// Base button code for the wire encoding.
    fn code(self) -> u8 {
        match self {
            Self::Left => 0,
            Self::Middle => 1,
            Self::Right => 2,
            Self::Release => 3,
            Self::WheelUp => 64,
            Self::WheelDown => 65,
        }
    }
}

/// Modifier flags added to the button byte.
#[derive(Debug, Clone, Copy, Default)]
pub struct MouseModifiers {
    pub shift: bool,
    pub alt: bool,
    pub ctrl: bool,
    /// Set when the event is a motion event (drag / any-motion).
    pub motion: bool,
}

impl MouseModifiers {
    fn bits(self) -> u8 {
        let mut b = 0u8;
        if self.shift {
            b |= 4;
        }
        if self.alt {
            b |= 8;
        }
        if self.ctrl {
            b |= 16;
        }
        if self.motion {
            b |= 32;
        }
        b
    }
}

/// Encode a mouse event in SGR format (CSI ? 1006).
///
/// Format: `ESC [ < Cb ; Cx ; Cy M` (press) or `ESC [ < Cb ; Cx ; Cy m` (release).
/// Coordinates are 1-based.
pub fn encode_mouse_sgr(
    button: MouseButton,
    mods: MouseModifiers,
    col: usize,
    row: usize,
    pressed: bool,
) -> String {
    let cb = match button {
        // For SGR release, use the original button code (not 3)
        MouseButton::Release => 0,
        b => b.code(),
    } + mods.bits();
    let suffix = if pressed { 'M' } else { 'm' };
    format!("\x1b[<{};{};{}{}", cb, col + 1, row + 1, suffix)
}

/// Encode a mouse event in legacy/normal format (X10 / Normal / Button / Any).
///
/// Format: `ESC [ M Cb Cx Cy` where each of Cb, Cx, Cy is a single byte.
/// Coordinates are 1-based and offset by 32 (`+ 0x20`), limiting them to 223.
/// Returns `None` if the coordinates exceed the encodable range.
pub fn encode_mouse_normal(
    button: MouseButton,
    mods: MouseModifiers,
    col: usize,
    row: usize,
) -> Option<Vec<u8>> {
    // Legacy format caps at 223 (0xFF - 0x20)
    if col > 222 || row > 222 {
        return None;
    }

    let cb = button.code() + mods.bits() + 32;
    let cx = (col as u8) + 1 + 32; // 1-based + offset
    let cy = (row as u8) + 1 + 32;
    Some(vec![0x1b, b'[', b'M', cb, cx, cy])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sgr_left_press() {
        let s = encode_mouse_sgr(MouseButton::Left, MouseModifiers::default(), 0, 0, true);
        assert_eq!(s, "\x1b[<0;1;1M");
    }

    #[test]
    fn test_sgr_left_release() {
        let s = encode_mouse_sgr(MouseButton::Left, MouseModifiers::default(), 5, 10, false);
        assert_eq!(s, "\x1b[<0;6;11m");
    }

    #[test]
    fn test_sgr_right_with_shift() {
        let mods = MouseModifiers {
            shift: true,
            ..Default::default()
        };
        let s = encode_mouse_sgr(MouseButton::Right, mods, 3, 7, true);
        // button=2, shift=+4 => 6
        assert_eq!(s, "\x1b[<6;4;8M");
    }

    #[test]
    fn test_sgr_wheel_up() {
        let s = encode_mouse_sgr(
            MouseButton::WheelUp,
            MouseModifiers::default(),
            10,
            20,
            true,
        );
        assert_eq!(s, "\x1b[<64;11;21M");
    }

    #[test]
    fn test_sgr_ctrl_middle() {
        let mods = MouseModifiers {
            ctrl: true,
            ..Default::default()
        };
        let s = encode_mouse_sgr(MouseButton::Middle, mods, 0, 0, true);
        // button=1, ctrl=+16 => 17
        assert_eq!(s, "\x1b[<17;1;1M");
    }

    #[test]
    fn test_sgr_motion() {
        let mods = MouseModifiers {
            motion: true,
            ..Default::default()
        };
        let s = encode_mouse_sgr(MouseButton::Left, mods, 5, 5, true);
        // button=0, motion=+32 => 32
        assert_eq!(s, "\x1b[<32;6;6M");
    }

    #[test]
    fn test_sgr_large_coordinates() {
        let s = encode_mouse_sgr(MouseButton::Left, MouseModifiers::default(), 300, 500, true);
        assert_eq!(s, "\x1b[<0;301;501M");
    }

    #[test]
    fn test_normal_left_press() {
        let seq = encode_mouse_normal(MouseButton::Left, MouseModifiers::default(), 0, 0);
        // cb = 0 + 32 = 32 (space), cx = 1 + 32 = 33 (!), cy = 1 + 32 = 33 (!)
        assert_eq!(seq, Some(vec![0x1b, b'[', b'M', 32, 33, 33]));
    }

    #[test]
    fn test_normal_right_press() {
        let seq = encode_mouse_normal(MouseButton::Right, MouseModifiers::default(), 4, 9);
        // cb = 2 + 32 = 34, cx = 5 + 32 = 37, cy = 10 + 32 = 42
        assert_eq!(seq, Some(vec![0x1b, b'[', b'M', 34, 37, 42]));
    }

    #[test]
    fn test_normal_release() {
        let seq = encode_mouse_normal(MouseButton::Release, MouseModifiers::default(), 0, 0);
        // cb = 3 + 32 = 35
        assert_eq!(seq, Some(vec![0x1b, b'[', b'M', 35, 33, 33]));
    }

    #[test]
    fn test_normal_overflow_returns_none() {
        let seq = encode_mouse_normal(MouseButton::Left, MouseModifiers::default(), 223, 0);
        assert!(seq.is_none());
    }

    #[test]
    fn test_normal_max_valid() {
        let seq = encode_mouse_normal(MouseButton::Left, MouseModifiers::default(), 222, 222);
        assert!(seq.is_some());
    }
}
