//! Macro/snippet system for saving and replaying command sequences.

use serde::{Deserialize, Serialize};
use std::time::Duration;

/// A single step within a macro.
#[derive(Debug, Clone, PartialEq)]
pub enum MacroStep {
    /// Send literal text to the terminal.
    SendText(String),
    /// Send a key sequence (e.g., "Enter", "Ctrl+C").
    SendKeys(String),
    /// Pause between steps.
    Wait(Duration),
    /// Type command text followed by Enter.
    RunCommand(String),
}

/// A saved macro consisting of named, ordered steps with an optional keybinding.
#[derive(Debug, Clone, PartialEq)]
pub struct Macro {
    pub name: String,
    pub description: String,
    pub steps: Vec<MacroStep>,
    pub keybinding: Option<String>,
}

/// Stores and manages a collection of macros.
#[derive(Debug, Default)]
pub struct MacroStore {
    macros: Vec<Macro>,
}

impl MacroStore {
    /// Add a macro to the store.
    pub fn add(&mut self, m: Macro) {
        self.macros.push(m);
    }

    /// Remove a macro by name. Returns `true` if found and removed.
    pub fn remove(&mut self, name: &str) -> bool {
        let before = self.macros.len();
        self.macros.retain(|m| m.name != name);
        self.macros.len() != before
    }

    /// Look up a macro by name.
    pub fn get(&self, name: &str) -> Option<&Macro> {
        self.macros.iter().find(|m| m.name == name)
    }

    /// Return all macros as a slice.
    pub fn list(&self) -> &[Macro] {
        &self.macros
    }

    /// Build a `MacroStore` from a slice of deserialized TOML macro configs.
    ///
    /// Macros with invalid step types are skipped and logged as warnings.
    /// Returns the store and a list of conversion errors (if any).
    pub fn load_from_config(configs: &[MacroConfig]) -> (Self, Vec<MacroConvertError>) {
        let mut macros = Vec::new();
        let mut errors = Vec::new();
        for c in configs {
            match c.to_macro() {
                Ok(m) => macros.push(m),
                Err(e) => errors.push(e),
            }
        }
        (Self { macros }, errors)
    }
}

/// TOML-serializable representation of a single macro step.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct MacroStepConfig {
    /// Step type: "send_text", "send_keys", "wait", "run_command".
    #[serde(rename = "type")]
    pub step_type: String,
    /// Value for the step (text, key sequence, command, or milliseconds for wait).
    pub value: String,
}

/// TOML-serializable representation of a macro.
///
/// Example TOML:
/// ```toml
/// [[macros]]
/// name = "git-status"
/// description = "Run git status"
/// steps = [{ type = "run_command", value = "git status" }]
/// keybinding = "ctrl+shift+g"
/// ```
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct MacroConfig {
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub steps: Vec<MacroStepConfig>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub keybinding: Option<String>,
}

impl MacroConfig {
    /// Convert this TOML config representation into a runtime `Macro`.
    ///
    /// Returns an error listing any step types that were not recognized.
    /// Valid step types: `send_text`, `send_keys`, `wait`, `run_command`.
    pub fn to_macro(&self) -> Result<Macro, MacroConvertError> {
        let mut steps = Vec::with_capacity(self.steps.len());
        let mut unknown = Vec::new();

        for s in &self.steps {
            match s.step_type.as_str() {
                "send_text" => steps.push(MacroStep::SendText(s.value.clone())),
                "send_keys" => steps.push(MacroStep::SendKeys(s.value.clone())),
                "wait" => {
                    let ms = s.value.parse::<u64>().map_err(|_| MacroConvertError {
                        macro_name: self.name.clone(),
                        unknown_steps: vec![format!(
                            "wait step has invalid duration: {:?}",
                            s.value
                        )],
                    })?;
                    steps.push(MacroStep::Wait(Duration::from_millis(ms)));
                }
                "run_command" => steps.push(MacroStep::RunCommand(s.value.clone())),
                other => unknown.push(other.to_string()),
            }
        }

        if !unknown.is_empty() {
            return Err(MacroConvertError {
                macro_name: self.name.clone(),
                unknown_steps: unknown,
            });
        }

        Ok(Macro {
            name: self.name.clone(),
            description: self.description.clone(),
            steps,
            keybinding: self.keybinding.clone(),
        })
    }
}

/// Error when converting a `MacroConfig` to a `Macro`.
#[derive(Debug, Clone, PartialEq)]
pub struct MacroConvertError {
    /// Name of the macro that failed conversion.
    pub macro_name: String,
    /// Step types that were not recognized.
    pub unknown_steps: Vec<String>,
}

impl std::fmt::Display for MacroConvertError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "macro {:?}: unknown step types: {}",
            self.macro_name,
            self.unknown_steps.join(", ")
        )
    }
}

impl std::error::Error for MacroConvertError {}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_macro(name: &str) -> Macro {
        Macro {
            name: name.to_string(),
            description: format!("Description for {name}"),
            steps: vec![MacroStep::RunCommand("echo hello".to_string())],
            keybinding: None,
        }
    }

    #[test]
    fn store_add_and_get() {
        let mut store = MacroStore::default();
        store.add(sample_macro("test"));
        assert!(store.get("test").is_some());
        assert_eq!(store.get("test").unwrap().name, "test");
    }

    #[test]
    fn store_remove_existing() {
        let mut store = MacroStore::default();
        store.add(sample_macro("rm-me"));
        assert!(store.remove("rm-me"));
        assert!(store.get("rm-me").is_none());
    }

    #[test]
    fn store_remove_nonexistent_returns_false() {
        let mut store = MacroStore::default();
        assert!(!store.remove("nope"));
    }

    #[test]
    fn store_list_returns_all() {
        let mut store = MacroStore::default();
        store.add(sample_macro("a"));
        store.add(sample_macro("b"));
        assert_eq!(store.list().len(), 2);
    }

    #[test]
    fn load_from_config_basic() {
        let configs = vec![MacroConfig {
            name: "git-status".to_string(),
            description: "Run git status".to_string(),
            steps: vec![MacroStepConfig {
                step_type: "run_command".to_string(),
                value: "git status".to_string(),
            }],
            keybinding: Some("ctrl+shift+g".to_string()),
        }];

        let (store, errors) = MacroStore::load_from_config(&configs);
        assert!(errors.is_empty());
        assert_eq!(store.list().len(), 1);
        let m = store.get("git-status").unwrap();
        assert_eq!(m.steps.len(), 1);
        assert_eq!(m.steps[0], MacroStep::RunCommand("git status".to_string()));
        assert_eq!(m.keybinding, Some("ctrl+shift+g".to_string()));
    }

    #[test]
    fn toml_deserialization_roundtrip() {
        let toml_str = r#"
            name = "deploy"
            description = "Deploy to prod"
            steps = [
                { type = "run_command", value = "cargo build --release" },
                { type = "wait", value = "500" },
                { type = "run_command", value = "./deploy.sh" },
            ]
            keybinding = "ctrl+shift+d"
        "#;

        let config: MacroConfig = toml::from_str(toml_str).unwrap();
        assert_eq!(config.name, "deploy");
        assert_eq!(config.steps.len(), 3);
        assert_eq!(config.keybinding, Some("ctrl+shift+d".to_string()));
    }

    #[test]
    fn empty_steps_produces_empty_macro() {
        let config = MacroConfig {
            name: "empty".to_string(),
            description: String::new(),
            steps: vec![],
            keybinding: None,
        };
        let m = config.to_macro().unwrap();
        assert!(m.steps.is_empty());
        assert_eq!(m.name, "empty");
    }

    #[test]
    fn macro_with_all_step_types() {
        let config = MacroConfig {
            name: "multi".to_string(),
            description: "All step types".to_string(),
            steps: vec![
                MacroStepConfig {
                    step_type: "send_text".to_string(),
                    value: "hello".to_string(),
                },
                MacroStepConfig {
                    step_type: "send_keys".to_string(),
                    value: "Enter".to_string(),
                },
                MacroStepConfig {
                    step_type: "wait".to_string(),
                    value: "200".to_string(),
                },
                MacroStepConfig {
                    step_type: "run_command".to_string(),
                    value: "ls -la".to_string(),
                },
            ],
            keybinding: None,
        };

        let m = config.to_macro().unwrap();
        assert_eq!(m.steps.len(), 4);
        assert_eq!(m.steps[0], MacroStep::SendText("hello".to_string()));
        assert_eq!(m.steps[1], MacroStep::SendKeys("Enter".to_string()));
        assert_eq!(m.steps[2], MacroStep::Wait(Duration::from_millis(200)));
        assert_eq!(m.steps[3], MacroStep::RunCommand("ls -la".to_string()));
    }

    #[test]
    fn toml_deserialization_array_of_macros() {
        let toml_str = r#"
            [[macros]]
            name = "one"
            description = "First"
            steps = [{ type = "send_text", value = "hi" }]

            [[macros]]
            name = "two"
            description = "Second"
            steps = []
        "#;

        #[derive(Deserialize)]
        struct Wrapper {
            macros: Vec<MacroConfig>,
        }

        let w: Wrapper = toml::from_str(toml_str).unwrap();
        assert_eq!(w.macros.len(), 2);
        assert_eq!(w.macros[0].name, "one");
        assert_eq!(w.macros[1].name, "two");

        let (store, errors) = MacroStore::load_from_config(&w.macros);
        assert!(errors.is_empty());
        assert_eq!(store.list().len(), 2);
    }

    #[test]
    fn invalid_step_type_returns_error() {
        let config = MacroConfig {
            name: "bad".to_string(),
            description: String::new(),
            steps: vec![
                MacroStepConfig {
                    step_type: "send_text".to_string(),
                    value: "hello".to_string(),
                },
                MacroStepConfig {
                    step_type: "explode".to_string(),
                    value: "boom".to_string(),
                },
            ],
            keybinding: None,
        };
        let result = config.to_macro();
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert_eq!(err.macro_name, "bad");
        assert_eq!(err.unknown_steps, vec!["explode"]);
    }

    #[test]
    fn invalid_wait_duration_returns_error() {
        let config = MacroConfig {
            name: "bad-wait".to_string(),
            description: String::new(),
            steps: vec![MacroStepConfig {
                step_type: "wait".to_string(),
                value: "not-a-number".to_string(),
            }],
            keybinding: None,
        };
        let result = config.to_macro();
        assert!(result.is_err());
    }

    #[test]
    fn load_from_config_reports_errors_for_bad_macros() {
        let configs = vec![
            MacroConfig {
                name: "good".to_string(),
                description: String::new(),
                steps: vec![MacroStepConfig {
                    step_type: "run_command".to_string(),
                    value: "ls".to_string(),
                }],
                keybinding: None,
            },
            MacroConfig {
                name: "bad".to_string(),
                description: String::new(),
                steps: vec![MacroStepConfig {
                    step_type: "teleport".to_string(),
                    value: "mars".to_string(),
                }],
                keybinding: None,
            },
        ];
        let (store, errors) = MacroStore::load_from_config(&configs);
        assert_eq!(store.list().len(), 1);
        assert_eq!(store.list()[0].name, "good");
        assert_eq!(errors.len(), 1);
        assert_eq!(errors[0].macro_name, "bad");
    }
}
