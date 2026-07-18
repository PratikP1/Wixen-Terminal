//! Check, register, and restore whether Wixen is the default terminal on Windows.
//!
//! Windows 11 allows third-party terminals to register as the default via
//! CLSIDs stored in `HKCU\Console\%%Startup` under the `DelegationConsole` and
//! `DelegationTerminal` values.
//!
//! # Scope of the registry writer vs. the COM handoff server
//!
//! [`set_as_default_terminal`] writes the delegation CLSIDs. That is only *half*
//! of what being the default terminal requires. When a console application
//! launches, Windows reads `DelegationTerminal`, `CoCreateInstance`s that CLSID,
//! and calls the console-handoff COM interface (`ITerminalHandoff` /
//! `ITerminalHandoff2` / `ITerminalHandoff3`) to pass the new ConPTY connection
//! to the chosen terminal.
//!
//! This module does **not** implement that COM server. Writing the registry
//! keys makes Windows *try* to hand off to [`WIXEN_TERMINAL_CLSID`]; if no COM
//! class factory is registered for that CLSID and no `ITerminalHandoff`
//! implementation is running, the `CoCreateInstance` fails and the console
//! launch fails with it. Therefore [`set_as_default_terminal`] must not be
//! exposed to users until the handoff server is implemented and its class
//! factory registered — otherwise it breaks console launches. Until then the
//! action should stay gated (e.g. behind a build flag or hidden in the UI).
//! [`restore_default_terminal`] is always safe to expose: it hands the choice
//! back to Windows.

/// Wixen Terminal's COM CLSID for default terminal delegation.
///
/// This GUID is registered during installation and checked by Windows when
/// deciding which terminal to launch for console applications.
pub const WIXEN_TERMINAL_CLSID: &str = "{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}";

/// The built-in Windows Console Host (`conhost.exe`) delegation CLSID.
///
/// Windows writes this to both `DelegationConsole` and `DelegationTerminal`
/// when the user picks "Windows Console Host" as their default terminal.
pub const CONHOST_CLSID: &str = "{B23D10C0-E52E-411E-9D5B-C09FDF709C7D}";

/// Windows Terminal's `DelegationTerminal` CLSID.
///
/// Present when Windows Terminal is the default terminal handler. (Windows
/// Terminal registers a distinct `DelegationConsole` CLSID; this is the value
/// that lands in `DelegationTerminal`, which is what the reader inspects.)
pub const WINDOWS_TERMINAL_CLSID: &str = "{E12CFF52-A866-4C77-9A90-F570A7AA2C6B}";

/// The null CLSID meaning "let Windows decide" which terminal to launch.
///
/// This is the pristine, out-of-box Windows 11 value for both delegation
/// values. Restoring to it hands the choice back to Windows, which always
/// falls back to the built-in console host, so console launches never break.
pub const WINDOWS_DECIDE_CLSID: &str = "{00000000-0000-0000-0000-000000000000}";

/// Registry subkey under `HKEY_CURRENT_USER` holding the console startup
/// delegation values.
const CONSOLE_STARTUP_SUBKEY: &str = r"Console\%%Startup";

/// Value naming the COM handler Windows delegates the terminal UI to.
const DELEGATION_TERMINAL_VALUE: &str = "DelegationTerminal";

/// Value naming the COM handler Windows delegates the console host to.
const DELEGATION_CONSOLE_VALUE: &str = "DelegationConsole";

/// Returns Wixen Terminal's CLSID constant for default terminal registration.
pub fn default_terminal_clsid() -> &'static str {
    WIXEN_TERMINAL_CLSID
}

/// The terminal handler a `DelegationTerminal` CLSID refers to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TerminalHandler {
    /// Wixen Terminal — Wixen is the default.
    Wixen,
    /// The built-in Windows Console Host (`conhost.exe`).
    Conhost,
    /// Windows Terminal.
    WindowsTerminal,
    /// The null CLSID — "let Windows decide".
    WindowsDecide,
    /// A CLSID we do not recognise (some other third-party terminal).
    Other,
}

/// Classify a `DelegationTerminal` CLSID string into a known [`TerminalHandler`].
///
/// Comparison ignores surrounding whitespace and ASCII case, matching how the
/// registry reader normalises values.
pub fn classify_delegation_terminal(clsid: &str) -> TerminalHandler {
    let clsid = clsid.trim();
    if clsid.eq_ignore_ascii_case(WIXEN_TERMINAL_CLSID) {
        TerminalHandler::Wixen
    } else if clsid.eq_ignore_ascii_case(CONHOST_CLSID) {
        TerminalHandler::Conhost
    } else if clsid.eq_ignore_ascii_case(WINDOWS_TERMINAL_CLSID) {
        TerminalHandler::WindowsTerminal
    } else if clsid.eq_ignore_ascii_case(WINDOWS_DECIDE_CLSID) {
        TerminalHandler::WindowsDecide
    } else {
        TerminalHandler::Other
    }
}

/// Encode a string as a null-terminated UTF-16LE byte buffer for a `REG_SZ`
/// value. The trailing null terminator is included in the returned bytes, as
/// `RegSetValueExW` expects for string values.
fn encode_reg_sz(value: &str) -> Vec<u8> {
    value
        .encode_utf16()
        .chain(std::iter::once(0))
        .flat_map(u16::to_le_bytes)
        .collect()
}

/// An error writing the default-terminal delegation values to the registry.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum RegistrationError {
    /// Opening or creating the `Console\%%Startup` key failed.
    #[error("failed to open registry key HKCU\\{subkey}: Win32 error {code}")]
    OpenKey {
        /// The subkey path under `HKEY_CURRENT_USER`.
        subkey: String,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// Writing one of the delegation values failed.
    #[error("failed to write registry value {value_name}: Win32 error {code}")]
    SetValue {
        /// The registry value name that failed to write.
        value_name: &'static str,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// Registration is only meaningful on Windows.
    #[error("default terminal registration is only supported on Windows")]
    Unsupported,
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

    let subkey = HSTRING::from(CONSOLE_STARTUP_SUBKEY);
    let value_name = HSTRING::from(DELEGATION_TERMINAL_VALUE);

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

    let is_default = classify_delegation_terminal(&clsid) == TerminalHandler::Wixen;

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

/// Register Wixen as the Windows default terminal.
///
/// Writes [`WIXEN_TERMINAL_CLSID`] to `DelegationTerminal` and [`CONHOST_CLSID`]
/// (the inbox console host) to `DelegationConsole` under `HKCU\Console\%%Startup`.
///
/// `DelegationConsole` must name a **console server** that implements
/// `IConsoleHandoff` — Wixen is a terminal, not a console server, so pointing
/// that value at Wixen (as an earlier version did) would fail every console
/// launch at the console hop before terminal handoff is ever reached. The inbox
/// conhost is a real console server that performs the terminal handoff to
/// `DelegationTerminal`.
///
/// # Gated — do not call in a shipping build yet
///
/// Even with the correct delegation pairing, being the default terminal also
/// requires a working `ITerminalHandoff` COM server (see [`crate::handoff`]) whose
/// interface IIDs and marshaling are confirmed against the Windows SDK, plus a
/// live end-to-end launch verified on Windows 11. Until that is done, calling
/// this can break console launches. It is intentionally never invoked in the app.
#[cfg(windows)]
pub fn set_as_default_terminal() -> Result<(), RegistrationError> {
    write_delegation_values(WIXEN_TERMINAL_CLSID, CONHOST_CLSID)
}

/// Non-Windows stub: registration is only supported on Windows.
#[cfg(not(windows))]
pub fn set_as_default_terminal() -> Result<(), RegistrationError> {
    Err(RegistrationError::Unsupported)
}

/// Restore the default terminal to the pristine "let Windows decide" state.
///
/// Writes [`WINDOWS_DECIDE_CLSID`] (the null CLSID) to both `DelegationConsole`
/// and `DelegationTerminal`. This is the out-of-box Windows 11 default: Windows
/// resumes choosing the handler itself and always falls back to the built-in
/// console host, so console launches keep working. After this succeeds,
/// [`check_default_terminal_status`] reports `is_default == false`.
#[cfg(windows)]
pub fn restore_default_terminal() -> Result<(), RegistrationError> {
    write_delegation_values(WINDOWS_DECIDE_CLSID, WINDOWS_DECIDE_CLSID)
}

/// Non-Windows stub: registration is only supported on Windows.
#[cfg(not(windows))]
pub fn restore_default_terminal() -> Result<(), RegistrationError> {
    Err(RegistrationError::Unsupported)
}

/// Write both delegation values under `HKCU\Console\%%Startup`, creating the
/// key if it does not exist. This is the single Win32 side-effecting wrapper;
/// the value bytes it writes come from the pure [`encode_reg_sz`] helper.
#[cfg(windows)]
fn write_delegation_values(
    terminal_clsid: &str,
    console_clsid: &str,
) -> Result<(), RegistrationError> {
    use windows::Win32::System::Registry::{
        HKEY, HKEY_CURRENT_USER, KEY_SET_VALUE, REG_OPTION_NON_VOLATILE, RegCloseKey,
        RegCreateKeyExW,
    };
    use windows::core::{HSTRING, PWSTR};

    let subkey = HSTRING::from(CONSOLE_STARTUP_SUBKEY);
    let mut hkey = HKEY::default();

    let create_result = unsafe {
        RegCreateKeyExW(
            HKEY_CURRENT_USER,
            &subkey,
            Some(0),
            PWSTR::null(),
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            None,
            &mut hkey,
            None,
        )
    };

    if create_result.is_err() {
        return Err(RegistrationError::OpenKey {
            subkey: CONSOLE_STARTUP_SUBKEY.to_string(),
            code: create_result.0,
        });
    }

    let write_result = set_reg_sz(hkey, DELEGATION_TERMINAL_VALUE, terminal_clsid)
        .and_then(|()| set_reg_sz(hkey, DELEGATION_CONSOLE_VALUE, console_clsid));

    unsafe {
        let _ = RegCloseKey(hkey);
    }

    write_result
}

/// Write a single `REG_SZ` value into an already-open key.
#[cfg(windows)]
fn set_reg_sz(
    hkey: windows::Win32::System::Registry::HKEY,
    value_name: &'static str,
    value: &str,
) -> Result<(), RegistrationError> {
    use windows::Win32::System::Registry::{REG_SZ, RegSetValueExW};
    use windows::core::HSTRING;

    let name = HSTRING::from(value_name);
    let data = encode_reg_sz(value);

    let status = unsafe { RegSetValueExW(hkey, &name, Some(0), REG_SZ, Some(&data)) };

    if status.is_err() {
        return Err(RegistrationError::SetValue {
            value_name,
            code: status.0,
        });
    }
    Ok(())
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

    // --- Registry path / value-name constants ------------------------------

    #[test]
    fn console_startup_subkey_is_stable() {
        assert_eq!(CONSOLE_STARTUP_SUBKEY, r"Console\%%Startup");
    }

    #[test]
    fn delegation_value_names_are_stable() {
        assert_eq!(DELEGATION_TERMINAL_VALUE, "DelegationTerminal");
        assert_eq!(DELEGATION_CONSOLE_VALUE, "DelegationConsole");
    }

    // --- Well-known CLSIDs are valid GUIDs ---------------------------------

    #[test]
    fn well_known_clsids_are_valid_guids() {
        for clsid in [CONHOST_CLSID, WINDOWS_TERMINAL_CLSID, WINDOWS_DECIDE_CLSID] {
            assert_guid_shape(clsid);
        }
    }

    // --- DelegationTerminal classification ---------------------------------

    #[test]
    fn classify_recognizes_wixen() {
        assert_eq!(
            classify_delegation_terminal(WIXEN_TERMINAL_CLSID),
            TerminalHandler::Wixen
        );
    }

    #[test]
    fn classify_is_case_insensitive() {
        assert_eq!(
            classify_delegation_terminal(&WIXEN_TERMINAL_CLSID.to_uppercase()),
            TerminalHandler::Wixen
        );
    }

    #[test]
    fn classify_trims_surrounding_whitespace() {
        let padded = format!("  {WIXEN_TERMINAL_CLSID}\n");
        assert_eq!(
            classify_delegation_terminal(&padded),
            TerminalHandler::Wixen
        );
    }

    #[test]
    fn classify_recognizes_conhost() {
        assert_eq!(
            classify_delegation_terminal(CONHOST_CLSID),
            TerminalHandler::Conhost
        );
    }

    #[test]
    fn classify_recognizes_windows_terminal() {
        assert_eq!(
            classify_delegation_terminal(WINDOWS_TERMINAL_CLSID),
            TerminalHandler::WindowsTerminal
        );
    }

    #[test]
    fn classify_recognizes_windows_decide_null_guid() {
        assert_eq!(
            classify_delegation_terminal(WINDOWS_DECIDE_CLSID),
            TerminalHandler::WindowsDecide
        );
    }

    #[test]
    fn classify_falls_back_to_other() {
        let unknown = "{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}";
        assert_eq!(
            classify_delegation_terminal(unknown),
            TerminalHandler::Other
        );
    }

    // --- REG_SZ encoding ---------------------------------------------------

    #[test]
    fn encode_reg_sz_appends_null_terminator() {
        // 'A' == U+0041 -> LE bytes 0x41 0x00, then the UTF-16 null 0x00 0x00.
        assert_eq!(encode_reg_sz("A"), vec![0x41, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn encode_reg_sz_empty_string_is_just_terminator() {
        assert_eq!(encode_reg_sz(""), vec![0x00, 0x00]);
    }

    #[test]
    fn encode_reg_sz_byte_length_covers_terminator() {
        // Each UTF-16 code unit is 2 bytes; the CLSID plus a null terminator.
        let expected_bytes = (WIXEN_TERMINAL_CLSID.chars().count() + 1) * 2;
        assert_eq!(encode_reg_sz(WIXEN_TERMINAL_CLSID).len(), expected_bytes);
    }

    // --- Error construction ------------------------------------------------

    #[test]
    fn set_value_error_reports_value_name_and_code() {
        let error = RegistrationError::SetValue {
            value_name: DELEGATION_TERMINAL_VALUE,
            code: 5,
        };
        let message = error.to_string();
        assert!(message.contains("DelegationTerminal"));
        assert!(message.contains('5'));
    }

    #[test]
    fn open_key_error_reports_subkey_and_code() {
        let error = RegistrationError::OpenKey {
            subkey: CONSOLE_STARTUP_SUBKEY.to_string(),
            code: 5,
        };
        let message = error.to_string();
        assert!(message.contains(r"Console\%%Startup"));
        assert!(message.contains('5'));
    }

    // --- Restore-to-default value is not Wixen -----------------------------

    #[test]
    fn restore_target_is_not_wixen() {
        // Whatever we restore to must classify as "not the default terminal",
        // so check_default_terminal_status reports is_default == false afterwards.
        assert_ne!(
            classify_delegation_terminal(WINDOWS_DECIDE_CLSID),
            TerminalHandler::Wixen
        );
    }

    /// Assert a string has the canonical `{8-4-4-4-12}` hex GUID shape.
    fn assert_guid_shape(clsid: &str) {
        assert!(clsid.starts_with('{') && clsid.ends_with('}'), "{clsid}");
        let inner = &clsid[1..clsid.len() - 1];
        let groups: Vec<&str> = inner.split('-').collect();
        assert_eq!(groups.len(), 5, "{clsid}");
        for (group, expected_len) in groups.iter().zip([8, 4, 4, 4, 12]) {
            assert_eq!(group.len(), expected_len, "{clsid}");
            assert!(group.chars().all(|c| c.is_ascii_hexdigit()), "{clsid}");
        }
    }
}
