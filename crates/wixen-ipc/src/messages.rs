//! IPC message types for client-server communication.
//!
//! All messages are JSON-serialized over named pipes. Each message is framed
//! as a 4-byte little-endian length prefix followed by the JSON payload.

use serde::{Deserialize, Serialize};

/// Top-level IPC message envelope.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum IpcMessage {
    Request(IpcRequest),
    Response(IpcResponse),
}

/// Client → Server requests.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum IpcRequest {
    /// Ping the server to check if it's alive.
    Ping,
    /// Request a new window (the server spawns a new window).
    NewWindow,
    /// Request a new tab in the active window.
    NewTab {
        /// Optional profile name to use for the new tab.
        profile: Option<String>,
    },
    /// List all active sessions.
    ListSessions,
    /// Focus a specific window by session ID.
    FocusWindow { session_id: String },
    /// Gracefully shut down the server and all windows.
    Shutdown,
}

/// Server → Client responses.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum IpcResponse {
    /// Acknowledge a successful operation.
    Ok,
    /// Pong response (server is alive).
    Pong { pid: u32, version: String },
    /// List of active sessions.
    Sessions { sessions: Vec<SessionSummary> },
    /// Error response.
    Error { message: String },
}

/// Summary of an active session (returned by ListSessions).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SessionSummary {
    pub session_id: String,
    pub window_title: String,
    pub tab_count: usize,
    pub pid: u32,
}

/// Frame a message as length-prefixed JSON bytes.
pub fn encode_message(msg: &IpcMessage) -> Result<Vec<u8>, serde_json::Error> {
    let json = serde_json::to_vec(msg)?;
    let len = (json.len() as u32).to_le_bytes();
    let mut frame = Vec::with_capacity(4 + json.len());
    frame.extend_from_slice(&len);
    frame.extend_from_slice(&json);
    Ok(frame)
}

/// Decode a length-prefixed JSON message from a byte buffer.
///
/// Returns `(message, bytes_consumed)` on success.
pub fn decode_message(buf: &[u8]) -> Result<Option<(IpcMessage, usize)>, DecodeError> {
    if buf.len() < 4 {
        return Ok(None); // Need more data
    }
    let len = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
    if len > MAX_MESSAGE_SIZE {
        return Err(DecodeError::MessageTooLarge(len));
    }
    let total = 4 + len;
    if buf.len() < total {
        return Ok(None); // Need more data
    }
    let msg: IpcMessage = serde_json::from_slice(&buf[4..total])?;
    Ok(Some((msg, total)))
}

/// Maximum IPC message size (1 MiB).
pub const MAX_MESSAGE_SIZE: usize = 1024 * 1024;

/// Errors from message decoding.
#[derive(Debug, thiserror::Error)]
pub enum DecodeError {
    #[error("JSON deserialization failed: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Message too large: {0} bytes (max {MAX_MESSAGE_SIZE})")]
    MessageTooLarge(usize),
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_decode_ping() {
        let msg = IpcMessage::Request(IpcRequest::Ping);
        let encoded = encode_message(&msg).unwrap();
        assert!(encoded.len() > 4);
        let (decoded, consumed) = decode_message(&encoded).unwrap().unwrap();
        assert_eq!(decoded, msg);
        assert_eq!(consumed, encoded.len());
    }

    #[test]
    fn test_encode_decode_pong() {
        let msg = IpcMessage::Response(IpcResponse::Pong {
            pid: 1234,
            version: "0.1.0".into(),
        });
        let encoded = encode_message(&msg).unwrap();
        let (decoded, _) = decode_message(&encoded).unwrap().unwrap();
        assert_eq!(decoded, msg);
    }

    #[test]
    fn test_encode_decode_new_tab_with_profile() {
        let msg = IpcMessage::Request(IpcRequest::NewTab {
            profile: Some("PowerShell".into()),
        });
        let encoded = encode_message(&msg).unwrap();
        let (decoded, _) = decode_message(&encoded).unwrap().unwrap();
        assert_eq!(decoded, msg);
    }

    #[test]
    fn test_decode_partial_buffer() {
        let msg = IpcMessage::Request(IpcRequest::Ping);
        let encoded = encode_message(&msg).unwrap();
        // Give only first 3 bytes — should return None (need more data)
        assert!(decode_message(&encoded[..3]).unwrap().is_none());
        // Give header but not full payload
        assert!(decode_message(&encoded[..5]).unwrap().is_none());
    }

    #[test]
    fn test_decode_empty_buffer() {
        assert!(decode_message(&[]).unwrap().is_none());
    }

    #[test]
    fn test_decode_message_too_large() {
        // Fake a length prefix of 2 MiB
        let buf = (2 * 1024 * 1024u32).to_le_bytes();
        let err = decode_message(&buf).unwrap_err();
        assert!(matches!(err, DecodeError::MessageTooLarge(_)));
    }

    #[test]
    fn test_sessions_response_roundtrip() {
        let msg = IpcMessage::Response(IpcResponse::Sessions {
            sessions: vec![
                SessionSummary {
                    session_id: "abc-123".into(),
                    window_title: "Wixen Terminal".into(),
                    tab_count: 3,
                    pid: 5678,
                },
                SessionSummary {
                    session_id: "def-456".into(),
                    window_title: "Dev Shell".into(),
                    tab_count: 1,
                    pid: 9012,
                },
            ],
        });
        let encoded = encode_message(&msg).unwrap();
        let (decoded, _) = decode_message(&encoded).unwrap().unwrap();
        assert_eq!(decoded, msg);
    }

    #[test]
    fn test_error_response() {
        let msg = IpcMessage::Response(IpcResponse::Error {
            message: "session not found".into(),
        });
        let encoded = encode_message(&msg).unwrap();
        let (decoded, _) = decode_message(&encoded).unwrap().unwrap();
        assert_eq!(decoded, msg);
    }

    #[test]
    fn test_length_prefix_format() {
        let msg = IpcMessage::Request(IpcRequest::Ping);
        let encoded = encode_message(&msg).unwrap();
        let payload_len = u32::from_le_bytes([encoded[0], encoded[1], encoded[2], encoded[3]]);
        assert_eq!(payload_len as usize + 4, encoded.len());
    }

    #[test]
    fn test_multiple_messages_in_buffer() {
        let msg1 = IpcMessage::Request(IpcRequest::Ping);
        let msg2 = IpcMessage::Request(IpcRequest::NewWindow);
        let mut buf = encode_message(&msg1).unwrap();
        buf.extend(encode_message(&msg2).unwrap());

        let (decoded1, consumed1) = decode_message(&buf).unwrap().unwrap();
        assert_eq!(decoded1, msg1);

        let (decoded2, consumed2) = decode_message(&buf[consumed1..]).unwrap().unwrap();
        assert_eq!(decoded2, msg2);
        assert_eq!(consumed1 + consumed2, buf.len());
    }
}
