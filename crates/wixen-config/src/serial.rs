//! Serial port connection profile configuration.

use serde::{Deserialize, Serialize};
use std::fmt;

/// Parity mode for serial communication.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
pub enum Parity {
    /// No parity bit.
    #[default]
    None,
    /// Even parity.
    Even,
    /// Odd parity.
    Odd,
}

impl fmt::Display for Parity {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Parity::None => write!(f, "none"),
            Parity::Even => write!(f, "even"),
            Parity::Odd => write!(f, "odd"),
        }
    }
}

/// Flow control mode for serial communication.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
pub enum FlowControl {
    /// No flow control.
    #[default]
    None,
    /// Hardware (RTS/CTS) flow control.
    Hardware,
    /// Software (XON/XOFF) flow control.
    Software,
}

impl fmt::Display for FlowControl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FlowControl::None => write!(f, "none"),
            FlowControl::Hardware => write!(f, "hardware"),
            FlowControl::Software => write!(f, "software"),
        }
    }
}

/// Configuration for a serial port connection profile.
///
/// Default is standard 9600-8-N-1 with no flow control.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct SerialConfig {
    /// Serial port name (e.g., "COM3" on Windows).
    pub port: String,
    /// Baud rate in bits per second.
    pub baud_rate: u32,
    /// Number of data bits (5, 6, 7, or 8).
    pub data_bits: u8,
    /// Number of stop bits (1 or 2).
    pub stop_bits: u8,
    /// Parity checking mode.
    pub parity: Parity,
    /// Flow control mode.
    pub flow_control: FlowControl,
}

impl Default for SerialConfig {
    fn default() -> Self {
        Self {
            port: String::new(),
            baud_rate: 9600,
            data_bits: 8,
            stop_bits: 1,
            parity: Parity::None,
            flow_control: FlowControl::None,
        }
    }
}

impl SerialConfig {
    /// Validate that all fields have legal values.
    ///
    /// Returns a list of validation errors (empty if valid).
    pub fn validate(&self) -> Vec<String> {
        let mut errors = Vec::new();
        if self.port.is_empty() {
            errors.push("port must not be empty".to_string());
        }
        if !(5..=8).contains(&self.data_bits) {
            errors.push(format!(
                "data_bits must be 5, 6, 7, or 8 (got {})",
                self.data_bits
            ));
        }
        if !matches!(self.stop_bits, 1 | 2) {
            errors.push(format!("stop_bits must be 1 or 2 (got {})", self.stop_bits));
        }
        if self.baud_rate == 0 {
            errors.push("baud_rate must be greater than 0".to_string());
        }
        errors
    }
}

/// Build a command and arguments for connecting to a serial port.
///
/// Validates the config first and returns errors if invalid.
/// Returns `("wixen-serial", args)` on success.
pub fn serial_to_profile_command(
    config: &SerialConfig,
) -> Result<(String, Vec<String>), Vec<String>> {
    let errors = config.validate();
    if !errors.is_empty() {
        return Err(errors);
    }
    let args = vec![
        "--port".to_string(),
        config.port.clone(),
        "--baud".to_string(),
        config.baud_rate.to_string(),
        "--data-bits".to_string(),
        config.data_bits.to_string(),
        "--stop-bits".to_string(),
        config.stop_bits.to_string(),
        "--parity".to_string(),
        config.parity.to_string(),
        "--flow-control".to_string(),
        config.flow_control.to_string(),
    ];
    Ok(("wixen-serial".to_string(), args))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_is_9600_8n1() {
        let config = SerialConfig::default();

        assert_eq!(config.port, "");
        assert_eq!(config.baud_rate, 9600);
        assert_eq!(config.data_bits, 8);
        assert_eq!(config.stop_bits, 1);
        assert_eq!(config.parity, Parity::None);
        assert_eq!(config.flow_control, FlowControl::None);
    }

    #[test]
    fn parity_display() {
        assert_eq!(Parity::None.to_string(), "none");
        assert_eq!(Parity::Even.to_string(), "even");
        assert_eq!(Parity::Odd.to_string(), "odd");
    }

    #[test]
    fn flow_control_display() {
        assert_eq!(FlowControl::None.to_string(), "none");
        assert_eq!(FlowControl::Hardware.to_string(), "hardware");
        assert_eq!(FlowControl::Software.to_string(), "software");
    }

    #[test]
    fn toml_deserialize_full() {
        let toml_str = r#"
            port = "COM3"
            baud_rate = 115200
            data_bits = 7
            stop_bits = 2
            parity = "even"
            flow_control = "hardware"
        "#;

        let config: SerialConfig = toml::from_str(toml_str).unwrap();

        assert_eq!(config.port, "COM3");
        assert_eq!(config.baud_rate, 115200);
        assert_eq!(config.data_bits, 7);
        assert_eq!(config.stop_bits, 2);
        assert_eq!(config.parity, Parity::Even);
        assert_eq!(config.flow_control, FlowControl::Hardware);
    }

    #[test]
    fn toml_deserialize_defaults() {
        let toml_str = r#"
            port = "COM1"
        "#;

        let config: SerialConfig = toml::from_str(toml_str).unwrap();

        assert_eq!(config.port, "COM1");
        assert_eq!(config.baud_rate, 9600);
        assert_eq!(config.data_bits, 8);
        assert_eq!(config.stop_bits, 1);
        assert_eq!(config.parity, Parity::None);
        assert_eq!(config.flow_control, FlowControl::None);
    }

    #[test]
    fn toml_deserialize_odd_parity_software_flow() {
        let toml_str = r#"
            port = "COM5"
            parity = "odd"
            flow_control = "software"
        "#;

        let config: SerialConfig = toml::from_str(toml_str).unwrap();

        assert_eq!(config.parity, Parity::Odd);
        assert_eq!(config.flow_control, FlowControl::Software);
    }

    #[test]
    fn serial_to_profile_command_builds_args() {
        let config = SerialConfig {
            port: "COM3".to_string(),
            baud_rate: 115200,
            data_bits: 8,
            stop_bits: 1,
            parity: Parity::None,
            flow_control: FlowControl::None,
        };

        let (program, args) = serial_to_profile_command(&config).unwrap();

        assert_eq!(program, "wixen-serial");
        assert_eq!(
            args,
            vec![
                "--port",
                "COM3",
                "--baud",
                "115200",
                "--data-bits",
                "8",
                "--stop-bits",
                "1",
                "--parity",
                "none",
                "--flow-control",
                "none",
            ]
        );
    }

    #[test]
    fn serial_to_profile_command_with_custom_settings() {
        let config = SerialConfig {
            port: "COM7".to_string(),
            baud_rate: 19200,
            data_bits: 7,
            stop_bits: 2,
            parity: Parity::Even,
            flow_control: FlowControl::Hardware,
        };

        let (program, args) = serial_to_profile_command(&config).unwrap();

        assert_eq!(program, "wixen-serial");
        assert!(args.contains(&"COM7".to_string()));
        assert!(args.contains(&"19200".to_string()));
        assert!(args.contains(&"7".to_string()));
        assert!(args.contains(&"2".to_string()));
        assert!(args.contains(&"even".to_string()));
        assert!(args.contains(&"hardware".to_string()));
    }

    #[test]
    fn toml_round_trip() {
        let config = SerialConfig {
            port: "COM4".to_string(),
            baud_rate: 57600,
            data_bits: 8,
            stop_bits: 1,
            parity: Parity::Odd,
            flow_control: FlowControl::Software,
        };

        let serialized = toml::to_string(&config).unwrap();
        let deserialized: SerialConfig = toml::from_str(&serialized).unwrap();

        assert_eq!(config, deserialized);
    }

    #[test]
    fn validate_rejects_invalid_data_bits() {
        let mut config = SerialConfig::default();
        config.port = "COM1".to_string();
        config.data_bits = 3;
        let errors = config.validate();
        assert!(!errors.is_empty());
        assert!(errors[0].contains("data_bits"));
    }

    #[test]
    fn validate_rejects_invalid_stop_bits() {
        let mut config = SerialConfig::default();
        config.port = "COM1".to_string();
        config.stop_bits = 5;
        let errors = config.validate();
        assert!(errors.iter().any(|e| e.contains("stop_bits")));
    }

    #[test]
    fn validate_rejects_empty_port() {
        let config = SerialConfig::default();
        let errors = config.validate();
        assert!(errors.iter().any(|e| e.contains("port")));
    }

    #[test]
    fn validate_rejects_zero_baud() {
        let mut config = SerialConfig::default();
        config.port = "COM1".to_string();
        config.baud_rate = 0;
        let errors = config.validate();
        assert!(errors.iter().any(|e| e.contains("baud_rate")));
    }

    #[test]
    fn validate_accepts_valid_config() {
        let config = SerialConfig {
            port: "COM1".to_string(),
            ..Default::default()
        };
        assert!(config.validate().is_empty());
    }

    #[test]
    fn command_rejects_invalid_config() {
        let config = SerialConfig {
            data_bits: 3,
            ..Default::default()
        };
        assert!(serial_to_profile_command(&config).is_err());
    }
}
