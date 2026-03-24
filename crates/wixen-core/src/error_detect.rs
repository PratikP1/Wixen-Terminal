//! Output line classification for accessibility — error, warning, info detection.
//!
//! Pure functions with no Win32 dependency, fully testable.

use regex::Regex;
use std::sync::LazyLock;

/// Classification of a single output line.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OutputLineClass {
    Normal,
    Error,
    Warning,
    Info,
}

// ── Compiled patterns ──

static CARET_LINE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\s*\^+\s*$").unwrap());

/// Rust compiler error: `error[E0308]: mismatched types`
static RUST_ERROR: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^error\[E\d+\]").unwrap());

/// Go compiler error: `./main.go:5:2: undefined: foo`
static GO_ERROR: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^\./[\w/]+\.go:\d+:\d+:").unwrap());

/// Python traceback header
static PYTHON_TRACEBACK: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^Traceback \(most recent call last\)").unwrap());

/// C/C++ compiler error: `file.c:10:5: error:`
static C_ERROR: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^[\w./\\]+\.\w+:\d+:\d+: error:").unwrap());

/// Classify a single output line.
pub fn classify_output_line(line: &str) -> OutputLineClass {
    let trimmed = line.trim_start();

    // ── Error patterns ──

    // Prefix-based
    if trimmed.starts_with("error:")
        || trimmed.starts_with("Error:")
        || trimmed.starts_with("ERROR")
        || trimmed.starts_with("FAILED")
        || trimmed.starts_with("fatal:")
        || trimmed.starts_with("panic:")
    {
        return OutputLineClass::Error;
    }

    // Caret pointer lines (e.g., `     ^^^`)
    if CARET_LINE.is_match(line) {
        return OutputLineClass::Error;
    }

    // Compiler error patterns
    if RUST_ERROR.is_match(trimmed)
        || GO_ERROR.is_match(trimmed)
        || PYTHON_TRACEBACK.is_match(trimmed)
        || C_ERROR.is_match(trimmed)
    {
        return OutputLineClass::Error;
    }

    // ── Warning patterns ──

    if trimmed.starts_with("warning:")
        || trimmed.starts_with("Warning:")
        || trimmed.starts_with("WARN")
        || trimmed.starts_with("deprecated")
    {
        return OutputLineClass::Warning;
    }

    // ── Info patterns ──

    if trimmed.starts_with("note:") || trimmed.starts_with("info:") || trimmed.starts_with("hint:")
    {
        return OutputLineClass::Info;
    }

    OutputLineClass::Normal
}

/// Count the number of error and warning lines.
///
/// Returns `(error_count, warning_count)`.
pub fn count_errors_warnings(lines: &[&str]) -> (usize, usize) {
    let mut errors = 0;
    let mut warnings = 0;
    for line in lines {
        match classify_output_line(line) {
            OutputLineClass::Error => errors += 1,
            OutputLineClass::Warning => warnings += 1,
            _ => {}
        }
    }
    (errors, warnings)
}

/// Produce a human-readable summary of errors and warnings.
///
/// Returns `None` if there are no errors or warnings.
pub fn error_summary(lines: &[&str]) -> Option<String> {
    let (errors, warnings) = count_errors_warnings(lines);
    if errors == 0 && warnings == 0 {
        return None;
    }
    let mut parts = Vec::new();
    if errors > 0 {
        parts.push(format!(
            "{} {}",
            errors,
            if errors == 1 { "error" } else { "errors" }
        ));
    }
    if warnings > 0 {
        parts.push(format!(
            "{} {}",
            warnings,
            if warnings == 1 { "warning" } else { "warnings" }
        ));
    }
    Some(parts.join(", "))
}

// ── Progress detection patterns ──

/// Percentage at end of line or standalone: `50%`, `50.5%`
static PERCENT_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(\d{1,3})(?:\.\d+)?%").unwrap());

/// Fraction-based progress: `(5/10)` or `5 of 10`
static FRACTION_PAREN: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\((\d+)/(\d+)\)").unwrap());

static FRACTION_OF: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(\d+)\s+of\s+(\d+)").unwrap());

/// Detect a progress percentage from an output line.
///
/// Returns `Some(0..=100)` if a progress indicator is found, `None` otherwise.
pub fn detect_progress(line: &str) -> Option<u8> {
    // Try percentage-based patterns first (most common)
    if let Some(caps) = PERCENT_RE.captures(line) {
        let pct: u32 = caps[1].parse().ok()?;
        if pct <= 100 {
            return Some(pct as u8);
        }
    }

    // Try fraction in parentheses: (5/10)
    if let Some(caps) = FRACTION_PAREN.captures(line) {
        let num: u32 = caps[1].parse().ok()?;
        let den: u32 = caps[2].parse().ok()?;
        if den > 0 {
            let pct = (num * 100) / den;
            if pct <= 100 {
                return Some(pct as u8);
            }
        }
    }

    // Try "N of M"
    if let Some(caps) = FRACTION_OF.captures(line) {
        let num: u32 = caps[1].parse().ok()?;
        let den: u32 = caps[2].parse().ok()?;
        if den > 0 {
            let pct = (num * 100) / den;
            if pct <= 100 {
                return Some(pct as u8);
            }
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── classify_output_line ──

    #[test]
    fn test_error_lowercase_prefix() {
        assert_eq!(
            classify_output_line("error: cannot find value `x`"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_titlecase_prefix() {
        assert_eq!(
            classify_output_line("Error: something went wrong"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_uppercase() {
        assert_eq!(
            classify_output_line("ERROR something broke"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_failed() {
        assert_eq!(
            classify_output_line("FAILED tests/foo.rs"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_fatal() {
        assert_eq!(
            classify_output_line("fatal: not a git repository"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_panic() {
        assert_eq!(
            classify_output_line("panic: runtime error"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_caret_pointer() {
        assert_eq!(classify_output_line("     ^^^"), OutputLineClass::Error);
        assert_eq!(classify_output_line("  ^"), OutputLineClass::Error);
        assert_eq!(classify_output_line("  ^^^^^^  "), OutputLineClass::Error);
    }

    #[test]
    fn test_error_rust_compiler() {
        assert_eq!(
            classify_output_line("error[E0308]: mismatched types"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_go_compiler() {
        assert_eq!(
            classify_output_line("./main.go:5:2: undefined: foo"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_python_traceback() {
        assert_eq!(
            classify_output_line("Traceback (most recent call last)"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_error_c_compiler() {
        assert_eq!(
            classify_output_line("main.c:10:5: error: expected ';'"),
            OutputLineClass::Error
        );
    }

    #[test]
    fn test_warning_lowercase() {
        assert_eq!(
            classify_output_line("warning: unused variable"),
            OutputLineClass::Warning
        );
    }

    #[test]
    fn test_warning_titlecase() {
        assert_eq!(
            classify_output_line("Warning: something fishy"),
            OutputLineClass::Warning
        );
    }

    #[test]
    fn test_warning_uppercase() {
        assert_eq!(
            classify_output_line("WARN connection lost"),
            OutputLineClass::Warning
        );
    }

    #[test]
    fn test_warning_deprecated() {
        assert_eq!(
            classify_output_line("deprecated: use new_api() instead"),
            OutputLineClass::Warning
        );
    }

    #[test]
    fn test_info_note() {
        assert_eq!(
            classify_output_line("note: see also --help"),
            OutputLineClass::Info
        );
    }

    #[test]
    fn test_info_info() {
        assert_eq!(
            classify_output_line("info: downloading component"),
            OutputLineClass::Info
        );
    }

    #[test]
    fn test_info_hint() {
        assert_eq!(
            classify_output_line("hint: consider adding a lifetime"),
            OutputLineClass::Info
        );
    }

    #[test]
    fn test_normal_plain_text() {
        assert_eq!(
            classify_output_line("Hello, world!"),
            OutputLineClass::Normal
        );
    }

    #[test]
    fn test_normal_empty_line() {
        assert_eq!(classify_output_line(""), OutputLineClass::Normal);
    }

    #[test]
    fn test_normal_line_containing_error_mid_word() {
        // "errors" at the start would match, but "my error log" should not
        // because it doesn't start with "error:" — it starts with "my"
        assert_eq!(
            classify_output_line("my error log"),
            OutputLineClass::Normal
        );
    }

    #[test]
    fn test_error_with_leading_whitespace() {
        // Leading whitespace should be trimmed before matching prefixes
        assert_eq!(
            classify_output_line("   error: indented error"),
            OutputLineClass::Error
        );
    }

    // ── count_errors_warnings ──

    #[test]
    fn test_count_mixed() {
        let lines = vec![
            "Compiling foo v0.1.0",
            "warning: unused variable",
            "error: cannot find value",
            "note: see --help",
            "error: aborting due to previous error",
            "warning: field is never read",
        ];
        assert_eq!(count_errors_warnings(&lines), (2, 2));
    }

    #[test]
    fn test_count_all_normal() {
        let lines = vec!["hello", "world", ""];
        assert_eq!(count_errors_warnings(&lines), (0, 0));
    }

    #[test]
    fn test_count_empty_input() {
        let lines: Vec<&str> = vec![];
        assert_eq!(count_errors_warnings(&lines), (0, 0));
    }

    // ── error_summary ──

    #[test]
    fn test_summary_errors_and_warnings() {
        let lines = vec![
            "error: one",
            "error: two",
            "error: three",
            "warning: w1",
            "warning: w2",
            "all good",
        ];
        assert_eq!(
            error_summary(&lines),
            Some("3 errors, 2 warnings".to_string())
        );
    }

    #[test]
    fn test_summary_errors_only() {
        let lines = vec!["error: one"];
        assert_eq!(error_summary(&lines), Some("1 error".to_string()));
    }

    #[test]
    fn test_summary_warnings_only() {
        let lines = vec!["warning: a", "warning: b"];
        assert_eq!(error_summary(&lines), Some("2 warnings".to_string()));
    }

    #[test]
    fn test_summary_none_when_clean() {
        let lines = vec!["hello", "world"];
        assert_eq!(error_summary(&lines), None);
    }

    // ── detect_progress ──

    #[test]
    fn test_progress_bar_with_percentage() {
        assert_eq!(detect_progress("[=====>    ] 50%"), Some(50));
    }

    #[test]
    fn test_progress_hash_bar() {
        assert_eq!(detect_progress("[########  ] 80%"), Some(80));
    }

    #[test]
    fn test_progress_standalone_percentage() {
        assert_eq!(detect_progress("50%"), Some(50));
    }

    #[test]
    fn test_progress_decimal_percentage() {
        // The integer part is captured
        assert_eq!(detect_progress("50.5%"), Some(50));
    }

    #[test]
    fn test_progress_label_prefix() {
        assert_eq!(detect_progress("Progress: 75%"), Some(75));
    }

    #[test]
    fn test_progress_downloading() {
        assert_eq!(detect_progress("Downloading... 50%"), Some(50));
    }

    #[test]
    fn test_progress_fraction_parens() {
        assert_eq!(detect_progress("(5/10)"), Some(50));
    }

    #[test]
    fn test_progress_fraction_of() {
        assert_eq!(detect_progress("5 of 10"), Some(50));
    }

    #[test]
    fn test_progress_100_percent() {
        assert_eq!(detect_progress("100%"), Some(100));
    }

    #[test]
    fn test_progress_0_percent() {
        assert_eq!(detect_progress("0%"), Some(0));
    }

    #[test]
    fn test_progress_none_plain_text() {
        assert_eq!(detect_progress("Hello, world!"), None);
    }

    #[test]
    fn test_progress_none_empty() {
        assert_eq!(detect_progress(""), None);
    }

    #[test]
    fn test_progress_over_100_ignored() {
        // 200% is not a valid progress
        assert_eq!(detect_progress("200%"), None);
    }

    #[test]
    fn test_progress_fraction_complete() {
        assert_eq!(detect_progress("(10/10)"), Some(100));
    }

    #[test]
    fn test_progress_fraction_with_context() {
        assert_eq!(detect_progress("Installing packages (3 of 12)"), Some(25));
    }
}
