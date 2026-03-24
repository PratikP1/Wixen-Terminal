//! Config file watcher — polls the TOML config file for changes and computes deltas.
//!
//! Uses mtime polling (checked every ~2 seconds) rather than OS-level file watchers
//! to keep the dependency surface minimal. The main event loop already runs at ~1ms,
//! so the polling overhead is negligible.

use std::path::PathBuf;
use std::time::{Duration, Instant, SystemTime};

use tracing::{info, warn};

use crate::Config;

/// How often to poll the filesystem for config changes.
const POLL_INTERVAL: Duration = Duration::from_secs(2);

/// Describes which sections of the config changed.
pub struct ConfigDelta {
    /// The new configuration (already parsed and validated).
    pub config: Config,
    /// Whether color settings changed (foreground, background, cursor, selection, palette).
    pub colors_changed: bool,
    /// Whether font settings changed (family, size, line_height).
    pub font_changed: bool,
    /// Whether terminal behavior changed (cursor style, cursor blink, blink interval).
    pub terminal_changed: bool,
    /// Whether window settings changed (title).
    pub window_changed: bool,
    /// Whether keybinding settings changed.
    pub keybindings_changed: bool,
}

/// Watches the config file for modifications and produces deltas on change.
pub struct ConfigWatcher {
    /// Path to the TOML config file.
    path: PathBuf,
    /// Path to the companion Lua config file (sibling of TOML).
    lua_path: PathBuf,
    /// The most recently loaded config (used for diffing).
    last_config: Config,
    /// The TOML file's mtime at last successful read.
    last_modified: Option<SystemTime>,
    /// The Lua file's mtime at last successful read.
    last_lua_modified: Option<SystemTime>,
    /// Timestamp of the last poll check.
    last_poll: Instant,
}

impl ConfigWatcher {
    /// Create a watcher for the given config file path.
    ///
    /// `initial_config` should be the config that was already loaded at startup.
    /// Also watches `config.lua` alongside the TOML file for Lua scripting changes.
    pub fn new(path: PathBuf, initial_config: Config) -> Self {
        let last_modified = std::fs::metadata(&path).and_then(|m| m.modified()).ok();
        let lua_path = path.with_extension("lua");
        let last_lua_modified = std::fs::metadata(&lua_path).and_then(|m| m.modified()).ok();

        Self {
            path,
            lua_path,
            last_config: initial_config,
            last_modified,
            last_lua_modified,
            last_poll: Instant::now(),
        }
    }

    /// Force the next `poll()` call to re-read and re-parse the config file,
    /// regardless of mtime or rate-limit.
    pub fn force_reload(&mut self) {
        self.last_modified = None;
        self.last_lua_modified = None;
        self.last_poll = Instant::now() - POLL_INTERVAL - Duration::from_secs(1);
    }

    /// Poll for config file changes.
    ///
    /// Returns `Some(ConfigDelta)` when the file was modified and successfully re-parsed.
    /// Returns `None` if no change detected, file doesn't exist, or parse failed.
    ///
    /// Call this once per main-loop iteration — it internally rate-limits to ~2s intervals.
    pub fn poll(&mut self) -> Option<ConfigDelta> {
        // Rate-limit: only check mtime every POLL_INTERVAL
        if self.last_poll.elapsed() < POLL_INTERVAL {
            return None;
        }
        self.last_poll = Instant::now();

        // Check TOML file mtime
        let current_mtime = match std::fs::metadata(&self.path).and_then(|m| m.modified()) {
            Ok(t) => t,
            Err(_) => return None, // File doesn't exist or can't be read
        };

        // Check Lua file mtime (optional — may not exist)
        let current_lua_mtime = std::fs::metadata(&self.lua_path)
            .and_then(|m| m.modified())
            .ok();

        // Compare against last known mtimes — both must match for "no change"
        let toml_unchanged = self.last_modified == Some(current_mtime);
        let lua_unchanged = self.last_lua_modified == current_lua_mtime;
        if toml_unchanged && lua_unchanged {
            return None; // No change in either file
        }

        // At least one file changed — re-read everything via Config::load()
        // (Config::load handles both TOML and Lua internally)
        let new_config = match Config::load(&self.path) {
            Ok(cfg) => cfg,
            Err(e) => {
                warn!(error = %e, "Config load error during hot-reload (keeping previous config)");
                // Update mtimes so we don't retry every 2s on a broken file.
                // User will see the warning; we'll re-check when they save again.
                self.last_modified = Some(current_mtime);
                self.last_lua_modified = current_lua_mtime;
                return None;
            }
        };

        // Diff against the previous config
        let delta = ConfigDelta {
            colors_changed: new_config.colors != self.last_config.colors,
            font_changed: new_config.font != self.last_config.font,
            terminal_changed: new_config.terminal != self.last_config.terminal,
            window_changed: new_config.window != self.last_config.window,
            keybindings_changed: new_config.keybindings != self.last_config.keybindings,
            config: new_config.clone(),
        };

        // Only emit a delta if something actually changed
        if !delta.colors_changed
            && !delta.font_changed
            && !delta.terminal_changed
            && !delta.window_changed
            && !delta.keybindings_changed
        {
            // File was re-saved but the parsed config is identical — no-op
            self.last_modified = Some(current_mtime);
            self.last_lua_modified = current_lua_mtime;
            return None;
        }

        info!(
            colors = delta.colors_changed,
            font = delta.font_changed,
            terminal = delta.terminal_changed,
            window = delta.window_changed,
            keybindings = delta.keybindings_changed,
            "Config hot-reloaded"
        );

        self.last_config = new_config;
        self.last_modified = Some(current_mtime);
        self.last_lua_modified = current_lua_mtime;

        Some(delta)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    #[test]
    fn test_force_reload() {
        let dir = std::env::temp_dir().join("wixen_config_test_force_reload");
        let _ = std::fs::create_dir_all(&dir);
        let config_path = dir.join("force_config.toml");

        let initial = Config::default();
        let toml_str = toml::to_string_pretty(&initial).unwrap();
        std::fs::write(&config_path, &toml_str).unwrap();

        let mut watcher = ConfigWatcher::new(config_path.clone(), initial);

        // Immediate poll should return None (rate limited)
        assert!(watcher.poll().is_none());

        // Write a changed config (new font size)
        std::thread::sleep(Duration::from_millis(50));
        std::fs::write(
            &config_path,
            "[font]\nfamily = \"Cascadia Code\"\nsize = 20.0\nline_height = 1.0\n",
        )
        .unwrap();

        // Still rate-limited — should return None
        assert!(watcher.poll().is_none());

        // Force reload bypasses rate limit and resets mtime tracking
        watcher.force_reload();
        let delta = watcher.poll();
        assert!(delta.is_some());
        assert_eq!(delta.unwrap().config.font.size, 20.0);

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_watcher_detects_change() {
        let dir = std::env::temp_dir().join("wixen_config_test_watcher");
        let _ = std::fs::create_dir_all(&dir);
        let config_path = dir.join("test_config.toml");

        // Write initial config
        let initial = Config::default();
        let toml_str = toml::to_string_pretty(&initial).unwrap();
        std::fs::write(&config_path, &toml_str).unwrap();

        let mut watcher = ConfigWatcher::new(config_path.clone(), initial.clone());

        // Immediate poll should return None (within rate-limit)
        assert!(watcher.poll().is_none());

        // Force past the rate limit
        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        // No change yet — mtime matches
        assert!(watcher.poll().is_none());

        // Modify the file with a new font size
        std::thread::sleep(Duration::from_millis(50)); // ensure mtime difference
        {
            let mut f = std::fs::File::create(&config_path).unwrap();
            write!(
                f,
                r#"[font]
family = "Cascadia Code"
size = 18.0
line_height = 1.0
"#
            )
            .unwrap();
        }

        // Force past the rate limit again
        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        let delta = watcher.poll();
        assert!(delta.is_some());
        let delta = delta.unwrap();
        assert!(delta.font_changed);
        assert!(!delta.colors_changed);
        assert_eq!(delta.config.font.size, 18.0);

        // Cleanup
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_watcher_tolerates_bad_toml() {
        let dir = std::env::temp_dir().join("wixen_config_test_bad_toml");
        let _ = std::fs::create_dir_all(&dir);
        let config_path = dir.join("bad_config.toml");

        let initial = Config::default();
        let toml_str = toml::to_string_pretty(&initial).unwrap();
        std::fs::write(&config_path, &toml_str).unwrap();

        let mut watcher = ConfigWatcher::new(config_path.clone(), initial);

        // Write invalid TOML
        std::thread::sleep(Duration::from_millis(50));
        std::fs::write(&config_path, "this is [not valid toml !!!").unwrap();

        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        // Should return None (parse error logged)
        assert!(watcher.poll().is_none());

        // Cleanup
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_watcher_no_change_same_content() {
        let dir = std::env::temp_dir().join("wixen_config_test_same");
        let _ = std::fs::create_dir_all(&dir);
        let config_path = dir.join("same_config.toml");

        let initial = Config::default();
        let toml_str = toml::to_string_pretty(&initial).unwrap();
        std::fs::write(&config_path, &toml_str).unwrap();

        let mut watcher = ConfigWatcher::new(config_path.clone(), initial);

        // Re-write same content (mtime changes but parsed result is identical)
        std::thread::sleep(Duration::from_millis(50));
        std::fs::write(&config_path, &toml_str).unwrap();

        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        // Should return None — content didn't change
        assert!(watcher.poll().is_none());

        // Cleanup
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_watcher_missing_file() {
        let path = std::env::temp_dir().join("wixen_config_nonexistent_12345.toml");
        let _ = std::fs::remove_file(&path); // ensure it doesn't exist

        let mut watcher = ConfigWatcher::new(path, Config::default());
        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        assert!(watcher.poll().is_none());
    }

    #[test]
    fn test_watcher_detects_lua_change() {
        let dir = std::env::temp_dir().join("wixen_config_test_lua_watch");
        let _ = std::fs::create_dir_all(&dir);
        let config_path = dir.join("config.toml");
        let lua_path = dir.join("config.lua");

        // Write initial TOML config
        let initial = Config::default();
        let toml_str = toml::to_string_pretty(&initial).unwrap();
        std::fs::write(&config_path, &toml_str).unwrap();

        // No Lua file initially
        let mut watcher = ConfigWatcher::new(config_path.clone(), initial);

        // Now create a Lua file that changes font.size
        std::thread::sleep(Duration::from_millis(50));
        std::fs::write(&lua_path, "wixen.set_option(\"font.size\", 24)\n").unwrap();

        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        let delta = watcher.poll();
        assert!(delta.is_some());
        let delta = delta.unwrap();
        assert!(delta.font_changed);
        assert_eq!(delta.config.font.size, 24.0);

        // Modify only the Lua file (TOML unchanged)
        std::thread::sleep(Duration::from_millis(50));
        std::fs::write(&lua_path, "wixen.set_option(\"font.size\", 30)\n").unwrap();

        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        let delta = watcher.poll();
        assert!(delta.is_some());
        assert_eq!(delta.unwrap().config.font.size, 30.0);

        // Cleanup
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_delta_color_change() {
        let dir = std::env::temp_dir().join("wixen_config_test_color");
        let _ = std::fs::create_dir_all(&dir);
        let config_path = dir.join("color_config.toml");

        let initial = Config::default();
        let toml_str = toml::to_string_pretty(&initial).unwrap();
        std::fs::write(&config_path, &toml_str).unwrap();

        let mut watcher = ConfigWatcher::new(config_path.clone(), initial);

        // Change just the foreground color
        std::thread::sleep(Duration::from_millis(50));
        std::fs::write(
            &config_path,
            r##"[colors]
foreground = "#ffffff"
"##,
        )
        .unwrap();

        watcher.last_poll = Instant::now() - Duration::from_secs(3);

        let delta = watcher.poll().unwrap();
        assert!(delta.colors_changed);
        assert!(!delta.font_changed);
        assert!(!delta.terminal_changed);
        assert!(!delta.window_changed);
        assert_eq!(delta.config.colors.foreground, "#ffffff");

        // Cleanup
        let _ = std::fs::remove_dir_all(&dir);
    }
}
