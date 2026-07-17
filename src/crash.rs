//! Crash logging support for the panic hook.
//!
//! The panic hook appends a line to a crash log so testers can report what
//! happened when stderr is not visible (the release build has no console).

use std::path::PathBuf;

/// File name for the appended crash log.
const CRASH_LOG_FILE: &str = "wixen-panic.log";

/// Directory that receives the crash log.
///
/// Prefers `%LOCALAPPDATA%\Wixen Terminal`. The install directory (for example
/// `C:\Program Files\Wixen Terminal`) is not writable by a normal user, so a log
/// written next to the executable would be silently lost for installed users.
/// Falls back to the system temp directory when `LOCALAPPDATA` is unset.
pub fn crash_log_dir() -> PathBuf {
    match std::env::var_os("LOCALAPPDATA") {
        Some(local) => {
            let mut dir = PathBuf::from(local);
            dir.push("Wixen Terminal");
            dir
        }
        None => std::env::temp_dir(),
    }
}

/// Full path to the crash log file.
pub fn crash_log_path() -> PathBuf {
    crash_log_dir().join(CRASH_LOG_FILE)
}

/// Format one crash-log entry.
///
/// Kept pure so the exact wording is unit-tested without triggering a panic.
/// `location` is `None` when the panic carried no source location.
pub fn format_panic_entry(
    timestamp: &str,
    version: &str,
    location: Option<&str>,
    payload: &str,
) -> String {
    let site = location.unwrap_or("unknown location");
    format!("[{timestamp}] wixen {version} panicked at {site}: {payload}\n")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crash_log_path_ends_with_log_file() {
        assert!(crash_log_path().ends_with(CRASH_LOG_FILE));
    }

    #[test]
    fn crash_log_path_is_absolute() {
        // Both the LOCALAPPDATA base and temp_dir fallback are absolute.
        assert!(crash_log_path().is_absolute());
    }

    #[test]
    fn format_entry_contains_every_field() {
        let entry = format_panic_entry(
            "2026-07-17T10:00:00Z",
            "1.0.0",
            Some("src/x.rs:9:1"),
            "boom",
        );
        assert!(entry.contains("2026-07-17T10:00:00Z"));
        assert!(entry.contains("1.0.0"));
        assert!(entry.contains("src/x.rs:9:1"));
        assert!(entry.contains("boom"));
        assert!(entry.ends_with('\n'));
    }

    #[test]
    fn format_entry_handles_missing_location() {
        let entry = format_panic_entry("t", "1.0.0", None, "boom");
        assert!(entry.contains("unknown location"));
    }
}
