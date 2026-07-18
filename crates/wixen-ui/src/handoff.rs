//! Windows console-handoff COM server: the piece that lets Wixen actually
//! *be* the default terminal.
//!
//! When Wixen is registered in `HKCU\Console\%%Startup` (see
//! [`crate::default_terminal`]), the console host `CoCreateInstance`s
//! [`crate::default_terminal::WIXEN_TERMINAL_CLSID`] and calls the
//! `ITerminalHandoff` family of interfaces to pass a freshly created ConPTY
//! connection to the terminal. This module provides:
//!
//! - The `ITerminalHandoff` / `ITerminalHandoff2` COM interface definitions
//!   and an implementation that converts an incoming handoff into a plain-data
//!   [`HandoffRequest`] delivered over a channel (no COM types leak out).
//! - [`HandoffServer`]: registers the class factory for the CLSID via
//!   `CoRegisterClassObject` and revokes it on drop.
//! - [`register_handoff_server`] / [`unregister_handoff_server`]: the
//!   `HKCU\Software\Classes\CLSID\{...}\LocalServer32` registration that makes
//!   COM launch `wixen.exe` when no instance is running.
//! - [`is_handoff_launch`]: startup detection for the COM launch flags.
//!
//! # External contract honesty
//!
//! The interface IIDs, method vtable order, and `TERMINAL_STARTUP_INFO` layout
//! are defined by Microsoft, not by us. They were transcribed on 2026-07-18
//! from `src/host/proxy/ITerminalHandoff.idl` in the `microsoft/terminal`
//! repository (`main` branch). Every such value is marked with a `// VERIFY:`
//! comment and must be re-confirmed against the Windows SDK / microsoft/terminal
//! sources before `set_as_default_terminal` is un-gated. Getting any of them
//! wrong does not crash — it silently produces a server the console host can
//! never talk to, which breaks console launches system-wide.

use crate::default_terminal::WIXEN_TERMINAL_CLSID;

// ---------------------------------------------------------------------------
// Launch detection
// ---------------------------------------------------------------------------

/// Wixen's own marker flag placed in the registered `LocalServer32` command
/// line, so a handoff launch is recognisable even if the COM-appended switch
/// ever changes shape.
pub const HANDOFF_COMMAND_LINE_FLAG: &str = "--handoff";

/// The switch COM appends when launching a registered local server.
///
/// // VERIFY: standard COM appends `-Embedding`; confirm the console host's
/// activation path does not use a different switch before un-gating.
pub const COM_EMBEDDING_SWITCH: &str = "-Embedding";

/// Returns `true` when the given arguments (excluding the executable name,
/// i.e. `std::env::args().skip(1)`) indicate this process was launched by COM
/// for a console handoff rather than by the user.
pub fn is_handoff_launch<I, S>(args: I) -> bool
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
{
    args.into_iter().any(|arg| is_handoff_switch(arg.as_ref()))
}

/// Whether a single argument is one of the handoff launch switches.
fn is_handoff_switch(arg: &str) -> bool {
    if arg == HANDOFF_COMMAND_LINE_FLAG {
        return true;
    }
    let Some(switch_name) = arg.strip_prefix(['-', '/']) else {
        return false;
    };
    switch_name.eq_ignore_ascii_case("Embedding")
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

/// An error setting up or registering the console-handoff COM server.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum HandoffError {
    /// A CLSID string did not have the canonical `{8-4-4-4-12}` hex shape.
    #[error("invalid CLSID {clsid:?}: {reason}")]
    InvalidClsid {
        /// The offending CLSID string.
        clsid: String,
        /// Why it was rejected.
        reason: &'static str,
    },
    /// The executable path cannot be safely quoted into a registry command.
    #[error("executable path {path:?} contains a double quote and cannot be registered")]
    UnquotableExePath {
        /// The offending path.
        path: String,
    },
    /// The current executable path could not be determined.
    #[error("failed to locate the current executable: {details}")]
    ExecutableUnavailable {
        /// Underlying OS error description.
        details: String,
    },
    /// Opening or creating a registry key failed.
    #[error("failed to open registry key HKCU\\{subkey}: Win32 error {code}")]
    OpenKey {
        /// The subkey path under `HKEY_CURRENT_USER`.
        subkey: String,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// Writing a registry value failed.
    #[error("failed to write registry value under HKCU\\{subkey}: Win32 error {code}")]
    SetValue {
        /// The subkey path under `HKEY_CURRENT_USER`.
        subkey: String,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// Deleting the registration key failed.
    #[error("failed to delete registry key HKCU\\{subkey}: Win32 error {code}")]
    DeleteKey {
        /// The subkey path under `HKEY_CURRENT_USER`.
        subkey: String,
        /// The Win32 error code returned by the registry API.
        code: u32,
    },
    /// `CoRegisterClassObject` failed.
    #[error("CoRegisterClassObject failed with HRESULT 0x{hresult:08X}")]
    ClassRegistration {
        /// The failing HRESULT, as an unsigned value for hex display.
        hresult: u32,
    },
    /// Console handoff is only meaningful on Windows.
    #[error("console handoff is only supported on Windows")]
    Unsupported,
}

// ---------------------------------------------------------------------------
// CLSID parsing
// ---------------------------------------------------------------------------

/// Parse a canonical `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}` CLSID string
/// (braces optional) into its 128-bit value, in textual order — the same order
/// `GUID::from_u128` expects.
fn parse_guid_u128(clsid: &str) -> Result<u128, HandoffError> {
    let invalid = |reason: &'static str| HandoffError::InvalidClsid {
        clsid: clsid.to_string(),
        reason,
    };

    let has_open = clsid.starts_with('{');
    let has_close = clsid.ends_with('}');
    let inner = match (has_open, has_close) {
        (true, true) => &clsid[1..clsid.len() - 1],
        (false, false) => clsid,
        _ => return Err(invalid("mismatched braces")),
    };

    const GROUP_LENGTHS: [usize; 5] = [8, 4, 4, 4, 12];
    let groups: Vec<&str> = inner.split('-').collect();
    if groups.len() != GROUP_LENGTHS.len() {
        return Err(invalid("expected 5 dash-separated groups"));
    }

    let mut value: u128 = 0;
    for (group, expected_length) in groups.iter().zip(GROUP_LENGTHS) {
        if group.len() != expected_length {
            return Err(invalid("group has wrong length"));
        }
        for digit in group.chars() {
            let digit = digit
                .to_digit(16)
                .ok_or_else(|| invalid("non-hex character"))?;
            value = (value << 4) | u128::from(digit);
        }
    }
    Ok(value)
}

// ---------------------------------------------------------------------------
// Registry registration (path construction — pure and tested)
// ---------------------------------------------------------------------------

/// Per-user COM class registration root under `HKEY_CURRENT_USER`.
const CLASSES_CLSID_SUBKEY: &str = r"Software\Classes\CLSID";

/// Subkey holding the local-server launch command.
const LOCAL_SERVER32_KEY: &str = "LocalServer32";

/// Friendly name written as the CLSID key's default value.
const HANDOFF_SERVER_FRIENDLY_NAME: &str = "Wixen Terminal";

/// Registry path (under `HKEY_CURRENT_USER`) of the COM class key for `clsid`.
pub fn clsid_registry_path(clsid: &str) -> String {
    format!(r"{CLASSES_CLSID_SUBKEY}\{clsid}")
}

/// Registry path (under `HKEY_CURRENT_USER`) of the `LocalServer32` key for
/// `clsid`.
pub fn local_server_registry_path(clsid: &str) -> String {
    format!(r"{}\{LOCAL_SERVER32_KEY}", clsid_registry_path(clsid))
}

/// The command line stored in `LocalServer32`: the quoted executable path plus
/// [`HANDOFF_COMMAND_LINE_FLAG`]. COM appends [`COM_EMBEDDING_SWITCH`] itself
/// at launch time.
///
/// The path is always wrapped in double quotes so paths containing spaces
/// survive `CreateProcess` tokenisation. A path that itself contains a double
/// quote cannot be represented and is rejected.
pub fn local_server_command(exe_path: &str) -> Result<String, HandoffError> {
    if exe_path.contains('"') {
        return Err(HandoffError::UnquotableExePath {
            path: exe_path.to_string(),
        });
    }
    Ok(format!(r#""{exe_path}" {HANDOFF_COMMAND_LINE_FLAG}"#))
}

/// Encode a string as a null-terminated UTF-16LE byte buffer for a `REG_SZ`
/// value, as `RegSetValueExW` expects.
fn encode_reg_sz(value: &str) -> Vec<u8> {
    value
        .encode_utf16()
        .chain(std::iter::once(0))
        .flat_map(u16::to_le_bytes)
        .collect()
}

// ---------------------------------------------------------------------------
// The plain-data handoff request delivered to the application
// ---------------------------------------------------------------------------

/// A raw OS handle value (pointer-sized), kept as a plain integer so consumers
/// need no COM or Win32 types.
pub type RawHandleValue = isize;

/// `STARTF_USESHOWWINDOW` from `STARTUPINFOW.dwFlags` — when set,
/// `wShowWindow` carries a meaningful `SW_*` command.
/// Documented Win32 constant (processthreadsapi.h).
const STARTF_USESHOWWINDOW: u32 = 0x0000_0001;

/// Startup metadata the console host forwards from the client application's
/// `STARTUPINFO`, already decoded into plain Rust data.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct HandoffStartupInfo {
    /// Window title requested by the client, if any.
    pub title: Option<String>,
    /// Icon path requested by the client, if any.
    pub icon_path: Option<String>,
    /// Icon index within `icon_path`.
    pub icon_index: i32,
    /// Requested window X position (`STARTUPINFO.dwX`).
    pub x: u32,
    /// Requested window Y position (`STARTUPINFO.dwY`).
    pub y: u32,
    /// Requested window width in pixels (`STARTUPINFO.dwXSize`).
    pub x_size: u32,
    /// Requested window height in pixels (`STARTUPINFO.dwYSize`).
    pub y_size: u32,
    /// Requested buffer width in characters (`STARTUPINFO.dwXCountChars`).
    pub x_count_chars: u32,
    /// Requested buffer height in characters (`STARTUPINFO.dwYCountChars`).
    pub y_count_chars: u32,
    /// Initial text attributes (`STARTUPINFO.dwFillAttribute`).
    pub fill_attribute: u32,
    /// Raw `STARTUPINFO.dwFlags` bits.
    pub flags: u32,
    /// Raw `STARTUPINFO.wShowWindow` value; meaningful only when
    /// [`Self::show_window`] returns `Some`.
    pub show_window_raw: u16,
}

impl HandoffStartupInfo {
    /// The `SW_*` show command the client asked for, or `None` when the
    /// `STARTF_USESHOWWINDOW` flag is absent and `show_window_raw` is noise.
    pub fn show_window(&self) -> Option<u16> {
        (self.flags & STARTF_USESHOWWINDOW != 0).then_some(self.show_window_raw)
    }
}

/// A console handoff received from the console host, reduced to plain data.
///
/// All handles were duplicated into this process by COM handle marshalling and
/// are owned by the receiver, which must eventually close them (typically by
/// handing them to the ConPTY/session layer).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HandoffRequest {
    /// Pipe the terminal reads client input *requests* from (VT input pipe).
    pub input_pipe: RawHandleValue,
    /// Pipe the terminal writes rendered output to (VT output pipe).
    pub output_pipe: RawHandleValue,
    /// Signal pipe for resize/close notifications.
    pub signal_pipe: RawHandleValue,
    /// Reference handle keeping the console session alive.
    pub reference_handle: RawHandleValue,
    /// Process handle of the console server (conhost/OpenConsole).
    pub server_process: RawHandleValue,
    /// Process handle of the client application being launched.
    pub client_process: RawHandleValue,
    /// Startup metadata forwarded from the client's `STARTUPINFO`.
    pub startup: HandoffStartupInfo,
}

// ---------------------------------------------------------------------------
// COM server (Windows only)
// ---------------------------------------------------------------------------

#[cfg(windows)]
pub use com_server::{HandoffServer, register_handoff_server, unregister_handoff_server};

/// Non-Windows stub: console handoff is only supported on Windows.
#[cfg(not(windows))]
pub fn register_handoff_server() -> Result<(), HandoffError> {
    Err(HandoffError::Unsupported)
}

/// Non-Windows stub: console handoff is only supported on Windows.
#[cfg(not(windows))]
pub fn unregister_handoff_server() -> Result<(), HandoffError> {
    Err(HandoffError::Unsupported)
}

#[cfg(windows)]
// COM method names and arities follow the external IDL contract verbatim;
// `EstablishPtyHandoff` on `ITerminalHandoff2` takes 8 arguments by design.
#[allow(non_snake_case, clippy::too_many_arguments)]
mod com_server {
    use super::*;
    use crossbeam_channel::Sender;
    use windows::Win32::Foundation::{
        CLASS_E_NOAGGREGATION, E_FAIL, ERROR_FILE_NOT_FOUND, HANDLE, S_OK,
    };
    use windows::Win32::System::Com::{
        CLSCTX_LOCAL_SERVER, CoRegisterClassObject, CoRevokeClassObject, IClassFactory,
        IClassFactory_Impl, REGCLS_MULTIPLEUSE,
    };
    use windows::Win32::System::Registry::{
        HKEY, HKEY_CURRENT_USER, KEY_SET_VALUE, REG_OPTION_NON_VOLATILE, REG_SZ, RegCloseKey,
        RegCreateKeyExW, RegDeleteTreeW, RegSetValueExW,
    };
    use windows::core::{
        BOOL, BSTR, GUID, HRESULT, HSTRING, IUnknown, IUnknown_Vtbl, Interface, PCWSTR, PWSTR, Ref,
        implement, interface,
    };

    // -- The external contract: transcribed from microsoft/terminal ---------
    //
    // Source: src/host/proxy/ITerminalHandoff.idl (microsoft/terminal, main
    // branch, transcribed 2026-07-18). `ITerminalHandoff3` (out-param pipes,
    // IID {6F23DA90-15C5-4203-9DB0-64E73F1B1B00}) is deliberately NOT
    // implemented: answering its QueryInterface and then failing the call
    // would break the launch, whereas a failed QI lets the console host fall
    // back to `ITerminalHandoff2`.
    // VERIFY: confirm the console host actually falls back across the
    // interface versions in that order before un-gating.

    /// `TERMINAL_STARTUP_INFO` from `ITerminalHandoff.idl`.
    ///
    /// The two `psz*` fields are BSTRs owned by the caller (`[in]` semantics):
    /// they are borrowed during the call and must not be freed here — hence
    /// raw pointers, not owning `BSTR` wrappers.
    ///
    /// // VERIFY: field order and types transcribed from the IDL; confirm
    /// against the exact IDL revision matching the OS builds targeted, and
    /// that by-value struct passing here matches the MIDL-generated x64 ABI.
    #[repr(C)]
    #[derive(Clone, Copy)]
    #[allow(non_camel_case_types, non_snake_case)]
    pub struct TERMINAL_STARTUP_INFO {
        /// `BSTR pszTitle` — window title, may be null.
        pub pszTitle: *mut u16,
        /// `BSTR pszIconPath` — icon path, may be null.
        pub pszIconPath: *mut u16,
        /// `LONG iconIndex`.
        pub iconIndex: i32,
        /// `DWORD dwX`.
        pub dwX: u32,
        /// `DWORD dwY`.
        pub dwY: u32,
        /// `DWORD dwXSize`.
        pub dwXSize: u32,
        /// `DWORD dwYSize`.
        pub dwYSize: u32,
        /// `DWORD dwXCountChars`.
        pub dwXCountChars: u32,
        /// `DWORD dwYCountChars`.
        pub dwYCountChars: u32,
        /// `DWORD dwFillAttribute`.
        pub dwFillAttribute: u32,
        /// `DWORD dwFlags`.
        pub dwFlags: u32,
        /// `WORD wShowWindow`.
        pub wShowWindow: u16,
    }

    /// `ITerminalHandoff` — the original (deprecated) handoff interface.
    ///
    /// // VERIFY: IID transcribed from ITerminalHandoff.idl
    /// (`59D55CCE-FC8A-48B4-ACE8-0A9286C6557F`); confirm against the Windows
    /// SDK / microsoft/terminal before shipping.
    #[interface("59D55CCE-FC8A-48B4-ACE8-0A9286C6557F")]
    pub unsafe trait ITerminalHandoff: IUnknown {
        /// Receives the ConPTY connection. Parameter order is the vtable
        /// contract: in, out, signal, ref, server, client.
        unsafe fn EstablishPtyHandoff(
            &self,
            input: HANDLE,
            output: HANDLE,
            signal: HANDLE,
            reference: HANDLE,
            server: HANDLE,
            client: HANDLE,
        ) -> HRESULT;
    }

    /// `ITerminalHandoff2` — adds `TERMINAL_STARTUP_INFO`.
    ///
    /// // VERIFY: IID transcribed from ITerminalHandoff.idl
    /// (`AA6B364F-4A50-4176-9002-0AE755E7B5EF`); confirm against the Windows
    /// SDK / microsoft/terminal before shipping.
    #[interface("AA6B364F-4A50-4176-9002-0AE755E7B5EF")]
    pub unsafe trait ITerminalHandoff2: IUnknown {
        /// Receives the ConPTY connection plus client startup metadata.
        /// // VERIFY: `startup_info` is `[in] TERMINAL_STARTUP_INFO` by value
        /// in the IDL; confirm the marshalled ABI matches this signature.
        unsafe fn EstablishPtyHandoff(
            &self,
            input: HANDLE,
            output: HANDLE,
            signal: HANDLE,
            reference: HANDLE,
            server: HANDLE,
            client: HANDLE,
            startup_info: TERMINAL_STARTUP_INFO,
        ) -> HRESULT;
    }

    /// The GUID form of [`WIXEN_TERMINAL_CLSID`].
    pub(super) fn wixen_class_guid() -> Result<GUID, HandoffError> {
        parse_guid_u128(WIXEN_TERMINAL_CLSID).map(GUID::from_u128)
    }

    /// Decode a caller-owned (`[in]`) BSTR without taking ownership.
    ///
    /// # Safety
    /// `bstr` must be null or a valid BSTR allocation (it is read through its
    /// length prefix) that outlives this call.
    pub(super) unsafe fn borrowed_bstr_to_string(bstr: *mut u16) -> Option<String> {
        if bstr.is_null() {
            return None;
        }
        // ManuallyDrop: the BSTR belongs to the COM caller; we only read it.
        let borrowed = std::mem::ManuallyDrop::new(unsafe { BSTR::from_raw(bstr) });
        let text = borrowed.to_string();
        (!text.is_empty()).then_some(text)
    }

    /// Convert the raw COM struct into plain-data [`HandoffStartupInfo`].
    ///
    /// # Safety
    /// The `psz*` pointers in `raw` must be null or valid BSTRs that outlive
    /// this call.
    pub(super) unsafe fn decode_startup_info(raw: &TERMINAL_STARTUP_INFO) -> HandoffStartupInfo {
        HandoffStartupInfo {
            title: unsafe { borrowed_bstr_to_string(raw.pszTitle) },
            icon_path: unsafe { borrowed_bstr_to_string(raw.pszIconPath) },
            icon_index: raw.iconIndex,
            x: raw.dwX,
            y: raw.dwY,
            x_size: raw.dwXSize,
            y_size: raw.dwYSize,
            x_count_chars: raw.dwXCountChars,
            y_count_chars: raw.dwYCountChars,
            fill_attribute: raw.dwFillAttribute,
            flags: raw.dwFlags,
            show_window_raw: raw.wShowWindow,
        }
    }

    // -- The handoff COM object ----------------------------------------------

    /// COM object the console host calls; forwards each handoff as a plain
    /// [`HandoffRequest`] over a channel so the rest of the app never touches
    /// COM types.
    #[implement(ITerminalHandoff, ITerminalHandoff2)]
    struct PtyHandoffCallback {
        requests: Sender<HandoffRequest>,
    }

    impl PtyHandoffCallback {
        /// Package the raw handles and startup metadata and deliver them.
        ///
        /// The `[in, system_handle]` handles were duplicated into this process
        /// by COM handle marshalling; ownership passes to the channel receiver.
        fn deliver(
            &self,
            input: HANDLE,
            output: HANDLE,
            signal: HANDLE,
            reference: HANDLE,
            server: HANDLE,
            client: HANDLE,
            startup: HandoffStartupInfo,
        ) -> HRESULT {
            let request = HandoffRequest {
                input_pipe: input.0 as RawHandleValue,
                output_pipe: output.0 as RawHandleValue,
                signal_pipe: signal.0 as RawHandleValue,
                reference_handle: reference.0 as RawHandleValue,
                server_process: server.0 as RawHandleValue,
                client_process: client.0 as RawHandleValue,
                startup,
            };
            match self.requests.send(request) {
                Ok(()) => S_OK,
                Err(_) => {
                    tracing::error!("handoff request receiver is gone; rejecting handoff");
                    E_FAIL
                }
            }
        }
    }

    #[allow(non_snake_case)]
    impl ITerminalHandoff_Impl for PtyHandoffCallback_Impl {
        unsafe fn EstablishPtyHandoff(
            &self,
            input: HANDLE,
            output: HANDLE,
            signal: HANDLE,
            reference: HANDLE,
            server: HANDLE,
            client: HANDLE,
        ) -> HRESULT {
            self.deliver(
                input,
                output,
                signal,
                reference,
                server,
                client,
                HandoffStartupInfo::default(),
            )
        }
    }

    #[allow(non_snake_case)]
    impl ITerminalHandoff2_Impl for PtyHandoffCallback_Impl {
        unsafe fn EstablishPtyHandoff(
            &self,
            input: HANDLE,
            output: HANDLE,
            signal: HANDLE,
            reference: HANDLE,
            server: HANDLE,
            client: HANDLE,
            startup_info: TERMINAL_STARTUP_INFO,
        ) -> HRESULT {
            let startup = unsafe { decode_startup_info(&startup_info) };
            self.deliver(input, output, signal, reference, server, client, startup)
        }
    }

    // -- The class factory ----------------------------------------------------

    /// Class factory COM activates for [`WIXEN_TERMINAL_CLSID`].
    #[implement(IClassFactory)]
    struct HandoffClassFactory {
        requests: Sender<HandoffRequest>,
    }

    #[allow(non_snake_case)]
    impl IClassFactory_Impl for HandoffClassFactory_Impl {
        fn CreateInstance(
            &self,
            outer: Ref<'_, IUnknown>,
            iid: *const GUID,
            object: *mut *mut core::ffi::c_void,
        ) -> windows::core::Result<()> {
            if !outer.is_null() {
                return Err(CLASS_E_NOAGGREGATION.into());
            }
            let callback: IUnknown = PtyHandoffCallback {
                requests: self.requests.clone(),
            }
            .into();
            unsafe { callback.query(iid, object) }.ok()
        }

        fn LockServer(&self, _lock: BOOL) -> windows::core::Result<()> {
            Ok(())
        }
    }

    // -- The running server ----------------------------------------------------

    /// A live class-factory registration for the handoff CLSID.
    ///
    /// While this value is alive, COM activation requests for
    /// [`WIXEN_TERMINAL_CLSID`] in this process resolve to the handoff class
    /// factory; each incoming handoff arrives as a [`HandoffRequest`] on the
    /// channel passed to [`HandoffServer::start`]. Dropping the server revokes
    /// the registration.
    ///
    /// The calling thread must have COM initialized. `REGCLS_MULTIPLEUSE` is
    /// used so one running Wixen instance can accept any number of handoffs
    /// (each typically becoming a new window or tab).
    pub struct HandoffServer {
        registration_cookie: u32,
    }

    impl HandoffServer {
        /// Register the class factory; incoming handoffs are sent to `requests`.
        pub fn start(requests: Sender<HandoffRequest>) -> Result<Self, HandoffError> {
            let class_id = wixen_class_guid()?;
            let factory: IClassFactory = HandoffClassFactory { requests }.into();
            let registration_cookie = unsafe {
                CoRegisterClassObject(&class_id, &factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE)
            }
            .map_err(|error| HandoffError::ClassRegistration {
                hresult: error.code().0 as u32,
            })?;
            tracing::info!(
                cookie = registration_cookie,
                "handoff class factory registered"
            );
            Ok(Self {
                registration_cookie,
            })
        }
    }

    impl Drop for HandoffServer {
        fn drop(&mut self) {
            // Best-effort revoke; failure only matters at process exit anyway.
            let revoke_result = unsafe { CoRevokeClassObject(self.registration_cookie) };
            if let Err(error) = revoke_result {
                tracing::warn!(?error, "CoRevokeClassObject failed");
            }
        }
    }

    // -- Registry registration (LocalServer32) ---------------------------------

    /// Write the per-user COM registration that lets `CoCreateInstance` launch
    /// `wixen.exe` when no running instance has registered the class factory:
    ///
    /// - `HKCU\Software\Classes\CLSID\{clsid}` (default) = friendly name
    /// - `HKCU\Software\Classes\CLSID\{clsid}\LocalServer32` (default) =
    ///   `"<current exe>" --handoff` (COM appends `-Embedding` at launch).
    pub fn register_handoff_server() -> Result<(), HandoffError> {
        let exe_path =
            std::env::current_exe().map_err(|error| HandoffError::ExecutableUnavailable {
                details: error.to_string(),
            })?;
        let exe_path = exe_path
            .to_str()
            .ok_or_else(|| HandoffError::ExecutableUnavailable {
                details: "executable path is not valid UTF-8".to_string(),
            })?;
        let command = local_server_command(exe_path)?;

        write_default_registry_value(
            &clsid_registry_path(WIXEN_TERMINAL_CLSID),
            HANDOFF_SERVER_FRIENDLY_NAME,
        )?;
        write_default_registry_value(&local_server_registry_path(WIXEN_TERMINAL_CLSID), &command)
    }

    /// Delete the per-user COM registration. Succeeds if it was never written.
    pub fn unregister_handoff_server() -> Result<(), HandoffError> {
        let subkey = clsid_registry_path(WIXEN_TERMINAL_CLSID);
        let status = unsafe { RegDeleteTreeW(HKEY_CURRENT_USER, &HSTRING::from(subkey.as_str())) };
        if status.is_ok() || status == ERROR_FILE_NOT_FOUND {
            Ok(())
        } else {
            Err(HandoffError::DeleteKey {
                subkey,
                code: status.0,
            })
        }
    }

    /// Create `subkey` under `HKEY_CURRENT_USER` (if needed) and set its
    /// default value to `value` as `REG_SZ`.
    fn write_default_registry_value(subkey: &str, value: &str) -> Result<(), HandoffError> {
        let subkey_wide = HSTRING::from(subkey);
        let mut hkey = HKEY::default();

        let create_result = unsafe {
            RegCreateKeyExW(
                HKEY_CURRENT_USER,
                &subkey_wide,
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
            return Err(HandoffError::OpenKey {
                subkey: subkey.to_string(),
                code: create_result.0,
            });
        }

        let data = encode_reg_sz(value);
        // Null value name selects the key's default value.
        let set_result =
            unsafe { RegSetValueExW(hkey, PCWSTR::null(), Some(0), REG_SZ, Some(&data)) };

        unsafe {
            let _ = RegCloseKey(hkey);
        }

        if set_result.is_err() {
            return Err(HandoffError::SetValue {
                subkey: subkey.to_string(),
                code: set_result.0,
            });
        }
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    // --- Launch detection ---------------------------------------------------

    #[test]
    fn plain_launch_is_not_handoff() {
        assert!(!is_handoff_launch(["--portable", "-w", "new-tab"]));
    }

    #[test]
    fn empty_args_are_not_handoff() {
        assert!(!is_handoff_launch(std::iter::empty::<&str>()));
    }

    #[test]
    fn com_embedding_switch_is_handoff() {
        assert!(is_handoff_launch(["-Embedding"]));
    }

    #[test]
    fn embedding_switch_is_case_insensitive() {
        assert!(is_handoff_launch(["-embedding"]));
    }

    #[test]
    fn slash_embedding_variant_is_handoff() {
        assert!(is_handoff_launch(["/Embedding"]));
    }

    #[test]
    fn explicit_handoff_flag_is_handoff() {
        assert!(is_handoff_launch(["--handoff"]));
    }

    #[test]
    fn handoff_flag_mixed_with_other_args_is_handoff() {
        assert!(is_handoff_launch(["--handoff", "-Embedding"]));
    }

    #[test]
    fn embedding_substring_inside_other_arg_is_not_handoff() {
        assert!(!is_handoff_launch(["--title=-Embedding-demo"]));
    }

    // --- CLSID parsing ------------------------------------------------------

    #[test]
    fn parses_wixen_clsid_to_u128_in_textual_order() {
        assert_eq!(
            parse_guid_u128(WIXEN_TERMINAL_CLSID),
            Ok(0x7b3e_d7a4_e02b_4742_9e23_1d9d_2a50_c5a1)
        );
    }

    #[test]
    fn parses_clsid_without_braces() {
        assert_eq!(
            parse_guid_u128("7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1"),
            Ok(0x7b3e_d7a4_e02b_4742_9e23_1d9d_2a50_c5a1)
        );
    }

    #[test]
    fn parses_null_clsid_to_zero() {
        assert_eq!(
            parse_guid_u128("{00000000-0000-0000-0000-000000000000}"),
            Ok(0)
        );
    }

    #[test]
    fn parsing_is_case_insensitive() {
        assert_eq!(
            parse_guid_u128(&WIXEN_TERMINAL_CLSID.to_uppercase()),
            Ok(0x7b3e_d7a4_e02b_4742_9e23_1d9d_2a50_c5a1)
        );
    }

    #[test]
    fn rejects_wrong_group_count() {
        assert!(parse_guid_u128("{7b3ed7a4-e02b-4742-9e231d9d2a50c5a1}").is_err());
    }

    #[test]
    fn rejects_wrong_group_length() {
        assert!(parse_guid_u128("{7b3ed7a-e02b-4742-9e23-1d9d2a50c5a1}").is_err());
    }

    #[test]
    fn rejects_non_hex_characters() {
        assert!(parse_guid_u128("{7b3ed7g4-e02b-4742-9e23-1d9d2a50c5a1}").is_err());
    }

    #[test]
    fn rejects_mismatched_braces() {
        assert!(parse_guid_u128("{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1").is_err());
    }

    // --- Registry path construction ----------------------------------------

    #[test]
    fn clsid_registry_path_is_under_hkcu_classes() {
        assert_eq!(
            clsid_registry_path(WIXEN_TERMINAL_CLSID),
            r"Software\Classes\CLSID\{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}"
        );
    }

    #[test]
    fn local_server_registry_path_appends_localserver32() {
        assert_eq!(
            local_server_registry_path(WIXEN_TERMINAL_CLSID),
            r"Software\Classes\CLSID\{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}\LocalServer32"
        );
    }

    // --- LocalServer32 command construction ---------------------------------

    #[test]
    fn local_server_command_quotes_exe_and_adds_handoff_flag() {
        assert_eq!(
            local_server_command(r"C:\Program Files\Wixen\wixen.exe").as_deref(),
            Ok(r#""C:\Program Files\Wixen\wixen.exe" --handoff"#)
        );
    }

    #[test]
    fn local_server_command_quotes_paths_without_spaces_too() {
        assert_eq!(
            local_server_command(r"C:\wixen\wixen.exe").as_deref(),
            Ok(r#""C:\wixen\wixen.exe" --handoff"#)
        );
    }

    #[test]
    fn local_server_command_rejects_embedded_quotes() {
        let result = local_server_command(r#"C:\evil"\wixen.exe"#);
        assert_eq!(
            result,
            Err(HandoffError::UnquotableExePath {
                path: r#"C:\evil"\wixen.exe"#.to_string(),
            })
        );
    }

    // --- REG_SZ payload ------------------------------------------------------

    #[test]
    fn encode_reg_sz_appends_null_terminator() {
        assert_eq!(encode_reg_sz("A"), vec![0x41, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn encode_reg_sz_empty_string_is_just_terminator() {
        assert_eq!(encode_reg_sz(""), vec![0x00, 0x00]);
    }

    #[test]
    fn encode_reg_sz_is_utf16_little_endian() {
        // '\u{20AC}' (Euro sign) encodes as 0xAC 0x20 little-endian.
        assert_eq!(encode_reg_sz("\u{20AC}"), vec![0xAC, 0x20, 0x00, 0x00]);
    }

    // --- Startup info mapping ------------------------------------------------

    #[test]
    fn show_window_is_none_without_useshowwindow_flag() {
        let startup = HandoffStartupInfo {
            show_window_raw: 3, // SW_MAXIMIZE — but the flag says "ignore me"
            flags: 0,
            ..HandoffStartupInfo::default()
        };
        assert_eq!(startup.show_window(), None);
    }

    #[test]
    fn show_window_is_some_with_useshowwindow_flag() {
        let startup = HandoffStartupInfo {
            show_window_raw: 3,
            flags: STARTF_USESHOWWINDOW,
            ..HandoffStartupInfo::default()
        };
        assert_eq!(startup.show_window(), Some(3));
    }

    #[test]
    fn default_startup_info_is_empty() {
        let startup = HandoffStartupInfo::default();
        assert_eq!(startup.title, None);
        assert_eq!(startup.icon_path, None);
        assert_eq!(startup.show_window(), None);
    }

    // --- Error display -------------------------------------------------------

    #[test]
    fn invalid_clsid_error_names_the_offender() {
        let error = HandoffError::InvalidClsid {
            clsid: "bogus".to_string(),
            reason: "expected 5 dash-separated groups",
        };
        let message = error.to_string();
        assert!(message.contains("bogus"));
        assert!(message.contains("5 dash-separated groups"));
    }

    #[test]
    fn class_registration_error_shows_hresult_in_hex() {
        let error = HandoffError::ClassRegistration {
            hresult: 0x8000_4005,
        };
        assert!(error.to_string().contains("0x80004005"));
    }
}

#[cfg(all(test, windows))]
mod windows_tests {
    use super::com_server::{
        ITerminalHandoff, ITerminalHandoff2, TERMINAL_STARTUP_INFO, borrowed_bstr_to_string,
        decode_startup_info, wixen_class_guid,
    };
    use super::*;
    use windows::core::{BSTR, GUID, Interface};

    // --- Interface identity (transcribed external contract) -----------------

    #[test]
    fn terminal_handoff_iid_matches_transcription() {
        assert_eq!(
            ITerminalHandoff::IID,
            GUID::from_u128(0x59d5_5cce_fc8a_48b4_ace8_0a92_86c6_557f)
        );
    }

    #[test]
    fn terminal_handoff2_iid_matches_transcription() {
        assert_eq!(
            ITerminalHandoff2::IID,
            GUID::from_u128(0xaa6b_364f_4a50_4176_9002_0ae7_55e7_b5ef)
        );
    }

    #[test]
    fn wixen_class_guid_matches_delegation_clsid() {
        assert_eq!(
            wixen_class_guid().expect("CLSID constant must parse"),
            GUID::from_u128(0x7b3e_d7a4_e02b_4742_9e23_1d9d_2a50_c5a1)
        );
    }

    // --- TERMINAL_STARTUP_INFO layout pinning -------------------------------
    //
    // Guards against accidental field reordering: the offsets below are what
    // the transcribed IDL field order produces under the x64 C ABI.

    #[cfg(target_pointer_width = "64")]
    #[test]
    fn startup_info_layout_matches_x64_c_abi() {
        use std::mem::{offset_of, size_of};
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, pszTitle), 0);
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, pszIconPath), 8);
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, iconIndex), 16);
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, dwX), 20);
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, dwFillAttribute), 44);
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, dwFlags), 48);
        assert_eq!(offset_of!(TERMINAL_STARTUP_INFO, wShowWindow), 52);
        assert_eq!(size_of::<TERMINAL_STARTUP_INFO>(), 56);
    }

    // --- BSTR decoding (no COM apartment required) ---------------------------

    #[test]
    fn borrowed_bstr_decodes_text_without_taking_ownership() {
        let title = BSTR::from("Wixen Terminal");
        let decoded = unsafe { borrowed_bstr_to_string(title.as_ptr().cast_mut()) };
        assert_eq!(decoded.as_deref(), Some("Wixen Terminal"));
        // `title` is still alive and droppable here — no double free.
        drop(title);
    }

    #[test]
    fn borrowed_bstr_null_is_none() {
        assert_eq!(
            unsafe { borrowed_bstr_to_string(std::ptr::null_mut()) },
            None
        );
    }

    // --- Raw startup-info mapping --------------------------------------------

    #[test]
    fn decode_startup_info_maps_all_fields() {
        let title = BSTR::from("cmd");
        let icon = BSTR::from(r"C:\icons\cmd.ico");
        let raw = TERMINAL_STARTUP_INFO {
            pszTitle: title.as_ptr().cast_mut(),
            pszIconPath: icon.as_ptr().cast_mut(),
            iconIndex: 2,
            dwX: 10,
            dwY: 20,
            dwXSize: 800,
            dwYSize: 600,
            dwXCountChars: 120,
            dwYCountChars: 40,
            dwFillAttribute: 7,
            dwFlags: STARTF_USESHOWWINDOW,
            wShowWindow: 3,
        };

        let decoded = unsafe { decode_startup_info(&raw) };

        assert_eq!(decoded.title.as_deref(), Some("cmd"));
        assert_eq!(decoded.icon_path.as_deref(), Some(r"C:\icons\cmd.ico"));
        assert_eq!(decoded.icon_index, 2);
        assert_eq!((decoded.x, decoded.y), (10, 20));
        assert_eq!((decoded.x_size, decoded.y_size), (800, 600));
        assert_eq!((decoded.x_count_chars, decoded.y_count_chars), (120, 40));
        assert_eq!(decoded.fill_attribute, 7);
        assert_eq!(decoded.show_window(), Some(3));
    }

    #[test]
    fn decode_startup_info_treats_null_strings_as_absent() {
        let raw = TERMINAL_STARTUP_INFO {
            pszTitle: std::ptr::null_mut(),
            pszIconPath: std::ptr::null_mut(),
            iconIndex: 0,
            dwX: 0,
            dwY: 0,
            dwXSize: 0,
            dwYSize: 0,
            dwXCountChars: 0,
            dwYCountChars: 0,
            dwFillAttribute: 0,
            dwFlags: 0,
            wShowWindow: 0,
        };

        let decoded = unsafe { decode_startup_info(&raw) };

        assert_eq!(decoded.title, None);
        assert_eq!(decoded.icon_path, None);
        assert_eq!(decoded.show_window(), None);
    }
}
