//! wixen-a11y: UIA provider and structured accessibility tree.
//!
//! This crate implements:
//! - `AccessibilityTree` — virtual document model from terminal state
//! - `TerminalProvider` — COM UIA provider (IRawElementProviderSimple/Fragment/FragmentRoot)
//! - `EventThrottler` — batched UIA event raising for screen reader output

pub mod events;
pub mod fragment;
pub mod grid_provider;
pub mod palette_provider;
pub mod provider;
pub mod settings_provider;
pub mod text_provider;
pub mod tree;

pub use events::{EventThrottler, strip_vt_escapes};
pub use fragment::ChildFragmentProvider;
pub use grid_provider::{GridSnapshot, TerminalGridProvider};
pub use palette_provider::{PaletteEntrySnapshot, PaletteSnapshot};
pub use provider::{TerminalA11yState, TerminalProvider};
pub use settings_provider::{FieldSnapshot, FieldType, SettingsSnapshot, SubFieldSnapshot};
pub use text_provider::TerminalTextProvider;
pub use tree::{A11yNode, AccessibilityTree, NodeId, SemanticRole};
