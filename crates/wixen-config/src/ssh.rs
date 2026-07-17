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

    // Defense in depth: a destination beginning with `-` (e.g. a hostile config
    // supplying host = "-oProxyCommand=calc.exe") would be parsed by ssh as an
    // option, smuggling arbitrary command execution. Terminate option parsing
    // with `--` so the destination is always treated literally.
    if dest.starts_with('-') {
        args.push("--".to_string());
    }
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

    // Reject users that ssh would interpret as options (argument injection):
    // e.g. `ssh://-oProxyCommand=calc@host` smuggles a command into the user field.
    if user.starts_with('-') {
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

    // Reject hosts that ssh would interpret as options (argument injection):
    // e.g. `ssh://-oProxyCommand=calc.exe` would run an arbitrary command.
    if host.starts_with('-') {
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

/// Resolve a command-palette `ssh_<name>` action id to its target.
///
/// The palette builds entry ids as `ssh_<name>` (see `CommandPalette::load_ssh_targets`),
/// so dispatch must look the target up by name, not by numeric index.
pub fn resolve_ssh_action<'a>(action: &str, targets: &'a [SshTarget]) -> Option<&'a SshTarget> {
    let name = action.strip_prefix("ssh_")?;
    targets.iter().find(|target| target.name == name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolve_ssh_action_finds_target_by_name() {
        let targets = vec![
            SshTarget {
                name: "Production".to_string(),
                host: "prod.example.com".to_string(),
                ..SshTarget::default()
            },
            SshTarget {
                name: "Staging".to_string(),
                host: "stage.example.com".to_string(),
                ..SshTarget::default()
            },
        ];
        let resolved = resolve_ssh_action("ssh_Staging", &targets);
        assert_eq!(resolved.map(|t| t.host.as_str()), Some("stage.example.com"));
    }

    #[test]
    fn resolve_ssh_action_returns_none_for_unknown_name() {
        let targets = vec![SshTarget {
            name: "Production".to_string(),
            ..SshTarget::default()
        }];
        assert!(resolve_ssh_action("ssh_Nope", &targets).is_none());
    }

    #[test]
    fn resolve_ssh_action_returns_none_without_prefix() {
        let targets = vec![SshTarget {
            name: "Production".to_string(),
            ..SshTarget::default()
        }];
        assert!(resolve_ssh_action("Production", &targets).is_none());
    }

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

    // ── Argument-injection hardening ──

    /// A host beginning with `-` would be parsed by `ssh` as an option, so a URL
    /// like `ssh://-oProxyCommand=calc.exe` could execute an arbitrary command.
    /// `parse_ssh_url` must reject it.
    #[test]
    fn parse_url_rejects_dash_host() {
        assert!(parse_ssh_url("ssh://-oProxyCommand=calc.exe").is_none());
        assert!(parse_ssh_url("ssh://-lroot").is_none());
        assert!(parse_ssh_url("ssh://user@-oProxyCommand=calc.exe").is_none());
    }

    /// A user beginning with `-` is equally dangerous and must be rejected.
    #[test]
    fn parse_url_rejects_dash_user() {
        assert!(parse_ssh_url("ssh://-oProxyCommand=calc@host.com").is_none());
    }

    /// Even when an `SshTarget` is built directly from a hostile config file
    /// (bypassing `parse_ssh_url`), `to_command` must never emit a host or user
    /// that `ssh` would interpret as an option.
    #[test]
    fn to_command_neutralizes_dash_host() {
        let target = SshTarget {
            host: "-oProxyCommand=calc.exe".into(),
            ..Default::default()
        };
        let (_, args) = ssh_target_to_command(&target);
        // The destination must be terminated from option parsing by `--`.
        let dash_dash = args.iter().position(|a| a == "--");
        assert!(dash_dash.is_some(), "expected `--` separator before host");
        let idx = dash_dash.unwrap();
        assert_eq!(
            args.get(idx + 1).map(String::as_str),
            Some("-oProxyCommand=calc.exe"),
            "host must follow the `--` separator so ssh treats it literally"
        );
    }

    #[test]
    fn to_command_no_separator_for_safe_host() {
        let target = SshTarget {
            host: "example.com".into(),
            ..Default::default()
        };
        let (_, args) = ssh_target_to_command(&target);
        assert!(
            !args.iter().any(|a| a == "--"),
            "no separator needed for a safe host"
        );
        assert_eq!(args, vec!["example.com"]);
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

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(1024))]

        /// parse_ssh_url must never panic on arbitrary input, including
        /// multibyte characters around the '@' and ':' separators.
        #[test]
        fn parse_never_panics(url in ".{0,200}") {
            let _ = parse_ssh_url(&url);
        }

        /// Any successfully parsed target has a non-empty host, and a user
        /// prefix in the URL never yields an empty user.
        #[test]
        fn parsed_targets_are_well_formed(url in "ssh://.{0,100}") {
            if let Some(target) = parse_ssh_url(&url) {
                prop_assert!(!target.host.is_empty());
                prop_assert_eq!(&target.name, &target.host);
            }
        }

        /// A well-formed ssh URL round-trips through the parser.
        #[test]
        fn well_formed_url_roundtrips(
            user in proptest::option::of("[a-z][a-z0-9]{0,10}"),
            host in "[a-z][a-z0-9.-]{0,20}",
            port in proptest::option::of(1u16..),
        ) {
            let mut url = String::from("ssh://");
            if let Some(u) = &user {
                url.push_str(u);
                url.push('@');
            }
            url.push_str(&host);
            if let Some(p) = port {
                url.push_str(&format!(":{p}"));
            }
            let target = parse_ssh_url(&url).expect("well-formed URL must parse");
            prop_assert_eq!(&target.host, &host);
            prop_assert_eq!(&target.user, user.as_deref().unwrap_or(""));
            prop_assert_eq!(target.port, port.unwrap_or(22));
        }

        /// ssh_target_to_command never panics and always ends with the
        /// destination argument.
        #[test]
        fn command_always_ends_with_destination(
            host in "[a-z][a-z0-9.-]{0,20}",
            user in "[a-z0-9]{0,10}",
            port in any::<u16>(),
            identity in ".{0,40}",
        ) {
            let target = SshTarget {
                host: host.clone(),
                user: user.clone(),
                port,
                identity_file: identity,
                ..Default::default()
            };
            let (_, args) = ssh_target_to_command(&target);
            let expected_dest = if user.is_empty() {
                host
            } else {
                format!("{user}@{host}")
            };
            prop_assert_eq!(args.last().map(String::as_str), Some(expected_dest.as_str()));
        }
    }
}
