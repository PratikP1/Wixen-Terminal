use crate::attrs::CellAttributes;

/// A single cell in the terminal grid.
#[derive(Debug, Clone)]
pub struct Cell {
    /// The character stored in this cell. Empty string for wide-char continuations.
    pub content: String,
    /// Visual attributes (colors, bold, italic, etc.)
    pub attrs: CellAttributes,
    /// Width of this cell (1 for normal, 2 for wide characters, 0 for continuation)
    pub width: u8,
}

impl Default for Cell {
    fn default() -> Self {
        Self {
            content: String::from(" "),
            attrs: CellAttributes::default(),
            width: 1,
        }
    }
}

impl Cell {
    pub fn new(content: impl Into<String>, attrs: CellAttributes) -> Self {
        Self {
            content: content.into(),
            attrs,
            width: 1,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.content == " " && self.attrs == CellAttributes::default()
    }

    pub fn clear(&mut self) {
        self.content = String::from(" ");
        self.attrs = CellAttributes::default();
        self.width = 1;
    }
}
