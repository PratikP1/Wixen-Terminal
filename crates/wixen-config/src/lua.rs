//! Lua scripting layer for Wixen Terminal configuration.
//!
//! Loads `config.lua` (if present alongside `config.toml`) and exposes a
//! `wixen` API table so users can script keybindings, event hooks, and
//! dynamic config overrides.

use mlua::prelude::*;
use std::collections::HashMap;
use std::path::Path;
use tracing::{debug, error, info};

/// A keybinding registered from Lua.
#[derive(Debug, Clone)]
pub struct LuaKeybinding {
    /// Key chord (e.g., "ctrl+shift+t").
    pub chord: String,
    /// Action name to dispatch (e.g., "new_tab", "copy", "paste").
    pub action: String,
}

/// Result of running a Lua config script.
#[derive(Debug, Default)]
pub struct LuaConfigResult {
    /// Config overrides: section.key → value (e.g., "font.size" → "16")
    pub overrides: HashMap<String, String>,
    /// Keybindings declared via `wixen.bind()`
    pub keybindings: Vec<LuaKeybinding>,
}

/// Strip dangerous globals from the Lua VM to create a safe sandbox.
///
/// Luau (via mlua's `luau` feature) already omits `io`, `loadfile`, and
/// `dofile`. This function additionally removes:
///
/// - **`os`** — even though Luau's `os` only exposes time functions
///   (`clock`, `date`, `time`, `difftime`), removing it eliminates any
///   future risk if mlua adds more `os` members.
/// - **`debug`** — Luau's `debug` is limited to `traceback` and `info`,
///   but `debug.info` can leak implementation details and assist in
///   sandbox-escape research.
///
/// Safe libraries (`string`, `table`, `math`, `utf8`, `bit32`, `buffer`)
/// and the user-facing `wixen` table are left intact.
pub fn sandbox_lua(lua: &mlua::Lua) -> LuaResult<()> {
    let globals = lua.globals();
    globals.set("os", mlua::Value::Nil)?;
    globals.set("debug", mlua::Value::Nil)?;
    Ok(())
}

/// The Lua scripting engine.
pub struct LuaEngine {
    lua: Lua,
}

impl LuaEngine {
    /// Create a new Lua engine with sandbox applied and `wixen` API table pre-loaded.
    pub fn new() -> LuaResult<Self> {
        let lua = Lua::new();
        sandbox_lua(&lua)?;
        Self::setup_api(&lua)?;
        Ok(Self { lua })
    }

    /// Set up the `wixen` API table in the Lua global environment.
    fn setup_api(lua: &Lua) -> LuaResult<()> {
        let wixen = lua.create_table()?;

        // wixen.set_option(section_key, value)
        // e.g., wixen.set_option("font.size", 16)
        let overrides_table = lua.create_table()?;
        lua.set_named_registry_value("wixen_overrides", overrides_table)?;

        let set_option = lua.create_function(|lua, (key, value): (String, LuaValue)| {
            let overrides: LuaTable = lua.named_registry_value("wixen_overrides")?;
            let val_str = match value {
                LuaValue::String(s) => s.to_str()?.to_string(),
                LuaValue::Integer(n) => n.to_string(),
                LuaValue::Number(n) => n.to_string(),
                LuaValue::Boolean(b) => b.to_string(),
                _ => return Err(LuaError::external("Unsupported value type")),
            };
            overrides.set(key, val_str)?;
            Ok(())
        })?;
        wixen.set("set_option", set_option)?;

        // wixen.bind(chord, action)
        // e.g., wixen.bind("ctrl+shift+t", "new_tab")
        let bindings_table = lua.create_table()?;
        lua.set_named_registry_value("wixen_bindings", bindings_table)?;

        let bind = lua.create_function(|lua, (chord, action): (String, String)| {
            let bindings: LuaTable = lua.named_registry_value("wixen_bindings")?;
            let len = bindings.len()? + 1;
            let entry = lua.create_table()?;
            entry.set("chord", chord)?;
            entry.set("action", action)?;
            bindings.set(len, entry)?;
            Ok(())
        })?;
        wixen.set("bind", bind)?;

        // wixen.log(message)
        let log = lua.create_function(|_, msg: String| {
            info!(lua = true, "{}", msg);
            Ok(())
        })?;
        wixen.set("log", log)?;

        lua.globals().set("wixen", wixen)?;
        Ok(())
    }

    /// Load and execute a Lua config file.
    pub fn load_file(&self, path: &Path) -> LuaResult<LuaConfigResult> {
        if !path.exists() {
            debug!(path = %path.display(), "No Lua config file found");
            return Ok(LuaConfigResult::default());
        }

        let source = std::fs::read_to_string(path).map_err(LuaError::external)?;
        info!(path = %path.display(), "Loading Lua config");

        if let Err(e) = self
            .lua
            .load(&source)
            .set_name(path.to_string_lossy())
            .exec()
        {
            error!(error = %e, "Lua config error");
            return Err(e);
        }

        self.collect_results()
    }

    /// Load and execute a Lua string (for testing / inline scripts).
    pub fn load_string(&self, source: &str) -> LuaResult<LuaConfigResult> {
        self.lua.load(source).exec()?;
        self.collect_results()
    }

    /// Collect the results from the Lua registry after script execution.
    fn collect_results(&self) -> LuaResult<LuaConfigResult> {
        let mut result = LuaConfigResult::default();

        // Collect overrides
        let overrides: LuaTable = self.lua.named_registry_value("wixen_overrides")?;
        for pair in overrides.pairs::<String, String>() {
            let (key, value) = pair?;
            result.overrides.insert(key, value);
        }

        // Collect keybindings
        let bindings: LuaTable = self.lua.named_registry_value("wixen_bindings")?;
        for pair in bindings.pairs::<i64, LuaTable>() {
            let (_, entry) = pair?;
            let chord: String = entry.get("chord")?;
            let action: String = entry.get("action")?;
            result.keybindings.push(LuaKeybinding { chord, action });
        }

        Ok(result)
    }

    /// Get the underlying Lua instance (for advanced event hook dispatch).
    pub fn lua(&self) -> &Lua {
        &self.lua
    }
}

impl Default for LuaEngine {
    fn default() -> Self {
        Self::new().expect("Failed to initialize Lua engine")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_set_option() {
        let engine = LuaEngine::new().unwrap();
        let result = engine
            .load_string(
                r##"
wixen.set_option("font.size", 16)
wixen.set_option("font.family", "Consolas")
wixen.set_option("terminal.cursor_blink", false)
"##,
            )
            .unwrap();
        assert_eq!(result.overrides.get("font.size").unwrap(), "16");
        assert_eq!(result.overrides.get("font.family").unwrap(), "Consolas");
        assert_eq!(
            result.overrides.get("terminal.cursor_blink").unwrap(),
            "false"
        );
    }

    #[test]
    fn test_bind_key() {
        let engine = LuaEngine::new().unwrap();
        let result = engine
            .load_string(
                r##"
wixen.bind("ctrl+shift+t", "new_tab")
wixen.bind("ctrl+shift+w", "close_tab")
wixen.bind("ctrl+c", "copy")
"##,
            )
            .unwrap();
        assert_eq!(result.keybindings.len(), 3);
        assert_eq!(result.keybindings[0].chord, "ctrl+shift+t");
        assert_eq!(result.keybindings[0].action, "new_tab");
        assert_eq!(result.keybindings[2].action, "copy");
    }

    #[test]
    fn test_log_function() {
        let engine = LuaEngine::new().unwrap();
        engine
            .load_string(r##"wixen.log("Hello from Lua!")"##)
            .unwrap();
    }

    #[test]
    fn test_empty_script() {
        let engine = LuaEngine::new().unwrap();
        let result = engine.load_string("-- empty config").unwrap();
        assert!(result.overrides.is_empty());
        assert!(result.keybindings.is_empty());
    }

    #[test]
    fn test_lua_syntax_error() {
        let engine = LuaEngine::new().unwrap();
        let result = engine.load_string("this is not valid lua !!!");
        assert!(result.is_err());
    }

    #[test]
    fn test_conditional_config() {
        let engine = LuaEngine::new().unwrap();
        let result = engine
            .load_string(
                r##"
local dark_mode = true
if dark_mode then
    wixen.set_option("colors.background", "#1e1e2e")
    wixen.set_option("colors.foreground", "#cdd6f4")
else
    wixen.set_option("colors.background", "#ffffff")
    wixen.set_option("colors.foreground", "#000000")
end
"##,
            )
            .unwrap();
        assert_eq!(
            result.overrides.get("colors.background").unwrap(),
            "#1e1e2e"
        );
        assert_eq!(
            result.overrides.get("colors.foreground").unwrap(),
            "#cdd6f4"
        );
    }
}

#[cfg(test)]
mod sandbox_tests {
    use super::*;

    #[test]
    fn test_sandbox_blocks_os_execute() {
        let engine = LuaEngine::new().unwrap();
        let result = engine.load_string(r#"os.execute("echo hacked")"#);
        assert!(result.is_err());
    }

    #[test]
    fn test_sandbox_blocks_io_open() {
        let engine = LuaEngine::new().unwrap();
        let result = engine.load_string(r#"io.open("/etc/passwd")"#);
        assert!(result.is_err());
    }

    #[test]
    fn test_sandbox_allows_string_ops() {
        let engine = LuaEngine::new().unwrap();
        let lua = engine.lua();
        let result: String = lua.load(r#"return string.upper("hello")"#).eval().unwrap();
        assert_eq!(result, "HELLO");
    }

    #[test]
    fn test_sandbox_allows_math() {
        let engine = LuaEngine::new().unwrap();
        let lua = engine.lua();
        let result: i64 = lua.load("return math.floor(3.7)").eval().unwrap();
        assert_eq!(result, 3);
    }

    #[test]
    fn test_sandbox_blocks_loadfile() {
        let engine = LuaEngine::new().unwrap();
        let result = engine.load_string(r#"loadfile("evil.lua")"#);
        assert!(result.is_err());
    }

    #[test]
    fn test_sandbox_blocks_debug() {
        let engine = LuaEngine::new().unwrap();
        let result = engine.load_string(r#"debug.info(1, "s")"#);
        assert!(result.is_err());
    }
}
