//! Adoption of an existing ConPTY connection handed off by the OS.
//!
//! When Windows delegates a console session to Wixen (default-terminal
//! handoff), the terminal receives already-open pipe handles instead of
//! creating its own ConPTY. This module adopts those pipes into a
//! [`crate::PtyHandle`] so the rest of the app sees the same API either way.

use crate::PtyError;

/// Raw OS `HANDLE` value as delivered by the console handoff (matches
/// `wixen_ui::handoff::RawHandleValue`).
pub type RawHandleValue = isize;

/// The three validated pipe handles of a handed-off console connection.
///
/// Construct via [`HandoffPipes::new`], which enforces that every handle is
/// non-null, not `INVALID_HANDLE_VALUE`, and distinct from the others — the
/// invariants the adoption path relies on to take ownership without ever
/// double-closing a handle.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HandoffPipes {
    input: RawHandleValue,
    output: RawHandleValue,
    signal: RawHandleValue,
}

impl HandoffPipes {
    /// Validate and bundle the raw pipe handles from a console handoff.
    pub fn new(
        input: RawHandleValue,
        output: RawHandleValue,
        signal: RawHandleValue,
    ) -> Result<Self, PtyError> {
        const INVALID_HANDLE_VALUE: RawHandleValue = -1;

        for (role, value) in [("input", input), ("output", output), ("signal", signal)] {
            match value {
                0 => return Err(PtyError::HandoffHandleNull { role }),
                INVALID_HANDLE_VALUE => return Err(PtyError::HandoffHandleInvalid { role }),
                _ => {}
            }
        }
        if input == output || output == signal || input == signal {
            return Err(PtyError::HandoffHandlesNotDistinct {
                input,
                output,
                signal,
            });
        }
        Ok(Self {
            input,
            output,
            signal,
        })
    }

    /// Pipe the terminal reads client output from (VT output pipe).
    pub fn output(&self) -> RawHandleValue {
        self.output
    }

    /// Pipe the terminal writes client input to (VT input pipe).
    pub fn input(&self) -> RawHandleValue {
        self.input
    }

    /// Pipe resize/close signals are written to.
    pub fn signal(&self) -> RawHandleValue {
        self.signal
    }
}

/// ConPTY signal-pipe opcode for a window resize.
const PTY_SIGNAL_RESIZE_WINDOW: u16 = 8;

/// Encode a ConPTY resize packet for the signal pipe:
/// `[PTY_SIGNAL_RESIZE_WINDOW u16 LE][cols u16 LE][rows u16 LE]`.
pub fn encode_resize_packet(cols: u16, rows: u16) -> [u8; 6] {
    let mut packet = [0u8; 6];
    packet[0..2].copy_from_slice(&PTY_SIGNAL_RESIZE_WINDOW.to_le_bytes());
    packet[2..4].copy_from_slice(&cols.to_le_bytes());
    packet[4..6].copy_from_slice(&rows.to_le_bytes());
    packet
}

/// Write a resize signal packet to the ConPTY signal pipe (or any sink).
pub fn send_resize<W: std::io::Write>(mut signal: W, cols: u16, rows: u16) -> Result<(), PtyError> {
    signal.write_all(&encode_resize_packet(cols, rows))?;
    signal.flush()?;
    Ok(())
}

impl crate::PtyHandle {
    /// Adopt an existing ConPTY connection from a console handoff.
    ///
    /// Takes ownership of the three pipe handles in `pipes`: they are closed
    /// when the returned handle (and its reader thread) shut down, so the
    /// caller must not close them itself. The connection reuses the same
    /// reader-thread/[`crate::PtyEvent`] plumbing as the spawn path:
    /// [`crate::PtyHandle::write`] sends to the input pipe,
    /// [`crate::PtyHandle::resize`] emits ConPTY signal packets on the signal
    /// pipe, and output arrives on the returned receiver. The initial window
    /// size is announced immediately over the signal pipe.
    ///
    /// Dropping the handle closes the input and signal pipes, which tells the
    /// console host to shut the session down; the host then closes the output
    /// pipe, which ends the reader thread with a final
    /// [`crate::PtyEvent::Exited`].
    #[cfg(windows)]
    pub fn from_handoff(
        pipes: HandoffPipes,
        initial_cols: u16,
        initial_rows: u16,
    ) -> Result<(Self, crossbeam_channel::Receiver<crate::PtyEvent>), PtyError> {
        use std::fs::File;
        use std::os::windows::io::FromRawHandle;

        // SAFETY: `HandoffPipes::new` guarantees the handles are non-null,
        // not INVALID_HANDLE_VALUE, and pairwise distinct, so each `File`
        // takes sole ownership of one live handle and no handle is ever
        // closed twice.
        let (input, output, signal) = unsafe {
            (
                File::from_raw_handle(pipes.input as _),
                File::from_raw_handle(pipes.output as _),
                File::from_raw_handle(pipes.signal as _),
            )
        };

        let rx = crate::spawn_reader_thread(output)?;
        send_resize(&signal, initial_cols, initial_rows)?;
        tracing::info!(
            cols = initial_cols,
            rows = initial_rows,
            "Adopted handed-off console connection"
        );

        Ok((
            Self {
                writer: Box::new(input),
                backend: crate::Backend::Adopted { signal },
            },
            rx,
        ))
    }

    /// Non-Windows stub: console handoff adoption requires ConPTY.
    #[cfg(not(windows))]
    pub fn from_handoff(
        _pipes: HandoffPipes,
        _initial_cols: u16,
        _initial_rows: u16,
    ) -> Result<(Self, crossbeam_channel::Receiver<crate::PtyEvent>), PtyError> {
        Err(PtyError::HandoffUnsupported)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_resize_packet_80x24_exact_bytes() {
        // [PTY_SIGNAL_RESIZE_WINDOW = 8u16 LE][cols u16 LE][rows u16 LE]
        assert_eq!(encode_resize_packet(80, 24), [8, 0, 80, 0, 24, 0]);
    }

    #[test]
    fn encode_resize_packet_multibyte_dimensions() {
        // 300 = 0x012C, 100 = 0x0064 — little-endian byte order matters.
        assert_eq!(encode_resize_packet(300, 100), [8, 0, 0x2C, 0x01, 100, 0]);
    }

    #[test]
    fn encode_resize_packet_extreme_dimensions() {
        assert_eq!(encode_resize_packet(0, 0), [8, 0, 0, 0, 0, 0]);
        assert_eq!(
            encode_resize_packet(u16::MAX, u16::MAX),
            [8, 0, 0xFF, 0xFF, 0xFF, 0xFF]
        );
    }

    #[test]
    fn handoff_pipes_accepts_distinct_valid_handles() {
        let pipes = HandoffPipes::new(0x100, 0x104, 0x108).expect("valid handles");
        assert_eq!(pipes.input(), 0x100);
        assert_eq!(pipes.output(), 0x104);
        assert_eq!(pipes.signal(), 0x108);
    }

    #[test]
    fn handoff_pipes_rejects_null_handles() {
        for (input, output, signal) in [(0, 0x104, 0x108), (0x100, 0, 0x108), (0x100, 0x104, 0)] {
            let err = HandoffPipes::new(input, output, signal).expect_err("null must be rejected");
            assert!(
                err.to_string().contains("null"),
                "error should name the problem: {err}"
            );
        }
    }

    #[test]
    fn handoff_pipes_rejects_invalid_handle_value() {
        // INVALID_HANDLE_VALUE is -1 on Windows.
        let err = HandoffPipes::new(-1, 0x104, 0x108).expect_err("-1 must be rejected");
        assert!(
            err.to_string().contains("invalid"),
            "error should name the problem: {err}"
        );
    }

    #[test]
    fn handoff_pipes_rejects_duplicate_handles() {
        // Adoption takes ownership of each handle; duplicates would double-close.
        let err = HandoffPipes::new(0x100, 0x100, 0x108).expect_err("duplicates must be rejected");
        assert!(
            err.to_string().contains("distinct"),
            "error should name the problem: {err}"
        );
        assert!(HandoffPipes::new(0x100, 0x104, 0x104).is_err());
        assert!(HandoffPipes::new(0x100, 0x104, 0x100).is_err());
    }

    #[test]
    fn send_resize_writes_exact_packet_to_sink() {
        let mut sink = Vec::new();
        send_resize(&mut sink, 120, 40).expect("write to Vec cannot fail");
        assert_eq!(sink, encode_resize_packet(120, 40));
    }

    /// Adopt three real OS handles (NUL device opens). Reads from NUL return
    /// EOF immediately, so the shared reader thread must report `Exited`;
    /// writes and resizes over the adopted pipes must succeed.
    #[cfg(windows)]
    #[test]
    fn from_handoff_adopts_real_handles() {
        use crate::{PtyEvent, PtyHandle};
        use std::os::windows::io::IntoRawHandle;
        use std::time::Duration;

        let output = std::fs::File::open("NUL").expect("open NUL for read");
        let nul_writer = || {
            std::fs::OpenOptions::new()
                .write(true)
                .open("NUL")
                .expect("open NUL for write")
        };
        let pipes = HandoffPipes::new(
            nul_writer().into_raw_handle() as RawHandleValue,
            output.into_raw_handle() as RawHandleValue,
            nul_writer().into_raw_handle() as RawHandleValue,
        )
        .expect("NUL handles are valid and distinct");

        let (mut handle, rx) = PtyHandle::from_handoff(pipes, 80, 24).expect("adoption succeeds");

        assert!(
            matches!(
                rx.recv_timeout(Duration::from_secs(5)),
                Ok(PtyEvent::Exited(_))
            ),
            "EOF on the adopted output pipe must surface as PtyEvent::Exited"
        );
        handle.write(b"hello").expect("write to adopted input pipe");
        handle
            .resize(120, 40)
            .expect("resize over adopted signal pipe");
    }
}
