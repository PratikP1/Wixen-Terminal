//! WSL distro auto-detection for generating terminal profiles.

use crate::Profile;

/// A detected WSL distribution.
#[derive(Debug, Clone, PartialEq)]
pub struct WslDistro {
    /// Distribution name (e.g., "Ubuntu", "Debian").
    pub name: String,
    /// Whether this is the default WSL distro (marked with `*` in `wsl -l -v`).
    pub default: bool,
    /// WSL version (1 or 2).
    pub version: u8,
}

/// Detect installed WSL distributions by running `wsl -l -v`.
///
/// Returns an empty `Vec` if WSL is not installed or the command fails.
pub fn detect_wsl_distros() -> Vec<WslDistro> {
    let output = std::process::Command::new("wsl")
        .args(["-l", "-v"])
        .output();

    match output {
        Ok(out) if out.status.success() => {
            // wsl -l outputs UTF-16LE on Windows; decode accordingly.
            let text = decode_utf16le_or_utf8(&out.stdout);
            parse_wsl_list_output(&text)
        }
        _ => Vec::new(),
    }
}

/// Decode bytes that may be UTF-16LE (with optional BOM) or plain UTF-8.
fn decode_utf16le_or_utf8(bytes: &[u8]) -> String {
    // UTF-16LE BOM is 0xFF 0xFE
    if bytes.len() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE {
        decode_utf16le(&bytes[2..])
    } else if bytes.len() >= 2 && bytes.len().is_multiple_of(2) {
        // Heuristic: if every other byte is 0x00, it's likely UTF-16LE without BOM.
        let likely_utf16 = bytes.chunks(2).take(20).all(|c| c.len() == 2 && c[1] == 0);
        if likely_utf16 {
            decode_utf16le(bytes)
        } else {
            String::from_utf8_lossy(bytes).into_owned()
        }
    } else {
        String::from_utf8_lossy(bytes).into_owned()
    }
}

/// Decode raw UTF-16LE bytes (after BOM) into a String.
fn decode_utf16le(bytes: &[u8]) -> String {
    let u16s: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect();
    String::from_utf16_lossy(&u16s)
}

/// Parse the text output of `wsl -l -v` into a list of [`WslDistro`].
///
/// The output format looks like:
/// ```text
///   NAME                   STATE           VERSION
/// * Ubuntu                 Running         2
///   Debian                 Stopped         2
/// ```
///
/// The asterisk marks the default distribution.
/// Handles UTF-16LE BOM artefacts and null-byte-padded column output.
pub fn parse_wsl_list_output(output: &str) -> Vec<WslDistro> {
    // Strip BOM if present (may be leftover after UTF-16LE decode).
    let output = output.trim_start_matches('\u{feff}');
    // Remove any stray null bytes that survive decoding.
    let cleaned: String = output.replace('\0', "");

    let mut distros = Vec::new();

    for line in cleaned.lines().skip(1) {
        // Skip blank lines.
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }

        let is_default = trimmed.starts_with('*');

        // Remove the leading asterisk or leading whitespace.
        let rest = if is_default {
            trimmed[1..].trim_start()
        } else {
            trimmed
        };

        // Split by whitespace; expect at least NAME, STATE, VERSION.
        let parts: Vec<&str> = rest.split_whitespace().collect();
        if parts.len() < 3 {
            continue;
        }

        let name = parts[0].to_string();
        let version = match parts[parts.len() - 1].parse::<u8>() {
            Ok(v) if v == 1 || v == 2 => v,
            _ => continue, // Not a valid distro line.
        };

        distros.push(WslDistro {
            name,
            default: is_default,
            version,
        });
    }

    distros
}

/// Convert a [`WslDistro`] into a terminal [`Profile`].
pub fn distro_to_profile(distro: &WslDistro) -> Profile {
    Profile {
        name: format!("WSL: {}", distro.name),
        program: "wsl.exe".to_string(),
        args: vec!["-d".to_string(), distro.name.clone()],
        working_directory: "~".to_string(),
        is_default: distro.default,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TYPICAL_OUTPUT: &str = "\
  NAME                   STATE           VERSION
* Ubuntu                 Running         2
  Debian                 Stopped         2
";

    #[test]
    fn parse_typical_output() {
        let distros = parse_wsl_list_output(TYPICAL_OUTPUT);

        assert_eq!(distros.len(), 2);

        assert_eq!(distros[0].name, "Ubuntu");
        assert!(distros[0].default);
        assert_eq!(distros[0].version, 2);

        assert_eq!(distros[1].name, "Debian");
        assert!(!distros[1].default);
        assert_eq!(distros[1].version, 2);
    }

    #[test]
    fn parse_empty_output() {
        let distros = parse_wsl_list_output("");
        assert!(distros.is_empty());
    }

    #[test]
    fn parse_header_only() {
        let distros = parse_wsl_list_output("  NAME                   STATE           VERSION\n");
        assert!(distros.is_empty());
    }

    #[test]
    fn parse_wsl_not_installed_error() {
        // When WSL is not installed, the output may be an error message.
        let error_output = "Windows Subsystem for Linux has no installed distributions.\n\
            Use 'wsl.exe --list --online' to list available distributions\n\
            and 'wsl.exe --install <Distro>' to install.\n";
        let distros = parse_wsl_list_output(error_output);
        assert!(distros.is_empty());
    }

    #[test]
    fn parse_with_utf16le_bom() {
        // Simulate the BOM character that survives UTF-16LE decoding.
        let with_bom = format!(
            "\u{feff}  NAME                   STATE           VERSION\n\
             * Ubuntu                 Running         2\n\
               Debian                 Stopped         1\n"
        );
        let distros = parse_wsl_list_output(&with_bom);

        assert_eq!(distros.len(), 2);
        assert_eq!(distros[0].name, "Ubuntu");
        assert!(distros[0].default);
        assert_eq!(distros[0].version, 2);
        assert_eq!(distros[1].name, "Debian");
        assert!(!distros[1].default);
        assert_eq!(distros[1].version, 1);
    }

    #[test]
    fn parse_with_null_bytes() {
        // wsl -l outputs UTF-16LE; after naive decode, null bytes may remain.
        let with_nulls = "  N\0A\0M\0E\0   S\0T\0A\0T\0E\0   V\0E\0R\0S\0I\0O\0N\0\n\
                           * U\0b\0u\0n\0t\0u\0   R\0u\0n\0n\0i\0n\0g\0   2\0\n";
        let distros = parse_wsl_list_output(with_nulls);

        assert_eq!(distros.len(), 1);
        assert_eq!(distros[0].name, "Ubuntu");
        assert!(distros[0].default);
        assert_eq!(distros[0].version, 2);
    }

    #[test]
    fn distro_to_profile_default() {
        let distro = WslDistro {
            name: "Ubuntu".to_string(),
            default: true,
            version: 2,
        };
        let profile = distro_to_profile(&distro);

        assert_eq!(profile.name, "WSL: Ubuntu");
        assert_eq!(profile.program, "wsl.exe");
        assert_eq!(profile.args, vec!["-d", "Ubuntu"]);
        assert_eq!(profile.working_directory, "~");
        assert!(profile.is_default);
    }

    #[test]
    fn distro_to_profile_non_default() {
        let distro = WslDistro {
            name: "Debian".to_string(),
            default: false,
            version: 1,
        };
        let profile = distro_to_profile(&distro);

        assert_eq!(profile.name, "WSL: Debian");
        assert_eq!(profile.program, "wsl.exe");
        assert_eq!(profile.args, vec!["-d", "Debian"]);
        assert!(!profile.is_default);
    }

    #[test]
    fn decode_utf16le_bytes_with_bom() {
        // "Hi" in UTF-16LE with BOM: FF FE 48 00 69 00
        let bytes: &[u8] = &[0xFF, 0xFE, 0x48, 0x00, 0x69, 0x00];
        let result = decode_utf16le_or_utf8(bytes);
        assert_eq!(result, "Hi");
    }

    #[test]
    fn decode_plain_utf8() {
        let bytes = b"Hello world";
        let result = decode_utf16le_or_utf8(bytes);
        assert_eq!(result, "Hello world");
    }

    #[test]
    fn parse_single_distro_wsl1() {
        let output = "\
  NAME      STATE           VERSION
  Alpine    Stopped         1
";
        let distros = parse_wsl_list_output(output);
        assert_eq!(distros.len(), 1);
        assert_eq!(distros[0].name, "Alpine");
        assert!(!distros[0].default);
        assert_eq!(distros[0].version, 1);
    }

    #[test]
    fn parse_varied_whitespace() {
        // Ensure parser is robust against different column widths
        let output = "  NAME  STATE  VERSION\n*   Ubuntu   Running   2\n";
        let distros = parse_wsl_list_output(output);
        assert_eq!(distros.len(), 1);
        assert_eq!(distros[0].name, "Ubuntu");
        assert!(distros[0].default);
        assert_eq!(distros[0].version, 2);
    }

    #[test]
    fn parse_tabs_instead_of_spaces() {
        let output = "  NAME\tSTATE\tVERSION\n  Debian\tRunning\t2\n";
        let distros = parse_wsl_list_output(output);
        assert_eq!(distros.len(), 1);
        assert_eq!(distros[0].name, "Debian");
    }
}
