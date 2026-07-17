//! Tabular output detection for terminal command output.
//!
//! Detects column-aligned text in terminal output (e.g., `ls -la`, `docker ps`,
//! `ps aux`) and exposes structure for the IGridProvider accessibility pattern.
//! Screen reader users can then navigate column-by-column instead of reading
//! entire lines.

/// A detected table in terminal output.
#[derive(Debug, Clone, PartialEq)]
pub struct DetectedTable {
    /// Column headers (if a header row was detected).
    pub headers: Vec<String>,
    /// Data rows, each containing one string per column.
    pub rows: Vec<Vec<String>>,
    /// The column separator positions (character offsets in the original text).
    pub column_boundaries: Vec<usize>,
    /// Starting row index in the terminal (0-based).
    pub start_row: usize,
    /// Number of columns detected.
    pub col_count: usize,
}

/// Try to detect a table in a slice of text lines.
///
/// Returns `Some(DetectedTable)` if at least 3 rows share consistent column
/// alignment, suggesting tabular data. Returns `None` for freeform text.
///
/// The detection heuristic:
/// 1. Find columns of whitespace that align across multiple rows
/// 2. If 3+ rows share the same column boundaries, it's a table
/// 3. The first such row is treated as a header if it has different content
///    patterns (e.g., all alphabetic while data rows have numbers)
pub fn detect_table(lines: &[&str], start_row: usize) -> Option<DetectedTable> {
    if lines.len() < 3 {
        return None;
    }

    // Find whitespace boundaries: positions where 2+ consecutive spaces appear
    // in the same column across multiple rows.
    let max_len = lines.iter().map(|l| l.len()).max().unwrap_or(0);
    if max_len == 0 {
        return None;
    }

    // For each column position, count how many lines have a space there
    // followed by a non-space (i.e., a column boundary).
    let mut boundary_votes: Vec<usize> = vec![0; max_len];
    for line in lines {
        let bytes = line.as_bytes();
        let len = bytes.len();
        let mut i = 0;
        while i < len {
            // Look for transitions from space to non-space (column start)
            if i > 0 && bytes[i] != b' ' && bytes[i - 1] == b' ' && i >= 2 && bytes[i - 2] == b' ' {
                boundary_votes[i] += 1;
            }
            i += 1;
        }
    }

    // Find boundaries that appear in at least 60% of lines
    let threshold = (lines.len() * 60) / 100;
    let mut boundaries: Vec<usize> = boundary_votes
        .iter()
        .enumerate()
        .filter(|&(_, &count)| count >= threshold.max(2))
        .map(|(pos, _)| pos)
        .collect();

    if boundaries.is_empty() {
        return None;
    }

    // Add implicit boundary at position 0
    boundaries.insert(0, 0);
    boundaries.sort();
    boundaries.dedup();

    // Split each line by the detected boundaries. Boundaries are byte
    // positions voted across *all* lines, so in any particular line they can
    // land inside a multibyte character — clamp to the nearest char boundary
    // at or below before slicing.
    let mut all_rows: Vec<Vec<String>> = Vec::new();
    for line in lines {
        let mut cols = Vec::new();
        for i in 0..boundaries.len() {
            let start = floor_char_boundary(line, boundaries[i]);
            let end = if i + 1 < boundaries.len() {
                floor_char_boundary(line, boundaries[i + 1])
            } else {
                line.len()
            };
            cols.push(line[start..end.max(start)].trim().to_string());
        }
        all_rows.push(cols);
    }

    let col_count = boundaries.len();

    // Verify consistency: at least 3 rows should have non-empty values in multiple columns
    let filled_rows = all_rows
        .iter()
        .filter(|row| row.iter().filter(|c| !c.is_empty()).count() >= 2)
        .count();

    if filled_rows < 3 {
        return None;
    }

    // First row is likely a header if it exists
    let headers = all_rows[0].clone();
    let rows = all_rows[1..].to_vec();

    Some(DetectedTable {
        headers,
        rows,
        column_boundaries: boundaries,
        start_row,
        col_count,
    })
}

/// Largest index `<= at` that is a valid char boundary in `s`.
fn floor_char_boundary(s: &str, at: usize) -> usize {
    let mut idx = at.min(s.len());
    while !s.is_char_boundary(idx) {
        idx -= 1;
    }
    idx
}

/// Quick check: does this block of text look tabular?
///
/// Cheaper than full detection — just checks for column alignment patterns.
pub fn looks_tabular(lines: &[&str]) -> bool {
    if lines.len() < 3 {
        return false;
    }

    // Count lines with 2+ runs of 2+ spaces (column separators)
    let multi_column_lines = lines
        .iter()
        .filter(|line| {
            let trimmed = line.trim();
            // Count distinct gaps of 2+ spaces
            let mut gap_count = 0;
            let mut space_run = 0;
            for ch in trimmed.chars() {
                if ch == ' ' {
                    space_run += 1;
                } else {
                    if space_run >= 2 {
                        gap_count += 1;
                    }
                    space_run = 0;
                }
            }
            gap_count >= 1
        })
        .count();

    // If 60%+ of lines have multi-space gaps, it's likely tabular
    multi_column_lines * 100 / lines.len() >= 60
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_ls_la_output() {
        let lines = vec![
            "total 48",
            "drwxr-xr-x  5 user  staff   160 Jan 10 10:00 .",
            "drwxr-xr-x  3 user  staff    96 Jan  9 09:00 ..",
            "-rw-r--r--  1 user  staff  1234 Jan 10 10:00 README.md",
            "-rw-r--r--  1 user  staff   567 Jan 10 10:00 Cargo.toml",
        ];
        // The first line ("total 48") may not align, but the rest should
        let result = detect_table(&lines[1..], 1);
        assert!(
            result.is_some(),
            "ls -la output should be detected as a table"
        );
        let table = result.unwrap();
        assert!(table.col_count >= 3, "Should detect multiple columns");
    }

    #[test]
    fn test_detect_docker_ps() {
        let lines = vec![
            "CONTAINER ID   IMAGE          COMMAND   CREATED          STATUS          PORTS     NAMES",
            "abc123def456   nginx:latest   \"nginx\"   2 hours ago      Up 2 hours      80/tcp    web",
            "789ghi012jkl   redis:7        \"redis\"   3 hours ago      Up 3 hours      6379/tcp  cache",
            "mno345pqr678   postgres:15    \"postgres\" 5 hours ago     Up 5 hours      5432/tcp  db",
        ];
        let result = detect_table(&lines, 0);
        assert!(
            result.is_some(),
            "docker ps output should be detected as a table"
        );
        let table = result.unwrap();
        assert!(!table.headers.is_empty());
    }

    #[test]
    fn test_freeform_text_not_detected() {
        let lines = vec![
            "This is a normal paragraph of text.",
            "It has no column alignment at all.",
            "Each line is just regular prose.",
            "Nothing tabular about this output.",
        ];
        let result = detect_table(&lines, 0);
        assert!(
            result.is_none(),
            "Freeform text should not be detected as a table"
        );
    }

    #[test]
    fn test_too_few_lines() {
        let lines = vec!["header1  header2", "val1     val2"];
        assert!(detect_table(&lines, 0).is_none());
    }

    #[test]
    fn test_empty_input() {
        let lines: Vec<&str> = vec![];
        assert!(detect_table(&lines, 0).is_none());
    }

    #[test]
    fn test_looks_tabular_true() {
        let lines = vec![
            "NAME     STATUS   VERSION",
            "Ubuntu   Running  2",
            "Debian   Stopped  2",
            "Alpine   Running  1",
        ];
        assert!(looks_tabular(&lines));
    }

    #[test]
    fn test_looks_tabular_false() {
        let lines = vec![
            "Hello world",
            "This is not tabular",
            "Just regular text",
            "Nothing aligned here",
        ];
        assert!(!looks_tabular(&lines));
    }

    #[test]
    fn test_detect_table_with_varied_spacing() {
        let lines = vec![
            "PID    TTY   TIME      CMD",
            "1234   pts/0 00:00:01  bash",
            "5678   pts/0 00:00:00  ps",
            "9012   pts/1 00:01:23  vim",
        ];
        let result = detect_table(&lines, 0);
        assert!(result.is_some());
        let table = result.unwrap();
        assert_eq!(table.headers[0], "PID");
    }

    #[test]
    fn test_detect_table_multibyte_boundary_no_panic() {
        // The boundary at byte 4 (voted by the first two lines) falls in the
        // middle of the second 'é' in the third line — slicing there must not
        // panic on the non-char-boundary.
        let lines = vec!["aa  b", "aa  b", "a\u{e9}\u{e9} b"];
        let _ = detect_table(&lines, 0);
    }

    #[test]
    fn test_table_row_count() {
        let lines = vec![
            "A     B     C",
            "1     2     3",
            "4     5     6",
            "7     8     9",
        ];
        let result = detect_table(&lines, 0).unwrap();
        assert_eq!(result.rows.len(), 3); // header is separate
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    /// Lines that resemble columnar output: words (ASCII or unicode)
    /// separated by variable-width space runs.
    fn column_like_line() -> impl Strategy<Value = String> {
        proptest::collection::vec(("[a-zA-Z0-9é漢字À-ÿ]{1,8}", 1usize..5), 1..6).prop_map(|parts| {
            parts
                .into_iter()
                .map(|(word, gap)| format!("{word}{}", " ".repeat(gap)))
                .collect()
        })
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(1024))]

        /// detect_table must never panic on arbitrary multi-line input,
        /// including multibyte characters near boundary positions.
        #[test]
        fn detect_table_never_panics(
            lines in proptest::collection::vec(".{0,120}", 0..20),
            start_row in 0usize..1000,
        ) {
            let refs: Vec<&str> = lines.iter().map(String::as_str).collect();
            let _ = detect_table(&refs, start_row);
        }

        /// Column-like unicode input must never panic, and any detected
        /// table must be dimensionally consistent: headers and every row
        /// carry exactly col_count values.
        #[test]
        fn detected_table_dimensions_are_consistent(
            lines in proptest::collection::vec(column_like_line(), 3..15),
            start_row in 0usize..100,
        ) {
            let refs: Vec<&str> = lines.iter().map(String::as_str).collect();
            if let Some(table) = detect_table(&refs, start_row) {
                prop_assert_eq!(table.headers.len(), table.col_count);
                for row in &table.rows {
                    prop_assert_eq!(row.len(), table.col_count);
                }
                prop_assert_eq!(table.column_boundaries.len(), table.col_count);
                prop_assert_eq!(table.rows.len() + 1, refs.len());
                prop_assert_eq!(table.start_row, start_row);
            }
        }

        /// looks_tabular must never panic on arbitrary input.
        #[test]
        fn looks_tabular_never_panics(
            lines in proptest::collection::vec(".{0,120}", 0..20),
        ) {
            let refs: Vec<&str> = lines.iter().map(String::as_str).collect();
            let _ = looks_tabular(&refs);
        }
    }
}
