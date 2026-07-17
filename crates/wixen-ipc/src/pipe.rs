//! Named pipe server and client for Windows IPC.
//!
//! The first wixen.exe instance creates a named pipe server. Subsequent instances
//! connect as clients to send commands (new window, new tab, focus, etc.).

use std::io::{self, Read, Write};
use std::time::Duration;
use tracing::{debug, warn};
use windows::Win32::Security::{PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES};

use crate::messages::{IpcMessage, IpcRequest, IpcResponse, decode_message, encode_message};

/// Default pipe name for Wixen Terminal IPC.
pub const DEFAULT_PIPE_NAME: &str = r"\\.\pipe\wixen-terminal-ipc";

/// Owner-only security descriptor for the IPC pipe, in SDDL form.
///
/// A named pipe created with default security lets every process in the same
/// interactive session open it and send any `IpcRequest` — including
/// `Shutdown`. This DACL is protected (`P`, so no inherited ACEs widen it) and
/// grants `GENERIC_ALL` only to the object owner (`OW` — the user who created
/// the pipe) and to LocalSystem (`SY`, so elevated service tooling can still
/// manage it). Every other principal is implicitly denied, so a peer process
/// running as a different user, or a sandboxed/low-integrity process, cannot
/// connect and issue commands.
const PIPE_SDDL: &str = "D:P(A;;GA;;;OW)(A;;GA;;;SY)";

/// Owns a security descriptor built from [`PIPE_SDDL`] and frees it on drop.
///
/// `ConvertStringSecurityDescriptorToSecurityDescriptorW` allocates the
/// descriptor with `LocalAlloc`, so it must be released with `LocalFree`.
struct PipeSecurity {
    descriptor: PSECURITY_DESCRIPTOR,
}

impl PipeSecurity {
    /// Build the owner-only descriptor from [`PIPE_SDDL`].
    ///
    /// Returns `None` only if the (compile-time-constant) SDDL fails to
    /// convert, which should not happen at runtime; callers log and fall back
    /// to default security rather than panic.
    fn owner_only() -> Option<Self> {
        use windows::Win32::Security::Authorization::{
            ConvertStringSecurityDescriptorToSecurityDescriptorW, SDDL_REVISION_1,
        };
        use windows::core::HSTRING;

        let sddl = HSTRING::from(PIPE_SDDL);
        let mut descriptor = PSECURITY_DESCRIPTOR::default();
        let result = unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                &sddl,
                SDDL_REVISION_1,
                &mut descriptor,
                None,
            )
        };
        match result {
            Ok(()) => Some(Self { descriptor }),
            Err(e) => {
                warn!(error = %e, "Failed to build IPC pipe security descriptor");
                None
            }
        }
    }

    /// A `SECURITY_ATTRIBUTES` referencing this descriptor. The result borrows
    /// `self` through a raw pointer, so it must not outlive the `PipeSecurity`.
    fn attributes(&self) -> SECURITY_ATTRIBUTES {
        SECURITY_ATTRIBUTES {
            nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: self.descriptor.0,
            bInheritHandle: false.into(),
        }
    }
}

impl Drop for PipeSecurity {
    fn drop(&mut self) {
        use windows::Win32::Foundation::{HLOCAL, LocalFree};

        if !self.descriptor.is_invalid() {
            unsafe {
                let _ = LocalFree(Some(HLOCAL(self.descriptor.0)));
            }
        }
    }
}

/// Named pipe identifier.
#[derive(Debug, Clone)]
pub struct PipeName(String);

impl PipeName {
    /// Create a pipe name with the default Wixen Terminal IPC path.
    pub fn default_name() -> Self {
        Self(DEFAULT_PIPE_NAME.to_string())
    }

    /// Create a pipe name with a custom path (useful for testing).
    pub fn custom(name: &str) -> Self {
        Self(name.to_string())
    }

    /// Get the pipe path as a string.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Default for PipeName {
    fn default() -> Self {
        Self::default_name()
    }
}

/// IPC server that listens on a named pipe.
///
/// The server accepts connections, reads requests, and returns responses.
/// It uses Win32 named pipes for Windows-native IPC.
pub struct IpcServer {
    pipe_name: PipeName,
    running: bool,
}

impl IpcServer {
    /// Create a new IPC server (does not start listening yet).
    pub fn new(pipe_name: PipeName) -> Self {
        Self {
            pipe_name,
            running: false,
        }
    }

    /// Get the pipe name.
    pub fn pipe_name(&self) -> &PipeName {
        &self.pipe_name
    }

    /// Check if the server is marked as running.
    pub fn is_running(&self) -> bool {
        self.running
    }

    /// Start the server (creates the named pipe and begins accepting connections).
    ///
    /// This is a blocking call — in production, run on a dedicated thread.
    /// Each accepted connection is handled synchronously: read one request, write one response.
    ///
    /// The `handler` closure is called for each request and must return a response.
    pub fn serve_once<F>(&mut self, handler: F) -> io::Result<()>
    where
        F: Fn(IpcRequest) -> IpcResponse,
    {
        use windows::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
        use windows::Win32::Storage::FileSystem::{
            FILE_FLAG_FIRST_PIPE_INSTANCE, FlushFileBuffers, PIPE_ACCESS_DUPLEX, ReadFile,
            WriteFile,
        };
        use windows::Win32::System::Pipes::{
            ConnectNamedPipe, CreateNamedPipeW, DisconnectNamedPipe, PIPE_READMODE_BYTE,
            PIPE_TYPE_BYTE, PIPE_WAIT,
        };
        use windows::core::HSTRING;

        let pipe_name = HSTRING::from(self.pipe_name.as_str());
        // Restrict the pipe to the owner (see `PIPE_SDDL`). `security` and
        // `attributes` must stay alive until after `CreateNamedPipeW` returns,
        // because `security_ptr` borrows into them.
        let security = PipeSecurity::owner_only();
        let attributes = security.as_ref().map(PipeSecurity::attributes);
        let security_ptr = attributes.as_ref().map(|a| a as *const SECURITY_ATTRIBUTES);
        let handle = unsafe {
            CreateNamedPipeW(
                &pipe_name,
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,            // max instances
                4096,         // out buffer
                4096,         // in buffer
                0,            // default timeout
                security_ptr, // owner-only DACL
            )
        };

        if handle == INVALID_HANDLE_VALUE {
            return Err(io::Error::last_os_error());
        }

        self.running = true;
        debug!(pipe = self.pipe_name.as_str(), "IPC server listening");

        // Wait for a client to connect (ignore error — client may already be connected)
        let _ = unsafe { ConnectNamedPipe(handle, None) };

        // Read request
        let mut buf = vec![0u8; 4096];
        let mut bytes_read = 0u32;
        let read_ok = unsafe { ReadFile(handle, Some(&mut buf), Some(&mut bytes_read), None) };

        if read_ok.is_ok()
            && bytes_read > 0
            && let Ok(Some((IpcMessage::Request(req), _))) =
                decode_message(&buf[..bytes_read as usize])
        {
            let response = handler(req);
            let response_msg = IpcMessage::Response(response);
            if let Ok(frame) = encode_message(&response_msg) {
                let mut written = 0u32;
                let _ = unsafe { WriteFile(handle, Some(&frame), Some(&mut written), None) };
                let _ = unsafe { FlushFileBuffers(handle) };
            }
        }

        let _ = unsafe { DisconnectNamedPipe(handle) };
        let _ = unsafe { CloseHandle(handle) };
        self.running = false;
        Ok(())
    }

    /// Try to claim the server pipe. Returns `true` if this process is the first instance.
    ///
    /// Non-blocking check: attempts to create the pipe with `FILE_FLAG_FIRST_PIPE_INSTANCE`.
    /// If the pipe already exists, another server is running and we should connect as a client.
    pub fn try_claim(pipe_name: &PipeName) -> bool {
        use windows::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
        use windows::Win32::Storage::FileSystem::{
            FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_ACCESS_DUPLEX,
        };
        use windows::Win32::System::Pipes::{
            CreateNamedPipeW, PIPE_READMODE_BYTE, PIPE_TYPE_BYTE, PIPE_WAIT,
        };
        use windows::core::HSTRING;

        let name = HSTRING::from(pipe_name.as_str());
        // Same owner-only DACL as `serve_once`, so the probe pipe another
        // instance would connect to is never world-accessible either.
        let security = PipeSecurity::owner_only();
        let attributes = security.as_ref().map(PipeSecurity::attributes);
        let security_ptr = attributes.as_ref().map(|a| a as *const SECURITY_ATTRIBUTES);
        let handle = unsafe {
            CreateNamedPipeW(
                &name,
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                4096,
                4096,
                0,
                security_ptr,
            )
        };

        if handle == INVALID_HANDLE_VALUE {
            // Pipe already exists — another server is running.
            false
        } else {
            // Successfully created — we're the first instance. Close the test handle.
            let _ = unsafe { CloseHandle(handle) };
            true
        }
    }
}

/// IPC client that connects to an existing server.
pub struct IpcClient {
    pipe_name: PipeName,
    timeout: Duration,
}

impl IpcClient {
    /// Create a new IPC client.
    pub fn new(pipe_name: PipeName) -> Self {
        Self {
            pipe_name,
            timeout: Duration::from_secs(5),
        }
    }

    /// Set the connection timeout.
    pub fn with_timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    /// Send a request and receive a response.
    pub fn send(&self, request: IpcRequest) -> io::Result<IpcResponse> {
        let msg = IpcMessage::Request(request);
        let frame =
            encode_message(&msg).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;

        // Connect to the named pipe as a regular file
        let mut file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(self.pipe_name.as_str())?;

        // Write the request
        file.write_all(&frame)?;
        file.flush()?;

        // Read the response
        let mut buf = vec![0u8; 4096];
        let n = file.read(&mut buf)?;
        if n == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "server closed connection",
            ));
        }

        match decode_message(&buf[..n]) {
            Ok(Some((IpcMessage::Response(resp), _))) => Ok(resp),
            Ok(Some((IpcMessage::Request(_), _))) => Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "expected response, got request",
            )),
            Ok(None) => Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "incomplete response",
            )),
            Err(e) => Err(io::Error::new(io::ErrorKind::InvalidData, e)),
        }
    }

    /// Ping the server.
    pub fn ping(&self) -> io::Result<IpcResponse> {
        self.send(IpcRequest::Ping)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_pipe_name_default() {
        let name = PipeName::default_name();
        assert_eq!(name.as_str(), DEFAULT_PIPE_NAME);
    }

    #[test]
    fn test_pipe_sddl_is_owner_only() {
        // Protected DACL granting full access to owner and SYSTEM only. If this
        // ever loosens (e.g. adds AU/Everyone), the pipe would again accept
        // commands from other processes in the session.
        assert_eq!(PIPE_SDDL, "D:P(A;;GA;;;OW)(A;;GA;;;SY)");
        assert!(PIPE_SDDL.contains("D:P"), "DACL must be protected");
        assert!(PIPE_SDDL.contains("(A;;GA;;;OW)"), "owner must have access");
        assert!(!PIPE_SDDL.contains(";;;WD)"), "must not grant to Everyone");
        assert!(
            !PIPE_SDDL.contains(";;;AU)"),
            "must not grant to Authenticated Users"
        );
    }

    #[test]
    fn test_owner_only_descriptor_builds() {
        // The constant SDDL must convert to a real security descriptor. This is
        // a pure in-memory conversion (no pipe is created), so it is safe to run
        // in unit tests. The descriptor is freed when `security` drops.
        let security = PipeSecurity::owner_only().expect("SDDL must convert");
        assert!(!security.descriptor.is_invalid());
        let attrs = security.attributes();
        assert_eq!(
            attrs.nLength as usize,
            std::mem::size_of::<SECURITY_ATTRIBUTES>()
        );
        assert!(!attrs.lpSecurityDescriptor.is_null());
    }

    #[test]
    fn test_pipe_name_custom() {
        let name = PipeName::custom(r"\\.\pipe\test-pipe");
        assert_eq!(name.as_str(), r"\\.\pipe\test-pipe");
    }

    #[test]
    fn test_server_initial_state() {
        let server = IpcServer::new(PipeName::default_name());
        assert!(!server.is_running());
        assert_eq!(server.pipe_name().as_str(), DEFAULT_PIPE_NAME);
    }

    #[test]
    fn test_client_creation() {
        let client = IpcClient::new(PipeName::default_name());
        assert_eq!(client.pipe_name.as_str(), DEFAULT_PIPE_NAME);
    }

    #[test]
    fn test_try_claim_first_instance() {
        // Use a unique pipe name to avoid conflict with other tests
        let name = PipeName::custom(r"\\.\pipe\wixen-test-claim-first");
        // First claim should succeed
        assert!(IpcServer::try_claim(&name));
    }

    #[test]
    fn test_client_server_roundtrip() {
        use std::thread;

        let pipe_name = PipeName::custom(r"\\.\pipe\wixen-test-roundtrip");
        let server_pipe = pipe_name.clone();

        // Spawn server thread
        let server_thread = thread::spawn(move || {
            let mut server = IpcServer::new(server_pipe);
            server
                .serve_once(|req| match req {
                    IpcRequest::Ping => IpcResponse::Pong {
                        pid: std::process::id(),
                        version: "0.1.0".into(),
                    },
                    _ => IpcResponse::Error {
                        message: "unexpected".into(),
                    },
                })
                .unwrap();
        });

        // Small delay to let server start
        thread::sleep(Duration::from_millis(100));

        // Client sends Ping
        let client = IpcClient::new(pipe_name);
        let response = client.ping().unwrap();
        match response {
            IpcResponse::Pong { pid, version } => {
                assert_eq!(pid, std::process::id());
                assert_eq!(version, "0.1.0");
            }
            other => panic!("Expected Pong, got: {other:?}"),
        }

        server_thread.join().unwrap();
    }

    #[test]
    fn test_client_server_new_window() {
        use std::thread;

        let pipe_name = PipeName::custom(r"\\.\pipe\wixen-test-new-window");
        let server_pipe = pipe_name.clone();

        let server_thread = thread::spawn(move || {
            let mut server = IpcServer::new(server_pipe);
            server
                .serve_once(|req| match req {
                    IpcRequest::NewWindow => IpcResponse::Ok,
                    _ => IpcResponse::Error {
                        message: "unexpected".into(),
                    },
                })
                .unwrap();
        });

        thread::sleep(Duration::from_millis(100));

        let client = IpcClient::new(pipe_name);
        let response = client.send(IpcRequest::NewWindow).unwrap();
        assert_eq!(response, IpcResponse::Ok);

        server_thread.join().unwrap();
    }
}
