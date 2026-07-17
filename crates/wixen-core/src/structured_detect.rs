//! Structured output detection for terminal command output.
//!
//! Detects JSON, YAML, and XML documents in terminal output (e.g., `curl` API
//! responses, `kubectl get -o yaml`, config file dumps) so screen reader users
//! hear a concise format summary instead of raw punctuation-heavy text.

use wixen_shell_integ::{CommandBlock, command_outcome, command_summary};

/// A structured data format detected in terminal output.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StructuredFormat {
    /// JSON object or array.
    Json,
    /// YAML document.
    Yaml,
    /// XML document.
    Xml,
}

/// Try to detect a structured data format in a slice of text lines.
///
/// Returns `None` for plain text or when fewer than 2 non-empty lines
/// are present.
pub fn detect_structured_format(lines: &[&str]) -> Option<StructuredFormat> {
    let non_empty: Vec<&str> = lines
        .iter()
        .map(|line| line.trim())
        .filter(|line| !line.is_empty())
        .collect();
    let (first, last) = match (non_empty.first(), non_empty.last()) {
        (Some(first), Some(last)) if non_empty.len() >= 2 => (*first, *last),
        _ => return None,
    };

    let opens_like_json = first.starts_with('{') || first.starts_with('[');
    let closes_like_json = last.ends_with('}') || last.ends_with(']');
    if opens_like_json && closes_like_json {
        return Some(StructuredFormat::Json);
    }

    if opens_like_xml(first) && last.ends_with('>') {
        return Some(StructuredFormat::Xml);
    }

    if first == "---" || has_yaml_key_value_majority(&non_empty) {
        return Some(StructuredFormat::Yaml);
    }

    None
}

/// Whether more than half of the given non-empty lines look like YAML
/// `key: value` mappings.
fn has_yaml_key_value_majority(non_empty: &[&str]) -> bool {
    let matching = non_empty
        .iter()
        .filter(|line| is_yaml_key_value(line))
        .count();
    matching * 2 > non_empty.len()
}

/// Whether a trimmed line looks like a YAML `key: value` mapping entry.
///
/// The key must be non-empty, contain no whitespace, and be followed by a
/// colon that ends the line or is followed by a space.
fn is_yaml_key_value(line: &str) -> bool {
    match line.split_once(':') {
        Some((key, value)) => {
            !key.is_empty()
                && !key.contains(char::is_whitespace)
                && (value.is_empty() || value.starts_with(' '))
        }
        None => false,
    }
}

/// Format a screen reader summary for detected structured output,
/// e.g. `"JSON output: 42 lines"`.
pub fn structured_summary(format: StructuredFormat, line_count: usize) -> String {
    let format_name = match format {
        StructuredFormat::Json => "JSON",
        StructuredFormat::Yaml => "YAML",
        StructuredFormat::Xml => "XML",
    };
    format!("{format_name} output: {line_count} lines")
}

/// Command completion announcement with structured-output context.
///
/// When the command's output lines look like JSON, YAML, or XML, the plain
/// line count is replaced with a format summary, e.g.
/// `"cargo metadata completed: JSON output: 42 lines"`. Otherwise this
/// falls back to `command_summary` unchanged.
pub fn completion_announcement(block: &CommandBlock, output_lines: &[&str]) -> String {
    match detect_structured_format(output_lines) {
        Some(format) => format!(
            "{}: {}",
            command_outcome(block),
            structured_summary(format, block.output_line_count)
        ),
        None => command_summary(block),
    }
}

/// Whether a trimmed line opens an XML document: an `<?xml` declaration or
/// a `<` immediately followed by a tag name.
fn opens_like_xml(line: &str) -> bool {
    if line.starts_with("<?xml") {
        return true;
    }
    line.strip_prefix('<')
        .and_then(|rest| rest.chars().next())
        .is_some_and(|tag_start| tag_start.is_ascii_alphabetic())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_structured_summary_names_format_and_line_count() {
        assert_eq!(
            structured_summary(StructuredFormat::Json, 42),
            "JSON output: 42 lines"
        );
        assert_eq!(
            structured_summary(StructuredFormat::Xml, 7),
            "XML output: 7 lines"
        );
        assert_eq!(
            structured_summary(StructuredFormat::Yaml, 12),
            "YAML output: 12 lines"
        );
    }

    #[test]
    fn test_detects_json_object() {
        let lines = ["{", "  \"status\": \"ok\"", "}"];
        assert_eq!(
            detect_structured_format(&lines),
            Some(StructuredFormat::Json)
        );
    }

    #[test]
    fn test_detects_xml_with_declaration() {
        let lines = [
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
            "<note>",
            "  <body>Hello</body>",
            "</note>",
        ];
        assert_eq!(
            detect_structured_format(&lines),
            Some(StructuredFormat::Xml)
        );
    }

    #[test]
    fn test_detects_xml_without_declaration() {
        let lines = ["<config>", "  <port>8080</port>", "</config>"];
        assert_eq!(
            detect_structured_format(&lines),
            Some(StructuredFormat::Xml)
        );
    }

    #[test]
    fn test_detects_yaml_with_document_start() {
        let lines = ["---", "name: wixen", "version: 1"];
        assert_eq!(
            detect_structured_format(&lines),
            Some(StructuredFormat::Yaml)
        );
    }

    #[test]
    fn test_detects_yaml_by_key_value_majority() {
        let lines = ["name: wixen", "version: 1.4.0", "features:", "  - images"];
        assert_eq!(
            detect_structured_format(&lines),
            Some(StructuredFormat::Yaml)
        );
    }

    #[test]
    fn test_plain_text_returns_none() {
        let lines = ["The quick brown fox", "jumps over the lazy dog"];
        assert_eq!(detect_structured_format(&lines), None);
    }

    #[test]
    fn test_empty_input_returns_none() {
        assert_eq!(detect_structured_format(&[]), None);
        assert_eq!(detect_structured_format(&["", "   "]), None);
    }

    #[test]
    fn test_single_line_returns_none() {
        assert_eq!(detect_structured_format(&["{\"compact\": true}"]), None);
    }

    #[test]
    fn test_detects_json_array() {
        let lines = ["[", "  1,", "  2", "]"];
        assert_eq!(
            detect_structured_format(&lines),
            Some(StructuredFormat::Json)
        );
    }

    // ── completion_announcement tests ──

    use wixen_shell_integ::BlockState;

    fn completed_block(
        command_text: Option<&str>,
        exit_code: Option<i32>,
        lines: usize,
    ) -> CommandBlock {
        CommandBlock {
            id: 1,
            prompt: None,
            input: None,
            output: None,
            exit_code,
            cwd: None,
            command_text: command_text.map(String::from),
            state: BlockState::Completed,
            started_at: None,
            output_line_count: lines,
        }
    }

    #[test]
    fn test_completion_announcement_with_json_detection() {
        let block = completed_block(Some("cargo metadata"), Some(0), 42);
        let lines = ["{", "  \"packages\": []", "}"];
        assert_eq!(
            completion_announcement(&block, &lines),
            "cargo metadata completed: JSON output: 42 lines"
        );
    }

    #[test]
    fn test_completion_announcement_failure_keeps_exit_code() {
        let block = completed_block(Some("curl api"), Some(22), 3);
        let lines = ["{", "  \"error\": true", "}"];
        assert_eq!(
            completion_announcement(&block, &lines),
            "curl api failed (exit 22): JSON output: 3 lines"
        );
    }

    #[test]
    fn test_completion_announcement_plain_output_falls_back_to_summary() {
        let block = completed_block(Some("ls"), Some(0), 2);
        let lines = ["file_a", "file_b"];
        assert_eq!(
            completion_announcement(&block, &lines),
            "ls completed: 2 lines of output"
        );
    }

    #[test]
    fn test_completion_announcement_empty_output_falls_back_to_summary() {
        let block = completed_block(Some("true"), Some(0), 0);
        assert_eq!(
            completion_announcement(&block, &[]),
            "true completed: 0 lines of output"
        );
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;
    use wixen_shell_integ::{BlockState, CommandBlock};

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(1024))]

        /// detect_structured_format must never panic on arbitrary lines,
        /// including unicode and control characters.
        #[test]
        fn detect_structured_format_never_panics(
            lines in proptest::collection::vec(".{0,120}", 0..30),
        ) {
            let refs: Vec<&str> = lines.iter().map(String::as_str).collect();
            let _ = detect_structured_format(&refs);
        }

        /// completion_announcement must never panic for arbitrary block
        /// contents and output lines, and always yields non-empty text.
        #[test]
        fn completion_announcement_never_panics(
            command_text in proptest::option::of(".{0,80}"),
            exit_code in proptest::option::of(any::<i32>()),
            line_count in any::<usize>(),
            lines in proptest::collection::vec(".{0,80}", 0..20),
        ) {
            let block = CommandBlock {
                id: 1,
                prompt: None,
                input: None,
                output: None,
                exit_code,
                cwd: None,
                command_text,
                state: BlockState::Completed,
                started_at: None,
                output_line_count: line_count,
            };
            let refs: Vec<&str> = lines.iter().map(String::as_str).collect();
            let announcement = completion_announcement(&block, &refs);
            prop_assert!(!announcement.is_empty());
        }
    }
}
