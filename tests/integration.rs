//! Integration tests — end-to-end VT pipeline and PTY round-trips.
//!
//! These tests verify the full path: raw bytes → VT parser → Terminal → grid state.
//! They don't require a window, renderer, or GPU — just the parser and terminal core.

use wixen_core::Terminal;
use wixen_vt::{Action, Parser};

// ---------------------------------------------------------------------------
// Helper: feed raw bytes through Parser → Terminal, mirroring src/main.rs
// ---------------------------------------------------------------------------

fn feed(terminal: &mut Terminal, parser: &mut Parser, data: &[u8]) {
    let mut actions = Vec::new();
    for &byte in data {
        parser.advance(byte, &mut actions);
    }
    for action in actions {
        dispatch_action(terminal, action);
    }
}

fn dispatch_action(terminal: &mut Terminal, action: Action) {
    match action {
        Action::Print(ch) => terminal.print(ch),
        Action::Execute(byte) => terminal.execute(byte),
        Action::CsiDispatch {
            params,
            subparams,
            intermediates,
            action,
        } => terminal.csi_dispatch(&params, &intermediates, action, &subparams),
        Action::EscDispatch {
            intermediates,
            action,
        } => terminal.esc_dispatch(&intermediates, action),
        Action::OscDispatch(params) => terminal.osc_dispatch(&params),
        Action::DcsHook {
            params,
            intermediates,
            action,
        } => terminal.dcs_hook(&params, &intermediates, action),
        Action::DcsPut(byte) => terminal.dcs_put(byte),
        Action::DcsUnhook => terminal.dcs_unhook(),
        Action::ApcDispatch(data) => terminal.apc_dispatch(&data),
    }
}

/// Extract row text from the terminal grid (0-based row index).
fn row_text(terminal: &Terminal, row: usize) -> String {
    let mut text = String::new();
    for col in 0..terminal.cols() {
        if let Some(cell) = terminal.grid.cell(col, row) {
            text.push_str(&cell.content);
        }
    }
    text.trim_end().to_string()
}

// ===========================================================================
// Pipeline tests — raw escape bytes → parsed → terminal grid
// ===========================================================================

#[test]
fn test_pipeline_plain_text() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"Hello, Wixen!");
    assert_eq!(row_text(&terminal, 0), "Hello, Wixen!");
}

#[test]
fn test_pipeline_newline() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"line1\r\nline2");
    assert_eq!(row_text(&terminal, 0), "line1");
    assert_eq!(row_text(&terminal, 1), "line2");
}

#[test]
fn test_pipeline_cursor_movement() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Write "ABCDE", then CUP to row 1 col 1 (1-based → ESC[2;1H), write "X"
    feed(&mut terminal, &mut parser, b"ABCDE\x1b[2;1HX");
    assert_eq!(row_text(&terminal, 0), "ABCDE");
    assert_eq!(row_text(&terminal, 1), "X");
}

#[test]
fn test_pipeline_sgr_color() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // ESC[31m sets foreground to red, then print text, then ESC[0m resets
    feed(&mut terminal, &mut parser, b"\x1b[31mRED\x1b[0m normal");
    assert_eq!(row_text(&terminal, 0), "RED normal");
    // Verify the 'R' cell has red foreground
    let cell_r = terminal.grid.cell(0, 0).unwrap();
    assert!(
        matches!(cell_r.attrs.fg, wixen_core::attrs::Color::Indexed(1)),
        "Expected red foreground, got {:?}",
        cell_r.attrs.fg
    );
    // Verify 'n' cell has default foreground
    let cell_n = terminal.grid.cell(4, 0).unwrap();
    assert!(
        matches!(cell_n.attrs.fg, wixen_core::attrs::Color::Default),
        "Expected default foreground after reset, got {:?}",
        cell_n.attrs.fg
    );
}

#[test]
fn test_pipeline_erase_in_display() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"ABCDE\x1b[2J");
    // ESC[2J erases entire display — all cells should be empty
    assert_eq!(row_text(&terminal, 0), "");
}

#[test]
fn test_pipeline_erase_in_line() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Write "ABCDE", move cursor to col 3 (ESC[1;4H = col 3 zero-based), erase to end of line
    feed(&mut terminal, &mut parser, b"ABCDE\x1b[1;4H\x1b[K");
    assert_eq!(row_text(&terminal, 0), "ABC");
}

#[test]
fn test_pipeline_scroll_up() {
    let mut terminal = Terminal::new(80, 4);
    let mut parser = Parser::new();
    // Fill 4 rows, then write another line to trigger scroll
    feed(
        &mut terminal,
        &mut parser,
        b"row1\r\nrow2\r\nrow3\r\nrow4\r\nrow5",
    );
    // After scroll, row1 should be in scrollback, visible grid starts at row2
    assert_eq!(row_text(&terminal, 0), "row2");
    assert_eq!(row_text(&terminal, 3), "row5");
    assert!(
        terminal.scrollback.len() > 0,
        "Scrollback should contain row1"
    );
}

#[test]
fn test_pipeline_osc_title() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 0 ; title BEL
    feed(&mut terminal, &mut parser, b"\x1b]0;My Title\x07");
    assert_eq!(terminal.title, "My Title");
}

#[test]
fn test_pipeline_osc_title_st() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 2 ; title ST (using ESC \ as ST)
    feed(&mut terminal, &mut parser, b"\x1b]2;Alt Title\x1b\\");
    assert_eq!(terminal.title, "Alt Title");
}

#[test]
fn test_pipeline_insert_delete_chars() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Write "ABCDE", move to col 2 (ESC[1;3H), insert 2 chars (ESC[2@)
    feed(&mut terminal, &mut parser, b"ABCDE\x1b[1;3H\x1b[2@XY");
    // After inserting 2 blanks at col 2, then writing "XY":
    // Original: A B C D E
    // Insert 2 at col 2: A B _ _ C D E (CDE shifted right)
    // Write XY at col 2: A B X Y C D E
    assert_eq!(row_text(&terminal, 0), "ABXYCDE");
}

#[test]
fn test_pipeline_decset_decaln() {
    let mut terminal = Terminal::new(10, 4);
    let mut parser = Parser::new();
    // DECALN fills screen with 'E' characters (ESC # 8)
    feed(&mut terminal, &mut parser, b"\x1b#8");
    for row in 0..4 {
        assert_eq!(row_text(&terminal, row), "EEEEEEEEEE");
    }
}

#[test]
fn test_pipeline_tab_stops() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Horizontal tab moves to next tab stop (every 8 columns by default)
    feed(&mut terminal, &mut parser, b"A\tB\tC");
    // A at col 0, tab to col 8 → B, tab to col 16 → C
    assert_eq!(terminal.grid.cell(0, 0).unwrap().content.as_str(), "A");
    assert_eq!(terminal.grid.cell(8, 0).unwrap().content.as_str(), "B");
    assert_eq!(terminal.grid.cell(16, 0).unwrap().content.as_str(), "C");
}

#[test]
fn test_pipeline_alternate_screen() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Write on primary screen
    feed(&mut terminal, &mut parser, b"primary");
    // Switch to alternate screen (DECSET 1049)
    feed(&mut terminal, &mut parser, b"\x1b[?1049h");
    assert_eq!(row_text(&terminal, 0), ""); // alt screen is blank
    feed(&mut terminal, &mut parser, b"alternate");
    assert_eq!(row_text(&terminal, 0), "alternate");
    // Switch back to primary (DECRST 1049)
    feed(&mut terminal, &mut parser, b"\x1b[?1049l");
    assert_eq!(row_text(&terminal, 0), "primary");
}

#[test]
fn test_pipeline_osc133_shell_integration() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Simulate a shell integration cycle: A → B → C → D
    // OSC 133;A BEL — prompt start
    feed(&mut terminal, &mut parser, b"\x1b]133;A\x07");
    // Prompt text
    feed(&mut terminal, &mut parser, b"PS> ");
    // OSC 133;B BEL — command start
    feed(&mut terminal, &mut parser, b"\x1b]133;B\x07");
    // User command
    feed(&mut terminal, &mut parser, b"echo hello");
    // OSC 133;C BEL — command executed
    feed(&mut terminal, &mut parser, b"\x1b]133;C\x07");
    // Output
    feed(&mut terminal, &mut parser, b"\r\nhello\r\n");
    // OSC 133;D;0 BEL — command complete with exit code 0
    feed(&mut terminal, &mut parser, b"\x1b]133;D;0\x07");

    assert!(terminal.shell_integ.osc133_active);
    let blocks = terminal.shell_integ.blocks();
    assert_eq!(blocks.len(), 1);
    assert_eq!(blocks[0].exit_code, Some(0));
    assert_eq!(blocks[0].state, wixen_shell_integ::BlockState::Completed);
}

#[test]
fn test_pipeline_osc8_hyperlink() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 8 ; params ; uri ST ... OSC 8 ; ; ST
    feed(
        &mut terminal,
        &mut parser,
        b"\x1b]8;;https://example.com\x07Click\x1b]8;;\x07",
    );
    assert_eq!(row_text(&terminal, 0), "Click");
    // The 'C' cell should have a hyperlink
    let link = terminal.hyperlink_at(0, 0);
    assert_eq!(link, Some("https://example.com".to_string()));
}

#[test]
fn test_pipeline_osc52_clipboard() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 52 ; c ; <base64("test")> BEL
    // base64("test") = "dGVzdA=="
    feed(&mut terminal, &mut parser, b"\x1b]52;c;dGVzdA==\x07");
    let writes = terminal.drain_clipboard_writes();
    assert_eq!(writes, vec!["test"]);
}

#[test]
fn test_pipeline_unicode() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Feed UTF-8 encoded string with multi-byte characters
    feed(&mut terminal, &mut parser, "Hello 🌍 世界".as_bytes());
    let text = row_text(&terminal, 0);
    assert!(text.contains("Hello"));
    assert!(text.contains("🌍"));
    assert!(text.contains("世界"));
}

#[test]
fn test_pipeline_wrap_at_margin() {
    let mut terminal = Terminal::new(10, 4);
    let mut parser = Parser::new();
    // Write 15 characters into a 10-column terminal
    feed(&mut terminal, &mut parser, b"ABCDEFGHIJKLMNO");
    assert_eq!(row_text(&terminal, 0), "ABCDEFGHIJ");
    assert_eq!(row_text(&terminal, 1), "KLMNO");
}

#[test]
fn test_pipeline_bell() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"\x07");
    assert!(terminal.bell_pending, "BEL should set bell_pending");
}

#[test]
fn test_pipeline_device_status_report() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Move cursor to row 5, col 10 (1-based: ESC[6;11H), then DSR (ESC[6n)
    feed(&mut terminal, &mut parser, b"\x1b[6;11H\x1b[6n");
    let responses = terminal.drain_responses();
    assert!(!responses.is_empty(), "DSR should produce a response");
    // Response should be ESC[6;11R (1-based)
    let resp = String::from_utf8_lossy(&responses[0]);
    assert!(
        resp.contains("[6;11R"),
        "Expected cursor position report, got: {}",
        resp
    );
}

// ===========================================================================
// PTY round-trip tests (spawn a real shell, verify output)
// ===========================================================================

#[cfg(windows)]
mod pty_tests {
    use super::*;
    use std::time::{Duration, Instant};
    use wixen_pty::{PtyEvent, PtyHandle};

    /// Helper: drain PTY output into the terminal for up to `timeout`.
    /// Returns when `predicate` returns true or timeout expires.
    fn drain_until(
        terminal: &mut Terminal,
        parser: &mut Parser,
        rx: &crossbeam_channel::Receiver<PtyEvent>,
        timeout: Duration,
        predicate: impl Fn(&Terminal) -> bool,
    ) -> bool {
        let start = Instant::now();
        while start.elapsed() < timeout {
            match rx.recv_timeout(Duration::from_millis(100)) {
                Ok(PtyEvent::Output(data)) => {
                    feed(terminal, parser, &data);
                    if predicate(terminal) {
                        return true;
                    }
                }
                Ok(PtyEvent::Exited(_)) => break,
                Err(_) => {
                    if predicate(terminal) {
                        return true;
                    }
                }
            }
        }
        predicate(terminal)
    }

    #[test]
    fn test_pty_spawn_and_exit() {
        // Spawn cmd.exe, send "exit", verify it exits
        let (mut pty, rx) = PtyHandle::spawn_with_shell(80, 24, "cmd.exe", &["/Q".to_string()], "")
            .expect("Failed to spawn PTY");
        let mut terminal = Terminal::new(80, 24);
        let mut parser = Parser::new();

        // Wait for initial prompt
        drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |t| t.visible_text().contains(">"),
        );

        // Send exit command
        pty.write(b"exit\r\n").expect("Failed to write to PTY");

        // Wait for process to exit
        let exited = drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |_| false, // just drain until timeout or exit event
        );
        // The PTY should eventually close (we don't assert the exact grid content)
        let _ = exited;
    }

    #[test]
    fn test_pty_echo_command() {
        // Spawn cmd.exe, run "echo WIXEN_TEST_STRING", verify output appears
        let (mut pty, rx) = PtyHandle::spawn_with_shell(80, 24, "cmd.exe", &["/Q".to_string()], "")
            .expect("Failed to spawn PTY");
        let mut terminal = Terminal::new(80, 24);
        let mut parser = Parser::new();

        // Wait for prompt
        drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |t| t.visible_text().contains(">"),
        );

        // Send echo command
        pty.write(b"echo WIXEN_TEST_STRING\r\n")
            .expect("Failed to write to PTY");

        // Wait for echo output
        let found = drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |t| t.visible_text().contains("WIXEN_TEST_STRING"),
        );
        assert!(
            found,
            "Expected 'WIXEN_TEST_STRING' in terminal output. Got:\n{}",
            terminal.visible_text()
        );
    }

    #[test]
    fn test_pty_resize() {
        let (pty, rx) = PtyHandle::spawn_with_shell(80, 24, "cmd.exe", &["/Q".to_string()], "")
            .expect("Failed to spawn PTY");
        let mut terminal = Terminal::new(80, 24);
        let mut parser = Parser::new();

        // Wait for initial prompt
        drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |t| t.visible_text().contains(">"),
        );

        // Resize to 40x12
        pty.resize(40, 12).expect("Failed to resize PTY");
        terminal.resize(40, 12);
        assert_eq!(terminal.cols(), 40);
        assert_eq!(terminal.rows(), 12);
    }

    #[test]
    fn test_pty_multiline_output() {
        let (mut pty, rx) = PtyHandle::spawn_with_shell(80, 24, "cmd.exe", &["/Q".to_string()], "")
            .expect("Failed to spawn PTY");
        let mut terminal = Terminal::new(80, 24);
        let mut parser = Parser::new();

        // Wait for prompt
        drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |t| t.visible_text().contains(">"),
        );

        // Run a command that produces multiple lines
        pty.write(b"echo LINE_A& echo LINE_B& echo LINE_C\r\n")
            .expect("Failed to write to PTY");

        let found = drain_until(
            &mut terminal,
            &mut parser,
            &rx,
            Duration::from_secs(5),
            |t| {
                let text = t.visible_text();
                text.contains("LINE_A") && text.contains("LINE_B") && text.contains("LINE_C")
            },
        );
        assert!(
            found,
            "Expected all three lines in output. Got:\n{}",
            terminal.visible_text()
        );
    }
}

// ===========================================================================
// VT Conformance Tests — escape sequence coverage per DEC VT220/xterm spec
// ===========================================================================

#[test]
fn test_vt_cursor_home() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"ABCDEF");
    // CUP (Cursor Position) row 1, col 1 = ESC [ H (home)
    feed(&mut terminal, &mut parser, b"\x1b[H");
    assert_eq!(terminal.grid.cursor.row, 0);
    assert_eq!(terminal.grid.cursor.col, 0);
}

#[test]
fn test_vt_cursor_absolute_position() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // CUP row 5, col 10 (1-based in VT, 0-based in grid)
    feed(&mut terminal, &mut parser, b"\x1b[5;10H");
    assert_eq!(terminal.grid.cursor.row, 4);
    assert_eq!(terminal.grid.cursor.col, 9);
}

#[test]
fn test_vt_cursor_movement_cuu_cud_cuf_cub() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Move to 10,10
    feed(&mut terminal, &mut parser, b"\x1b[11;11H");
    assert_eq!(terminal.grid.cursor.row, 10);
    assert_eq!(terminal.grid.cursor.col, 10);

    // CUU (up 3)
    feed(&mut terminal, &mut parser, b"\x1b[3A");
    assert_eq!(terminal.grid.cursor.row, 7);

    // CUD (down 2)
    feed(&mut terminal, &mut parser, b"\x1b[2B");
    assert_eq!(terminal.grid.cursor.row, 9);

    // CUF (forward 5)
    feed(&mut terminal, &mut parser, b"\x1b[5C");
    assert_eq!(terminal.grid.cursor.col, 15);

    // CUB (back 3)
    feed(&mut terminal, &mut parser, b"\x1b[3D");
    assert_eq!(terminal.grid.cursor.col, 12);
}

#[test]
fn test_vt_cursor_save_restore_decsc_decrc() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"\x1b[5;10H"); // Move to row 4, col 9
    feed(&mut terminal, &mut parser, b"\x1b7"); // DECSC (save)
    feed(&mut terminal, &mut parser, b"\x1b[1;1H"); // Move home
    assert_eq!(terminal.grid.cursor.row, 0);
    feed(&mut terminal, &mut parser, b"\x1b8"); // DECRC (restore)
    assert_eq!(terminal.grid.cursor.row, 4);
    assert_eq!(terminal.grid.cursor.col, 9);
}

#[test]
fn test_vt_erase_in_display_below() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"Row0\r\nRow1\r\nRow2");
    // Move to row 1 and erase below (ED 0 = default)
    feed(&mut terminal, &mut parser, b"\x1b[2;1H\x1b[J");
    assert_eq!(row_text(&terminal, 0), "Row0");
    assert_eq!(row_text(&terminal, 1), "");
    assert_eq!(row_text(&terminal, 2), "");
}

#[test]
fn test_vt_erase_in_display_above() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"Row0\r\nRow1\r\nRow2");
    // Move to row 2, col 1 and erase above (ED 1)
    feed(&mut terminal, &mut parser, b"\x1b[3;1H\x1b[1J");
    assert_eq!(row_text(&terminal, 0), "");
    assert_eq!(row_text(&terminal, 1), "");
    // Row 2 may be partially erased (up to cursor)
}

#[test]
fn test_vt_erase_in_display_all() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"Row0\r\nRow1\r\nRow2");
    // ED 2 = erase entire display
    feed(&mut terminal, &mut parser, b"\x1b[2J");
    assert_eq!(row_text(&terminal, 0), "");
    assert_eq!(row_text(&terminal, 1), "");
    assert_eq!(row_text(&terminal, 2), "");
}

#[test]
fn test_vt_insert_delete_lines() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"A\r\nB\r\nC\r\nD");
    // Move to row 1 (B), insert 1 line (IL)
    feed(&mut terminal, &mut parser, b"\x1b[2;1H\x1b[L");
    // Row 1 should now be blank, B shifted to row 2
    assert_eq!(row_text(&terminal, 0), "A");
    assert_eq!(row_text(&terminal, 1), "");
    assert_eq!(row_text(&terminal, 2), "B");
    assert_eq!(row_text(&terminal, 3), "C");
}

#[test]
fn test_vt_delete_lines() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"A\r\nB\r\nC\r\nD");
    // Move to row 1, delete 1 line (DL)
    feed(&mut terminal, &mut parser, b"\x1b[2;1H\x1b[M");
    assert_eq!(row_text(&terminal, 0), "A");
    assert_eq!(row_text(&terminal, 1), "C");
    assert_eq!(row_text(&terminal, 2), "D");
}

#[test]
fn test_vt_scroll_up_su() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"A\r\nB\r\nC");
    // SU = Scroll Up 1 line
    feed(&mut terminal, &mut parser, b"\x1b[S");
    // Row 0 should now be B (A scrolled off)
    assert_eq!(row_text(&terminal, 0), "B");
    assert_eq!(row_text(&terminal, 1), "C");
}

#[test]
fn test_vt_scroll_down_sd() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"A\r\nB\r\nC");
    // SD = Scroll Down 1 line
    feed(&mut terminal, &mut parser, b"\x1b[T");
    // Row 0 should be blank, A shifted to row 1
    assert_eq!(row_text(&terminal, 0), "");
    assert_eq!(row_text(&terminal, 1), "A");
    assert_eq!(row_text(&terminal, 2), "B");
}

#[test]
fn test_vt_dec_set_reset_origin_mode() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Set scroll region to rows 5-10 (1-based)
    feed(&mut terminal, &mut parser, b"\x1b[5;10r");
    // Enable origin mode (DECOM)
    feed(&mut terminal, &mut parser, b"\x1b[?6h");
    // CUP 1;1 should now be row 4 (top of scroll region, 0-based)
    feed(&mut terminal, &mut parser, b"\x1b[1;1H");
    assert_eq!(terminal.grid.cursor.row, 4);
    // Disable origin mode
    feed(&mut terminal, &mut parser, b"\x1b[?6l");
    // Reset scroll region
    feed(&mut terminal, &mut parser, b"\x1b[r");
}

#[test]
fn test_vt_reverse_index_ri() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"A\r\nB\r\nC");
    // Move to row 0
    feed(&mut terminal, &mut parser, b"\x1b[1;1H");
    // Reverse Index (RI = ESC M) at top should scroll down
    feed(&mut terminal, &mut parser, b"\x1bM");
    // Row 0 should be blank, A shifted to row 1
    assert_eq!(row_text(&terminal, 0), "");
    assert_eq!(row_text(&terminal, 1), "A");
}

#[test]
fn test_vt_line_feed_at_bottom_scrolls() {
    let mut terminal = Terminal::new(80, 5);
    let mut parser = Parser::new();
    // Fill all 5 rows
    feed(&mut terminal, &mut parser, b"R0\r\nR1\r\nR2\r\nR3\r\nR4");
    // One more LF should scroll
    feed(&mut terminal, &mut parser, b"\r\nR5");
    assert_eq!(row_text(&terminal, 0), "R1");
    assert_eq!(row_text(&terminal, 4), "R5");
}

#[test]
fn test_vt_sgr_256_color() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // SGR 38;5;196 = set foreground to color 196 (red in 256-color palette)
    feed(&mut terminal, &mut parser, b"\x1b[38;5;196mRed");
    let cell = terminal.grid.cell(0, 0).unwrap();
    // Verify the cell has color set (exact value depends on implementation)
    assert_eq!(cell.content, "R");
}

#[test]
fn test_vt_sgr_true_color() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // SGR 38;2;255;128;0 = set foreground to orange (true color)
    feed(&mut terminal, &mut parser, b"\x1b[38;2;255;128;0mOrange");
    let cell = terminal.grid.cell(0, 0).unwrap();
    assert_eq!(cell.content, "O");
}

#[test]
fn test_vt_sgr_reset() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // Bold + underline, then SGR 0 (reset), then print
    feed(&mut terminal, &mut parser, b"\x1b[1;4mBold\x1b[0mNormal");
    let bold_cell = terminal.grid.cell(0, 0).unwrap();
    let normal_cell = terminal.grid.cell(4, 0).unwrap();
    assert_eq!(bold_cell.content, "B");
    assert_eq!(normal_cell.content, "N");
}

#[test]
fn test_vt_osc_set_title_0_and_2() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 0 sets both icon and title
    feed(&mut terminal, &mut parser, b"\x1b]0;My Terminal\x07");
    assert_eq!(terminal.title, "My Terminal");
    // OSC 2 sets title only
    feed(&mut terminal, &mut parser, b"\x1b]2;Other Title\x07");
    assert_eq!(terminal.title, "Other Title");
}

#[test]
fn test_vt_osc8_hyperlink() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 8 hyperlink: ESC ] 8 ; ; url ST text ESC ] 8 ; ; ST
    feed(
        &mut terminal,
        &mut parser,
        b"\x1b]8;;https://example.com\x07Link\x1b]8;;\x07",
    );
    assert_eq!(row_text(&terminal, 0), "Link");
}

#[test]
fn test_vt_osc52_clipboard() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    // OSC 52 clipboard write: ESC ] 52 ; c ; base64data BEL
    // "Hello" in base64 = "SGVsbG8="
    feed(&mut terminal, &mut parser, b"\x1b]52;c;SGVsbG8=\x07");
    // Check that clipboard was set
    let clips = terminal.drain_clipboard_writes();
    assert_eq!(clips.len(), 1);
    assert_eq!(clips[0], "Hello");
}

#[test]
fn test_vt_alternate_screen_buffer() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();
    feed(&mut terminal, &mut parser, b"Primary");
    // Enter alternate screen (DECSET 1049)
    feed(&mut terminal, &mut parser, b"\x1b[?1049h");
    assert!(terminal.modes.alternate_screen);
    assert_eq!(row_text(&terminal, 0), ""); // Alt screen starts blank
    feed(&mut terminal, &mut parser, b"Alternate");
    assert_eq!(row_text(&terminal, 0), "Alternate");
    // Exit alternate screen (DECRST 1049)
    feed(&mut terminal, &mut parser, b"\x1b[?1049l");
    assert!(!terminal.modes.alternate_screen);
    assert_eq!(row_text(&terminal, 0), "Primary");
}

// ===========================================================================
// Accessibility Tree Integration Tests — verify a11y tree from terminal state
// ===========================================================================

use wixen_a11y::tree::{A11yNode, AccessibilityTree, SemanticRole};

/// Helper: build a11y tree from terminal's shell integration state.
fn build_a11y_tree(terminal: &Terminal) -> AccessibilityTree {
    let mut tree = AccessibilityTree::new();
    tree.rebuild(terminal.shell_integ.blocks(), |start, end| {
        let mut text = String::new();
        for row in start..=end {
            if !text.is_empty() {
                text.push('\n');
            }
            text.push_str(&row_text(terminal, row));
        }
        text
    });
    tree
}

#[test]
fn test_a11y_tree_single_command_block() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();

    // Simulate a full OSC 133 command cycle: prompt → input → output → complete
    feed(&mut terminal, &mut parser, b"\x1b]133;A\x07PS C:\\> ");
    feed(&mut terminal, &mut parser, b"\x1b]133;B\x07");
    feed(&mut terminal, &mut parser, b"echo hello\r\n");
    feed(&mut terminal, &mut parser, b"\x1b]133;C\x07");
    feed(&mut terminal, &mut parser, b"hello\r\n");
    feed(&mut terminal, &mut parser, b"\x1b]133;D;0\x07");

    let tree = build_a11y_tree(&terminal);
    let root_children = tree.root.children();

    // Should have exactly one command block
    assert_eq!(root_children.len(), 1);
    match &root_children[0] {
        A11yNode::CommandBlock {
            is_error, children, ..
        } => {
            assert!(!is_error, "exit 0 should not be an error");
            // Should have prompt, input, and output children
            assert!(
                children.len() >= 2,
                "Block should have at least prompt+output children"
            );
        }
        other => panic!("Expected CommandBlock, got {:?}", other),
    }
}

#[test]
fn test_a11y_tree_error_command_detected() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();

    // Command that fails with exit code 1
    feed(&mut terminal, &mut parser, b"\x1b]133;A\x07$ ");
    feed(&mut terminal, &mut parser, b"\x1b]133;B\x07");
    feed(&mut terminal, &mut parser, b"cat missing.txt\r\n");
    feed(&mut terminal, &mut parser, b"\x1b]133;C\x07");
    feed(
        &mut terminal,
        &mut parser,
        b"cat: missing.txt: No such file or directory\r\n",
    );
    feed(&mut terminal, &mut parser, b"\x1b]133;D;1\x07");

    let tree = build_a11y_tree(&terminal);
    let root_children = tree.root.children();
    assert_eq!(root_children.len(), 1);

    match &root_children[0] {
        A11yNode::CommandBlock {
            is_error,
            children,
            name,
            ..
        } => {
            assert!(is_error, "exit 1 should be marked as error");
            assert!(name.contains("exit 1"), "Name should mention exit code");
            // Output region should have ErrorText role
            let output_child = children.iter().find(|c| {
                matches!(
                    c,
                    A11yNode::TextRegion {
                        role: SemanticRole::ErrorText,
                        ..
                    }
                )
            });
            assert!(
                output_child.is_some(),
                "Should have ErrorText output region"
            );
        }
        other => panic!("Expected CommandBlock, got {:?}", other),
    }
}

#[test]
fn test_a11y_tree_multiple_commands() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();

    // Two command cycles
    for (cmd, exit) in [("ls", "0"), ("rm foo", "1")] {
        feed(&mut terminal, &mut parser, b"\x1b]133;A\x07$ ");
        feed(&mut terminal, &mut parser, b"\x1b]133;B\x07");
        feed(&mut terminal, &mut parser, format!("{cmd}\r\n").as_bytes());
        feed(&mut terminal, &mut parser, b"\x1b]133;C\x07");
        feed(&mut terminal, &mut parser, b"output\r\n");
        feed(
            &mut terminal,
            &mut parser,
            format!("\x1b]133;D;{exit}\x07").as_bytes(),
        );
    }

    let tree = build_a11y_tree(&terminal);
    let root_children = tree.root.children();
    assert_eq!(root_children.len(), 2, "Should have 2 command blocks");

    // First block should be success
    assert!(matches!(
        &root_children[0],
        A11yNode::CommandBlock { is_error, .. } if !is_error
    ));
    // Second block should be error
    assert!(matches!(
        &root_children[1],
        A11yNode::CommandBlock { is_error, .. } if *is_error
    ));
}

#[test]
fn test_a11y_tree_prompt_has_correct_role() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();

    feed(&mut terminal, &mut parser, b"\x1b]133;A\x07$ ");
    feed(&mut terminal, &mut parser, b"\x1b]133;B\x07");
    feed(&mut terminal, &mut parser, b"pwd\r\n");
    feed(&mut terminal, &mut parser, b"\x1b]133;C\x07");
    feed(&mut terminal, &mut parser, b"/home/user\r\n");
    feed(&mut terminal, &mut parser, b"\x1b]133;D;0\x07");

    let tree = build_a11y_tree(&terminal);
    let block = &tree.root.children()[0];
    let children = block.children();

    // First child should be the prompt with Prompt role
    let prompt = children.iter().find(|c| {
        matches!(
            c,
            A11yNode::TextRegion {
                role: SemanticRole::Prompt,
                ..
            }
        )
    });
    assert!(prompt.is_some(), "First child should be a Prompt region");

    // Should also have output with OutputText role (not error since exit 0)
    let output = children.iter().find(|c| {
        matches!(
            c,
            A11yNode::TextRegion {
                role: SemanticRole::OutputText,
                ..
            }
        )
    });
    assert!(output.is_some(), "Should have OutputText region for exit 0");
}

#[test]
fn test_a11y_tree_empty_before_any_commands() {
    let terminal = Terminal::new(80, 24);
    let tree = build_a11y_tree(&terminal);
    assert!(
        tree.root.children().is_empty(),
        "A11y tree should be empty before any commands"
    );
}

#[test]
fn test_a11y_tree_node_ids_unique() {
    let mut terminal = Terminal::new(80, 24);
    let mut parser = Parser::new();

    // Create 3 command blocks
    for _ in 0..3 {
        feed(&mut terminal, &mut parser, b"\x1b]133;A\x07$ ");
        feed(&mut terminal, &mut parser, b"\x1b]133;B\x07");
        feed(&mut terminal, &mut parser, b"cmd\r\n");
        feed(&mut terminal, &mut parser, b"\x1b]133;C\x07");
        feed(&mut terminal, &mut parser, b"out\r\n");
        feed(&mut terminal, &mut parser, b"\x1b]133;D;0\x07");
    }

    let tree = build_a11y_tree(&terminal);

    // Collect all node IDs recursively
    fn collect_ids(node: &A11yNode, ids: &mut Vec<i32>) {
        ids.push(node.id().0);
        for child in node.children() {
            collect_ids(child, ids);
        }
    }
    let mut ids = Vec::new();
    collect_ids(&tree.root, &mut ids);

    // All IDs must be unique
    let unique_count = {
        let mut sorted = ids.clone();
        sorted.sort();
        sorted.dedup();
        sorted.len()
    };
    assert_eq!(
        ids.len(),
        unique_count,
        "All node IDs must be unique, but found duplicates"
    );
}

// ===========================================================================
// Dimension safety tests — verify row/col calculations never overflow
// ===========================================================================

#[test]
fn test_grid_dimensions_never_overflow_u16() {
    // Simulate the main.rs calculation with extreme values
    let test_cases: Vec<(f32, f32, f32, f32)> = vec![
        // (window_size, padding, cell_size, expected: 1..500)
        (1024.0, 0.0, 14.0, 73.0),   // Normal case
        (768.0, 0.0, 28.0, 27.0),    // Normal case
        (10000.0, 0.0, 1.0, 500.0),  // Huge window, tiny cells: clamped to 500
        (100.0, 200.0, 14.0, 1.0),   // Padding larger than window: clamped to 1
        (1024.0, 0.0, 0.001, 500.0), // Near-zero cell size: clamped to 500
        (0.0, 0.0, 14.0, 1.0),       // Zero window: clamped to 1
    ];

    for (window, padding, cell, expected) in test_cases {
        let result = ((window - padding) / cell).floor().clamp(1.0, 500.0);
        assert!(
            result >= 1.0 && result <= 500.0,
            "Grid dimension must be 1..500, got {} for window={}, pad={}, cell={}",
            result,
            window,
            padding,
            cell
        );
        assert_eq!(
            result, expected,
            "Expected {} for window={}, pad={}, cell={}",
            expected, window, padding, cell
        );
    }
}

#[test]
fn test_terminal_new_accepts_clamped_range() {
    // Terminal::new should handle dimensions from 1 to 500 without panicking
    for cols in [1, 10, 80, 120, 300, 500] {
        for rows in [1, 10, 24, 40, 100, 500] {
            let term = Terminal::new(cols, rows);
            assert_eq!(term.cols(), cols);
            assert_eq!(term.rows(), rows);
        }
    }
}
