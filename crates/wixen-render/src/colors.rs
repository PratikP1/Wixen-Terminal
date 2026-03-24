//! Color resolution — maps terminal Color values to RGBA float arrays.
//!
//! Supports configurable color schemes loaded from TOML config.

use wixen_core::attrs::Color;

/// Standard 16-color palette (CGA / Windows Terminal defaults).
const DEFAULT_PALETTE_16: [[u8; 3]; 16] = [
    [12, 12, 12],    // 0: Black
    [197, 15, 31],   // 1: Red
    [19, 161, 14],   // 2: Green
    [193, 156, 0],   // 3: Yellow
    [0, 55, 218],    // 4: Blue
    [136, 23, 152],  // 5: Magenta
    [58, 150, 221],  // 6: Cyan
    [204, 204, 204], // 7: White
    [118, 118, 118], // 8: Bright Black
    [231, 72, 86],   // 9: Bright Red
    [22, 198, 12],   // 10: Bright Green
    [249, 241, 165], // 11: Bright Yellow
    [59, 120, 255],  // 12: Bright Blue
    [180, 0, 158],   // 13: Bright Magenta
    [97, 214, 214],  // 14: Bright Cyan
    [242, 242, 242], // 15: Bright White
];

/// A complete terminal color scheme.
#[derive(Debug, Clone)]
pub struct ColorScheme {
    /// Default foreground color.
    pub fg: [f32; 4],
    /// Default background color (also used for clear color).
    pub bg: [f32; 4],
    /// Cursor color.
    pub cursor: [f32; 4],
    /// Selection background color.
    pub selection_bg: [f32; 4],
    /// 16-color ANSI palette.
    pub palette: [[f32; 4]; 16],
    /// Minimum WCAG contrast ratio to enforce (0.0 = disabled).
    pub min_contrast_ratio: f64,
}

impl Default for ColorScheme {
    fn default() -> Self {
        let mut palette = [[0.0f32; 4]; 16];
        for (i, [r, g, b]) in DEFAULT_PALETTE_16.iter().enumerate() {
            palette[i] = [*r as f32 / 255.0, *g as f32 / 255.0, *b as f32 / 255.0, 1.0];
        }

        Self {
            fg: [0.85, 0.85, 0.85, 1.0],           // #d9d9d9
            bg: [0.05, 0.05, 0.08, 1.0],           // #0d0d14
            cursor: [0.8, 0.8, 0.8, 0.9],          // #cccccc with slight transparency
            selection_bg: [0.15, 0.31, 0.47, 1.0], // #264f78
            palette,
            min_contrast_ratio: 0.0,
        }
    }
}

impl ColorScheme {
    /// Build a color scheme from hex string config values.
    ///
    /// Any value that fails to parse falls back to the default.
    pub fn from_hex(
        fg: &str,
        bg: &str,
        cursor: &str,
        selection_bg: &str,
        palette_overrides: &[String],
    ) -> Self {
        let defaults = Self::default();

        let fg = parse_hex_color(fg).unwrap_or(defaults.fg);
        let bg = parse_hex_color(bg).unwrap_or(defaults.bg);
        let cursor_color = parse_hex_color(cursor)
            .map(|mut c| {
                c[3] = 0.9;
                c
            }) // keep cursor slightly transparent
            .unwrap_or(defaults.cursor);
        let selection_bg = parse_hex_color(selection_bg).unwrap_or(defaults.selection_bg);

        let mut palette = defaults.palette;
        for (i, hex) in palette_overrides.iter().enumerate() {
            if i < 16
                && let Some(color) = parse_hex_color(hex)
            {
                palette[i] = color;
            }
        }

        Self {
            fg,
            bg,
            cursor: cursor_color,
            selection_bg,
            palette,
            min_contrast_ratio: 0.0,
        }
    }

    /// Resolve a terminal Color to an RGBA f32 array using this scheme.
    pub fn resolve(&self, color: Color, is_foreground: bool) -> [f32; 4] {
        match color {
            Color::Default => {
                if is_foreground {
                    self.fg
                } else {
                    self.bg
                }
            }
            Color::Indexed(idx) => {
                if idx < 16 {
                    self.palette[idx as usize]
                } else if idx < 232 {
                    // 216-color cube: 16..231
                    let n = idx - 16;
                    let b = (n % 6) * 51;
                    let g = ((n / 6) % 6) * 51;
                    let r = (n / 36) * 51;
                    [r as f32 / 255.0, g as f32 / 255.0, b as f32 / 255.0, 1.0]
                } else {
                    // Grayscale: 232..255
                    let level = (idx - 232) * 10 + 8;
                    let v = level as f32 / 255.0;
                    [v, v, v, 1.0]
                }
            }
            Color::Rgb(rgb) => [
                rgb.r as f32 / 255.0,
                rgb.g as f32 / 255.0,
                rgb.b as f32 / 255.0,
                1.0,
            ],
        }
    }

    /// Resolve a foreground color and enforce minimum contrast against the given
    /// background RGBA.  Returns the (possibly adjusted) foreground.
    pub fn resolve_fg_contrasted(&self, fg: Color, bg_rgba: [f32; 4]) -> [f32; 4] {
        let mut rgba = self.resolve(fg, true);
        if self.min_contrast_ratio > 0.0 {
            let fg_u8 = (
                (rgba[0] * 255.0) as u8,
                (rgba[1] * 255.0) as u8,
                (rgba[2] * 255.0) as u8,
            );
            let bg_u8 = (
                (bg_rgba[0] * 255.0) as u8,
                (bg_rgba[1] * 255.0) as u8,
                (bg_rgba[2] * 255.0) as u8,
            );
            let adjusted = enforce_min_contrast(fg_u8, bg_u8, self.min_contrast_ratio);
            rgba[0] = adjusted.0 as f32 / 255.0;
            rgba[1] = adjusted.1 as f32 / 255.0;
            rgba[2] = adjusted.2 as f32 / 255.0;
        }
        rgba
    }

    /// Get the background as a wgpu clear color.
    pub fn clear_color(&self) -> wgpu::Color {
        wgpu::Color {
            r: self.bg[0] as f64,
            g: self.bg[1] as f64,
            b: self.bg[2] as f64,
            a: 1.0,
        }
    }
}

/// Parse a hex color string like "#d9d9d9" or "#0d0d14" into [f32; 4] RGBA.
///
/// Supports "#RRGGBB" and "RRGGBB" (with or without leading #).
fn parse_hex_color(s: &str) -> Option<[f32; 4]> {
    let hex = s.strip_prefix('#').unwrap_or(s);
    if hex.len() != 6 {
        return None;
    }
    let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
    let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
    let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
    Some([r as f32 / 255.0, g as f32 / 255.0, b as f32 / 255.0, 1.0])
}

/// Legacy shim — resolve without a scheme (uses defaults).
///
/// Prefer `ColorScheme::resolve()` for configurable colors.
pub fn resolve_color(color: Color, is_foreground: bool) -> [f32; 4] {
    ColorScheme::default().resolve(color, is_foreground)
}

/// Compute the WCAG 2.1 relative luminance of an sRGB color.
///
/// See <https://www.w3.org/TR/WCAG21/#dfn-relative-luminance>.
pub fn relative_luminance(r: u8, g: u8, b: u8) -> f64 {
    fn linearize(channel: u8) -> f64 {
        let s = channel as f64 / 255.0;
        if s <= 0.04045 {
            s / 12.92
        } else {
            ((s + 0.055) / 1.055).powf(2.4)
        }
    }
    0.2126 * linearize(r) + 0.7152 * linearize(g) + 0.0722 * linearize(b)
}

/// Compute the WCAG 2.1 contrast ratio between two sRGB colors.
///
/// Returns a value in the range [1.0, 21.0].
pub fn contrast_ratio(fg: (u8, u8, u8), bg: (u8, u8, u8)) -> f64 {
    let l1 = relative_luminance(fg.0, fg.1, fg.2);
    let l2 = relative_luminance(bg.0, bg.1, bg.2);
    let lighter = l1.max(l2);
    let darker = l1.min(l2);
    (lighter + 0.05) / (darker + 0.05)
}

/// Adjust `fg` so that its contrast ratio against `bg` meets `min_ratio`.
///
/// If the existing ratio already meets the threshold the color is returned
/// unchanged.  Otherwise the foreground is lightened (when the background
/// is dark, luminance < 0.5) or darkened (when the background is light).
pub fn enforce_min_contrast(fg: (u8, u8, u8), bg: (u8, u8, u8), min_ratio: f64) -> (u8, u8, u8) {
    if contrast_ratio(fg, bg) >= min_ratio {
        return fg;
    }

    let bg_lum = relative_luminance(bg.0, bg.1, bg.2);
    let lighten = bg_lum < 0.5;

    let mut r = fg.0 as i16;
    let mut g = fg.1 as i16;
    let mut b = fg.2 as i16;
    let step: i16 = if lighten { 1 } else { -1 };

    for _ in 0..256 {
        r = (r + step).clamp(0, 255);
        g = (g + step).clamp(0, 255);
        b = (b + step).clamp(0, 255);
        let candidate = (r as u8, g as u8, b as u8);
        if contrast_ratio(candidate, bg) >= min_ratio {
            return candidate;
        }
    }

    // Fallback: if we saturated without meeting the ratio, return the extreme.
    (r as u8, g as u8, b as u8)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_hex_color() {
        let c = parse_hex_color("#ff8000").unwrap();
        assert!((c[0] - 1.0).abs() < 0.01);
        assert!((c[1] - 0.502).abs() < 0.01);
        assert!((c[2] - 0.0).abs() < 0.01);
        assert_eq!(c[3], 1.0);
    }

    #[test]
    fn test_parse_hex_no_hash() {
        let c = parse_hex_color("00ff00").unwrap();
        assert!((c[1] - 1.0).abs() < 0.01);
    }

    #[test]
    fn test_parse_hex_invalid() {
        assert!(parse_hex_color("not-a-color").is_none());
        assert!(parse_hex_color("#fff").is_none()); // too short
        assert!(parse_hex_color("").is_none());
    }

    #[test]
    fn test_scheme_from_hex() {
        let scheme = ColorScheme::from_hex(
            "#ffffff",
            "#000000",
            "#ff0000",
            "#0000ff",
            &["#111111".to_string()],
        );
        assert!((scheme.fg[0] - 1.0).abs() < 0.01);
        assert!((scheme.bg[0] - 0.0).abs() < 0.01);
        assert!((scheme.cursor[0] - 1.0).abs() < 0.01);
        assert!((scheme.selection_bg[2] - 1.0).abs() < 0.01);
        // Palette index 0 overridden
        assert!((scheme.palette[0][0] - 0.067).abs() < 0.01);
        // Palette index 1 still default
        assert!((scheme.palette[1][0] - 197.0 / 255.0).abs() < 0.01);
    }

    #[test]
    fn test_resolve_default_fg_bg() {
        let scheme = ColorScheme::default();
        let fg = scheme.resolve(Color::Default, true);
        let bg = scheme.resolve(Color::Default, false);
        assert!((fg[0] - 0.85).abs() < 0.01);
        assert!((bg[2] - 0.08).abs() < 0.01);
    }

    #[test]
    fn test_white_on_black_high_contrast() {
        let ratio = contrast_ratio((255, 255, 255), (0, 0, 0));
        assert!(
            (ratio - 21.0).abs() < 0.1,
            "White on black should be ~21:1, got {ratio}"
        );
    }

    #[test]
    fn test_black_on_white_high_contrast() {
        let ratio = contrast_ratio((0, 0, 0), (255, 255, 255));
        assert!(
            (ratio - 21.0).abs() < 0.1,
            "Black on white should be ~21:1, got {ratio}"
        );
    }

    #[test]
    fn test_gray_on_gray_low_contrast() {
        let ratio = contrast_ratio((119, 119, 119), (136, 136, 136));
        assert!(
            ratio < 2.0,
            "Gray on gray should have low contrast, got {ratio}"
        );
    }

    #[test]
    fn test_relative_luminance_black() {
        let lum = relative_luminance(0, 0, 0);
        assert!((lum - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_relative_luminance_white() {
        let lum = relative_luminance(255, 255, 255);
        assert!((lum - 1.0).abs() < 0.001);
    }

    #[test]
    fn test_enforce_min_contrast_high_contrast_unchanged() {
        let fg = (255, 255, 255);
        let bg = (0, 0, 0);
        let result = enforce_min_contrast(fg, bg, 4.5);
        assert_eq!(result, fg, "High-contrast pair should be unchanged");
    }

    #[test]
    fn test_enforce_min_contrast_adjusts_low_contrast_dark_bg() {
        // Dark gray on black — should lighten fg.
        let fg = (30, 30, 30);
        let bg = (0, 0, 0);
        let result = enforce_min_contrast(fg, bg, 4.5);
        let ratio = contrast_ratio(result, bg);
        assert!(
            ratio >= 4.5,
            "Adjusted ratio should meet minimum, got {ratio}"
        );
        // Result should be lighter than original.
        assert!(result.0 > fg.0, "Should have lightened fg");
    }

    #[test]
    fn test_enforce_min_contrast_adjusts_low_contrast_light_bg() {
        // Light gray on white — should darken fg.
        let fg = (230, 230, 230);
        let bg = (255, 255, 255);
        let result = enforce_min_contrast(fg, bg, 4.5);
        let ratio = contrast_ratio(result, bg);
        assert!(
            ratio >= 4.5,
            "Adjusted ratio should meet minimum, got {ratio}"
        );
        // Result should be darker than original.
        assert!(result.0 < fg.0, "Should have darkened fg");
    }

    #[test]
    fn test_resolve_indexed_256() {
        let scheme = ColorScheme::default();
        // Index 1 = red
        let red = scheme.resolve(Color::Indexed(1), true);
        assert!((red[0] - 197.0 / 255.0).abs() < 0.01);

        // Grayscale 232 = darkest gray
        let gray = scheme.resolve(Color::Indexed(232), true);
        assert!((gray[0] - 8.0 / 255.0).abs() < 0.01);
    }
}
