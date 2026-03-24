//! wixen-ipc: Client-server IPC layer for multi-window terminal sessions.
//!
//! This crate provides:
//! - Typed IPC messages (serde-serializable) for terminal coordination
//! - Named pipe server/client for Windows inter-process communication
//! - Session persistence (save/restore tab layout and metadata)
//!
//! Architecture: A "daemon" (the first wixen.exe instance) listens on a named pipe.
//! Subsequent instances connect as clients and either join the existing session
//! or request a new window. The daemon owns all PTY processes and terminal state.

pub mod messages;
pub mod pipe;
pub mod session;

pub use messages::{IpcMessage, IpcRequest, IpcResponse};
pub use pipe::{IpcClient, IpcServer, PipeName};
pub use session::{SessionInfo, SessionStore};
