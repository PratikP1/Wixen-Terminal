//! Lua plugin/extension API: manifest, registry, and event definitions.

use mlua::prelude::*;
use serde::{Deserialize, Serialize};
use std::path::Path;

/// Describes a plugin that can be loaded by the terminal.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PluginManifest {
    /// Human-readable plugin name (must be unique in the registry).
    pub name: String,
    /// SemVer version string.
    pub version: String,
    /// Short description shown in the plugin list.
    pub description: String,
    /// Path to the Lua entry-point file, relative to the plugin directory.
    pub entry_point: String,
    /// Events this plugin subscribes to (e.g. `"on_tab_created"`, `"on_output"`).
    pub events: Vec<String>,
}

/// Maintains the set of registered plugins.
#[derive(Debug, Default)]
pub struct PluginRegistry {
    plugins: Vec<PluginManifest>,
}

impl PluginRegistry {
    /// Register a plugin. Returns `false` if a plugin with the same name already exists.
    pub fn register(&mut self, manifest: PluginManifest) -> bool {
        if self.plugins.iter().any(|p| p.name == manifest.name) {
            return false;
        }
        self.plugins.push(manifest);
        true
    }

    /// Unregister a plugin by name. Returns `true` if it was found and removed.
    pub fn unregister(&mut self, name: &str) -> bool {
        let before = self.plugins.len();
        self.plugins.retain(|p| p.name != name);
        self.plugins.len() < before
    }

    /// List all registered plugins.
    pub fn list(&self) -> &[PluginManifest] {
        &self.plugins
    }

    /// Return all plugins that subscribe to the given event name.
    pub fn plugins_for_event(&self, event: &str) -> Vec<&PluginManifest> {
        self.plugins
            .iter()
            .filter(|p| p.events.iter().any(|e| e == event))
            .collect()
    }
}

/// Events the plugin system can dispatch.
#[derive(Debug, Clone, PartialEq)]
pub enum PluginEvent {
    TabCreated {
        tab_id: u64,
    },
    TabClosed {
        tab_id: u64,
    },
    CommandComplete {
        command: String,
        exit_code: i32,
        duration_ms: u64,
    },
    OutputReceived {
        text: String,
    },
    ConfigReloaded,
}

/// Map a `PluginEvent` variant to its canonical string name.
pub fn event_name(event: &PluginEvent) -> &'static str {
    match event {
        PluginEvent::TabCreated { .. } => "on_tab_created",
        PluginEvent::TabClosed { .. } => "on_tab_closed",
        PluginEvent::CommandComplete { .. } => "on_command_complete",
        PluginEvent::OutputReceived { .. } => "on_output",
        PluginEvent::ConfigReloaded => "on_config_reloaded",
    }
}

// ---------------------------------------------------------------------------
// Plugin event execution
// ---------------------------------------------------------------------------

/// Convert a [`PluginEvent`] into a Lua table for passing to handler functions.
fn event_to_lua_table(lua: &Lua, event: &PluginEvent) -> LuaResult<LuaTable> {
    let t = lua.create_table()?;
    match event {
        PluginEvent::TabCreated { tab_id } => {
            t.set("tab_id", *tab_id)?;
        }
        PluginEvent::TabClosed { tab_id } => {
            t.set("tab_id", *tab_id)?;
        }
        PluginEvent::CommandComplete {
            command,
            exit_code,
            duration_ms,
        } => {
            t.set("command", command.as_str())?;
            t.set("exit_code", *exit_code)?;
            t.set("duration_ms", *duration_ms)?;
        }
        PluginEvent::OutputReceived { text } => {
            t.set("text", text.as_str())?;
        }
        PluginEvent::ConfigReloaded => {}
    }
    Ok(t)
}

/// Load a Lua file from `entry_point` and call the handler matching `event`.
///
/// If the handler function does not exist in the loaded file, this returns
/// `Ok(())` — not every plugin handles every event.  If the handler exists
/// but raises an error, the error is propagated.
pub fn execute_plugin_event(
    lua: &Lua,
    entry_point: &str,
    event: &PluginEvent,
) -> Result<(), mlua::Error> {
    let source = std::fs::read_to_string(Path::new(entry_point)).map_err(mlua::Error::external)?;
    execute_plugin_event_from_source(lua, &source, event)
}

/// Execute a plugin event handler from Lua source code directly.
///
/// Behaves identically to [`execute_plugin_event`] but takes source text
/// instead of a file path, making it convenient for testing.
pub fn execute_plugin_event_from_source(
    lua: &Lua,
    source: &str,
    event: &PluginEvent,
) -> Result<(), mlua::Error> {
    lua.load(source).exec()?;

    let handler_name = event_name(event);
    let handler: LuaValue = lua.globals().get(handler_name)?;

    match handler {
        LuaValue::Function(func) => {
            let args = event_to_lua_table(lua, event)?;
            func.call::<()>(args)?;
            Ok(())
        }
        _ => Ok(()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_manifest(name: &str, events: &[&str]) -> PluginManifest {
        PluginManifest {
            name: name.to_string(),
            version: "1.0.0".to_string(),
            description: format!("{name} plugin"),
            entry_point: format!("{name}/init.lua"),
            events: events.iter().map(|s| (*s).to_string()).collect(),
        }
    }

    #[test]
    fn register_and_list() {
        let mut reg = PluginRegistry::default();
        let m = sample_manifest("alpha", &["on_output"]);
        assert!(reg.register(m));
        assert_eq!(reg.list().len(), 1);
        assert_eq!(reg.list()[0].name, "alpha");
    }

    #[test]
    fn duplicate_name_rejected() {
        let mut reg = PluginRegistry::default();
        assert!(reg.register(sample_manifest("dup", &[])));
        assert!(!reg.register(sample_manifest("dup", &["on_output"])));
        assert_eq!(reg.list().len(), 1);
    }

    #[test]
    fn unregister_existing() {
        let mut reg = PluginRegistry::default();
        reg.register(sample_manifest("gone", &[]));
        assert!(reg.unregister("gone"));
        assert!(reg.list().is_empty());
    }

    #[test]
    fn unregister_missing() {
        let mut reg = PluginRegistry::default();
        assert!(!reg.unregister("nope"));
    }

    #[test]
    fn plugins_for_event_filtering() {
        let mut reg = PluginRegistry::default();
        reg.register(sample_manifest("a", &["on_output", "on_tab_created"]));
        reg.register(sample_manifest("b", &["on_tab_created"]));
        reg.register(sample_manifest("c", &["on_command_complete"]));

        let tab_plugins = reg.plugins_for_event("on_tab_created");
        assert_eq!(tab_plugins.len(), 2);

        let output_plugins = reg.plugins_for_event("on_output");
        assert_eq!(output_plugins.len(), 1);
        assert_eq!(output_plugins[0].name, "a");

        let none = reg.plugins_for_event("on_config_reloaded");
        assert!(none.is_empty());
    }

    #[test]
    fn event_name_mapping() {
        assert_eq!(
            event_name(&PluginEvent::TabCreated { tab_id: 1 }),
            "on_tab_created"
        );
        assert_eq!(
            event_name(&PluginEvent::TabClosed { tab_id: 2 }),
            "on_tab_closed"
        );
        assert_eq!(
            event_name(&PluginEvent::CommandComplete {
                command: "ls".into(),
                exit_code: 0,
                duration_ms: 42,
            }),
            "on_command_complete"
        );
        assert_eq!(
            event_name(&PluginEvent::OutputReceived {
                text: "hello".into()
            }),
            "on_output"
        );
        assert_eq!(
            event_name(&PluginEvent::ConfigReloaded),
            "on_config_reloaded"
        );
    }

    #[test]
    fn manifest_toml_deserialization() {
        let toml_str = r#"
name = "my-plugin"
version = "0.2.1"
description = "A test plugin"
entry_point = "my-plugin/init.lua"
events = ["on_output", "on_tab_created"]
"#;
        let manifest: PluginManifest = toml::from_str(toml_str).expect("valid TOML");
        assert_eq!(manifest.name, "my-plugin");
        assert_eq!(manifest.version, "0.2.1");
        assert_eq!(manifest.entry_point, "my-plugin/init.lua");
        assert_eq!(manifest.events, vec!["on_output", "on_tab_created"]);
    }

    // -- Plugin event execution tests --

    #[test]
    fn test_plugin_calls_event_handler() {
        let lua = Lua::new();
        let source = r#"
            function on_command_complete(e)
                result = e.command
            end
        "#;
        let event = PluginEvent::CommandComplete {
            command: "cargo build".into(),
            exit_code: 0,
            duration_ms: 1200,
        };
        execute_plugin_event_from_source(&lua, source, &event).unwrap();

        let result: String = lua.globals().get("result").unwrap();
        assert_eq!(result, "cargo build");
    }

    #[test]
    fn test_plugin_missing_handler_is_ok() {
        let lua = Lua::new();
        let source = "-- this plugin does not handle on_command_complete";
        let event = PluginEvent::CommandComplete {
            command: "ls".into(),
            exit_code: 0,
            duration_ms: 10,
        };
        let result = execute_plugin_event_from_source(&lua, source, &event);
        assert!(result.is_ok());
    }

    #[test]
    fn test_plugin_error_propagates() {
        let lua = Lua::new();
        let source = r#"
            function on_command_complete()
                error("boom")
            end
        "#;
        let event = PluginEvent::CommandComplete {
            command: "ls".into(),
            exit_code: 0,
            duration_ms: 10,
        };
        let result = execute_plugin_event_from_source(&lua, source, &event);
        assert!(result.is_err());
    }
}
