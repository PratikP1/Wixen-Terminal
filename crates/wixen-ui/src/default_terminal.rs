//! Check, register, and restore whether Wixen is the default terminal on Windows.
//!
//! Windows 11 lets a third-party terminal become the default through two
//! pieces of per-user registry state, both written by
//! [`register_default_terminal`] (no elevation required):
//!
//! 1. **COM registration** under `HKCU\Software\Classes` for the console
//!    server (the bundled `OpenConsole.exe`), the proxy/stub marshaler (the
//!    bundled `OpenConsoleProxy.dll`) with its proxied handoff interfaces,
//!    and Wixen's own terminal-handoff class (`wixen.exe --handoff`).
//! 2. **Delegation values** under `HKCU\Console\%%Startup`:
//!    `DelegationConsole` names the console server, `DelegationTerminal`
//!    names the terminal.
//!
//! When a console application launches, the inbox conhost reads
//! `DelegationConsole` and hands the connection to OpenConsole via
//! `IConsoleHandoff`; OpenConsole then `CoCreateInstance`s
//! `DelegationTerminal` (Wixen) and passes the ConPTY connection over
//! `ITerminalHandoff3`. Wixen reuses Microsoft's prebuilt OpenConsole
//! binaries and their compiled-in Release CLSIDs (microsoft/terminal @
//! `694db4c`), so those binaries must ship in Wixen's install directory.
//!
//! Failure degrades gracefully: if any hop fails at launch (missing binary,
//! no running handoff server), Windows falls back to a normal conhost window.
//! Registration cannot brick console launches — at worst Wixen is not used.
//! [`unregister_default_terminal`] / [`restore_default_terminal`] remove the
//! COM keys and hand the choice back to Windows.

use std::path::Path;

/// Wixen Terminal's COM CLSID for default terminal delegation.
///
/// This GUID is registered during installation and checked by Windows when
/// deciding which terminal to launch for console applications.
pub const WIXEN_TERMINAL_CLSID: &str = "{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}";

/// OpenConsole's console-handoff CLSID (microsoft/terminal @ `694db4c`, Release).
///
/// This CLSID is compiled into Microsoft's prebuilt `OpenConsole.exe`, which
/// Wixen bundles as its console server. It goes into `DelegationConsole` and
/// gets a `LocalServer32` registration pointing at the bundled executable.
pub const OPENCONSOLE_CLSID: &str = "{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}";

/// OpenConsoleProxy.dll's proxy/stub CLSID (microsoft/terminal @ `694db4c`, Release).
///
/// The proxy/stub marshaler for the console-handoff COM interfaces. It gets an
/// `InprocServer32` registration pointing at the bundled DLL, and each proxied
/// interface's `ProxyStubClsid32` names it.
pub const OPENCONSOLE_PROXY_CLSID: &str = "{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}";

/// `ITerminalHandoff3` — the interface OpenConsole calls on the terminal to
/// hand over a new ConPTY connection.
pub const ITERMINAL_HANDOFF3_IID: &str = "{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}";

/// `IConsoleHandoff` — the interface conhost calls on the delegated console
/// server (OpenConsole) to hand over a new console connection.
pub const ICONSOLE_HANDOFF_IID: &str = "{E686C757-9A35-4A1C-B3CE-0BCC8B5C69F4}";

/// `IDefaultTerminalMarker` — the marker interface Windows uses to probe
/// whether a registered terminal supports default-terminal handoff.
pub const IDEFAULT_TERMINAL_MARKER_IID: &str = "{746E6BC0-AB05-4E38-AB14-71E86763141F}";

/// The console-handoff interfaces marshaled by `OpenConsoleProxy.dll`, paired
/// with each interface's `NumMethods` registry string (method count including
/// the three `IUnknown` methods).
const PROXIED_INTERFACES: [(&str, &str); 3] = [
    (ITERMINAL_HANDOFF3_IID, "4"),
    (ICONSOLE_HANDOFF_IID, "4"),
    (IDEFAULT_TERMINAL_MARKER_IID, "3"),
];

/// File name of the bundled OpenConsole console server executable.
const OPENCONSOLE_EXE: &str = "OpenConsole.exe";

/// File name of the bundled OpenConsole proxy/stub marshaler DLL.
const OPENCONSOLE_PROXY_DLL: &str = "OpenConsoleProxy.dll";

/// File name of the Wixen Terminal executable.
const WIXEN_EXE: &str = "wixen.exe";

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

/// Registry subkey under `HKEY_CURRENT_USER` for a per-user COM class
/// registration.
fn clsid_subkey(clsid: &str) -> String {
    format!(r"Software\Classes\CLSID\{clsid}")
}

/// Registry subkey under `HKEY_CURRENT_USER` for a per-user COM interface
/// registration.
fn interface_subkey(iid: &str) -> String {
    format!(r"Software\Classes\Interface\{iid}")
}

/// Absolute path of a bundled file inside the install directory, as the
/// registry string to write.
fn install_path(install_dir: &Path, file_name: &str) -> String {
    install_dir.join(file_name).display().to_string()
}

/// The `LocalServer32` launch command for Wixen's handoff COM server: the
/// quoted executable path followed by the `--handoff` flag.
fn handoff_launch_command(install_dir: &Path) -> String {
    format!("\"{}\" --handoff", install_path(install_dir, WIXEN_EXE))
}

/// An error registering or unregistering the default terminal in the registry.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum RegistrationError {
    /// Opening or creating one of the registration keys failed.
    #[error("failed to open registry key HKCU\\{subkey}: Win32 error {code}")]
    OpenKey {
        /// The subkey path under `HKEY_CURRENT_USER`.
        subkey: String,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// Writing one of the registration values failed.
    #[error("failed to write registry value {value_name}: Win32 error {code}")]
    SetValue {
        /// The registry value name that failed to write (`(default)` for a
        /// key's default value).
        value_name: &'static str,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// Deleting one of the registration keys failed.
    #[error("failed to delete registry key HKCU\\{subkey}: Win32 error {code}")]
    DeleteKey {
        /// The subkey path under `HKEY_CURRENT_USER`.
        subkey: String,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// The install directory could not be resolved from the running executable.
    #[error("failed to resolve the install directory: {reason}")]
    InstallDir {
        /// Why the install directory could not be determined.
        reason: String,
    },
    /// Registration is only meaningful on Windows.
    #[error("default terminal registration is only supported on Windows")]
    Unsupported,
}

/// A single `REG_SZ` registry value to write under `HKEY_CURRENT_USER`.
///
/// `name` of `None` targets the key's default value. Building the full write
/// set as data (see [`registration_plan`]) keeps every path and payload pure
/// and unit-testable; only the thin Win32 wrappers touch the live registry.
#[derive(Debug, Clone, PartialEq, Eq)]
struct RegSzValue {
    /// The subkey path under `HKEY_CURRENT_USER`, created if absent.
    subkey: String,
    /// The value name, or `None` for the key's default value.
    name: Option<&'static str>,
    /// The string payload to store as `REG_SZ`.
    data: String,
}

/// The full per-user write set that makes Wixen the default terminal.
///
/// Order matters: the COM class and interface registrations come first, and
/// the `HKCU\Console\%%Startup` delegation values that point Windows at those
/// classes come last.
fn registration_plan(install_dir: &Path) -> Vec<RegSzValue> {
    let proxy_key = clsid_subkey(OPENCONSOLE_PROXY_CLSID);
    let mut plan = vec![
        RegSzValue {
            subkey: format!(r"{proxy_key}\InprocServer32"),
            name: None,
            data: install_path(install_dir, OPENCONSOLE_PROXY_DLL),
        },
        RegSzValue {
            subkey: format!(r"{proxy_key}\InprocServer32"),
            name: Some("ThreadingModel"),
            data: "Both".to_string(),
        },
    ];

    for (iid, num_methods) in PROXIED_INTERFACES {
        let interface_key = interface_subkey(iid);
        plan.push(RegSzValue {
            subkey: format!(r"{interface_key}\ProxyStubClsid32"),
            name: None,
            data: OPENCONSOLE_PROXY_CLSID.to_string(),
        });
        plan.push(RegSzValue {
            subkey: format!(r"{interface_key}\NumMethods"),
            name: None,
            data: num_methods.to_string(),
        });
    }

    plan.push(RegSzValue {
        subkey: format!(r"{}\LocalServer32", clsid_subkey(OPENCONSOLE_CLSID)),
        name: None,
        data: install_path(install_dir, OPENCONSOLE_EXE),
    });
    plan.push(RegSzValue {
        subkey: format!(r"{}\LocalServer32", clsid_subkey(WIXEN_TERMINAL_CLSID)),
        name: None,
        data: handoff_launch_command(install_dir),
    });
    plan.extend(delegation_plan(OPENCONSOLE_CLSID, WIXEN_TERMINAL_CLSID));
    plan
}

/// The two `HKCU\Console\%%Startup` delegation values naming the console
/// server and the terminal Windows should hand new console sessions to.
fn delegation_plan(console_clsid: &str, terminal_clsid: &str) -> [RegSzValue; 2] {
    let delegation = |name, clsid: &str| RegSzValue {
        subkey: CONSOLE_STARTUP_SUBKEY.to_string(),
        name: Some(name),
        data: clsid.to_string(),
    };
    [
        delegation(DELEGATION_CONSOLE_VALUE, console_clsid),
        delegation(DELEGATION_TERMINAL_VALUE, terminal_clsid),
    ]
}

/// The `HKEY_CURRENT_USER` subkeys to delete on unregistration: every COM
/// class and interface key [`registration_plan`] creates. The delegation
/// values are reset to [`WINDOWS_DECIDE_CLSID`] rather than removed.
fn removal_subkeys() -> [String; 6] {
    [
        clsid_subkey(OPENCONSOLE_PROXY_CLSID),
        interface_subkey(ITERMINAL_HANDOFF3_IID),
        interface_subkey(ICONSOLE_HANDOFF_IID),
        interface_subkey(IDEFAULT_TERMINAL_MARKER_IID),
        clsid_subkey(OPENCONSOLE_CLSID),
        clsid_subkey(WIXEN_TERMINAL_CLSID),
    ]
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

/// Register Wixen as the Windows default terminal, with the bundled console
/// server and proxy/stub binaries in `install_dir`.
///
/// `install_dir` must contain `OpenConsole.exe`, `OpenConsoleProxy.dll`, and
/// `wixen.exe`. Writes the full per-user registration described by
/// [`registration_plan`]: the proxy's `InprocServer32`, `ProxyStubClsid32` and
/// `NumMethods` for each proxied handoff interface, `LocalServer32` for both
/// OpenConsole and Wixen's `--handoff` launch, and finally the
/// `HKCU\Console\%%Startup` delegation values. Everything lands under
/// `HKEY_CURRENT_USER`, so no elevation is required.
#[cfg(windows)]
pub fn register_default_terminal(install_dir: &Path) -> Result<(), RegistrationError> {
    registration_plan(install_dir)
        .iter()
        .try_for_each(write_reg_sz_value)
}

/// Non-Windows stub: registration is only supported on Windows.
#[cfg(not(windows))]
pub fn register_default_terminal(_install_dir: &Path) -> Result<(), RegistrationError> {
    Err(RegistrationError::Unsupported)
}

/// Remove Wixen's default-terminal registration.
///
/// Deletes every COM class and interface key [`register_default_terminal`]
/// created (see [`removal_subkeys`]) and resets both `HKCU\Console\%%Startup`
/// delegation values to [`WINDOWS_DECIDE_CLSID`], the out-of-box "let Windows
/// decide" state. Keys that are already absent are skipped, so this is
/// idempotent and safe to call without a prior registration.
#[cfg(windows)]
pub fn unregister_default_terminal() -> Result<(), RegistrationError> {
    for subkey in removal_subkeys() {
        delete_registry_tree(&subkey)?;
    }
    delegation_plan(WINDOWS_DECIDE_CLSID, WINDOWS_DECIDE_CLSID)
        .iter()
        .try_for_each(write_reg_sz_value)
}

/// Non-Windows stub: registration is only supported on Windows.
#[cfg(not(windows))]
pub fn unregister_default_terminal() -> Result<(), RegistrationError> {
    Err(RegistrationError::Unsupported)
}

/// Register Wixen as the Windows default terminal, resolving the install
/// directory from the running executable's location.
///
/// Delegates to [`register_default_terminal`] with the directory containing
/// the current executable. The registration itself is complete and correct;
/// what remains before wiring this to a user-facing action is:
///
/// - the install directory must actually contain the bundled
///   `OpenConsole.exe` and `OpenConsoleProxy.dll` next to `wixen.exe`, and
/// - a live end-to-end console launch must be verified on Windows 11.
///
/// Failure degrades gracefully: if any handoff hop fails at console launch
/// (missing binary, no running handoff server), Windows falls back to a
/// normal conhost window. It cannot brick console launches — at worst Wixen
/// is simply not used. Invoking this remains main.rs's concern; the app does
/// not call it yet.
#[cfg(windows)]
pub fn set_as_default_terminal() -> Result<(), RegistrationError> {
    let exe = std::env::current_exe().map_err(|error| RegistrationError::InstallDir {
        reason: format!("could not locate the running executable: {error}"),
    })?;
    let install_dir = exe.parent().ok_or_else(|| RegistrationError::InstallDir {
        reason: format!("executable path {} has no parent directory", exe.display()),
    })?;
    register_default_terminal(install_dir)
}

/// Non-Windows stub: registration is only supported on Windows.
#[cfg(not(windows))]
pub fn set_as_default_terminal() -> Result<(), RegistrationError> {
    Err(RegistrationError::Unsupported)
}

/// Restore the default terminal to the pristine "let Windows decide" state.
///
/// Delegates to [`unregister_default_terminal`]: the COM class and interface
/// keys registration created are removed, and both `HKCU\Console\%%Startup`
/// delegation values are reset to [`WINDOWS_DECIDE_CLSID`] (the null CLSID).
/// Windows resumes choosing the handler itself and always falls back to the
/// built-in console host, so console launches keep working. After this
/// succeeds, [`check_default_terminal_status`] reports `is_default == false`.
#[cfg(windows)]
pub fn restore_default_terminal() -> Result<(), RegistrationError> {
    unregister_default_terminal()
}

/// Non-Windows stub: registration is only supported on Windows.
#[cfg(not(windows))]
pub fn restore_default_terminal() -> Result<(), RegistrationError> {
    Err(RegistrationError::Unsupported)
}

/// Write one [`RegSzValue`] under `HKEY_CURRENT_USER`, creating its subkey if
/// absent. This is the single Win32 write wrapper; every path and payload it
/// writes comes from the pure plan builders and [`encode_reg_sz`].
#[cfg(windows)]
fn write_reg_sz_value(value: &RegSzValue) -> Result<(), RegistrationError> {
    use windows::Win32::System::Registry::{
        HKEY, HKEY_CURRENT_USER, KEY_SET_VALUE, REG_OPTION_NON_VOLATILE, REG_SZ, RegCloseKey,
        RegCreateKeyExW, RegSetValueExW,
    };
    use windows::core::{HSTRING, PWSTR};

    let subkey = HSTRING::from(value.subkey.as_str());
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
            subkey: value.subkey.clone(),
            code: create_result.0,
        });
    }

    // An empty value name addresses the key's default value.
    let name = HSTRING::from(value.name.unwrap_or(""));
    let data = encode_reg_sz(&value.data);

    let status = unsafe { RegSetValueExW(hkey, &name, Some(0), REG_SZ, Some(&data)) };

    unsafe {
        let _ = RegCloseKey(hkey);
    }

    if status.is_err() {
        return Err(RegistrationError::SetValue {
            value_name: value.name.unwrap_or("(default)"),
            code: status.0,
        });
    }
    Ok(())
}

/// Delete a subkey tree under `HKEY_CURRENT_USER`. An already-absent key is
/// success, keeping unregistration idempotent. This is the single Win32
/// delete wrapper; the subkeys it removes come from [`removal_subkeys`].
#[cfg(windows)]
fn delete_registry_tree(subkey: &str) -> Result<(), RegistrationError> {
    use windows::Win32::Foundation::ERROR_FILE_NOT_FOUND;
    use windows::Win32::System::Registry::{HKEY_CURRENT_USER, RegDeleteTreeW};
    use windows::core::HSTRING;

    let subkey_w = HSTRING::from(subkey);
    let status = unsafe { RegDeleteTreeW(HKEY_CURRENT_USER, &subkey_w) };

    if status.is_err() && status != ERROR_FILE_NOT_FOUND {
        return Err(RegistrationError::DeleteKey {
            subkey: subkey.to_string(),
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
        let other_clsid = "{DEADBEEF-0000-4000-8000-000000000000}";
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
        for clsid in [
            CONHOST_CLSID,
            WINDOWS_TERMINAL_CLSID,
            WINDOWS_DECIDE_CLSID,
            OPENCONSOLE_CLSID,
            OPENCONSOLE_PROXY_CLSID,
        ] {
            assert_guid_shape(clsid);
        }
    }

    // --- OpenConsole handoff constants -------------------------------------

    #[test]
    fn openconsole_clsids_match_upstream_release_values() {
        assert_eq!(OPENCONSOLE_CLSID, "{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}");
        assert_eq!(
            OPENCONSOLE_PROXY_CLSID,
            "{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}"
        );
    }

    #[test]
    fn proxied_interfaces_cover_handoff_console_and_marker() {
        assert_eq!(
            PROXIED_INTERFACES,
            [
                ("{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}", "4"),
                ("{E686C757-9A35-4A1C-B3CE-0BCC8B5C69F4}", "4"),
                ("{746E6BC0-AB05-4E38-AB14-71E86763141F}", "3"),
            ]
        );
    }

    #[test]
    fn proxied_interface_iids_are_valid_guids() {
        for (iid, _) in PROXIED_INTERFACES {
            assert_guid_shape(iid);
        }
    }

    // --- Registry subkey builders ------------------------------------------

    #[test]
    fn clsid_subkey_lives_under_per_user_classes() {
        assert_eq!(
            clsid_subkey(OPENCONSOLE_PROXY_CLSID),
            r"Software\Classes\CLSID\{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}"
        );
    }

    #[test]
    fn interface_subkey_lives_under_per_user_classes() {
        assert_eq!(
            interface_subkey(ITERMINAL_HANDOFF3_IID),
            r"Software\Classes\Interface\{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}"
        );
    }

    // --- Install-directory path builders -----------------------------------

    #[cfg(windows)]
    #[test]
    fn install_path_joins_file_onto_install_dir() {
        let dir = Path::new(r"C:\Program Files\Wixen");
        assert_eq!(
            install_path(dir, OPENCONSOLE_PROXY_DLL),
            r"C:\Program Files\Wixen\OpenConsoleProxy.dll"
        );
        assert_eq!(
            install_path(dir, OPENCONSOLE_EXE),
            r"C:\Program Files\Wixen\OpenConsole.exe"
        );
    }

    #[cfg(windows)]
    #[test]
    fn handoff_launch_command_quotes_exe_and_appends_flag() {
        let dir = Path::new(r"C:\Program Files\Wixen");
        assert_eq!(
            handoff_launch_command(dir),
            r#""C:\Program Files\Wixen\wixen.exe" --handoff"#
        );
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
        let unknown = "{DEADBEEF-0000-4000-8000-000000000000}";
        assert_eq!(
            classify_delegation_terminal(unknown),
            TerminalHandler::Other
        );
    }

    #[test]
    fn classify_treats_openconsole_as_other() {
        // OpenConsole is a console server (a DelegationConsole value), never a
        // DelegationTerminal handler, so the terminal classifier must not
        // claim it as a known terminal.
        assert_eq!(
            classify_delegation_terminal(OPENCONSOLE_CLSID),
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

    // --- Registration plan -------------------------------------------------

    #[cfg(windows)]
    #[test]
    fn registration_plan_registers_proxy_inproc_server() {
        let plan = registration_plan(Path::new(r"C:\Wixen"));
        let subkey =
            r"Software\Classes\CLSID\{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}\InprocServer32";

        assert!(plan.contains(&RegSzValue {
            subkey: subkey.into(),
            name: None,
            data: r"C:\Wixen\OpenConsoleProxy.dll".into(),
        }));
        assert!(plan.contains(&RegSzValue {
            subkey: subkey.into(),
            name: Some("ThreadingModel"),
            data: "Both".into(),
        }));
    }

    #[test]
    fn registration_plan_registers_each_proxied_interface() {
        let plan = registration_plan(Path::new("dir"));

        for (iid, num_methods) in PROXIED_INTERFACES {
            let base = interface_subkey(iid);
            assert!(plan.contains(&RegSzValue {
                subkey: format!(r"{base}\ProxyStubClsid32"),
                name: None,
                data: OPENCONSOLE_PROXY_CLSID.into(),
            }));
            assert!(plan.contains(&RegSzValue {
                subkey: format!(r"{base}\NumMethods"),
                name: None,
                data: num_methods.into(),
            }));
        }
    }

    #[cfg(windows)]
    #[test]
    fn registration_plan_registers_openconsole_local_server() {
        let plan = registration_plan(Path::new(r"C:\Wixen"));

        assert!(
            plan.contains(&RegSzValue {
                subkey:
                    r"Software\Classes\CLSID\{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}\LocalServer32"
                        .into(),
                name: None,
                data: r"C:\Wixen\OpenConsole.exe".into(),
            })
        );
    }

    #[cfg(windows)]
    #[test]
    fn registration_plan_registers_wixen_handoff_local_server() {
        let plan = registration_plan(Path::new(r"C:\Wixen"));

        assert!(
            plan.contains(&RegSzValue {
                subkey:
                    r"Software\Classes\CLSID\{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}\LocalServer32"
                        .into(),
                name: None,
                data: r#""C:\Wixen\wixen.exe" --handoff"#.into(),
            })
        );
    }

    #[test]
    fn registration_plan_ends_with_delegation_values() {
        let plan = registration_plan(Path::new("dir"));

        // COM classes must be registered before the delegation values point
        // Windows at them, so delegation comes last.
        assert_eq!(
            &plan[plan.len() - 2..],
            delegation_plan(OPENCONSOLE_CLSID, WIXEN_TERMINAL_CLSID).as_slice()
        );
    }

    #[test]
    fn registration_plan_writes_exactly_twelve_values() {
        // 2 proxy + 3 interfaces x 2 + 2 local servers + 2 delegation values.
        assert_eq!(registration_plan(Path::new("dir")).len(), 12);
    }

    // --- Delegation plan ---------------------------------------------------

    #[test]
    fn delegation_plan_targets_console_startup_key() {
        let [console, terminal] = delegation_plan(OPENCONSOLE_CLSID, WIXEN_TERMINAL_CLSID);

        assert_eq!(
            console,
            RegSzValue {
                subkey: r"Console\%%Startup".into(),
                name: Some("DelegationConsole"),
                data: OPENCONSOLE_CLSID.into(),
            }
        );
        assert_eq!(
            terminal,
            RegSzValue {
                subkey: r"Console\%%Startup".into(),
                name: Some("DelegationTerminal"),
                data: WIXEN_TERMINAL_CLSID.into(),
            }
        );
    }

    #[test]
    fn unregister_delegation_hands_choice_back_to_windows() {
        for value in delegation_plan(WINDOWS_DECIDE_CLSID, WINDOWS_DECIDE_CLSID) {
            assert_eq!(value.data, WINDOWS_DECIDE_CLSID);
        }
    }

    // --- Removal list ------------------------------------------------------

    #[test]
    fn removal_subkeys_cover_every_registered_class_and_interface() {
        assert_eq!(
            removal_subkeys(),
            [
                r"Software\Classes\CLSID\{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}".to_string(),
                r"Software\Classes\Interface\{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}".to_string(),
                r"Software\Classes\Interface\{E686C757-9A35-4A1C-B3CE-0BCC8B5C69F4}".to_string(),
                r"Software\Classes\Interface\{746E6BC0-AB05-4E38-AB14-71E86763141F}".to_string(),
                r"Software\Classes\CLSID\{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}".to_string(),
                r"Software\Classes\CLSID\{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}".to_string(),
            ]
        );
    }

    #[test]
    fn removal_subkeys_match_registration_plan_roots() {
        let removals = removal_subkeys();

        // Every COM subkey the plan writes must live under a removal root, so
        // unregistration leaves nothing behind. Delegation values are reset,
        // not removed, so they are exempt.
        for value in registration_plan(Path::new("dir")) {
            if value.subkey == CONSOLE_STARTUP_SUBKEY {
                continue;
            }
            assert!(
                removals
                    .iter()
                    .any(|root| value.subkey.starts_with(root.as_str())),
                "no removal root covers {}",
                value.subkey
            );
        }
    }

    // --- Error construction ------------------------------------------------

    #[test]
    fn delete_key_error_reports_subkey_and_code() {
        let error = RegistrationError::DeleteKey {
            subkey: clsid_subkey(OPENCONSOLE_PROXY_CLSID),
            code: 5,
        };
        let message = error.to_string();
        assert!(message.contains(OPENCONSOLE_PROXY_CLSID));
        assert!(message.contains('5'));
    }

    #[test]
    fn install_dir_error_reports_reason() {
        let error = RegistrationError::InstallDir {
            reason: "executable path has no parent directory".into(),
        };
        assert!(
            error
                .to_string()
                .contains("executable path has no parent directory")
        );
    }

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
