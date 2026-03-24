//! Bridge between UI events and the Lua plugin system.
//!
//! [`PluginBridge`] wraps a [`PluginRegistry`] and resolves which plugin
//! entry-point files should be invoked for a given [`PluginEvent`].
//! The actual Lua execution happens in `main.rs`; this module only
//! determines *which* plugins match.

use wixen_config::plugin::{PluginEvent, PluginRegistry, event_name};

/// Connects UI-layer events to the plugin registry so the main loop
/// knows which Lua scripts to execute.
pub struct PluginBridge {
    registry: PluginRegistry,
}

impl PluginBridge {
    /// Create a bridge wrapping an existing registry.
    pub fn new(registry: PluginRegistry) -> Self {
        Self { registry }
    }

    /// Return the `entry_point` paths of every plugin that subscribes to
    /// the given event.
    pub fn dispatch_event(&self, event: &PluginEvent) -> Vec<String> {
        let name = event_name(event);
        self.registry
            .plugins_for_event(name)
            .into_iter()
            .map(|p| p.entry_point.clone())
            .collect()
    }

    /// Borrow the inner registry (e.g., for listing plugins in the UI).
    pub fn registry(&self) -> &PluginRegistry {
        &self.registry
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use wixen_config::plugin::PluginManifest;

    fn make_manifest(name: &str, entry: &str, events: &[&str]) -> PluginManifest {
        PluginManifest {
            name: name.to_string(),
            version: "1.0.0".to_string(),
            description: format!("{name} test plugin"),
            entry_point: entry.to_string(),
            events: events.iter().map(|s| (*s).to_string()).collect(),
        }
    }

    #[test]
    fn dispatch_with_no_plugins_returns_empty() {
        let bridge = PluginBridge::new(PluginRegistry::default());
        let result = bridge.dispatch_event(&PluginEvent::ConfigReloaded);
        assert!(result.is_empty());
    }

    #[test]
    fn dispatch_returns_matching_entry_points() {
        let mut reg = PluginRegistry::default();
        reg.register(make_manifest(
            "notifier",
            "notifier/init.lua",
            &["on_tab_created", "on_output"],
        ));
        reg.register(make_manifest("logger", "logger/init.lua", &["on_output"]));

        let bridge = PluginBridge::new(reg);

        let result = bridge.dispatch_event(&PluginEvent::OutputReceived {
            text: "hello".into(),
        });
        assert_eq!(result, vec!["notifier/init.lua", "logger/init.lua"]);
    }

    #[test]
    fn dispatch_with_non_matching_event_returns_empty() {
        let mut reg = PluginRegistry::default();
        reg.register(make_manifest(
            "tab-watcher",
            "tab-watcher/init.lua",
            &["on_tab_created"],
        ));

        let bridge = PluginBridge::new(reg);

        let result = bridge.dispatch_event(&PluginEvent::ConfigReloaded);
        assert!(result.is_empty());
    }
}
