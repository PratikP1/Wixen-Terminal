/// Cursor shape for rendering.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum CursorShape {
    #[default]
    Block,
    Underline,
    Bar,
}

/// Terminal cursor state.
#[derive(Debug, Clone)]
pub struct Cursor {
    /// Column (0-based)
    pub col: usize,
    /// Row (0-based, relative to viewport top)
    pub row: usize,
    /// Whether the cursor is visible
    pub visible: bool,
    /// Cursor shape
    pub shape: CursorShape,
    /// Whether the cursor is blinking
    pub blinking: bool,
}

impl Default for Cursor {
    fn default() -> Self {
        Self {
            col: 0,
            row: 0,
            visible: true,
            shape: CursorShape::Block,
            blinking: true,
        }
    }
}

impl Cursor {
    pub fn move_to(&mut self, col: usize, row: usize) {
        self.col = col;
        self.row = row;
    }

    pub fn move_forward(&mut self, n: usize) {
        self.col += n;
    }

    pub fn move_down(&mut self, n: usize) {
        self.row += n;
    }
}
