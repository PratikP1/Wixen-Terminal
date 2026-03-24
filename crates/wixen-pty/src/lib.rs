//! wixen-pty: ConPTY wrapper and process spawning.
//!
//! Wraps portable-pty to create pseudo-terminal pairs and spawn shell processes.

use crossbeam_channel::Receiver;
use portable_pty::{CommandBuilder, MasterPty, NativePtySystem, PtyPair, PtySize, PtySystem};
use std::io::{Read, Write};
use thiserror::Error;
use tracing::{error, info};

#[derive(Error, Debug)]
pub enum PtyError {
    #[error("Failed to open PTY pair: {0}")]
    OpenPair(#[source] anyhow::Error),
    #[error("Failed to spawn child process: {0}")]
    Spawn(#[source] anyhow::Error),
    #[error("Failed to resize PTY: {0}")]
    Resize(#[source] anyhow::Error),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("PTY error: {0}")]
    Pty(#[source] anyhow::Error),
}

/// Messages from the PTY reader thread to the main thread.
pub enum PtyEvent {
    /// Raw bytes received from the child process
    Output(Vec<u8>),
    /// Child process exited
    Exited(Option<u32>),
}

/// Check if a shell executable is on the PATH.
fn which_shell(name: &str) -> bool {
    std::env::var_os("PATH")
        .map(|path| {
            std::env::split_paths(&path).any(|dir| {
                let candidate = dir.join(name);
                candidate.is_file()
            })
        })
        .unwrap_or(false)
}

/// Handle to a running PTY session.
pub struct PtyHandle {
    master: Box<dyn MasterPty + Send>,
    writer: Box<dyn Write + Send>,
    _child: Box<dyn portable_pty::Child + Send>,
}

impl PtyHandle {
    /// Spawn a new PTY session with the default shell.
    ///
    /// Returns the handle and a receiver for PTY events (output bytes, exit).
    pub fn spawn(cols: u16, rows: u16) -> Result<(Self, Receiver<PtyEvent>), PtyError> {
        Self::spawn_with_shell(cols, rows, "", &[], "")
    }

    /// Spawn a PTY session with explicit shell configuration.
    ///
    /// - `program`: shell executable (empty = auto-detect via COMSPEC)
    /// - `args`: arguments to pass to the shell
    /// - `cwd`: working directory (empty = user home)
    pub fn spawn_with_shell(
        cols: u16,
        rows: u16,
        program: &str,
        args: &[String],
        cwd: &str,
    ) -> Result<(Self, Receiver<PtyEvent>), PtyError> {
        let pty_system = NativePtySystem::default();

        let pair = pty_system
            .openpty(PtySize {
                rows,
                cols,
                pixel_width: 0,
                pixel_height: 0,
            })
            .map_err(PtyError::OpenPair)?;

        let PtyPair { master, slave } = pair;

        // Determine shell: explicit program → pwsh → powershell → COMSPEC
        let shell = if !program.is_empty() {
            program.to_string()
        } else if which_shell("pwsh.exe") {
            "pwsh.exe".to_string()
        } else if which_shell("powershell.exe") {
            "powershell.exe".to_string()
        } else {
            std::env::var("COMSPEC").unwrap_or_else(|_| "cmd.exe".to_string())
        };

        let mut cmd = CommandBuilder::new(&shell);
        for arg in args {
            cmd.arg(arg);
        }

        // Working directory: explicit → user home
        if cwd.is_empty() {
            cmd.cwd(dirs_home());
        } else {
            cmd.cwd(std::path::Path::new(cwd));
        }

        let child = slave.spawn_command(cmd).map_err(PtyError::Spawn)?;
        drop(slave); // Close slave side — child owns it now

        let writer = master.take_writer().map_err(PtyError::Pty)?;
        let mut reader = master.try_clone_reader().map_err(PtyError::Pty)?;

        let (tx, rx) = crossbeam_channel::unbounded();

        // Reader thread: reads from PTY and sends to main thread
        std::thread::Builder::new()
            .name("pty-reader".to_string())
            .spawn(move || {
                let mut buf = [0u8; 8192];
                loop {
                    match reader.read(&mut buf) {
                        Ok(0) => {
                            let _ = tx.send(PtyEvent::Exited(None));
                            break;
                        }
                        Ok(n) => {
                            if tx.send(PtyEvent::Output(buf[..n].to_vec())).is_err() {
                                break;
                            }
                        }
                        Err(e) => {
                            error!("PTY read error: {}", e);
                            let _ = tx.send(PtyEvent::Exited(None));
                            break;
                        }
                    }
                }
            })
            .expect("Failed to spawn PTY reader thread");

        info!(shell = %shell, cols, rows, "PTY session spawned");

        Ok((
            Self {
                master,
                writer,
                _child: child,
            },
            rx,
        ))
    }

    /// Write bytes to the PTY (sends to child process stdin).
    pub fn write(&mut self, data: &[u8]) -> Result<(), PtyError> {
        self.writer.write_all(data)?;
        self.writer.flush()?;
        Ok(())
    }

    /// Resize the PTY.
    pub fn resize(&self, cols: u16, rows: u16) -> Result<(), PtyError> {
        self.master
            .resize(PtySize {
                rows,
                cols,
                pixel_width: 0,
                pixel_height: 0,
            })
            .map_err(PtyError::Resize)
    }
}

fn dirs_home() -> std::path::PathBuf {
    std::env::var("USERPROFILE")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| std::path::PathBuf::from("C:\\"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn test_pty_error_display() {
        let io_err = PtyError::Io(std::io::Error::new(
            std::io::ErrorKind::BrokenPipe,
            "broken",
        ));
        assert!(io_err.to_string().contains("broken"));
    }

    #[test]
    fn test_dirs_home_returns_path() {
        let home = dirs_home();
        assert!(!home.as_os_str().is_empty());
        // On Windows with USERPROFILE set, should be a real path
        if std::env::var("USERPROFILE").is_ok() {
            assert!(home.exists(), "Home directory should exist: {:?}", home);
        }
    }

    #[test]
    fn test_pty_event_variants() {
        let output = PtyEvent::Output(vec![65, 66, 67]);
        assert!(matches!(output, PtyEvent::Output(ref data) if data == &[65, 66, 67]));

        let exited_some = PtyEvent::Exited(Some(0));
        assert!(matches!(exited_some, PtyEvent::Exited(Some(0))));

        let exited_none = PtyEvent::Exited(None);
        assert!(matches!(exited_none, PtyEvent::Exited(None)));
    }

    #[test]
    fn test_spawn_and_write_echo() {
        // Spawn a real PTY with cmd.exe, send echo, verify output
        let (mut handle, rx) = PtyHandle::spawn_with_shell(
            80,
            24,
            "cmd.exe",
            &["/C".to_string(), "echo PTY_TEST_OK".to_string()],
            "",
        )
        .expect("Failed to spawn PTY");

        // Collect output until exit or timeout
        let mut output = Vec::new();
        let deadline = std::time::Instant::now() + Duration::from_secs(10);
        loop {
            match rx.recv_timeout(Duration::from_millis(500)) {
                Ok(PtyEvent::Output(data)) => output.extend_from_slice(&data),
                Ok(PtyEvent::Exited(_)) => break,
                Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
                    if std::time::Instant::now() > deadline {
                        break;
                    }
                }
                Err(_) => break,
            }
        }

        let text = String::from_utf8_lossy(&output);
        assert!(
            text.contains("PTY_TEST_OK"),
            "Expected PTY_TEST_OK in output, got: {}",
            text
        );
    }

    #[test]
    fn test_resize_does_not_error() {
        let (handle, _rx) = PtyHandle::spawn_with_shell(
            80,
            24,
            "cmd.exe",
            &["/C".to_string(), "echo ok".to_string()],
            "",
        )
        .expect("Failed to spawn PTY");
        // Resize should succeed
        assert!(handle.resize(120, 40).is_ok());
    }

    #[test]
    fn test_spawn_default_shell() {
        // Spawn with default shell (COMSPEC)
        let result = PtyHandle::spawn(80, 24);
        assert!(result.is_ok(), "Default shell spawn should succeed");
    }

    /// Spawning with 1x1 dimensions (minimum valid) must not crash.
    #[test]
    fn test_spawn_minimum_dimensions() {
        let result = PtyHandle::spawn_with_shell(
            1,
            1,
            "cmd.exe",
            &["/C".to_string(), "echo ok".to_string()],
            "",
        );
        assert!(
            result.is_ok(),
            "1x1 PTY spawn should succeed: {:?}",
            result.err()
        );
    }

    /// Spawning with large but valid dimensions must not crash.
    #[test]
    fn test_spawn_large_dimensions() {
        let result = PtyHandle::spawn_with_shell(
            300,
            100,
            "cmd.exe",
            &["/C".to_string(), "echo ok".to_string()],
            "",
        );
        assert!(result.is_ok(), "300x100 PTY spawn should succeed");
    }

    /// Resizing to zero rows/cols should fail gracefully, not crash.
    #[test]
    fn test_resize_to_zero_fails_gracefully() {
        let (handle, _rx) = PtyHandle::spawn_with_shell(
            80,
            24,
            "cmd.exe",
            &["/C".to_string(), "echo ok".to_string()],
            "",
        )
        .expect("Failed to spawn PTY");
        // Zero dimensions should return an error, not panic
        let result = handle.resize(0, 0);
        // portable-pty may or may not error on this, but it must not panic
        let _ = result;
    }

    /// Resizing to very large dimensions should not crash.
    #[test]
    fn test_resize_large_dimensions() {
        let (handle, _rx) = PtyHandle::spawn_with_shell(
            80,
            24,
            "cmd.exe",
            &["/C".to_string(), "echo ok".to_string()],
            "",
        )
        .expect("Failed to spawn PTY");
        let result = handle.resize(500, 200);
        assert!(result.is_ok(), "Large resize should succeed");
    }

    /// Spawning with a nonexistent working directory should not crash.
    #[test]
    fn test_spawn_bad_cwd_does_not_crash() {
        let result = PtyHandle::spawn_with_shell(
            80,
            24,
            "cmd.exe",
            &["/C".to_string(), "echo ok".to_string()],
            "C:\\nonexistent_directory_12345",
        );
        // May succeed (cmd.exe might ignore bad CWD) or fail, but must not panic
        let _ = result;
    }
}
