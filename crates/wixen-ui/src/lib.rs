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
pub use history::{CommandHistory, HistoryBrowser, HistoryEntry, history_entry_label};
pub use panes::{PaneId, PaneRect, PaneTree, SplitDirection};
pub use plugin_bridge::PluginBridge;
pub use settings::{SettingsField, SettingsTab, SettingsUI};
pub use tabs::{Tab, TabColor, TabId, TabManager, tab_color_presets};
pub use window::Window;
