//! SSH connection targets: parsing, command building, and management.

use schemars::JsonSchema;
use serde::{Deserialize, Serialize};

/// An SSH connection target stored in the config.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, JsonSchema)]
#[serde(default)]
pub struct SshTarget {
    /// Display name for this connection (e.g., "Production Server").
    pub name: String,
    /// Remote host (hostname or IP).
    pub host: String,
    /// SSH port (default 22).
    pub port: u16,
    /// Username on the remote host (empty = current user).
    pub user: String,
    /// Path to an identity/key file (empty = default).
    pub identity_file: String,
    /// Additional ssh arguments (e.g., ["-o", "StrictHostKeyChecking=no"]).
    pub extra_args: Vec<String>,
}

impl Default for SshTarget {
    fn default() -> Self {
        Self {
            name: String::new(),
            host: String::new(),
            port: 22,
            user: String::new(),
            identity_file: String::new(),
            extra_args: Vec::new(),
        }
    }
}

impl SshTarget {
    /// Build the ssh command-line arguments for this target.
    ///
    /// Returns `("ssh", args)` suitable for passing to `PtyHandle::spawn_with_shell`.
    pub fn to_command(&self) -> (String, Vec<String>) {
        ssh_target_to_command(self)
    }
}

/// Build an ssh command from a target.
///
/// Returns the program name and argument list.
///
/// Uses `ssh.exe` when compiled for Windows, `ssh` otherwise (compile-time
/// decision via `cfg!(windows)`).
pub fn ssh_target_to_command(target: &SshTarget) -> (String, Vec<String>) {
    let program = if cfg!(windows) {
        "ssh.exe".to_string()
    } else {
        "ssh".to_string()
    };

    let mut args = Vec::new();

    if target.port != 22 {
        args.push("-p".to_string());
        args.push(target.port.to_string());
    }

    if !target.identity_file.is_empty() {
        args.push("-i".to_string());
        args.push(target.identity_file.clone());
    }

    for arg in &target.extra_args {
        args.push(arg.clone());
    }

    let dest = if target.user.is_empty() {
        target.host.clone()
    } else {
        format!("{}@{}", target.user, target.host)
    };
    args.push(dest);

    (program, args)
}

/// Parse an `ssh://[user@]host[:port]` URL into an [`SshTarget`].
///
/// Returns `None` if the URL is not a valid ssh URL.
pub fn parse_ssh_url(url: &str) -> Option<SshTarget> {
    let rest = url.strip_prefix("ssh://")?;
    if rest.is_empty() {
        return None;
    }

    let (user, host_port) = if let Some(at_pos) = rest.find('@') {
        let user = &rest[..at_pos];
        if user.is_empty() {
            return None;
        }
        (user.to_string(), &rest[at_pos + 1..])
    } else {
        (String::new(), rest)
    };

    if host_port.is_empty() {
        return None;
    }

    let (host, port) = if let Some(colon_pos) = host_port.find(':') {
        let host = &host_port[..colon_pos];
        let port_str = &host_port[colon_pos + 1..];
        let port: u16 = port_str.parse().ok()?;
        (host.to_string(), port)
    } else {
        (host_port.to_string(), 22)
    };

    if host.is_empty() {
        return None;
    }

    Some(SshTarget {
        name: host.clone(),
        host,
        port,
        user,
        ..Default::default()
    })
}

/// Manages a collection of SSH targets.
#[derive(Debug, Clone, Default)]
pub struct SshManager {
    targets: Vec<SshTarget>,
}

impl SshManager {
    /// Create an empty manager.
    pub fn new() -> Self {
        Self {
            targets: Vec::new(),
        }
    }

    /// Create a manager pre-populated with the given targets.
    pub fn from_targets(targets: Vec<SshTarget>) -> Self {
        Self { targets }
    }

    /// Add a target.
    pub fn add(&mut self, target: SshTarget) {
        self.targets.push(target);
    }

    /// Remove the first target whose name matches. Returns `true` if found.
    pub fn remove(&mut self, name: &str) -> bool {
        if let Some(pos) = self.targets.iter().position(|t| t.name == name) {
            self.targets.remove(pos);
            true
        } else {
            false
        }
    }

    /// Look up a target by name.
    pub fn get(&self, name: &str) -> Option<&SshTarget> {
        self.targets.iter().find(|t| t.name == name)
    }

    /// Return all targets.
    pub fn list(&self) -> &[SshTarget] {
        &self.targets
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── ssh_target_to_command ──

    #[test]
    fn command_host_only() {
        let target = SshTarget {
            host: "example.com".into(),
            ..Default::default()
        };
        let (prog, args) = ssh_target_to_command(&target);
        if cfg!(windows) {
            assert_eq!(prog, "ssh.exe");
        } else {
            assert_eq!(prog, "ssh");
        }
        assert_eq!(args, vec!["example.com"]);
    }

    #[test]
    fn command_with_user_port_identity() {
        let target = SshTarget {
            host: "server.io".into(),
            user: "admin".into(),
            port: 2222,
            identity_file: "~/.ssh/id_ed25519".into(),
            ..Default::default()
        };
        let (_, args) = ssh_target_to_command(&target);
        assert_eq!(
            args,
            vec!["-p", "2222", "-i", "~/.ssh/id_ed25519", "admin@server.io"]
        );
    }

    #[test]
    fn command_with_extra_args() {
        let target = SshTarget {
            host: "server.io".into(),
            extra_args: vec!["-o".into(), "StrictHostKeyChecking=no".into()],
            ..Default::default()
        };
        let (_, args) = ssh_target_to_command(&target);
        assert_eq!(args, vec!["-o", "StrictHostKeyChecking=no", "server.io"]);
    }

    // ── parse_ssh_url ──

    #[test]
    fn parse_url_user_host_port() {
        let target = parse_ssh_url("ssh://deploy@prod.example.com:2222").unwrap();
        assert_eq!(target.user, "deploy");
        assert_eq!(target.host, "prod.example.com");
        assert_eq!(target.port, 2222);
    }

    #[test]
    fn parse_url_host_only() {
        let target = parse_ssh_url("ssh://myhost.local").unwrap();
        assert!(target.user.is_empty());
        assert_eq!(target.host, "myhost.local");
        assert_eq!(target.port, 22);
    }

    #[test]
    fn parse_url_invalid_returns_none() {
        assert!(parse_ssh_url("http://example.com").is_none());
        assert!(parse_ssh_url("ssh://").is_none());
        assert!(parse_ssh_url("not a url").is_none());
        assert!(parse_ssh_url("ssh://:9999").is_none());
        assert!(parse_ssh_url("ssh://user@:9999").is_none());
    }

    // ── SshManager ──

    #[test]
    fn manager_add_remove_get_list() {
        let mut mgr = SshManager::new();
        assert!(mgr.list().is_empty());

        mgr.add(SshTarget {
            name: "prod".into(),
            host: "prod.example.com".into(),
            ..Default::default()
        });
        mgr.add(SshTarget {
            name: "staging".into(),
            host: "staging.example.com".into(),
            ..Default::default()
        });

        assert_eq!(mgr.list().len(), 2);
        assert_eq!(mgr.get("prod").unwrap().host, "prod.example.com");
        assert!(mgr.get("nonexistent").is_none());

        assert!(mgr.remove("prod"));
        assert!(!mgr.remove("prod")); // already removed
        assert_eq!(mgr.list().len(), 1);
        assert!(mgr.get("prod").is_none());
    }

    // ── TOML deserialization ──

    #[test]
    fn toml_deserialize_ssh_target() {
        let toml_str = r#"
name = "prod"
host = "prod.example.com"
user = "deploy"
port = 22
"#;
        let target: SshTarget = toml::from_str(toml_str).unwrap();
        assert_eq!(target.name, "prod");
        assert_eq!(target.host, "prod.example.com");
        assert_eq!(target.user, "deploy");
        assert_eq!(target.port, 22);
    }

    // ── Default port ──

    #[test]
    fn default_port_is_22() {
        let target = SshTarget::default();
        assert_eq!(target.port, 22);
    }
}
