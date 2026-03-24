//! Tmux control mode (`tmux -CC`) event parser.
//!
//! In control mode, tmux sends structured notifications on stdout as lines
//! starting with `%`. This module parses those lines into typed events that
//! the terminal can use to synchronize native tabs/panes with tmux sessions.
//!
//! Reference: `tmux(1)` CONTROL MODE section.

/// A parsed tmux control mode event.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TmuxEvent {
    /// A new window was created: `%window-add @<id>`
    WindowAdd(u32),
    /// A window was closed: `%window-close @<id>`
    WindowClose(u32),
    /// A window was renamed: `%window-renamed @<id> <name>`
    WindowRenamed(u32, String),
    /// A new session was created: `%session-changed $<id> <name>`
    SessionChanged(u32, String),
    /// A pane mode changed: `%pane-mode-changed %<id>`
    PaneModeChanged(u32),
    /// Output from tmux (a data line, not a notification).
    Output(String),
    /// Layout changed: `%layout-change @<id> <layout>`
    LayoutChange(u32, String),
    /// Client session changed: `%client-session-changed $<id> <name>`
    ClientSessionChanged(u32, String),
    /// Unrecognized `%`-prefixed notification.
    Unknown(String),
    /// Marks the beginning of control mode: `%begin <timestamp> <number> <flags>`
    Begin,
    /// Marks the end of a command response: `%end <timestamp> <number> <flags>`
    End,
    /// Marks an error response: `%error <timestamp> <number> <flags>`
    Error(String),
    /// Exit notification: `%exit [reason]`
    Exit(Option<String>),
}

/// Parse a single line of tmux control mode output into a `TmuxEvent`.
///
/// Lines not starting with `%` are treated as `TmuxEvent::Output`.
pub fn parse_control_line(line: &str) -> TmuxEvent {
    let line = line.trim_end_matches('\n').trim_end_matches('\r');

    if !line.starts_with('%') {
        return TmuxEvent::Output(line.to_string());
    }

    let parts: Vec<&str> = line.splitn(3, ' ').collect();
    let cmd = parts[0];

    match cmd {
        "%window-add" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_at_id(s)) {
                TmuxEvent::WindowAdd(id)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%window-close" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_at_id(s)) {
                TmuxEvent::WindowClose(id)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%window-renamed" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_at_id(s)) {
                let name = parts.get(2).unwrap_or(&"").to_string();
                TmuxEvent::WindowRenamed(id, name)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%session-changed" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_dollar_id(s)) {
                let name = parts.get(2).unwrap_or(&"").to_string();
                TmuxEvent::SessionChanged(id, name)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%client-session-changed" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_dollar_id(s)) {
                let name = parts.get(2).unwrap_or(&"").to_string();
                TmuxEvent::ClientSessionChanged(id, name)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%pane-mode-changed" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_percent_id(s)) {
                TmuxEvent::PaneModeChanged(id)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%layout-change" => {
            if let Some(id) = parts.get(1).and_then(|s| parse_at_id(s)) {
                let layout = parts.get(2).unwrap_or(&"").to_string();
                TmuxEvent::LayoutChange(id, layout)
            } else {
                TmuxEvent::Unknown(line.to_string())
            }
        }
        "%begin" => TmuxEvent::Begin,
        "%end" => TmuxEvent::End,
        "%error" => {
            let rest = if parts.len() > 1 {
                line["%error ".len()..].to_string()
            } else {
                String::new()
            };
            TmuxEvent::Error(rest)
        }
        "%exit" => {
            let reason = if parts.len() > 1 {
                Some(line["%exit ".len()..].to_string())
            } else {
                None
            };
            TmuxEvent::Exit(reason)
        }
        _ => TmuxEvent::Unknown(line.to_string()),
    }
}

/// Parse `@<number>` (tmux window ID format).
fn parse_at_id(s: &str) -> Option<u32> {
    s.strip_prefix('@').and_then(|n| n.parse().ok())
}

/// Parse `$<number>` (tmux session ID format).
fn parse_dollar_id(s: &str) -> Option<u32> {
    s.strip_prefix('$').and_then(|n| n.parse().ok())
}

/// Parse `%<number>` (tmux pane ID format).
fn parse_percent_id(s: &str) -> Option<u32> {
    s.strip_prefix('%').and_then(|n| n.parse().ok())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_window_add() {
        assert_eq!(
            parse_control_line("%window-add @0"),
            TmuxEvent::WindowAdd(0)
        );
        assert_eq!(
            parse_control_line("%window-add @42"),
            TmuxEvent::WindowAdd(42)
        );
    }

    #[test]
    fn test_parse_window_close() {
        assert_eq!(
            parse_control_line("%window-close @3"),
            TmuxEvent::WindowClose(3)
        );
    }

    #[test]
    fn test_parse_window_renamed() {
        assert_eq!(
            parse_control_line("%window-renamed @1 my-session"),
            TmuxEvent::WindowRenamed(1, "my-session".to_string())
        );
    }

    #[test]
    fn test_parse_session_changed() {
        assert_eq!(
            parse_control_line("%session-changed $0 main"),
            TmuxEvent::SessionChanged(0, "main".to_string())
        );
    }

    #[test]
    fn test_parse_client_session_changed() {
        assert_eq!(
            parse_control_line("%client-session-changed $1 work"),
            TmuxEvent::ClientSessionChanged(1, "work".to_string())
        );
    }

    #[test]
    fn test_parse_pane_mode_changed() {
        assert_eq!(
            parse_control_line("%pane-mode-changed %5"),
            TmuxEvent::PaneModeChanged(5)
        );
    }

    #[test]
    fn test_parse_layout_change() {
        assert_eq!(
            parse_control_line("%layout-change @0 abc1,80x24,0,0,0"),
            TmuxEvent::LayoutChange(0, "abc1,80x24,0,0,0".to_string())
        );
    }

    #[test]
    fn test_parse_begin_end_error() {
        assert_eq!(parse_control_line("%begin 1234 0 0"), TmuxEvent::Begin);
        assert_eq!(parse_control_line("%end 1234 0 0"), TmuxEvent::End);
        assert_eq!(
            parse_control_line("%error 1234 0 0"),
            TmuxEvent::Error("1234 0 0".to_string())
        );
    }

    #[test]
    fn test_parse_exit() {
        assert_eq!(parse_control_line("%exit"), TmuxEvent::Exit(None));
        assert_eq!(
            parse_control_line("%exit server exited"),
            TmuxEvent::Exit(Some("server exited".to_string()))
        );
    }

    #[test]
    fn test_parse_output_line() {
        assert_eq!(
            parse_control_line("Hello, world!"),
            TmuxEvent::Output("Hello, world!".to_string())
        );
    }

    #[test]
    fn test_parse_unknown_notification() {
        assert_eq!(
            parse_control_line("%future-event @1 data"),
            TmuxEvent::Unknown("%future-event @1 data".to_string())
        );
    }

    #[test]
    fn test_parse_window_renamed_with_spaces() {
        assert_eq!(
            parse_control_line("%window-renamed @2 my cool window"),
            TmuxEvent::WindowRenamed(2, "my cool window".to_string())
        );
    }

    #[test]
    fn test_parse_invalid_id() {
        assert_eq!(
            parse_control_line("%window-add invalid"),
            TmuxEvent::Unknown("%window-add invalid".to_string())
        );
    }

    #[test]
    fn test_parse_strips_trailing_newline() {
        assert_eq!(
            parse_control_line("%window-add @0\n"),
            TmuxEvent::WindowAdd(0)
        );
        assert_eq!(
            parse_control_line("%window-add @0\r\n"),
            TmuxEvent::WindowAdd(0)
        );
    }
}
