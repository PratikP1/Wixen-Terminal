//! Built-in color themes for the terminal.
//!
//! Each theme provides fg, bg, cursor, selection_bg, and the 16-color ANSI palette
//! as `[u8; 3]` RGB triples. The rendering layer converts these to its own format.

/// A built-in theme identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BuiltinTheme {
    Default,
    Dracula,
    CatppuccinMocha,
    SolarizedDark,
    OneDark,
    GruvboxDark,
}

impl BuiltinTheme {
    /// Parse a theme name (case-insensitive, kebab/snake/space tolerant).
    pub fn from_name(name: &str) -> Option<Self> {
        match name.to_lowercase().replace([' ', '_'], "-").as_str() {
            "default" => Some(Self::Default),
            "dracula" => Some(Self::Dracula),
            "catppuccin-mocha" | "catppuccin" => Some(Self::CatppuccinMocha),
            "solarized-dark" | "solarized" => Some(Self::SolarizedDark),
            "one-dark" | "onedark" => Some(Self::OneDark),
            "gruvbox-dark" | "gruvbox" => Some(Self::GruvboxDark),
            _ => None,
        }
    }
}

/// Complete theme color data — fg, bg, cursor, selection_bg, and 16 ANSI colors.
#[derive(Debug, Clone)]
pub struct ThemeColors {
    pub fg: [u8; 3],
    pub bg: [u8; 3],
    pub cursor: [u8; 3],
    pub selection_bg: [u8; 3],
    pub palette: [[u8; 3]; 16],
}

impl ThemeColors {
    /// Convert a color to hex string "#rrggbb".
    pub fn to_hex(c: [u8; 3]) -> String {
        format!("#{:02x}{:02x}{:02x}", c[0], c[1], c[2])
    }
}

/// Get the color data for a built-in theme.
pub fn builtin_scheme(theme: BuiltinTheme) -> ThemeColors {
    match theme {
        BuiltinTheme::Default => default_theme(),
        BuiltinTheme::Dracula => dracula_theme(),
        BuiltinTheme::CatppuccinMocha => catppuccin_mocha_theme(),
        BuiltinTheme::SolarizedDark => solarized_dark_theme(),
        BuiltinTheme::OneDark => one_dark_theme(),
        BuiltinTheme::GruvboxDark => gruvbox_dark_theme(),
    }
}

fn default_theme() -> ThemeColors {
    ThemeColors {
        fg: [217, 217, 217],         // #d9d9d9
        bg: [13, 13, 20],            // #0d0d14
        cursor: [204, 204, 204],     // #cccccc
        selection_bg: [38, 79, 120], // #264f78
        palette: [
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
        ],
    }
}

fn dracula_theme() -> ThemeColors {
    ThemeColors {
        fg: [248, 248, 242],        // #f8f8f2
        bg: [40, 42, 54],           // #282a36
        cursor: [248, 248, 242],    // #f8f8f2
        selection_bg: [68, 71, 90], // #44475a
        palette: [
            [33, 34, 44],    // 0: Black
            [255, 85, 85],   // 1: Red
            [80, 250, 123],  // 2: Green
            [241, 250, 140], // 3: Yellow
            [189, 147, 249], // 4: Blue (Purple)
            [255, 121, 198], // 5: Magenta (Pink)
            [139, 233, 253], // 6: Cyan
            [248, 248, 242], // 7: White
            [98, 114, 164],  // 8: Bright Black (Comment)
            [255, 110, 110], // 9: Bright Red
            [105, 255, 148], // 10: Bright Green
            [255, 255, 165], // 11: Bright Yellow
            [210, 168, 255], // 12: Bright Blue
            [255, 146, 215], // 13: Bright Magenta
            [164, 255, 255], // 14: Bright Cyan
            [255, 255, 255], // 15: Bright White
        ],
    }
}

fn catppuccin_mocha_theme() -> ThemeColors {
    ThemeColors {
        fg: [205, 214, 244],        // #cdd6f4 (Text)
        bg: [30, 30, 46],           // #1e1e2e (Base)
        cursor: [245, 224, 220],    // #f5e0dc (Rosewater)
        selection_bg: [69, 71, 90], // #45475a (Surface1)
        palette: [
            [69, 71, 90],    // 0: Surface1
            [243, 139, 168], // 1: Red
            [166, 227, 161], // 2: Green
            [249, 226, 175], // 3: Yellow
            [137, 180, 250], // 4: Blue
            [203, 166, 247], // 5: Mauve
            [148, 226, 213], // 6: Teal
            [186, 194, 222], // 7: Subtext1
            [88, 91, 112],   // 8: Overlay0
            [243, 139, 168], // 9: Red
            [166, 227, 161], // 10: Green
            [249, 226, 175], // 11: Yellow
            [137, 180, 250], // 12: Blue
            [203, 166, 247], // 13: Mauve
            [148, 226, 213], // 14: Teal
            [205, 214, 244], // 15: Text
        ],
    }
}

fn solarized_dark_theme() -> ThemeColors {
    ThemeColors {
        fg: [131, 148, 150],       // #839496 (base0)
        bg: [0, 43, 54],           // #002b36 (base03)
        cursor: [131, 148, 150],   // #839496
        selection_bg: [7, 54, 66], // #073642 (base02)
        palette: [
            [7, 54, 66],     // 0: base02
            [220, 50, 47],   // 1: red
            [133, 153, 0],   // 2: green
            [181, 137, 0],   // 3: yellow
            [38, 139, 210],  // 4: blue
            [211, 54, 130],  // 5: magenta
            [42, 161, 152],  // 6: cyan
            [238, 232, 213], // 7: base2
            [0, 43, 54],     // 8: base03
            [203, 75, 22],   // 9: orange
            [88, 110, 117],  // 10: base01
            [101, 123, 131], // 11: base00
            [131, 148, 150], // 12: base0
            [108, 113, 196], // 13: violet
            [147, 161, 161], // 14: base1
            [253, 246, 227], // 15: base3
        ],
    }
}

fn one_dark_theme() -> ThemeColors {
    ThemeColors {
        fg: [171, 178, 191],        // #abb2bf
        bg: [40, 44, 52],           // #282c34
        cursor: [97, 175, 239],     // #61afef
        selection_bg: [62, 68, 81], // #3e4451
        palette: [
            [40, 44, 52],    // 0: Black
            [224, 108, 117], // 1: Red
            [152, 195, 121], // 2: Green
            [229, 192, 123], // 3: Yellow (Dark Yellow)
            [97, 175, 239],  // 4: Blue
            [198, 120, 221], // 5: Magenta
            [86, 182, 194],  // 6: Cyan
            [171, 178, 191], // 7: White
            [92, 99, 112],   // 8: Bright Black
            [224, 108, 117], // 9: Bright Red
            [152, 195, 121], // 10: Bright Green
            [229, 192, 123], // 11: Bright Yellow
            [97, 175, 239],  // 12: Bright Blue
            [198, 120, 221], // 13: Bright Magenta
            [86, 182, 194],  // 14: Bright Cyan
            [255, 255, 255], // 15: Bright White
        ],
    }
}

fn gruvbox_dark_theme() -> ThemeColors {
    ThemeColors {
        fg: [235, 219, 178],        // #ebdbb2
        bg: [40, 40, 40],           // #282828
        cursor: [235, 219, 178],    // #ebdbb2
        selection_bg: [80, 73, 69], // #504945
        palette: [
            [40, 40, 40],    // 0: bg
            [204, 36, 29],   // 1: red
            [152, 151, 26],  // 2: green
            [215, 153, 33],  // 3: yellow
            [69, 133, 136],  // 4: blue
            [177, 98, 134],  // 5: purple
            [104, 157, 106], // 6: aqua
            [168, 153, 132], // 7: fg4
            [146, 131, 116], // 8: gray
            [251, 73, 52],   // 9: bright red
            [184, 187, 38],  // 10: bright green
            [250, 189, 47],  // 11: bright yellow
            [131, 165, 152], // 12: bright blue
            [211, 134, 155], // 13: bright purple
            [142, 192, 124], // 14: bright aqua
            [235, 219, 178], // 15: fg
        ],
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builtin_dracula_fg() {
        let colors = builtin_scheme(BuiltinTheme::Dracula);
        assert_eq!(colors.fg, [248, 248, 242]);
    }

    #[test]
    fn test_builtin_default_matches_current() {
        let colors = builtin_scheme(BuiltinTheme::Default);
        assert_eq!(colors.fg, [217, 217, 217]);
        assert_eq!(colors.bg, [13, 13, 20]);
        assert_eq!(colors.cursor, [204, 204, 204]);
    }

    #[test]
    fn test_all_themes_have_16_palette_colors() {
        let themes = [
            BuiltinTheme::Default,
            BuiltinTheme::Dracula,
            BuiltinTheme::CatppuccinMocha,
            BuiltinTheme::SolarizedDark,
            BuiltinTheme::OneDark,
            BuiltinTheme::GruvboxDark,
        ];
        for theme in themes {
            let colors = builtin_scheme(theme);
            assert_eq!(colors.palette.len(), 16);
        }
    }

    #[test]
    fn test_from_name_case_insensitive() {
        assert_eq!(
            BuiltinTheme::from_name("Dracula"),
            Some(BuiltinTheme::Dracula)
        );
        assert_eq!(
            BuiltinTheme::from_name("DRACULA"),
            Some(BuiltinTheme::Dracula)
        );
        assert_eq!(
            BuiltinTheme::from_name("dracula"),
            Some(BuiltinTheme::Dracula)
        );
    }

    #[test]
    fn test_from_name_kebab_snake() {
        assert_eq!(
            BuiltinTheme::from_name("catppuccin-mocha"),
            Some(BuiltinTheme::CatppuccinMocha)
        );
        assert_eq!(
            BuiltinTheme::from_name("catppuccin_mocha"),
            Some(BuiltinTheme::CatppuccinMocha)
        );
        assert_eq!(
            BuiltinTheme::from_name("catppuccin"),
            Some(BuiltinTheme::CatppuccinMocha)
        );
        assert_eq!(
            BuiltinTheme::from_name("one-dark"),
            Some(BuiltinTheme::OneDark)
        );
        assert_eq!(
            BuiltinTheme::from_name("onedark"),
            Some(BuiltinTheme::OneDark)
        );
    }

    #[test]
    fn test_from_name_unknown() {
        assert_eq!(BuiltinTheme::from_name("nonexistent"), None);
    }

    #[test]
    fn test_to_hex() {
        assert_eq!(ThemeColors::to_hex([255, 128, 0]), "#ff8000");
        assert_eq!(ThemeColors::to_hex([0, 0, 0]), "#000000");
    }
}
