//! F1 help — locate and open the bundled user guide.
//!
//! The installed layout ships `docs/user-guide.html` next to the executable.
//! During development the executable lives in `target/release/` (or
//! `target/debug/`), two levels below the repo root, so the repo's `docs/`
//! directory is checked as a fallback.

use std::path::{Path, PathBuf};

/// Path to the bundled user guide relative to the executable directory.
pub fn help_file_path(exe_dir: &Path) -> PathBuf {
    exe_dir.join("docs").join("user-guide.html")
}

/// Find the user guide on disk, checking the installed layout first.
///
/// Returns the first existing candidate, or `None` if the guide is not found.
pub fn resolve_help_file(exe_dir: &Path) -> Option<PathBuf> {
    let candidates = [
        // Installed layout: docs/ next to the executable.
        help_file_path(exe_dir),
        // Dev layout: exe in target/{debug,release}, docs/ at repo root.
        exe_dir
            .join("..")
            .join("..")
            .join("docs")
            .join("user-guide.html"),
    ];
    candidates.into_iter().find(|p| p.exists())
}

/// Open the user guide in the default browser via `ShellExecuteW`.
#[cfg(windows)]
pub fn open_help(path: &Path) {
    use std::os::windows::ffi::OsStrExt;
    use windows::Win32::UI::Shell::ShellExecuteW;
    use windows::core::PCWSTR;

    let wide: Vec<u16> = path
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let verb: Vec<u16> = "open".encode_utf16().chain(std::iter::once(0)).collect();
    unsafe {
        ShellExecuteW(
            None,
            PCWSTR(verb.as_ptr()),
            PCWSTR(wide.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL,
        );
    }
    tracing::info!(path = %path.display(), "Opened help via ShellExecuteW");
}

/// Non-Windows stub — opening the help file requires the Windows shell.
#[cfg(not(windows))]
pub fn open_help(_path: &Path) {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    /// A unique temp directory, removed (best-effort) on drop.
    ///
    /// Uniqueness combines the process id, a per-process counter, and a
    /// timestamp, so parallel test threads and concurrent test processes
    /// never share a directory. Cleanup runs even when an assertion fails
    /// and is best-effort: on Windows a virus scanner or indexer can
    /// briefly hold a handle, and a failed remove must not fail the test.
    struct TestDir(PathBuf);

    impl TestDir {
        fn new(tag: &str) -> Self {
            static COUNTER: AtomicU64 = AtomicU64::new(0);
            let nanos = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .expect("system clock before UNIX epoch")
                .as_nanos();
            let unique = format!(
                "wixen-help-test-{tag}-{}-{}-{nanos}",
                std::process::id(),
                COUNTER.fetch_add(1, Ordering::Relaxed),
            );
            let dir = std::env::temp_dir().join(unique);
            std::fs::create_dir_all(&dir).expect("create test temp dir");
            Self(dir)
        }

        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for TestDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn resolve_finds_installed_layout_file() {
        let root = TestDir::new("installed");
        let exe_dir = root.path().join("app");
        let docs = exe_dir.join("docs");
        std::fs::create_dir_all(&docs).unwrap();
        let guide = docs.join("user-guide.html");
        std::fs::write(&guide, "<html></html>").unwrap();

        assert_eq!(resolve_help_file(&exe_dir), Some(guide));
    }

    #[test]
    fn resolve_finds_dev_layout_file() {
        // Dev layout: repo/target/release/wixen.exe with docs/ at repo root.
        let repo = TestDir::new("dev");
        let exe_dir = repo.path().join("target").join("release");
        std::fs::create_dir_all(&exe_dir).unwrap();
        let docs = repo.path().join("docs");
        std::fs::create_dir_all(&docs).unwrap();
        std::fs::write(docs.join("user-guide.html"), "<html></html>").unwrap();

        let resolved = resolve_help_file(&exe_dir).expect("dev layout guide should be found");
        // The resolved path must point at the repo-root copy.
        assert_eq!(
            resolved.canonicalize().unwrap(),
            docs.join("user-guide.html").canonicalize().unwrap()
        );
    }

    #[test]
    fn resolve_returns_none_when_no_guide_exists() {
        let root = TestDir::new("missing");
        let exe_dir = root.path().join("target").join("release");
        std::fs::create_dir_all(&exe_dir).unwrap();

        assert_eq!(resolve_help_file(&exe_dir), None);
    }

    #[test]
    fn help_file_path_is_docs_user_guide_under_exe_dir() {
        let exe_dir = std::path::Path::new(r"C:\Program Files\Wixen");
        let path = help_file_path(exe_dir);
        assert_eq!(
            path,
            std::path::Path::new(r"C:\Program Files\Wixen")
                .join("docs")
                .join("user-guide.html")
        );
    }
}
