//! Regex-based URL detection for plain-text terminal output.
//!
//! Called lazily on hover/click — not on every row change — to keep the
//! hot path fast. Supports `http(s)://`, `ftp://`, and `file://` schemes.

use regex::Regex;
use std::sync::LazyLock;

/// Compiled URL pattern — initialized once, reused across calls.
static URL_RE: LazyLock<Regex> = LazyLock::new(|| {
    // Match common URL schemes followed by non-whitespace characters.
    // Trailing punctuation (except `/`) is stripped to avoid matching periods
    // or commas at the end of sentences.
    Regex::new(r"(?i)(https?://|ftp://|file://)\S+").unwrap()
});

/// A detected URL span within a row.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UrlMatch {
    /// Column where the URL starts (0-based).
    pub col_start: usize,
    /// Column where the URL ends (exclusive, 0-based).
    pub col_end: usize,
    /// The URL string.
    pub url: String,
}

/// Detect URLs in a single line of text.
///
/// Returns all matches sorted by `col_start`.
pub fn detect_urls(text: &str) -> Vec<UrlMatch> {
    URL_RE
        .find_iter(text)
        .map(|m| {
            let raw = m.as_str();
            // Strip trailing punctuation that's likely sentence-ending, not part of URL.
            let url = raw.trim_end_matches(|c: char| {
                matches!(c, '.' | ',' | ';' | ':' | '!' | '?' | ')' | ']' | '>')
            });
            // Adjust end column for stripped chars.
            let stripped = raw.len() - url.len();
            UrlMatch {
                col_start: m.start(),
                col_end: m.end() - stripped,
                url: url.to_string(),
            }
        })
        .filter(|m| !m.url.is_empty())
        .collect()
}

/// Check if a specific column position falls within any URL in the given text.
/// Returns the URL if found.
pub fn url_at_col(text: &str, col: usize) -> Option<String> {
    detect_urls(text)
        .into_iter()
        .find(|m| col >= m.col_start && col < m.col_end)
        .map(|m| m.url)
}

/// Check whether a URL uses a safe scheme (http, https, or ftp).
///
/// Returns `false` for dangerous schemes like `file://`, `javascript:`, `data:`,
/// or empty/malformed URLs.
pub fn is_safe_url_scheme(url: &str) -> bool {
    let lower = url.to_ascii_lowercase();
    lower.starts_with("http://") || lower.starts_with("https://") || lower.starts_with("ftp://")
}

/// Detect URLs in a line of text, filtering out unsafe schemes.
///
/// Like [`detect_urls`] but excludes URLs with schemes other than
/// `http://`, `https://`, and `ftp://`.
pub fn detect_safe_urls(text: &str) -> Vec<UrlMatch> {
    detect_urls(text)
        .into_iter()
        .filter(|m| is_safe_url_scheme(&m.url))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_http_url() {
        let urls = detect_urls("Visit https://example.com for info");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "https://example.com");
        assert_eq!(urls[0].col_start, 6);
    }

    #[test]
    fn test_detect_multiple_urls() {
        let urls = detect_urls("http://a.com and https://b.com/path");
        assert_eq!(urls.len(), 2);
        assert_eq!(urls[0].url, "http://a.com");
        assert_eq!(urls[1].url, "https://b.com/path");
    }

    #[test]
    fn test_strip_trailing_period() {
        let urls = detect_urls("See https://example.com/page.");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "https://example.com/page");
    }

    #[test]
    fn test_strip_trailing_comma() {
        let urls = detect_urls("https://example.com, and more");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "https://example.com");
    }

    #[test]
    fn test_strip_trailing_paren() {
        let urls = detect_urls("(see https://example.com/path)");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "https://example.com/path");
    }

    #[test]
    fn test_url_with_query_and_fragment() {
        let urls = detect_urls("go to https://example.com/search?q=rust#section now");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "https://example.com/search?q=rust#section");
    }

    #[test]
    fn test_ftp_url() {
        let urls = detect_urls("Download from ftp://files.example.com/pub");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "ftp://files.example.com/pub");
    }

    #[test]
    fn test_file_url() {
        let urls = detect_urls("Open file:///C:/Users/readme.txt please");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "file:///C:/Users/readme.txt");
    }

    #[test]
    fn test_no_urls() {
        let urls = detect_urls("Just a normal line of text");
        assert!(urls.is_empty());
    }

    #[test]
    fn test_url_at_col_hit() {
        let text = "See https://example.com for info";
        assert_eq!(url_at_col(text, 4), Some("https://example.com".to_string()));
        assert_eq!(
            url_at_col(text, 10),
            Some("https://example.com".to_string())
        );
    }

    #[test]
    fn test_url_at_col_miss() {
        let text = "See https://example.com for info";
        assert_eq!(url_at_col(text, 0), None);
        assert_eq!(url_at_col(text, 30), None);
    }

    #[test]
    fn test_case_insensitive() {
        let urls = detect_urls("HTTPS://EXAMPLE.COM/PATH");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "HTTPS://EXAMPLE.COM/PATH");
    }

    // --- URL scheme safety tests ---

    #[test]
    fn test_http_is_safe() {
        assert!(is_safe_url_scheme("http://example.com"));
    }

    #[test]
    fn test_https_is_safe() {
        assert!(is_safe_url_scheme("https://example.com"));
    }

    #[test]
    fn test_ftp_is_safe() {
        assert!(is_safe_url_scheme("ftp://files.example.com/pub"));
    }

    #[test]
    fn test_file_is_unsafe() {
        assert!(!is_safe_url_scheme("file:///C:/Users/readme.txt"));
    }

    #[test]
    fn test_javascript_is_unsafe() {
        assert!(!is_safe_url_scheme("javascript:alert(1)"));
    }

    #[test]
    fn test_data_is_unsafe() {
        assert!(!is_safe_url_scheme("data:text/html,<h1>hi</h1>"));
    }

    #[test]
    fn test_empty_is_unsafe() {
        assert!(!is_safe_url_scheme(""));
    }

    #[test]
    fn test_detect_safe_urls_excludes_file_scheme() {
        let urls = detect_safe_urls("Visit file:///etc/passwd and https://safe.com today");
        assert_eq!(urls.len(), 1);
        assert_eq!(urls[0].url, "https://safe.com");
    }

    #[test]
    fn test_detect_safe_urls_keeps_http_and_ftp() {
        let urls = detect_safe_urls("http://a.com ftp://b.com data:text/html,x");
        assert_eq!(urls.len(), 2);
        assert_eq!(urls[0].url, "http://a.com");
        assert_eq!(urls[1].url, "ftp://b.com");
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(5_000))]

        /// detect_urls must never panic on arbitrary input.
        #[test]
        fn detect_urls_never_panics(text in ".*") {
            let _ = detect_urls(&text);
        }

        /// url_at_col must never panic regardless of column value.
        #[test]
        fn url_at_col_never_panics(text in ".{0,500}", col in 0usize..1000) {
            let _ = url_at_col(&text, col);
        }

        /// All returned matches must have valid non-overlapping ranges within the text.
        #[test]
        fn matches_have_valid_ranges(text in ".{0,500}") {
            let urls = detect_urls(&text);
            for m in &urls {
                prop_assert!(m.col_start < m.col_end, "col_start must be less than col_end");
                prop_assert!(m.col_end <= text.len(), "col_end must not exceed text length");
                prop_assert!(!m.url.is_empty(), "URL must not be empty");
            }
            // Check non-overlapping
            for pair in urls.windows(2) {
                prop_assert!(pair[0].col_end <= pair[1].col_start,
                    "URL matches must not overlap");
            }
        }

        /// Injecting a known URL into random text must detect it.
        #[test]
        fn injected_url_is_detected(
            prefix in "[a-zA-Z0-9 ]{0,50}",
            host in "[a-z]{3,10}",
            suffix in "[a-zA-Z0-9 ]{0,50}",
        ) {
            let url = format!("https://{host}.com/path");
            let text = format!("{prefix} {url} {suffix}");
            let urls = detect_urls(&text);
            let found = urls.iter().any(|m| m.url.contains(&format!("{host}.com")));
            prop_assert!(found, "Injected URL must be detected in: {}", text);
        }
    }
}
