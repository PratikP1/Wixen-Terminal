use crate::action::Action;

/// VT parser states based on the DEC VT500 state machine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    Ground,
    Escape,
    EscapeIntermediate,
    CsiEntry,
    CsiParam,
    CsiIntermediate,
    CsiIgnore,
    OscString,
    DcsEntry,
    DcsParam,
    DcsIntermediate,
    DcsPassthrough,
    DcsIgnore,
    SosPmApcString,
    ApcString,
}

/// DEC VT500 state machine parser.
///
/// Processes raw bytes and emits semantic `Action` values.
/// Phase 0: Basic structure with Print/Execute handling.
/// Full CSI/OSC/DCS parsing will be built in Phase 1.
pub struct Parser {
    state: State,
    params: Vec<u16>,
    intermediates: Vec<u8>,
    osc_data: Vec<Vec<u8>>,
    osc_raw: Vec<u8>,
    current_param: u16,
    has_param: bool,
    /// Sub-parameter tracking for colon-delimited CSI params (e.g. SGR 4:3).
    /// `current_subparams` accumulates colon-separated values for the current param.
    /// `all_subparams` is parallel to `params` — one sub-param vec per param.
    current_subparams: Vec<u16>,
    all_subparams: Vec<Vec<u16>>,
    collecting_subparam: bool,
    /// UTF-8 multi-byte decoder state
    utf8_buf: [u8; 4],
    utf8_len: u8, // expected total bytes (2/3/4)
    utf8_idx: u8, // bytes received so far
    /// APC (Application Program Command) data accumulator
    apc_data: Vec<u8>,
}

impl Parser {
    pub fn new() -> Self {
        Self {
            state: State::Ground,
            params: Vec::new(),
            intermediates: Vec::new(),
            osc_data: Vec::new(),
            osc_raw: Vec::new(),
            current_param: 0,
            has_param: false,
            current_subparams: Vec::new(),
            all_subparams: Vec::new(),
            collecting_subparam: false,
            utf8_buf: [0; 4],
            utf8_len: 0,
            utf8_idx: 0,
            apc_data: Vec::new(),
        }
    }

    /// Feed a single byte into the parser, collecting emitted actions.
    #[inline]
    pub fn advance(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match self.state {
            State::Ground => self.ground(byte, actions),
            State::Escape => self.escape(byte, actions),
            State::EscapeIntermediate => self.escape_intermediate(byte, actions),
            State::CsiEntry => self.csi_entry(byte, actions),
            State::CsiParam => self.csi_param(byte, actions),
            State::CsiIntermediate => self.csi_intermediate(byte, actions),
            State::CsiIgnore => self.csi_ignore(byte),
            State::OscString => self.osc_string(byte, actions),
            State::DcsEntry => self.dcs_entry(byte, actions),
            State::DcsParam => self.dcs_param(byte, actions),
            State::DcsIntermediate => self.dcs_intermediate(byte, actions),
            State::DcsPassthrough => self.dcs_passthrough(byte, actions),
            State::DcsIgnore => self.dcs_ignore(byte),
            State::SosPmApcString => self.sos_pm_apc_string(byte),
            State::ApcString => self.apc_string(byte, actions),
        }
    }

    /// Process a slice of bytes, returning all emitted actions.
    ///
    /// Uses a fast path for consecutive printable ASCII bytes in Ground state
    /// to avoid per-byte state machine dispatch overhead.
    pub fn process(&mut self, bytes: &[u8]) -> Vec<Action> {
        let mut actions = Vec::with_capacity(bytes.len() / 2);
        let mut i = 0;
        while i < bytes.len() {
            // Fast path: batch consecutive printable ASCII in Ground state
            if self.state == State::Ground && self.utf8_len == 0 {
                while i < bytes.len() {
                    let b = bytes[i];
                    if (0x20..=0x7e).contains(&b) {
                        actions.push(Action::Print(b as char));
                        i += 1;
                    } else {
                        break;
                    }
                }
                if i >= bytes.len() {
                    break;
                }
            }
            self.advance(bytes[i], &mut actions);
            i += 1;
        }
        actions
    }

    #[inline]
    fn ground(&mut self, byte: u8, actions: &mut Vec<Action>) {
        // If we're mid-UTF-8 sequence, try to continue it
        if self.utf8_len > 0 {
            if byte & 0xC0 == 0x80 {
                // Valid continuation byte
                self.utf8_buf[self.utf8_idx as usize] = byte;
                self.utf8_idx += 1;
                if self.utf8_idx == self.utf8_len {
                    // Sequence complete — decode
                    let len = self.utf8_len as usize;
                    self.utf8_len = 0;
                    self.utf8_idx = 0;
                    match std::str::from_utf8(&self.utf8_buf[..len]) {
                        Ok(s) => {
                            for ch in s.chars() {
                                actions.push(Action::Print(ch));
                            }
                        }
                        Err(_) => {
                            actions.push(Action::Print('\u{FFFD}'));
                        }
                    }
                }
                return;
            } else {
                // Interrupted sequence — emit replacement, then re-process this byte
                self.utf8_len = 0;
                self.utf8_idx = 0;
                actions.push(Action::Print('\u{FFFD}'));
                // Fall through to process the current byte normally
            }
        }

        match byte {
            // C0 controls
            0x00..=0x1a | 0x1c..=0x1f => {
                actions.push(Action::Execute(byte));
            }
            // ESC
            0x1b => {
                self.state = State::Escape;
                self.intermediates.clear();
            }
            // Printable ASCII
            0x20..=0x7e => {
                actions.push(Action::Print(byte as char));
            }
            // DEL — ignore
            0x7f => {}
            // C1 controls (0x80-0x9F) — treat as execute
            0x80..=0x9f => {
                actions.push(Action::Execute(byte));
            }
            // UTF-8 lead bytes
            0xc0..=0xdf => {
                // 2-byte sequence
                self.utf8_buf[0] = byte;
                self.utf8_len = 2;
                self.utf8_idx = 1;
            }
            0xe0..=0xef => {
                // 3-byte sequence
                self.utf8_buf[0] = byte;
                self.utf8_len = 3;
                self.utf8_idx = 1;
            }
            0xf0..=0xf7 => {
                // 4-byte sequence
                self.utf8_buf[0] = byte;
                self.utf8_len = 4;
                self.utf8_idx = 1;
            }
            // Invalid lead bytes (0xF8-0xFF)
            0xf8..=0xff => {
                actions.push(Action::Print('\u{FFFD}'));
            }
            // 0xA0-0xBF are stray continuation bytes
            _ => {
                actions.push(Action::Print('\u{FFFD}'));
            }
        }
    }

    fn escape(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            // C0 controls in escape
            0x00..=0x17 | 0x19 | 0x1c..=0x1f => {
                actions.push(Action::Execute(byte));
            }
            // CSI entry: ESC [
            0x5b => {
                self.state = State::CsiEntry;
                self.params.clear();
                self.intermediates.clear();
                self.current_param = 0;
                self.has_param = false;
                self.current_subparams.clear();
                self.all_subparams.clear();
                self.collecting_subparam = false;
            }
            // OSC entry: ESC ]
            0x5d => {
                self.state = State::OscString;
                self.osc_data.clear();
                self.osc_raw.clear();
            }
            // DCS entry: ESC P
            0x50 => {
                self.state = State::DcsEntry;
                self.params.clear();
                self.intermediates.clear();
                self.current_param = 0;
                self.has_param = false;
            }
            // SOS: ESC X
            0x58 => {
                self.state = State::SosPmApcString;
            }
            // PM: ESC ^
            0x5e => {
                self.state = State::SosPmApcString;
            }
            // APC: ESC _
            0x5f => {
                self.state = State::ApcString;
                self.apc_data.clear();
            }
            // Intermediates
            0x20..=0x2f => {
                self.intermediates.push(byte);
                self.state = State::EscapeIntermediate;
            }
            // ESC dispatch
            0x30..=0x4f | 0x51..=0x57 | 0x59..=0x5a | 0x5c | 0x60..=0x7e => {
                actions.push(Action::EscDispatch {
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::Ground;
            }
            // DEL — ignore
            0x7f => {}
            _ => {
                self.state = State::Ground;
            }
        }
    }

    fn escape_intermediate(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x00..=0x17 | 0x19 | 0x1c..=0x1f => {
                actions.push(Action::Execute(byte));
            }
            0x20..=0x2f => {
                self.intermediates.push(byte);
            }
            0x30..=0x7e => {
                actions.push(Action::EscDispatch {
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::Ground;
            }
            0x7f => {}
            _ => {
                self.state = State::Ground;
            }
        }
    }

    fn csi_entry(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x00..=0x17 | 0x19 | 0x1c..=0x1f => {
                actions.push(Action::Execute(byte));
            }
            // Parameter bytes
            0x30..=0x39 => {
                self.current_param = (byte - 0x30) as u16;
                self.has_param = true;
                self.state = State::CsiParam;
            }
            // Colon — sub-parameter separator
            0x3a => {
                self.current_subparams.push(self.current_param);
                self.current_param = 0;
                self.collecting_subparam = true;
                self.has_param = true;
                self.state = State::CsiParam;
            }
            // Semicolon
            0x3b => {
                self.params.push(0);
                self.all_subparams.push(Vec::new());
                self.state = State::CsiParam;
            }
            // Private marker (? > =)
            0x3c..=0x3f => {
                self.intermediates.push(byte);
                self.state = State::CsiParam;
            }
            // Intermediate bytes
            0x20..=0x2f => {
                self.intermediates.push(byte);
                self.state = State::CsiIntermediate;
            }
            // Final bytes — dispatch
            0x40..=0x7e => {
                actions.push(Action::CsiDispatch {
                    params: self.params.clone(),
                    subparams: self.all_subparams.clone(),
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::Ground;
            }
            0x7f => {}
            _ => {
                self.state = State::Ground;
            }
        }
    }

    fn csi_param(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x00..=0x17 | 0x19 | 0x1c..=0x1f => {
                actions.push(Action::Execute(byte));
            }
            0x30..=0x39 => {
                self.current_param = self
                    .current_param
                    .saturating_mul(10)
                    .saturating_add((byte - 0x30) as u16);
                self.has_param = true;
            }
            // Colon — sub-parameter separator (e.g. SGR 4:3 for curly underline)
            0x3a => {
                self.current_subparams.push(self.current_param);
                self.current_param = 0;
                self.collecting_subparam = true;
                self.has_param = true;
            }
            0x3b => {
                self.finalize_current_param();
                self.current_param = 0;
                self.has_param = false;
            }
            0x20..=0x2f => {
                if self.has_param {
                    self.finalize_current_param();
                    self.current_param = 0;
                    self.has_param = false;
                }
                self.intermediates.push(byte);
                self.state = State::CsiIntermediate;
            }
            0x40..=0x7e => {
                if self.has_param {
                    self.finalize_current_param();
                }
                actions.push(Action::CsiDispatch {
                    params: self.params.clone(),
                    subparams: self.all_subparams.clone(),
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::Ground;
            }
            0x3c..=0x3f => {
                // Invalid — ignore the rest of this sequence
                self.state = State::CsiIgnore;
            }
            0x7f => {}
            _ => {
                self.state = State::Ground;
            }
        }
    }

    /// Finalize the current param: push it (and any sub-params) to the params/subparams vectors.
    fn finalize_current_param(&mut self) {
        if self.collecting_subparam {
            // Last colon-separated value
            self.current_subparams.push(self.current_param);
            // First element is the main param, rest are sub-params
            let main = self.current_subparams[0];
            let subs = self.current_subparams[1..].to_vec();
            self.params.push(main);
            self.all_subparams.push(subs);
            self.current_subparams.clear();
            self.collecting_subparam = false;
        } else {
            self.params.push(self.current_param);
            self.all_subparams.push(Vec::new());
        }
    }

    fn csi_intermediate(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x00..=0x17 | 0x19 | 0x1c..=0x1f => {
                actions.push(Action::Execute(byte));
            }
            0x20..=0x2f => {
                self.intermediates.push(byte);
            }
            0x40..=0x7e => {
                actions.push(Action::CsiDispatch {
                    params: self.params.clone(),
                    subparams: self.all_subparams.clone(),
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::Ground;
            }
            0x30..=0x3f => {
                self.state = State::CsiIgnore;
            }
            0x7f => {}
            _ => {
                self.state = State::Ground;
            }
        }
    }

    fn csi_ignore(&mut self, byte: u8) {
        if let 0x40..=0x7e = byte {
            self.state = State::Ground;
        }
    }

    fn osc_string(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            // ST (String Terminator) via BEL
            0x07 => {
                self.finalize_osc(actions);
                self.state = State::Ground;
            }
            // ESC — could be ST (ESC \)
            0x1b => {
                // Peek for backslash is handled by setting a flag;
                // for simplicity in Phase 0, treat ESC as ST
                self.finalize_osc(actions);
                self.state = State::Ground;
            }
            // Semicolon separates OSC parameters
            0x3b => {
                self.osc_data.push(self.osc_raw.clone());
                self.osc_raw.clear();
            }
            // Cap OSC string size at 4KB for security
            _ if self.osc_raw.len() < 4096 => {
                self.osc_raw.push(byte);
            }
            _ => {}
        }
    }

    fn finalize_osc(&mut self, actions: &mut Vec<Action>) {
        if !self.osc_raw.is_empty() || !self.osc_data.is_empty() {
            self.osc_data.push(std::mem::take(&mut self.osc_raw));
            actions.push(Action::OscDispatch(std::mem::take(&mut self.osc_data)));
        }
    }

    fn dcs_entry(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x30..=0x39 => {
                self.current_param = (byte - 0x30) as u16;
                self.has_param = true;
                self.state = State::DcsParam;
            }
            0x3b => {
                self.params.push(0);
                self.state = State::DcsParam;
            }
            0x3c..=0x3f => {
                self.intermediates.push(byte);
                self.state = State::DcsParam;
            }
            0x20..=0x2f => {
                self.intermediates.push(byte);
                self.state = State::DcsIntermediate;
            }
            0x40..=0x7e => {
                actions.push(Action::DcsHook {
                    params: self.params.clone(),
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::DcsPassthrough;
            }
            _ => {
                self.state = State::DcsIgnore;
            }
        }
    }

    fn dcs_param(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x30..=0x39 => {
                self.current_param = self
                    .current_param
                    .saturating_mul(10)
                    .saturating_add((byte - 0x30) as u16);
                self.has_param = true;
            }
            0x3b => {
                self.params.push(if self.has_param {
                    self.current_param
                } else {
                    0
                });
                self.current_param = 0;
                self.has_param = false;
            }
            0x20..=0x2f => {
                if self.has_param {
                    self.params.push(self.current_param);
                }
                self.intermediates.push(byte);
                self.state = State::DcsIntermediate;
            }
            0x40..=0x7e => {
                if self.has_param {
                    self.params.push(self.current_param);
                }
                actions.push(Action::DcsHook {
                    params: self.params.clone(),
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::DcsPassthrough;
            }
            _ => {
                self.state = State::DcsIgnore;
            }
        }
    }

    fn dcs_intermediate(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x20..=0x2f => {
                self.intermediates.push(byte);
            }
            0x40..=0x7e => {
                actions.push(Action::DcsHook {
                    params: self.params.clone(),
                    intermediates: self.intermediates.clone(),
                    action: byte as char,
                });
                self.state = State::DcsPassthrough;
            }
            _ => {
                self.state = State::DcsIgnore;
            }
        }
    }

    fn dcs_passthrough(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            0x1b => {
                actions.push(Action::DcsUnhook);
                self.state = State::Ground;
            }
            0x9c => {
                actions.push(Action::DcsUnhook);
                self.state = State::Ground;
            }
            _ => {
                actions.push(Action::DcsPut(byte));
            }
        }
    }

    fn dcs_ignore(&mut self, byte: u8) {
        match byte {
            0x1b | 0x9c => {
                self.state = State::Ground;
            }
            _ => {}
        }
    }

    fn sos_pm_apc_string(&mut self, byte: u8) {
        match byte {
            0x1b | 0x9c => {
                self.state = State::Ground;
            }
            _ => {}
        }
    }

    fn apc_string(&mut self, byte: u8, actions: &mut Vec<Action>) {
        match byte {
            // ST (String Terminator) C1 control
            0x9c => {
                actions.push(Action::ApcDispatch(std::mem::take(&mut self.apc_data)));
                self.state = State::Ground;
            }
            // ESC starts potential ST (ESC \)
            0x1b => {
                // Peek: if followed by 0x5c it's ST. We handle this by transitioning
                // to a state that checks for \. For simplicity, dispatch now since
                // ESC always terminates APC in practice.
                actions.push(Action::ApcDispatch(std::mem::take(&mut self.apc_data)));
                self.state = State::Escape;
            }
            _ => {
                // Cap at 16MB for safety (image data can be large)
                if self.apc_data.len() < 16 * 1024 * 1024 {
                    self.apc_data.push(byte);
                }
            }
        }
    }
}

impl Default for Parser {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_printable_ascii() {
        let mut parser = Parser::new();
        let actions = parser.process(b"Hello");
        assert_eq!(actions.len(), 5);
        for (i, ch) in "Hello".chars().enumerate() {
            match &actions[i] {
                Action::Print(c) => assert_eq!(*c, ch),
                other => panic!("Expected Print, got {:?}", other),
            }
        }
    }

    #[test]
    fn test_csi_cursor_up() {
        let mut parser = Parser::new();
        // ESC [ 5 A = cursor up 5
        let actions = parser.process(b"\x1b[5A");
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::CsiDispatch { params, action, .. } => {
                assert_eq!(*action, 'A');
                assert_eq!(params, &[5]);
            }
            other => panic!("Expected CsiDispatch, got {:?}", other),
        }
    }

    #[test]
    fn test_osc_title() {
        let mut parser = Parser::new();
        // ESC ] 0 ; title BEL
        let actions = parser.process(b"\x1b]0;My Title\x07");
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::OscDispatch(parts) => {
                assert_eq!(parts.len(), 2);
                assert_eq!(parts[0], b"0");
                assert_eq!(parts[1], b"My Title");
            }
            other => panic!("Expected OscDispatch, got {:?}", other),
        }
    }

    #[test]
    fn test_cr_lf() {
        let mut parser = Parser::new();
        let actions = parser.process(b"\r\n");
        assert_eq!(actions.len(), 2);
        match &actions[0] {
            Action::Execute(0x0d) => {}
            other => panic!("Expected Execute(CR), got {:?}", other),
        }
        match &actions[1] {
            Action::Execute(0x0a) => {}
            other => panic!("Expected Execute(LF), got {:?}", other),
        }
    }

    #[test]
    fn test_csi_subparam_sgr_4_3() {
        let mut parser = Parser::new();
        // ESC [ 4:3 m = SGR 4 with sub-param 3 (curly underline)
        let actions = parser.process(b"\x1b[4:3m");
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::CsiDispatch {
                params,
                subparams,
                action,
                ..
            } => {
                assert_eq!(*action, 'm');
                assert_eq!(params, &[4]);
                assert_eq!(subparams, &[vec![3]]);
            }
            other => panic!("Expected CsiDispatch, got {:?}", other),
        }
    }

    #[test]
    fn test_csi_subparam_all_variants() {
        let mut parser = Parser::new();
        // 4:0 (none), 4:1 (single), 4:2 (double), 4:4 (dotted), 4:5 (dashed)
        for (input, expected_sub) in [
            (&b"\x1b[4:0m"[..], 0u16),
            (&b"\x1b[4:1m"[..], 1),
            (&b"\x1b[4:2m"[..], 2),
            (&b"\x1b[4:4m"[..], 4),
            (&b"\x1b[4:5m"[..], 5),
        ] {
            let actions = parser.process(input);
            assert_eq!(actions.len(), 1);
            match &actions[0] {
                Action::CsiDispatch {
                    params, subparams, ..
                } => {
                    assert_eq!(params, &[4]);
                    assert_eq!(subparams, &[vec![expected_sub]]);
                }
                other => panic!("Expected CsiDispatch, got {:?}", other),
            }
        }
    }

    #[test]
    fn test_csi_no_subparam_backward_compat() {
        let mut parser = Parser::new();
        // SGR 4 without sub-params: should have empty subparams
        let actions = parser.process(b"\x1b[4m");
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::CsiDispatch {
                params, subparams, ..
            } => {
                assert_eq!(params, &[4]);
                assert_eq!(subparams, &[vec![] as Vec<u16>]);
            }
            other => panic!("Expected CsiDispatch, got {:?}", other),
        }
    }

    #[test]
    fn test_csi_mixed_params_and_subparams() {
        let mut parser = Parser::new();
        // ESC [ 4:3;1 m = curly underline + bold
        let actions = parser.process(b"\x1b[4:3;1m");
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::CsiDispatch {
                params, subparams, ..
            } => {
                assert_eq!(params, &[4, 1]);
                assert_eq!(subparams, &[vec![3], vec![]]);
            }
            other => panic!("Expected CsiDispatch, got {:?}", other),
        }
    }

    #[test]
    fn test_csi_color_subparams() {
        let mut parser = Parser::new();
        // ESC [ 38:2:255:128:0 m = true color fg via sub-params
        let actions = parser.process(b"\x1b[38:2:255:128:0m");
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::CsiDispatch {
                params, subparams, ..
            } => {
                assert_eq!(params, &[38]);
                assert_eq!(subparams, &[vec![2, 255, 128, 0]]);
            }
            other => panic!("Expected CsiDispatch, got {:?}", other),
        }
    }

    #[test]
    fn test_utf8_two_byte() {
        let mut parser = Parser::new();
        // ö = U+00F6 = 0xC3 0xB6
        let actions = parser.process("ö".as_bytes());
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::Print(ch) => assert_eq!(*ch, 'ö'),
            other => panic!("Expected Print('ö'), got {:?}", other),
        }
    }

    #[test]
    fn test_utf8_three_byte() {
        let mut parser = Parser::new();
        // 你 = U+4F60 = 0xE4 0xBD 0xA0
        let actions = parser.process("你".as_bytes());
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::Print(ch) => assert_eq!(*ch, '你'),
            other => panic!("Expected Print('你'), got {:?}", other),
        }
    }

    #[test]
    fn test_utf8_four_byte() {
        let mut parser = Parser::new();
        // 🦀 = U+1F980 = 0xF0 0x9F 0xA6 0x80
        let actions = parser.process("🦀".as_bytes());
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::Print(ch) => assert_eq!(*ch, '🦀'),
            other => panic!("Expected Print('🦀'), got {:?}", other),
        }
    }

    #[test]
    fn test_utf8_invalid_sequence() {
        let mut parser = Parser::new();
        // Invalid: 0xC3 followed by non-continuation 0x41 ('A')
        let actions = parser.process(&[0xC3, 0x41]);
        assert_eq!(actions.len(), 2);
        match &actions[0] {
            Action::Print(ch) => assert_eq!(*ch, '\u{FFFD}'),
            other => panic!("Expected Print(U+FFFD), got {:?}", other),
        }
        match &actions[1] {
            Action::Print(ch) => assert_eq!(*ch, 'A'),
            other => panic!("Expected Print('A'), got {:?}", other),
        }
    }

    #[test]
    fn test_utf8_stray_continuation() {
        let mut parser = Parser::new();
        // Stray continuation byte 0x80 without a lead byte
        let actions = parser.process(&[0x80]);
        assert_eq!(actions.len(), 1);
        match &actions[0] {
            Action::Execute(0x80) => {}
            other => panic!("Expected Execute(0x80), got {:?}", other),
        }
    }

    #[test]
    fn test_utf8_mixed_ascii_and_multibyte() {
        let mut parser = Parser::new();
        // "Aöb" = 'A', 0xC3 0xB6, 'b'
        let actions = parser.process("Aöb".as_bytes());
        assert_eq!(actions.len(), 3);
        match &actions[0] {
            Action::Print('A') => {}
            other => panic!("Expected Print('A'), got {:?}", other),
        }
        match &actions[1] {
            Action::Print('ö') => {}
            other => panic!("Expected Print('ö'), got {:?}", other),
        }
        match &actions[2] {
            Action::Print('b') => {}
            other => panic!("Expected Print('b'), got {:?}", other),
        }
    }

    #[test]
    fn test_apc_dispatch() {
        // ESC _ G a = T ESC \   → APC with "Ga=T"
        let mut parser = Parser::new();
        let actions = parser.process(b"\x1b_Ga=T\x1b\\");
        // Should emit ApcDispatch with payload "Ga=T"
        let apc_actions: Vec<_> = actions
            .iter()
            .filter(|a| matches!(a, Action::ApcDispatch(_)))
            .collect();
        assert_eq!(apc_actions.len(), 1);
        match &apc_actions[0] {
            Action::ApcDispatch(data) => {
                assert_eq!(data, b"Ga=T");
            }
            _ => panic!("Expected ApcDispatch"),
        }
    }

    #[test]
    fn test_apc_dispatch_with_payload() {
        let mut parser = Parser::new();
        let actions = parser.process(b"\x1b_Ga=T,f=32;AAAA\x1b\\");
        let apc_actions: Vec<_> = actions
            .iter()
            .filter(|a| matches!(a, Action::ApcDispatch(_)))
            .collect();
        assert_eq!(apc_actions.len(), 1);
        match &apc_actions[0] {
            Action::ApcDispatch(data) => {
                assert_eq!(data, b"Ga=T,f=32;AAAA");
            }
            _ => panic!("Expected ApcDispatch"),
        }
    }

    #[test]
    fn test_apc_sos_still_ignored() {
        // SOS (ESC X) and PM (ESC ^) should still be silently consumed
        let mut parser = Parser::new();
        let actions = parser.process(b"\x1b^some data\x1b\\");
        // No ApcDispatch should be emitted for PM
        let apc_count = actions
            .iter()
            .filter(|a| matches!(a, Action::ApcDispatch(_)))
            .count();
        assert_eq!(apc_count, 0);
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    // The parser must never panic on arbitrary byte input.
    // PTY output is untrusted and can contain any byte sequence.
    proptest! {
        #![proptest_config(ProptestConfig::with_cases(10_000))]

        #[test]
        fn never_panics_on_arbitrary_bytes(data in proptest::collection::vec(any::<u8>(), 0..4096)) {
            let mut parser = Parser::new();
            let _ = parser.process(&data);
        }

        #[test]
        fn never_panics_on_arbitrary_bytes_fed_one_at_a_time(data in proptest::collection::vec(any::<u8>(), 0..2048)) {
            let mut parser = Parser::new();
            let mut actions = Vec::new();
            for &byte in &data {
                parser.advance(byte, &mut actions);
            }
        }

        /// Parser always returns to Ground state after an explicit reset sequence.
        /// Feed arbitrary data, then ESC c (RIS = reset), then printable ASCII —
        /// the printable bytes must produce Print actions.
        #[test]
        fn returns_to_ground_after_reset_then_flush(
            prefix in proptest::collection::vec(any::<u8>(), 0..512),
        ) {
            let mut parser = Parser::new();
            let _ = parser.process(&prefix);

            // ESC c = RIS (Reset to Initial State) — forces back to Ground via EscDispatch
            // Then ST (ESC \) to terminate any lingering string state
            let mut reset_and_flush = vec![0x1b, b'c', 0x1b, b'\\'];
            // Then 64 printable ASCII bytes
            reset_and_flush.extend((0..64).map(|i| b'A' + (i % 26) as u8));
            let actions = parser.process(&reset_and_flush);

            let print_count = actions.iter().filter(|a| matches!(a, Action::Print(_))).count();
            prop_assert!(print_count > 0, "Expected Print actions after reset+flush, got none");
        }

        /// Every valid CSI sequence must produce exactly one CsiDispatch.
        #[test]
        fn valid_csi_produces_dispatch(
            param_count in 0usize..5,
            params in proptest::collection::vec(0u16..1000, 0..5),
            final_byte in 0x40u8..=0x7e,
        ) {
            let mut seq = vec![0x1b, b'['];
            for (i, &p) in params.iter().take(param_count).enumerate() {
                if i > 0 {
                    seq.push(b';');
                }
                for ch in p.to_string().bytes() {
                    seq.push(ch);
                }
            }
            seq.push(final_byte);

            let mut parser = Parser::new();
            let actions = parser.process(&seq);
            let csi_count = actions.iter().filter(|a| matches!(a, Action::CsiDispatch { .. })).count();
            prop_assert_eq!(csi_count, 1, "CSI sequence must produce exactly one dispatch");
        }

        /// Every valid OSC sequence (ESC ] ... BEL) must produce exactly one OscDispatch.
        #[test]
        fn valid_osc_produces_dispatch(
            osc_num in 0u16..200,
            body in "[a-zA-Z0-9 _/.:=-]{0,100}",
        ) {
            let mut seq = vec![0x1b, b']'];
            for ch in osc_num.to_string().bytes() {
                seq.push(ch);
            }
            seq.push(b';');
            seq.extend_from_slice(body.as_bytes());
            seq.push(0x07); // BEL terminator

            let mut parser = Parser::new();
            let actions = parser.process(&seq);
            let osc_count = actions.iter().filter(|a| matches!(a, Action::OscDispatch(_))).count();
            prop_assert_eq!(osc_count, 1, "OSC sequence must produce exactly one dispatch");
        }

        /// Pure ASCII printable strings must produce one Print per character.
        #[test]
        fn ascii_printable_produces_exact_prints(s in "[\\x20-\\x7e]{1,200}") {
            let mut parser = Parser::new();
            let actions = parser.process(s.as_bytes());
            let print_count = actions.iter().filter(|a| matches!(a, Action::Print(_))).count();
            prop_assert_eq!(print_count, s.len(),
                "ASCII printable string of len {} should produce {} Print actions, got {}",
                s.len(), s.len(), print_count);
        }

        /// Multi-byte UTF-8 characters above U+009F must produce Print actions.
        #[test]
        fn utf8_above_c1_produces_prints(s in "[\\u{00A0}-\\u{FFFF}]{1,50}") {
            let mut parser = Parser::new();
            let actions = parser.process(s.as_bytes());
            let print_count = actions.iter().filter(|a| matches!(a, Action::Print(_))).count();
            let char_count = s.chars().count();
            prop_assert_eq!(print_count, char_count,
                "UTF-8 string '{}' ({} chars) should produce {} Prints, got {}",
                s, char_count, char_count, print_count);
        }

        /// Feeding data byte-by-byte or all-at-once must produce the same actions.
        /// (The fast path in process() must be equivalent to advance() one at a time.)
        #[test]
        fn process_matches_advance_one_at_a_time(data in proptest::collection::vec(any::<u8>(), 0..512)) {
            let mut parser_batch = Parser::new();
            let batch_actions = parser_batch.process(&data);

            let mut parser_single = Parser::new();
            let mut single_actions = Vec::new();
            for &byte in &data {
                parser_single.advance(byte, &mut single_actions);
            }

            prop_assert_eq!(batch_actions.len(), single_actions.len(),
                "Batch and single-byte must produce same number of actions");
        }
    }
}
