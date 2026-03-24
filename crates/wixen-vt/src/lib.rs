//! wixen-vt: VT parser — CSI, OSC, DCS escape sequence processing.
//!
//! Custom DEC VT500 state machine for parsing terminal escape sequences.
//! Tightly integrated with OSC 133 shell integration for structured accessibility.

pub mod action;
pub mod parser;

pub use action::Action;
pub use parser::Parser;
