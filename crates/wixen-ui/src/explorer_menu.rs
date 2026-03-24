//! Explorer context menu integration — registers "Open Wixen Terminal here".
//!
//! Generates the `HKCU` registry key/value pairs needed to add a
//! right-click entry for directories and directory backgrounds in
//! Windows Explorer. No registry writes happen at import time; callers
//! use [`registry_commands`] to obtain entries and write them with the
//! platform registry API.

/// A single registry key + value pair to be written.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RegistryEntry {
    /// Full registry key path (e.g., `HKCU\Software\Classes\...`).
    pub key: String,
    /// Value name (`""` means the default value).
    pub name: String,
    /// Value data (REG_SZ string).
    pub value: String,
}

/// Configuration for the Explorer context menu entry.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExplorerMenuConfig {
    /// Whether the context menu registration is enabled.
    pub enabled: bool,
    /// Display label shown in the Explorer menu.
    pub label: String,
    /// Optional path to an icon file shown next to the label.
    pub icon_path: Option<String>,
}

impl Default for ExplorerMenuConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            label: "Open Wixen Terminal here".to_string(),
            icon_path: None,
        }
    }
}

/// Registry root paths for the two context menu locations.
const DIR_BG_SHELL: &str = r"HKCU\Software\Classes\Directory\Background\shell\WixenTerminal";
const DIR_SHELL: &str = r"HKCU\Software\Classes\Directory\shell\WixenTerminal";

/// Produce the registry entries needed to install (or verify) the
/// Explorer context menu for Wixen Terminal.
///
/// Returns an empty `Vec` when `config.enabled` is `false`.
///
/// Two locations are covered:
/// - `Directory\Background\shell` — right-click on empty space inside a folder.
/// - `Directory\shell` — right-click on a folder itself.
pub fn registry_commands(exe_path: &str, config: &ExplorerMenuConfig) -> Vec<RegistryEntry> {
    if !config.enabled {
        return Vec::new();
    }

    let mut entries = Vec::new();

    for root in &[DIR_BG_SHELL, DIR_SHELL] {
        // Default value — the label shown in Explorer.
        entries.push(RegistryEntry {
            key: root.to_string(),
            name: String::new(),
            value: config.label.clone(),
        });

        // Icon value — the exe (or custom icon) shown next to the label.
        let icon_value = config.icon_path.as_deref().unwrap_or(exe_path).to_string();
        entries.push(RegistryEntry {
            key: root.to_string(),
            name: "Icon".to_string(),
            value: icon_value,
        });

        // Command subkey — what Explorer runs when clicked.
        entries.push(RegistryEntry {
            key: format!(r"{root}\command"),
            name: String::new(),
            value: format!(r#""{exe_path}" --dir "%V""#),
        });
    }

    entries
}

/// Strip the `HKCU\` prefix from a registry key path, returning the subkey
/// portion suitable for use with `HKEY_CURRENT_USER`.
fn strip_hkcu_prefix(key: &str) -> &str {
    key.strip_prefix(r"HKCU\").unwrap_or(key)
}

/// Registry key paths used by the context menu (without the `HKCU\` prefix).
const DIR_BG_SHELL_SUBKEY: &str = r"Software\Classes\Directory\Background\shell\WixenTerminal";
const DIR_SHELL_SUBKEY: &str = r"Software\Classes\Directory\shell\WixenTerminal";

/// Write a set of registry entries to `HKCU`.
///
/// Each [`RegistryEntry`] is expected to have a key path starting with `HKCU\`.
/// The function creates the key (if it doesn't exist) and sets the value.
///
/// Returns `Ok(())` on success, or the first error encountered.
#[cfg(windows)]
pub fn write_registry_entries(entries: &[RegistryEntry]) -> Result<(), std::io::Error> {
    use std::io::{Error, ErrorKind};
    use windows::Win32::Foundation::ERROR_SUCCESS;
    use windows::Win32::System::Registry::{
        HKEY_CURRENT_USER, KEY_SET_VALUE, REG_OPTION_NON_VOLATILE, REG_SZ, RegCreateKeyExW,
        RegSetValueExW,
    };
    use windows::core::HSTRING;

    for entry in entries {
        let subkey = strip_hkcu_prefix(&entry.key);
        let hsubkey = HSTRING::from(subkey);
        let mut hkey = windows::Win32::System::Registry::HKEY::default();

        let status = unsafe {
            RegCreateKeyExW(
                HKEY_CURRENT_USER,
                &hsubkey,
                None,
                None,
                REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE,
                None,
                &mut hkey,
                None,
            )
        };
        if status != ERROR_SUCCESS {
            return Err(Error::new(
                ErrorKind::PermissionDenied,
                format!("RegCreateKeyExW failed: {status:?}"),
            ));
        }

        let value_name = HSTRING::from(entry.name.as_str());
        let wide: Vec<u16> = entry
            .value
            .encode_utf16()
            .chain(std::iter::once(0))
            .collect();
        let data_bytes =
            unsafe { std::slice::from_raw_parts(wide.as_ptr() as *const u8, wide.len() * 2) };

        let result = unsafe { RegSetValueExW(hkey, &value_name, None, REG_SZ, Some(data_bytes)) };

        unsafe {
            let _ = windows::Win32::System::Registry::RegCloseKey(hkey);
        }

        if result != ERROR_SUCCESS {
            return Err(Error::new(
                ErrorKind::PermissionDenied,
                format!("RegSetValueExW failed: {result:?}"),
            ));
        }
    }

    Ok(())
}

/// Delete the Wixen Terminal context menu registry keys from `HKCU`.
///
/// Removes both the directory-background and directory shell keys
/// (and all their subkeys).
#[cfg(windows)]
pub fn delete_registry_entries() -> Result<(), std::io::Error> {
    use std::io::{Error, ErrorKind};
    use windows::Win32::System::Registry::{HKEY_CURRENT_USER, RegDeleteTreeW};
    use windows::core::HSTRING;

    use windows::Win32::Foundation::{ERROR_FILE_NOT_FOUND, ERROR_SUCCESS};

    for subkey in &[DIR_BG_SHELL_SUBKEY, DIR_SHELL_SUBKEY] {
        let hsubkey = HSTRING::from(*subkey);
        let result = unsafe { RegDeleteTreeW(HKEY_CURRENT_USER, &hsubkey) };
        // ERROR_FILE_NOT_FOUND means the key doesn't exist — not an error.
        if result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND {
            return Err(Error::new(
                ErrorKind::PermissionDenied,
                format!("RegDeleteTreeW failed: {result:?}"),
            ));
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registry_commands_covers_both_shell_locations() {
        let config = ExplorerMenuConfig {
            enabled: true,
            ..Default::default()
        };
        let entries = registry_commands(r"C:\Program Files\Wixen\wixen.exe", &config);

        let has_bg = entries
            .iter()
            .any(|e| e.key.contains(r"Directory\Background\shell"));
        let has_dir = entries
            .iter()
            .any(|e| e.key.contains(r"Directory\shell\WixenTerminal"));
        assert!(has_bg, "expected Directory\\Background\\shell entries");
        assert!(has_dir, "expected Directory\\shell entries");
    }

    #[test]
    fn default_config_has_correct_label() {
        let config = ExplorerMenuConfig::default();
        assert_eq!(config.label, "Open Wixen Terminal here");
        assert!(!config.enabled);
        assert!(config.icon_path.is_none());
    }

    #[test]
    fn command_value_includes_exe_path_and_percent_v() {
        let config = ExplorerMenuConfig {
            enabled: true,
            ..Default::default()
        };
        let exe = r"C:\Wixen\wixen.exe";
        let entries = registry_commands(exe, &config);

        let commands: Vec<_> = entries
            .iter()
            .filter(|e| e.key.ends_with(r"\command"))
            .collect();
        assert_eq!(commands.len(), 2);
        for cmd in &commands {
            assert!(
                cmd.value.contains(exe),
                "command should contain exe path: {}",
                cmd.value
            );
            assert!(
                cmd.value.contains("%V"),
                "command should contain %V: {}",
                cmd.value
            );
        }
    }

    #[test]
    fn disabled_config_returns_empty_vec() {
        let config = ExplorerMenuConfig::default(); // enabled: false
        let entries = registry_commands(r"C:\whatever.exe", &config);
        assert!(entries.is_empty());
    }

    #[test]
    fn strip_hkcu_prefix_removes_prefix() {
        assert_eq!(
            strip_hkcu_prefix(r"HKCU\Software\Classes\Test"),
            r"Software\Classes\Test"
        );
    }

    #[test]
    fn strip_hkcu_prefix_no_prefix_passthrough() {
        assert_eq!(
            strip_hkcu_prefix(r"Software\Classes\Test"),
            r"Software\Classes\Test"
        );
    }

    #[cfg(windows)]
    #[test]
    fn write_registry_entries_empty_slice_succeeds() {
        let result = write_registry_entries(&[]);
        assert!(result.is_ok());
    }
}
