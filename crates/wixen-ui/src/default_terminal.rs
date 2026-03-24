//! Check and report whether Wixen is registered as the default terminal on Windows.
//!
//! Windows 11 allows third-party terminals to register as the default via a
//! CLSID stored in `HKCU\Console\%%Startup` under the `DelegationTerminal` value.

/// Wixen Terminal's COM CLSID for default terminal delegation.
///
/// This GUID is registered during installation and checked by Windows when
/// deciding which terminal to launch for console applications.
pub const WIXEN_TERMINAL_CLSID: &str = "{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}";

/// Returns Wixen Terminal's CLSID constant for default terminal registration.
pub fn default_terminal_clsid() -> &'static str {
    WIXEN_TERMINAL_CLSID
}

/// Status of the default terminal registration.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DefaultTerminalStatus {
    /// Whether Wixen is currently the default terminal.
    pub is_default: bool,
    /// The CLSID of the currently registered default terminal handler, if any.
    pub current_handler: Option<String>,
}

/// Check whether Wixen is registered as the default terminal.
///
/// Reads `HKCU\Console\%%Startup\DelegationTerminal` from the Windows Registry
/// and compares it to [`WIXEN_TERMINAL_CLSID`].
///
/// On non-Windows platforms this always returns `false`.
#[cfg(windows)]
pub fn is_default_terminal() -> bool {
    check_default_terminal_status().is_default
}

/// Non-Windows stub: always returns `false`.
#[cfg(not(windows))]
pub fn is_default_terminal() -> bool {
    false
}

/// Query the registry for the current default terminal status.
///
/// Returns a [`DefaultTerminalStatus`] indicating whether Wixen is the default
/// and what CLSID is currently registered.
#[cfg(windows)]
pub fn check_default_terminal_status() -> DefaultTerminalStatus {
    use windows::Win32::System::Registry::{
        HKEY, HKEY_CURRENT_USER, KEY_READ, REG_SZ, REG_VALUE_TYPE, RegCloseKey, RegOpenKeyExW,
        RegQueryValueExW,
    };
    use windows::core::HSTRING;

    let subkey = HSTRING::from(r"Console\%%Startup");
    let value_name = HSTRING::from("DelegationTerminal");

    let mut hkey = HKEY::default();

    // Try to open the registry key.
    let open_result =
        unsafe { RegOpenKeyExW(HKEY_CURRENT_USER, &subkey, Some(0), KEY_READ, &mut hkey) };

    if open_result.is_err() {
        return DefaultTerminalStatus {
            is_default: false,
            current_handler: None,
        };
    }

    // Query the value size first.
    let mut data_type = REG_VALUE_TYPE::default();
    let mut data_size: u32 = 0;

    let query_result = unsafe {
        RegQueryValueExW(
            hkey,
            &value_name,
            Some(std::ptr::null()),
            Some(&mut data_type),
            None,
            Some(&mut data_size),
        )
    };

    if query_result.is_err() || data_type != REG_SZ || data_size == 0 {
        unsafe {
            let _ = RegCloseKey(hkey);
        }
        return DefaultTerminalStatus {
            is_default: false,
            current_handler: None,
        };
    }

    // Read the actual value.
    let mut buffer = vec![0u8; data_size as usize];
    let query_result = unsafe {
        RegQueryValueExW(
            hkey,
            &value_name,
            Some(std::ptr::null()),
            Some(&mut data_type),
            Some(buffer.as_mut_ptr()),
            Some(&mut data_size),
        )
    };

    unsafe {
        let _ = RegCloseKey(hkey);
    }

    if query_result.is_err() {
        return DefaultTerminalStatus {
            is_default: false,
            current_handler: None,
        };
    }

    // Decode UTF-16LE string (registry REG_SZ is UTF-16LE with null terminator).
    let u16_slice: &[u16] = unsafe {
        std::slice::from_raw_parts(buffer.as_ptr().cast::<u16>(), data_size as usize / 2)
    };
    let clsid = String::from_utf16_lossy(u16_slice)
        .trim_end_matches('\0')
        .to_string();

    let is_default = clsid.eq_ignore_ascii_case(WIXEN_TERMINAL_CLSID);

    DefaultTerminalStatus {
        is_default,
        current_handler: Some(clsid),
    }
}

/// Non-Windows stub: always reports not-default with no handler.
#[cfg(not(windows))]
pub fn check_default_terminal_status() -> DefaultTerminalStatus {
    DefaultTerminalStatus {
        is_default: false,
        current_handler: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clsid_is_valid_guid_format() {
        let clsid = default_terminal_clsid();

        // GUID format: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
        assert!(clsid.starts_with('{'), "CLSID must start with '{{' brace");
        assert!(clsid.ends_with('}'), "CLSID must end with '}}' brace");

        let inner = &clsid[1..clsid.len() - 1];
        let parts: Vec<&str> = inner.split('-').collect();
        assert_eq!(parts.len(), 5, "GUID must have 5 dash-separated groups");
        assert_eq!(parts[0].len(), 8, "First group must be 8 hex chars");
        assert_eq!(parts[1].len(), 4, "Second group must be 4 hex chars");
        assert_eq!(parts[2].len(), 4, "Third group must be 4 hex chars");
        assert_eq!(parts[3].len(), 4, "Fourth group must be 4 hex chars");
        assert_eq!(parts[4].len(), 12, "Fifth group must be 12 hex chars");

        // All characters (excluding braces and dashes) must be hex digits.
        for part in &parts {
            assert!(
                part.chars().all(|c| c.is_ascii_hexdigit()),
                "All GUID characters must be hex digits, got: {part}"
            );
        }
    }

    #[test]
    fn default_terminal_clsid_returns_constant() {
        assert_eq!(default_terminal_clsid(), WIXEN_TERMINAL_CLSID);
    }

    #[test]
    fn status_construction_not_default() {
        let status = DefaultTerminalStatus {
            is_default: false,
            current_handler: None,
        };

        assert!(!status.is_default);
        assert_eq!(status.current_handler, None);
    }

    #[test]
    fn status_construction_is_default() {
        let status = DefaultTerminalStatus {
            is_default: true,
            current_handler: Some(WIXEN_TERMINAL_CLSID.to_string()),
        };

        assert!(status.is_default);
        assert_eq!(
            status.current_handler.as_deref(),
            Some(WIXEN_TERMINAL_CLSID)
        );
    }

    #[test]
    fn status_construction_other_terminal() {
        let other_clsid = "{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}";
        let status = DefaultTerminalStatus {
            is_default: false,
            current_handler: Some(other_clsid.to_string()),
        };

        assert!(!status.is_default);
        assert_eq!(status.current_handler.as_deref(), Some(other_clsid));
    }

    #[test]
    fn status_debug_and_clone() {
        let status = DefaultTerminalStatus {
            is_default: true,
            current_handler: Some(WIXEN_TERMINAL_CLSID.to_string()),
        };

        let cloned = status.clone();
        assert_eq!(status, cloned);

        // Verify Debug is implemented (would fail to compile otherwise).
        let debug_str = format!("{status:?}");
        assert!(debug_str.contains("is_default: true"));
    }
}
