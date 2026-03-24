//! Heuristic prompt detection for shells without OSC 133 support.
//!
//! When the shell doesn't emit semantic prompt markers, we fall back to
//! pattern matching on terminal row text to guess prompt boundaries.
//! This enables basic command-block structure for cmd.exe (without clink),
//! PowerShell (without prompt integration), and other raw shells.

use regex::Regex;
use std::sync::LazyLock;

/// A detected prompt location.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DetectedPrompt {
    /// The row index where the prompt was detected.
    pub row: usize,
    /// The byte offset where the prompt text ends (where user input begins).
    pub input_start: usize,
    /// Which pattern matched.
    pub kind: PromptKind,
}

/// The type of prompt pattern that matched.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PromptKind {
    /// Windows cmd.exe: `C:\Users\foo>`
    CmdExe,
    /// PowerShell: `PS C:\Users\foo>`
    PowerShell,
    /// Unix-style: `user@host:path$ ` or `user@host:path# `
    UnixBash,
    /// Minimal Unix: `$ ` or `# ` at line start
    UnixMinimal,
    /// Python REPL: `>>> ` or `... `
    PythonRepl,
}

// Compiled regex patterns (one-time initialization via LazyLock).
static CMD_EXE_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[A-Za-z]:\\[^>]*>").unwrap());

static POWERSHELL_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^PS [A-Za-z]:\\[^>]*>").unwrap());

static UNIX_BASH_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^[a-zA-Z0-9._-]+@[a-zA-Z0-9._-]+:[^\$#]*[\$#]\s").unwrap());

static UNIX_MINIMAL_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[\$#%>]\s").unwrap());

static PYTHON_REPL_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(>>>|\.\.\.)\s").unwrap());

/// Check a single line of terminal text for a prompt pattern.
///
/// Returns `Some(DetectedPrompt)` if the line looks like a prompt, with
/// `input_start` pointing to the byte offset where user input begins.
pub fn detect_prompt(row: usize, text: &str) -> Option<DetectedPrompt> {
    if text.trim().is_empty() {
        return None;
    }

    // Try patterns in order of specificity (most specific first).
    // Match against the original text (not trimmed) so trailing-space patterns work.
    if let Some(m) = POWERSHELL_RE.find(text) {
        return Some(DetectedPrompt {
            row,
            input_start: m.end(),
            kind: PromptKind::PowerShell,
        });
    }

    if let Some(m) = CMD_EXE_RE.find(text) {
        return Some(DetectedPrompt {
            row,
            input_start: m.end(),
            kind: PromptKind::CmdExe,
        });
    }

    if let Some(m) = UNIX_BASH_RE.find(text) {
        return Some(DetectedPrompt {
            row,
            input_start: m.end(),
            kind: PromptKind::UnixBash,
        });
    }

    if let Some(m) = PYTHON_REPL_RE.find(text) {
        return Some(DetectedPrompt {
            row,
            input_start: m.end(),
            kind: PromptKind::PythonRepl,
        });
    }

    if let Some(m) = UNIX_MINIMAL_RE.find(text) {
        return Some(DetectedPrompt {
            row,
            input_start: m.end(),
            kind: PromptKind::UnixMinimal,
        });
    }

    None
}

/// Scan a range of rows for prompts.
///
/// `get_text` is a closure that returns the text of a given row index.
/// Returns all detected prompts in order.
pub fn scan_for_prompts<F>(start_row: usize, end_row: usize, get_text: F) -> Vec<DetectedPrompt>
where
    F: Fn(usize) -> String,
{
    let mut prompts = Vec::new();
    for row in start_row..=end_row {
        let text = get_text(row);
        if let Some(prompt) = detect_prompt(row, &text) {
            prompts.push(prompt);
        }
    }
    prompts
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cmd_exe_prompt() {
        let p = detect_prompt(0, r"C:\Users\Pratik>echo hello").unwrap();
        assert_eq!(p.kind, PromptKind::CmdExe);
        assert_eq!(p.row, 0);
        // input_start should be right after '>'
        assert_eq!(
            &r"C:\Users\Pratik>echo hello"[p.input_start..],
            "echo hello"
        );
    }

    #[test]
    fn test_powershell_prompt() {
        let p = detect_prompt(5, r"PS C:\Projects\Wixen> Get-Process").unwrap();
        assert_eq!(p.kind, PromptKind::PowerShell);
        assert_eq!(
            &r"PS C:\Projects\Wixen> Get-Process"[p.input_start..],
            " Get-Process"
        );
    }

    #[test]
    fn test_unix_bash_prompt() {
        let p = detect_prompt(10, "user@hostname:~/projects$ ls -la").unwrap();
        assert_eq!(p.kind, PromptKind::UnixBash);
        assert_eq!(
            &"user@hostname:~/projects$ ls -la"[p.input_start..],
            "ls -la"
        );
    }

    #[test]
    fn test_unix_root_prompt() {
        let p = detect_prompt(0, "root@server:/etc# cat passwd").unwrap();
        assert_eq!(p.kind, PromptKind::UnixBash);
    }

    #[test]
    fn test_unix_minimal_dollar() {
        let p = detect_prompt(0, "$ whoami").unwrap();
        assert_eq!(p.kind, PromptKind::UnixMinimal);
        assert_eq!(&"$ whoami"[p.input_start..], "whoami");
    }

    #[test]
    fn test_unix_minimal_hash() {
        let p = detect_prompt(0, "# whoami").unwrap();
        assert_eq!(p.kind, PromptKind::UnixMinimal);
    }

    #[test]
    fn test_python_repl() {
        let p = detect_prompt(0, ">>> print('hello')").unwrap();
        assert_eq!(p.kind, PromptKind::PythonRepl);
        assert_eq!(&">>> print('hello')"[p.input_start..], "print('hello')");
    }

    #[test]
    fn test_python_continuation() {
        let p = detect_prompt(0, "... ").unwrap();
        assert_eq!(p.kind, PromptKind::PythonRepl);
    }

    #[test]
    fn test_no_prompt_plain_text() {
        assert!(detect_prompt(0, "Hello, world!").is_none());
    }

    #[test]
    fn test_no_prompt_empty() {
        assert!(detect_prompt(0, "").is_none());
        assert!(detect_prompt(0, "   ").is_none());
    }

    #[test]
    fn test_scan_for_prompts() {
        let lines = vec![
            r"C:\Users\Pratik>echo hello".to_string(),
            "hello".to_string(),
            r"C:\Users\Pratik>dir".to_string(),
            "file1.txt".to_string(),
            "file2.txt".to_string(),
        ];
        let prompts = scan_for_prompts(0, 4, |row| lines[row].clone());
        assert_eq!(prompts.len(), 2);
        assert_eq!(prompts[0].row, 0);
        assert_eq!(prompts[1].row, 2);
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(5_000))]

        /// detect_prompt must never panic on arbitrary text.
        #[test]
        fn detect_prompt_never_panics(text in ".*", row in 0usize..100_000) {
            let _ = detect_prompt(row, &text);
        }

        /// If detect_prompt returns Some, input_start must be within text bounds.
        #[test]
        fn input_start_within_bounds(text in ".{1,500}", row in 0usize..100) {
            if let Some(p) = detect_prompt(row, &text) {
                prop_assert!(p.input_start <= text.len(),
                    "input_start {} exceeds text len {}", p.input_start, text.len());
                prop_assert_eq!(p.row, row);
            }
        }

        /// Known cmd.exe prompts must always be detected.
        #[test]
        fn cmd_prompt_always_detected(
            drive in "[A-Z]",
            path in "[A-Za-z0-9_]{1,20}",
            cmd in "[a-z]{1,10}",
        ) {
            let text = format!("{drive}:\\{path}>{cmd}");
            let p = detect_prompt(0, &text);
            prop_assert!(p.is_some(), "cmd.exe prompt must be detected: {}", text);
            prop_assert_eq!(p.unwrap().kind, PromptKind::CmdExe);
        }

        /// Known PowerShell prompts must always be detected.
        #[test]
        fn ps_prompt_always_detected(
            drive in "[A-Z]",
            path in "[A-Za-z0-9_]{1,20}",
            cmd in "[a-z]{1,10}",
        ) {
            let text = format!("PS {drive}:\\{path}> {cmd}");
            let p = detect_prompt(0, &text);
            prop_assert!(p.is_some(), "PS prompt must be detected: {}", text);
            prop_assert_eq!(p.unwrap().kind, PromptKind::PowerShell);
        }

        /// scan_for_prompts must produce results sorted by row.
        #[test]
        fn scan_results_sorted_by_row(
            lines in proptest::collection::vec(".{0,80}", 1..50),
        ) {
            let prompts = scan_for_prompts(0, lines.len() - 1, |row| lines[row].clone());
            for pair in prompts.windows(2) {
                prop_assert!(pair[0].row < pair[1].row,
                    "Prompts must be sorted by row: {} < {}", pair[0].row, pair[1].row);
            }
        }
    }
}
