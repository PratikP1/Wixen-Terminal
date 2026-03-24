/// RGB color value.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Rgb {
    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }
}

/// Terminal color — indexed (0-255) or true color (RGB).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Color {
    /// Default foreground/background (theme-dependent)
    #[default]
    Default,
    /// Standard 256-color palette index
    Indexed(u8),
    /// 24-bit true color
    Rgb(Rgb),
}

/// SGR (Select Graphic Rendition) attributes for a cell.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CellAttributes {
    pub fg: Color,
    pub bg: Color,
    pub bold: bool,
    pub dim: bool,
    pub italic: bool,
    pub underline: UnderlineStyle,
    pub blink: bool,
    pub inverse: bool,
    pub hidden: bool,
    pub strikethrough: bool,
    pub overline: bool,
    pub underline_color: Color,
    /// Hyperlink ID (0 = no link). References into a hyperlink table.
    pub hyperlink_id: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum UnderlineStyle {
    #[default]
    None,
    Single,
    Double,
    Curly,
    Dotted,
    Dashed,
}

impl Default for CellAttributes {
    fn default() -> Self {
        Self {
            fg: Color::Default,
            bg: Color::Default,
            bold: false,
            dim: false,
            italic: false,
            underline: UnderlineStyle::None,
            blink: false,
            inverse: false,
            hidden: false,
            strikethrough: false,
            overline: false,
            underline_color: Color::Default,
            hyperlink_id: 0,
        }
    }
}
