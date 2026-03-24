//! Custom user WGSL shader support: validation, parameter management, and defaults.

use std::collections::HashMap;
use std::fmt;

/// Maximum allowed shader source size in bytes (64 KB).
const MAX_SHADER_SIZE: usize = 64 * 1024;

/// Configuration for a user-supplied post-process shader.
#[derive(Debug, Clone)]
pub struct CustomShaderConfig {
    /// Whether the custom shader pipeline is active.
    pub enabled: bool,
    /// Path to a `.wgsl` file on disk. `None` means use the built-in default.
    pub path: Option<String>,
    /// User-adjustable float parameters passed as uniforms.
    pub params: HashMap<String, f32>,
}

/// Errors produced during shader source validation.
#[derive(Debug, PartialEq)]
pub enum ShaderError {
    /// The source string is empty.
    Empty,
    /// The source exceeds the maximum allowed size (carries actual size).
    TooLarge(usize),
    /// The source does not contain an `@fragment` entry point.
    MissingFragmentEntry,
    /// An I/O error occurred while reading the shader file.
    IoError(String),
}

impl fmt::Display for ShaderError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ShaderError::Empty => write!(f, "shader source is empty"),
            ShaderError::TooLarge(size) => {
                write!(
                    f,
                    "shader source is {size} bytes, exceeding the 64 KB limit"
                )
            }
            ShaderError::MissingFragmentEntry => {
                write!(f, "shader source is missing an @fragment entry point")
            }
            ShaderError::IoError(msg) => write!(f, "I/O error: {msg}"),
        }
    }
}

/// Perform structural validation on raw WGSL source.
///
/// This does **not** compile the shader (that requires a `wgpu::Device`); it only
/// checks basic structural requirements.
pub fn validate_shader_source(source: &str) -> Result<(), ShaderError> {
    if source.is_empty() {
        return Err(ShaderError::Empty);
    }
    if source.len() > MAX_SHADER_SIZE {
        return Err(ShaderError::TooLarge(source.len()));
    }
    if !source.contains("@fragment") {
        return Err(ShaderError::MissingFragmentEntry);
    }
    Ok(())
}

/// Typed wrapper around a set of named float parameters for GPU uniforms.
#[derive(Debug, Clone, Default)]
pub struct ShaderParams {
    inner: HashMap<String, f32>,
}

impl ShaderParams {
    /// Get a parameter value, returning `0.0` if not present.
    pub fn get(&self, name: &str) -> f32 {
        self.inner.get(name).copied().unwrap_or(0.0)
    }

    /// Set (or overwrite) a parameter.
    pub fn set(&mut self, name: impl Into<String>, value: f32) {
        self.inner.insert(name.into(), value);
    }

    /// Produce a `Vec<f32>` of values sorted by key name for deterministic GPU
    /// uniform buffer layout.
    pub fn to_uniform_data(&self) -> Vec<f32> {
        let mut keys: Vec<&String> = self.inner.keys().collect();
        keys.sort();
        keys.iter().map(|k| self.inner[*k]).collect()
    }
}

/// A minimal passthrough WGSL fragment shader.
pub const DEFAULT_POST_PROCESS_SHADER: &str = r#"
@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    return textureSample(t_screen, s_screen, uv);
}
"#;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_empty_source() {
        assert_eq!(validate_shader_source(""), Err(ShaderError::Empty));
    }

    #[test]
    fn validate_too_large() {
        let big = "a".repeat(MAX_SHADER_SIZE + 1);
        assert_eq!(
            validate_shader_source(&big),
            Err(ShaderError::TooLarge(MAX_SHADER_SIZE + 1))
        );
    }

    #[test]
    fn validate_missing_fragment() {
        let src = "fn main() {}";
        assert_eq!(
            validate_shader_source(src),
            Err(ShaderError::MissingFragmentEntry)
        );
    }

    #[test]
    fn validate_valid_shader() {
        let src = "@fragment\nfn fs_main() -> @location(0) vec4<f32> { return vec4(1.0); }";
        assert!(validate_shader_source(src).is_ok());
    }

    #[test]
    fn shader_params_get_default() {
        let params = ShaderParams::default();
        assert_eq!(params.get("missing"), 0.0);
    }

    #[test]
    fn shader_params_set_and_get() {
        let mut params = ShaderParams::default();
        params.set("brightness", 0.75);
        assert_eq!(params.get("brightness"), 0.75);
    }

    #[test]
    fn shader_params_to_uniform_data_sorted() {
        let mut params = ShaderParams::default();
        params.set("zoom", 2.0);
        params.set("alpha", 0.5);
        params.set("brightness", 1.0);
        let data = params.to_uniform_data();
        // Keys sorted: alpha, brightness, zoom
        assert_eq!(data, vec![0.5, 1.0, 2.0]);
    }

    #[test]
    fn default_shader_passes_validation() {
        assert!(validate_shader_source(DEFAULT_POST_PROCESS_SHADER).is_ok());
    }

    #[test]
    fn shader_error_display() {
        assert_eq!(ShaderError::Empty.to_string(), "shader source is empty");
        assert!(ShaderError::TooLarge(99999).to_string().contains("99999"));
        assert!(
            ShaderError::MissingFragmentEntry
                .to_string()
                .contains("@fragment")
        );
        assert!(
            ShaderError::IoError("not found".into())
                .to_string()
                .contains("not found")
        );
    }
}
