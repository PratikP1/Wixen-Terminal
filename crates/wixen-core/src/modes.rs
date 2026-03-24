/// Terminal mode flags (DEC private modes and ANSI modes).
#[derive(Debug, Clone)]
pub struct TerminalModes {
    /// DECCKM: Cursor key mode (application vs normal)
    pub cursor_keys_application: bool,
    /// DECANM: ANSI mode (vs VT52)
    pub ansi_mode: bool,
    /// DECAWM: Auto-wrap mode
    pub auto_wrap: bool,
    /// DECTCEM: Text cursor enable
    pub cursor_visible: bool,
    /// Alternate screen buffer active
    pub alternate_screen: bool,
    /// Bracketed paste mode
    pub bracketed_paste: bool,
    /// Focus reporting
    pub focus_reporting: bool,
    /// Mouse tracking modes
    pub mouse_tracking: MouseMode,
    /// SGR mouse encoding (CSI ? 1006) — extended coordinates
    pub mouse_sgr: bool,
    /// Line feed / new line mode
    pub line_feed_new_line: bool,
    /// Insert mode (IRM)
    pub insert_mode: bool,
    /// Origin mode (DECOM)
    pub origin_mode: bool,
    /// DECSCNM: Reverse video (swap fg/bg for entire screen)
    pub reverse_video: bool,
    /// Synchronized output mode (Mode 2026)
    pub synchronized_output: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum MouseMode {
    #[default]
    None,
    X10,
    Normal,
    Button,
    Any,
}

impl Default for TerminalModes {
    fn default() -> Self {
        Self {
            cursor_keys_application: false,
            ansi_mode: true,
            auto_wrap: true,
            cursor_visible: true,
            alternate_screen: false,
            bracketed_paste: false,
            focus_reporting: false,
            mouse_tracking: MouseMode::None,
            mouse_sgr: false,
            line_feed_new_line: false,
            insert_mode: false,
            origin_mode: false,
            reverse_video: false,
            synchronized_output: false,
        }
    }
}
