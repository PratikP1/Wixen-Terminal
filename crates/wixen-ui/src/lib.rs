//! wixen-ui: Window chrome, tabs, splits, command palette, settings GUI.
//!
//! - `window` — Raw Win32 window creation with HWND/WndProc
//! - `tabs` — Tab management (add, close, cycle, reorder)
//! - `panes` — Split pane tree (binary splits, layout computation)
//! - `command_palette` — Fuzzy-searchable action list (Ctrl+Shift+P)

pub mod audio;
pub mod command_palette;
pub mod default_terminal;
pub mod explorer_menu;
pub mod handoff;
pub mod help;
pub mod history;
pub mod jumplist;
pub mod panes;
pub mod plugin_bridge;
pub mod settings;
pub mod tabs;
pub mod tray;
pub mod window;

pub use command_palette::{CommandPalette, PaletteEntry, PaletteMode, PaletteResult};
pub use default_terminal::{
    RegistrationError, TerminalHandler, check_default_terminal_status, restore_default_terminal,
    set_as_default_terminal,
};
pub use handoff::{
    HandoffError, HandoffRequest, HandoffStartupInfo, is_handoff_launch, register_handoff_server,
    unregister_handoff_server,
};
pub use history::{CommandHistory, HistoryBrowser, HistoryEntry, history_entry_label};
pub use panes::{PaneId, PaneRect, PaneTree, SplitDirection};
pub use plugin_bridge::PluginBridge;
pub use settings::{SettingsField, SettingsTab, SettingsUI};
pub use tabs::{ClosedTabs, Tab, TabColor, TabId, TabManager, tab_color_presets};
pub use window::{
    TaggedWindowEvent, Window, WindowEventMux, WindowId, WindowRegistry, pump_thread_messages,
};
