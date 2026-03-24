//! wixen-core: Terminal state machine, grid, and buffer management.
//!
//! This crate provides the core terminal data structures:
//! - Cell grid with attribute tracking (SGR)
//! - Scrollback buffer with zstd compression
//! - Cursor state and movement
//! - Selection model
//! - Terminal modes (DEC private, ANSI)

pub mod attrs;
pub mod buffer;
pub mod cell;
pub mod cursor;
pub mod error_detect;
pub mod grid;
pub mod hyperlink;
pub mod image;
pub mod keyboard;
pub mod modes;
pub mod mouse;
pub mod selection;
pub mod session;
pub mod table_detect;
pub mod terminal;
pub mod themes;
pub mod url;

pub use attrs::CellAttributes;
pub use buffer::ScrollbackBuffer;
pub use cell::Cell;
pub use cursor::Cursor;
pub use grid::{Grid, Row};
pub use hyperlink::{Hyperlink, HyperlinkStore};
pub use session::{RestoreMode, SessionState};
pub use terminal::{
    HoverState, ImageAnnouncement, ImageProtocol, Osc52Policy, ProgressState, Terminal,
    ViewportImage,
};
