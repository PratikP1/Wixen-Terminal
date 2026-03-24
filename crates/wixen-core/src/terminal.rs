//! Terminal emulator — interprets VT parser actions and mutates the grid.

use crate::attrs::{CellAttributes, Color, Rgb, UnderlineStyle};
use crate::buffer::ScrollbackBuffer;
use crate::cursor::CursorShape;
use crate::grid::Grid;
use crate::hyperlink::HyperlinkStore;
use crate::image::{ImageStore, KittyChunkState};
use crate::modes::{MouseMode, TerminalModes};
use crate::selection::{GridPoint, Selection, SelectionType};
use tracing::{debug, trace};
use wixen_shell_integ::ShellIntegration;

/// Image protocol that produced an inline image.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImageProtocol {
    /// Sixel raster graphics (DEC).
    Sixel,
    /// iTerm2 inline images (OSC 1337).
    ITerm2,
    /// Kitty graphics protocol (APC G...).
    Kitty,
}

/// Announcement for an image placed on the terminal grid.
///
/// Screen readers use this to announce image placement via UIA notification.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImageAnnouncement {
    /// Image width in pixels.
    pub width: u32,
    /// Image height in pixels.
    pub height: u32,
    /// Number of cell columns the image spans.
    pub cell_cols: usize,
    /// Number of cell rows the image spans.
    pub cell_rows: usize,
    /// Which protocol placed the image.
    pub protocol: ImageProtocol,
    /// Filename if known (iTerm2 provides this, Sixel/Kitty do not).
    pub filename: Option<String>,
}

impl ImageAnnouncement {
    /// Format the announcement message for screen reader output.
    ///
    /// Includes the filename when available (iTerm2 protocol provides it).
    /// Falls back to protocol name and dimensions.
    pub fn message(&self) -> String {
        let proto = match self.protocol {
            ImageProtocol::Sixel => "Sixel",
            ImageProtocol::ITerm2 => "iTerm2",
            ImageProtocol::Kitty => "Kitty",
        };
        match &self.filename {
            Some(name) => format!(
                "{proto} image: {name} ({}\u{00d7}{} pixels)",
                self.width, self.height
            ),
            None => format!(
                "{proto} image: {}\u{00d7}{} pixels, {}\u{00d7}{} cells",
                self.width, self.height, self.cell_cols, self.cell_rows
            ),
        }
    }
}

/// Character set designator for G0/G1 (VT102).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Charset {
    /// US-ASCII (default).
    Ascii,
    /// DEC Special Graphics (line drawing characters, ESC ( 0).
    DecLineDrawing,
}

/// Map a character through the DEC Special Graphics (line drawing) charset.
///
/// Characters in the range 0x60–0x7E are replaced with box-drawing and
/// symbol Unicode equivalents. All other characters pass through unchanged.
pub fn map_line_drawing(ch: char) -> char {
    match ch {
        '`' => '◆', // U+25C6 diamond
        'a' => '▒', // U+2592 checkerboard
        'b' => '␉', // U+2409 HT symbol
        'c' => '␌', // U+240C FF symbol
        'd' => '␍', // U+240D CR symbol
        'e' => '␊', // U+240A LF symbol
        'f' => '°', // U+00B0 degree sign
        'g' => '±', // U+00B1 plus/minus
        'h' => '␤', // U+2424 NL symbol
        'i' => '␋', // U+240B VT symbol
        'j' => '┘', // U+2518 lower-right corner
        'k' => '┐', // U+2510 upper-right corner
        'l' => '┌', // U+250C upper-left corner
        'm' => '└', // U+2514 lower-left corner
        'n' => '┼', // U+253C crossing lines
        'o' => '⎺', // U+23BA scan 1
        'p' => '⎻', // U+23BB scan 3
        'q' => '─', // U+2500 horizontal line
        'r' => '⎼', // U+23BC scan 7
        's' => '⎽', // U+23BD scan 9
        't' => '├', // U+251C left tee
        'u' => '┤', // U+2524 right tee
        'v' => '┴', // U+2534 bottom tee
        'w' => '┬', // U+252C top tee
        'x' => '│', // U+2502 vertical line
        'y' => '≤', // U+2264 less-than-or-equal
        'z' => '≥', // U+2265 greater-than-or-equal
        '{' => 'π', // U+03C0 pi
        '|' => '≠', // U+2260 not-equal
        '}' => '£', // U+00A3 pound sign
        '~' => '·', // U+00B7 middle dot
        _ => ch,
    }
}

/// Scroll region (top and bottom row, inclusive, 0-based).
#[derive(Debug, Clone, Copy)]
pub struct ScrollRegion {
    pub top: usize,
    pub bottom: usize,
}

/// State for URL hover highlighting.
///
/// When the mouse cursor is over a detected URL (OSC 8 or regex),
/// this stores the range and URL for the renderer to underline.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HoverState {
    /// The URL being hovered.
    pub url: String,
    /// Viewport row containing the URL.
    pub viewport_row: usize,
    /// Column where the URL starts (inclusive, 0-based).
    pub col_start: usize,
    /// Column where the URL ends (exclusive, 0-based).
    pub col_end: usize,
}

/// An image visible in the current viewport with pixel coordinates.
///
/// Returned by [`Terminal::visible_images`]. Pixel coordinates are relative to
/// the top-left of the terminal grid area (the renderer adds tab bar offset).
pub struct ViewportImage<'a> {
    /// Reference to the RGBA pixel data (row-major, 4 bytes per pixel).
    pub pixels: &'a [u8],
    /// Image width in pixels.
    pub width: u32,
    /// Image height in pixels.
    pub height: u32,
    /// X position in pixels (column * cell_width).
    pub px_x: f32,
    /// Y position in pixels (viewport-relative row * cell_height, can be negative).
    pub px_y: f32,
}

/// ConEmu-compatible progress state (OSC 9;4).
///
/// Terminals can report progress to the host via `OSC 9 ; 4 ; <state> ; <value> ST`.
/// The host then reflects this on the Windows taskbar via `ITaskbarList3`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ProgressState {
    /// No progress indicator (hidden).
    #[default]
    Hidden,
    /// Normal progress (0–100).
    Normal(u8),
    /// Error state (red taskbar, value 0–100).
    Error(u8),
    /// Indeterminate (pulsing animation).
    Indeterminate,
    /// Paused (yellow taskbar, value 0–100).
    Paused(u8),
}

/// Controls whether programs can read/write the system clipboard via OSC 52.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Osc52Policy {
    /// Programs can write to clipboard but not read it (safe default).
    #[default]
    WriteOnly,
    /// Programs can both read and write clipboard.
    ReadWrite,
    /// OSC 52 is completely disabled.
    Disabled,
}

/// The terminal emulator state.
pub struct Terminal {
    /// Primary screen grid
    pub grid: Grid,
    /// Alternate screen grid (for TUI apps)
    alt_grid: Option<Grid>,
    /// Scrollback buffer
    pub scrollback: ScrollbackBuffer,
    /// Terminal modes
    pub modes: TerminalModes,
    /// Scroll region
    scroll_region: ScrollRegion,
    /// Saved cursor (DECSC/DECRC)
    saved_cursor: Option<SavedCursor>,
    /// Saved cursor for alternate screen (used in future DECSC/DECRC on alt screen)
    _saved_cursor_alt: Option<SavedCursor>,
    /// Window title (set via OSC 0/2)
    pub title: String,
    /// Whether the title has been updated since last checked.
    pub title_dirty: bool,
    /// Tab stops
    tab_stops: Vec<bool>,
    /// Whether the terminal needs redrawing
    pub dirty: bool,
    /// Pending wrap — cursor is at the right margin but hasn't wrapped yet
    pending_wrap: bool,
    /// Viewport offset into scrollback (0 = live, >0 = scrolled back N lines)
    pub viewport_offset: usize,
    /// Active text selection (None = no selection)
    pub selection: Option<Selection>,
    /// Shell integration state (OSC 133 command blocks)
    pub shell_integ: ShellIntegration,
    /// OSC 8 hyperlink store
    pub hyperlinks: HyperlinkStore,
    /// Bell pending flag — set by BEL char, cleared by the UI layer after handling
    pub bell_pending: bool,
    /// Responses to send back to the PTY (DSR, DECRQSS, etc.).
    pub pending_responses: Vec<Vec<u8>>,
    /// DCS accumulator: final char from DcsHook
    dcs_action: char,
    /// DCS accumulator: intermediates from DcsHook
    dcs_intermediates: Vec<u8>,
    /// DCS accumulator: params from DcsHook
    dcs_params: Vec<u16>,
    /// DCS accumulator: collected data bytes
    dcs_data: Vec<u8>,
    /// Pending clipboard writes from OSC 52. The main loop drains and handles these.
    clipboard_writes: Vec<String>,
    /// Whether an OSC 52 clipboard read query is pending.
    pub clipboard_read_pending: bool,
    /// OSC 52 clipboard access policy.
    pub osc52_policy: Osc52Policy,
    /// Inline images (Sixel, iTerm2, Kitty).
    pub images: ImageStore,
    /// Cell dimensions for image grid placement (set by renderer).
    pub cell_pixel_width: f32,
    /// Cell height in pixels for image grid placement (set by renderer).
    pub cell_pixel_height: f32,
    /// Kitty graphics protocol chunk accumulator.
    kitty_chunks: KittyChunkState,
    /// Current URL hover state (None = nothing hovered).
    hover: Option<HoverState>,
    /// ConEmu-compatible progress state (OSC 9;4).
    pub progress: ProgressState,
    /// G0 charset designator (default ASCII).
    g0: Charset,
    /// G1 charset designator (default ASCII).
    g1: Charset,
    /// Active charset (GL — maps to G0 or G1 via SI/SO).
    gl: Charset,
    /// Whether tmux control mode is active.
    tmux_control: bool,
    /// Buffered tmux control mode output lines.
    tmux_lines: Vec<String>,
    /// Default foreground color (RGB, set by renderer/config for OSC 10 queries).
    default_fg: [u8; 3],
    /// Default background color (RGB, set by renderer/config for OSC 11 queries).
    default_bg: [u8; 3],
    /// Default cursor color (RGB, set by renderer/config for OSC 12 queries).
    default_cursor_color: [u8; 3],
    /// Custom palette overrides (OSC 4 set). Key = index 0–255, value = RGB.
    palette_overrides: std::collections::HashMap<u8, [u8; 3]>,
    /// Default 16-color palette (set from config, used for OSC 4 query fallback).
    default_palette: [[u8; 3]; 16],
    /// Transmitted-but-not-displayed Kitty images, keyed by image_id.
    transmitted_images: std::collections::HashMap<u32, TransmittedImage>,
    /// Pending image placement announcements for screen readers.
    pub pending_image_announcements: Vec<ImageAnnouncement>,
    /// Whether echo is currently suppressed (password/no-echo mode).
    pub echo_suppressed: bool,
    /// Pending echo check: the typed character and when it was typed.
    pub echo_check_pending: Option<(char, std::time::Instant)>,
}

/// A Kitty image that has been transmitted (a=t) but not yet displayed (a=p).
#[derive(Debug, Clone)]
struct TransmittedImage {
    width: u32,
    height: u32,
    pixels: Vec<u8>,
}

#[derive(Debug, Clone)]
struct SavedCursor {
    col: usize,
    row: usize,
    attrs: CellAttributes,
    auto_wrap: bool,
    origin_mode: bool,
    g0: Charset,
    g1: Charset,
    gl: Charset,
}

impl Terminal {
    pub fn new(cols: usize, rows: usize) -> Self {
        let mut tab_stops = vec![false; cols];
        for i in (0..cols).step_by(8) {
            tab_stops[i] = true;
        }

        Self {
            grid: Grid::new(cols, rows),
            alt_grid: None,
            scrollback: ScrollbackBuffer::new(),
            modes: TerminalModes::default(),
            scroll_region: ScrollRegion {
                top: 0,
                bottom: rows.saturating_sub(1),
            },
            saved_cursor: None,
            _saved_cursor_alt: None,
            title: String::from("Wixen Terminal"),
            title_dirty: false,
            tab_stops,
            dirty: true,
            pending_wrap: false,
            viewport_offset: 0,
            selection: None,
            shell_integ: ShellIntegration::new(),
            hyperlinks: HyperlinkStore::new(),
            bell_pending: false,
            pending_responses: Vec::new(),
            dcs_action: '\0',
            dcs_intermediates: Vec::new(),
            dcs_params: Vec::new(),
            dcs_data: Vec::new(),
            clipboard_writes: Vec::new(),
            clipboard_read_pending: false,
            osc52_policy: Osc52Policy::default(),
            images: ImageStore::new(),
            cell_pixel_width: 8.0,
            cell_pixel_height: 16.0,
            kitty_chunks: KittyChunkState::new(),
            hover: None,
            progress: ProgressState::Hidden,
            g0: Charset::Ascii,
            g1: Charset::Ascii,
            gl: Charset::Ascii,
            tmux_control: false,
            tmux_lines: Vec::new(),
            default_fg: [0xd9, 0xd9, 0xd9],
            default_bg: [0x0d, 0x0d, 0x14],
            default_cursor_color: [0xcc, 0xcc, 0xcc],
            palette_overrides: std::collections::HashMap::new(),
            default_palette: Self::default_ansi_palette(),
            transmitted_images: std::collections::HashMap::new(),
            pending_image_announcements: Vec::new(),
            echo_suppressed: false,
            echo_check_pending: None,
        }
    }

    /// Standard 16-color ANSI palette (matches VGA defaults).
    fn default_ansi_palette() -> [[u8; 3]; 16] {
        [
            [0x00, 0x00, 0x00], // 0: Black
            [0xaa, 0x00, 0x00], // 1: Red
            [0x00, 0xaa, 0x00], // 2: Green
            [0xaa, 0x55, 0x00], // 3: Yellow/Brown
            [0x00, 0x00, 0xaa], // 4: Blue
            [0xaa, 0x00, 0xaa], // 5: Magenta
            [0x00, 0xaa, 0xaa], // 6: Cyan
            [0xaa, 0xaa, 0xaa], // 7: White
            [0x55, 0x55, 0x55], // 8: Bright Black
            [0xff, 0x55, 0x55], // 9: Bright Red
            [0x55, 0xff, 0x55], // 10: Bright Green
            [0xff, 0xff, 0x55], // 11: Bright Yellow
            [0x55, 0x55, 0xff], // 12: Bright Blue
            [0xff, 0x55, 0xff], // 13: Bright Magenta
            [0x55, 0xff, 0xff], // 14: Bright Cyan
            [0xff, 0xff, 0xff], // 15: Bright White
        ]
    }

    pub fn cols(&self) -> usize {
        self.grid.cols
    }

    pub fn rows(&self) -> usize {
        self.grid.rows
    }

    /// Get the effective tab title.
    ///
    /// Returns the OSC 0/2 title if explicitly set, or derives one from
    /// the working directory (OSC 7). Falls back to the default title.
    pub fn effective_title(&self) -> &str {
        // If OSC 0/2 set a title, use it
        if self.title != "Wixen Terminal" {
            return &self.title;
        }
        // Otherwise derive from CWD
        if let Some(cwd) = self.shell_integ.cwd() {
            // Extract the last path segment
            cwd.rsplit(['/', '\\'])
                .find(|s| !s.is_empty())
                .unwrap_or(cwd)
        } else {
            &self.title
        }
    }

    /// Drain pending PTY responses (DSR, DECRQSS, etc.). Call after dispatching actions.
    pub fn drain_responses(&mut self) -> Vec<Vec<u8>> {
        std::mem::take(&mut self.pending_responses)
    }

    /// Drain pending clipboard write requests from OSC 52.
    pub fn drain_clipboard_writes(&mut self) -> Vec<String> {
        std::mem::take(&mut self.clipboard_writes)
    }

    /// Drain pending image placement announcements for screen reader output.
    pub fn drain_image_announcements(&mut self) -> Vec<ImageAnnouncement> {
        std::mem::take(&mut self.pending_image_announcements)
    }

    /// Check whether the echo timeout has elapsed for a pending character.
    ///
    /// Returns `true` on the first transition into echo-suppressed mode
    /// (i.e., the character was typed 200ms ago and never echoed back).
    /// Subsequent calls return `false` until echo resumes.
    pub fn check_echo_timeout(&mut self) -> bool {
        let Some((_, typed_at)) = self.echo_check_pending else {
            return false;
        };
        if typed_at.elapsed() < std::time::Duration::from_millis(200) {
            return false;
        }
        self.echo_check_pending = None;
        if self.echo_suppressed {
            return false;
        }
        self.echo_suppressed = true;
        true
    }

    /// Called when the terminal echoes back a character that matches
    /// the pending echo check.  Clears the pending state and resets
    /// the suppressed flag (echo has resumed).
    pub fn on_char_echoed(&mut self, ch: char) {
        if let Some((pending_ch, _)) = self.echo_check_pending
            && pending_ch == ch
        {
            self.echo_check_pending = None;
            self.echo_suppressed = false;
        }
    }

    /// Check if a transmitted (but not yet displayed) Kitty image exists for the given ID.
    pub fn has_transmitted_image(&self, image_id: u32) -> bool {
        self.transmitted_images.contains_key(&image_id)
    }

    /// Set the default fg/bg/cursor colors (called from config/renderer layer).
    pub fn set_default_colors(&mut self, fg: [u8; 3], bg: [u8; 3], cursor: [u8; 3]) {
        self.default_fg = fg;
        self.default_bg = bg;
        self.default_cursor_color = cursor;
    }

    /// Get a palette color override by index (0–255).
    /// Returns `Some(rgb)` if overridden via OSC 4, `None` if using default.
    pub fn palette_color(&self, index: u8) -> Option<[u8; 3]> {
        self.palette_overrides.get(&index).copied()
    }

    /// Get the effective palette color (override or default for 0–15, computed for 16–255).
    fn effective_palette_color(&self, index: u8) -> [u8; 3] {
        if let Some(c) = self.palette_overrides.get(&index) {
            return *c;
        }
        if (index as usize) < 16 {
            return self.default_palette[index as usize];
        }
        // 6x6x6 cube (indices 16–231)
        if index < 232 {
            let idx = (index - 16) as usize;
            let b = (idx % 6) as u8;
            let g = ((idx / 6) % 6) as u8;
            let r = (idx / 36) as u8;
            let to_byte = |v: u8| if v == 0 { 0 } else { 55 + 40 * v };
            return [to_byte(r), to_byte(g), to_byte(b)];
        }
        // Grayscale ramp (indices 232–255)
        let v = 8 + 10 * (index - 232);
        [v, v, v]
    }

    /// Format an RGB color as an xterm OSC response: `rgb:RRRR/GGGG/BBBB`
    /// (each channel is scaled to 16-bit by doubling the hex digit).
    fn format_xterm_rgb(c: [u8; 3]) -> String {
        format!(
            "rgb:{:02x}{:02x}/{:02x}{:02x}/{:02x}{:02x}",
            c[0], c[0], c[1], c[1], c[2], c[2]
        )
    }

    /// Parse an xterm color spec like `rgb:RR/GG/BB` or `#RRGGBB` into [u8; 3].
    fn parse_color_spec(spec: &str) -> Option<[u8; 3]> {
        if let Some(rest) = spec.strip_prefix("rgb:") {
            let parts: Vec<&str> = rest.split('/').collect();
            if parts.len() == 3 {
                // Each part can be 1, 2, or 4 hex digits — take the high byte
                let parse_component = |s: &str| -> Option<u8> {
                    let val = u16::from_str_radix(s, 16).ok()?;
                    Some(if s.len() <= 2 {
                        val as u8
                    } else {
                        (val >> 8) as u8
                    })
                };
                let r = parse_component(parts[0])?;
                let g = parse_component(parts[1])?;
                let b = parse_component(parts[2])?;
                return Some([r, g, b]);
            }
        } else if let Some(hex) = spec.strip_prefix('#')
            && hex.len() == 6
        {
            let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
            let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
            let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
            return Some([r, g, b]);
        }
        None
    }

    /// Inject a clipboard response for an OSC 52 read query.
    ///
    /// The response is formatted as `OSC 52 ; c ; <base64> BEL` and queued
    /// in `pending_responses` for the PTY to send back to the program.
    pub fn inject_clipboard_response(&mut self, text: &str) {
        use base64::Engine;
        let encoded = base64::engine::general_purpose::STANDARD.encode(text);
        let response = format!("\x1b]52;c;{}\x07", encoded);
        self.pending_responses.push(response.into_bytes());
        self.clipboard_read_pending = false;
    }

    /// Get a cell at a viewport-relative position, accounting for scrollback offset.
    ///
    /// When `viewport_offset == 0`, this returns `grid.cell(col, row)` (live view).
    /// When scrolled back, rows at the top of the viewport come from the scrollback buffer.
    pub fn viewport_cell(&self, col: usize, viewport_row: usize) -> Option<&crate::cell::Cell> {
        let scrollback_len = self.scrollback.len();
        let abs_row = scrollback_len.saturating_sub(self.viewport_offset) + viewport_row;

        if abs_row < scrollback_len {
            // In scrollback buffer
            self.scrollback.get(abs_row).and_then(|row| row.get(col))
        } else {
            // In active grid
            let grid_row = abs_row - scrollback_len;
            self.grid.cell(col, grid_row)
        }
    }

    /// Get the hyperlink at a viewport-relative position, if any.
    ///
    /// First checks for an explicit OSC 8 hyperlink. If none is found, falls
    /// back to regex-based URL detection on the row text.
    pub fn hyperlink_at(&self, col: usize, viewport_row: usize) -> Option<String> {
        if let Some(cell) = self.viewport_cell(col, viewport_row)
            && cell.attrs.hyperlink_id > 0
            && let Some(link) = self.hyperlinks.get(cell.attrs.hyperlink_id)
        {
            return Some(link.uri.clone());
        }

        // Fallback: regex URL detection on the row text.
        let row_text = self.viewport_row_text(viewport_row);
        crate::url::url_at_col(&row_text, col)
    }

    /// Update the URL hover state for the given viewport-relative position.
    ///
    /// If the cursor is over a URL (OSC 8 or regex-detected), the hover state
    /// is set with the URL range. Otherwise it is cleared. The renderer uses
    /// this to underline the hovered URL.
    pub fn update_hover(&mut self, col: usize, viewport_row: usize) {
        // Check for OSC 8 hyperlink first
        if let Some(cell) = self.viewport_cell(col, viewport_row)
            && cell.attrs.hyperlink_id > 0
            && let Some(link) = self.hyperlinks.get(cell.attrs.hyperlink_id)
        {
            // Find the span of cells with this hyperlink_id
            let link_id = cell.attrs.hyperlink_id;
            let cols = self.grid.cols;
            let mut start = col;
            let mut end = col + 1;
            while start > 0 {
                if let Some(c) = self.viewport_cell(start - 1, viewport_row)
                    && c.attrs.hyperlink_id == link_id
                {
                    start -= 1;
                } else {
                    break;
                }
            }
            while end < cols {
                if let Some(c) = self.viewport_cell(end, viewport_row)
                    && c.attrs.hyperlink_id == link_id
                {
                    end += 1;
                } else {
                    break;
                }
            }
            self.hover = Some(HoverState {
                url: link.uri.clone(),
                viewport_row,
                col_start: start,
                col_end: end,
            });
            self.dirty = true;
            return;
        }

        // Fallback: regex URL detection
        let row_text = self.viewport_row_text(viewport_row);
        let matches = crate::url::detect_urls(&row_text);
        if let Some(m) = matches
            .into_iter()
            .find(|m| col >= m.col_start && col < m.col_end)
        {
            self.hover = Some(HoverState {
                url: m.url,
                viewport_row,
                col_start: m.col_start,
                col_end: m.col_end,
            });
            self.dirty = true;
        } else {
            if self.hover.is_some() {
                self.dirty = true;
            }
            self.hover = None;
        }
    }

    /// Get the current URL hover state.
    pub fn hover_state(&self) -> Option<&HoverState> {
        self.hover.as_ref()
    }

    /// Clear the hover state (e.g., when mouse leaves the terminal area).
    pub fn clear_hover(&mut self) {
        if self.hover.is_some() {
            self.hover = None;
            self.dirty = true;
        }
    }

    /// Extract the plain-text content of a viewport-relative row.
    fn viewport_row_text(&self, viewport_row: usize) -> String {
        let cols = self.grid.cols;
        (0..cols)
            .map(|c| {
                self.viewport_cell(c, viewport_row)
                    .and_then(|cell| {
                        if cell.width == 0 {
                            None
                        } else {
                            cell.content.chars().next()
                        }
                    })
                    .unwrap_or(' ')
            })
            .collect()
    }

    /// Check whether the cursor should be displayed in the current viewport.
    ///
    /// The cursor is only visible when the viewport is at the live position (offset 0).
    pub fn cursor_in_viewport(&self) -> bool {
        self.viewport_offset == 0
    }

    /// Scroll the viewport into scrollback. Positive delta = scroll up (into history).
    /// Negative delta = scroll down (towards live terminal).
    pub fn scroll_viewport(&mut self, delta: i32) {
        let max_offset = self.scrollback.len();
        let new_offset = (self.viewport_offset as i32 + delta).clamp(0, max_offset as i32);
        let new_offset = new_offset as usize;
        if new_offset != self.viewport_offset {
            self.viewport_offset = new_offset;
            self.dirty = true;
        }
    }

    /// Snap viewport back to live terminal (offset 0).
    #[inline]
    pub fn snap_to_bottom(&mut self) {
        if self.viewport_offset != 0 {
            self.viewport_offset = 0;
            self.dirty = true;
        }
    }

    /// Scroll the viewport so the start of the current selection is visible.
    ///
    /// Selection rows are grid-relative (0..rows). The selection is always
    /// on the currently-visible grid, so the simplest correct behaviour is:
    /// if the viewport is scrolled into history, snap to the live terminal
    /// so the grid (and its selection) is fully visible.
    ///
    /// Returns `true` if the viewport was adjusted.
    pub fn scroll_to_selection(&mut self) -> bool {
        if self.selection.is_none() {
            return false;
        }
        if self.viewport_offset != 0 {
            self.viewport_offset = 0;
            self.dirty = true;
            return true;
        }
        false
    }

    /// Scroll the viewport to make the cursor visible (snap to live terminal).
    /// Returns `true` if the viewport was adjusted.
    pub fn scroll_to_cursor(&mut self) -> bool {
        if self.viewport_offset != 0 {
            self.viewport_offset = 0;
            self.dirty = true;
            true
        } else {
            false
        }
    }

    /// Return images visible in the current viewport with viewport-relative positions.
    ///
    /// Each returned `ViewportImage` has pixel coordinates relative to the top-left
    /// of the terminal grid area (row 0, col 0). The renderer adds any tab bar offset.
    pub fn visible_images(&self, cell_width: f32, cell_height: f32) -> Vec<ViewportImage<'_>> {
        let scrollback_len = self.scrollback.len();
        let viewport_start = scrollback_len.saturating_sub(self.viewport_offset);
        let viewport_end = viewport_start + self.rows();
        let mut result = Vec::new();

        for img in self.images.images() {
            let img_end_row = img.row + img.cell_rows;
            // Skip images entirely above or below the viewport
            if img_end_row <= viewport_start || img.row >= viewport_end {
                continue;
            }
            // Viewport-relative row (can be negative if image starts above viewport)
            let rel_row = img.row as i32 - viewport_start as i32;
            let px_x = img.col as f32 * cell_width;
            let px_y = rel_row as f32 * cell_height;
            result.push(ViewportImage {
                pixels: &img.pixels,
                width: img.width,
                height: img.height,
                px_x,
                px_y,
            });
        }
        result
    }

    // -----------------------------------------------------------------------
    // Command-block navigation (OSC 133 powered)
    // -----------------------------------------------------------------------

    /// Jump to the previous command prompt.
    ///
    /// Finds the command block whose prompt is above the current viewport top
    /// and scrolls so that prompt is at the top of the viewport.
    /// Returns `true` if a jump occurred.
    pub fn jump_to_previous_prompt(&mut self) -> bool {
        let blocks = self.shell_integ.blocks();
        if blocks.is_empty() {
            return false;
        }

        let scrollback_len = self.scrollback.len();
        // Current viewport top in absolute row coordinates
        let viewport_top = scrollback_len.saturating_sub(self.viewport_offset);

        // Find the last block whose prompt.start is strictly above the current viewport top
        let target = blocks
            .iter()
            .rev()
            .find_map(|b| b.prompt.filter(|p| p.start < viewport_top));

        if let Some(prompt) = target {
            let prompt_row = prompt.start;
            // Set viewport_offset to show prompt_row at the top
            self.viewport_offset = scrollback_len.saturating_sub(prompt_row);
            self.dirty = true;
            true
        } else {
            false
        }
    }

    /// Jump to the next command prompt.
    ///
    /// Finds the command block whose prompt is below the current viewport top
    /// and scrolls so that prompt is at the top of the viewport.
    /// Returns `true` if a jump occurred.
    pub fn jump_to_next_prompt(&mut self) -> bool {
        let blocks = self.shell_integ.blocks();
        if blocks.is_empty() {
            return false;
        }

        let scrollback_len = self.scrollback.len();
        let viewport_top = scrollback_len.saturating_sub(self.viewport_offset);

        // Find the first block whose prompt.start is strictly below the current viewport top
        let target = blocks
            .iter()
            .find_map(|b| b.prompt.filter(|p| p.start > viewport_top));

        if let Some(prompt) = target {
            let prompt_row = prompt.start;
            let new_offset = scrollback_len.saturating_sub(prompt_row);
            // Don't go below live (offset 0)
            self.viewport_offset = new_offset;
            self.dirty = true;
            true
        } else {
            // Jump to live position
            if self.viewport_offset > 0 {
                self.viewport_offset = 0;
                self.dirty = true;
                true
            } else {
                false
            }
        }
    }

    /// Get the output text of the last completed command block.
    ///
    /// Returns `None` if there are no completed blocks or the last one has no output.
    pub fn last_command_output(&self) -> Option<String> {
        self.shell_integ
            .blocks()
            .iter()
            .rev()
            .find(|b| b.state == wixen_shell_integ::BlockState::Completed && b.output.is_some())
            .and_then(|b| {
                let range = b.output?;
                let text = self.extract_row_text(range.start, range.end);
                let trimmed = text.trim().to_string();
                if trimmed.is_empty() {
                    None
                } else {
                    Some(trimmed)
                }
            })
    }

    /// Get the command text of the last completed command block.
    ///
    /// Returns `None` if there are no completed blocks.
    pub fn last_command_text(&self) -> Option<String> {
        self.shell_integ
            .blocks()
            .iter()
            .rev()
            .find(|b| b.state == wixen_shell_integ::BlockState::Completed)
            .and_then(|b| b.command_text.clone())
    }

    /// Write a "[Process exited ...]" banner to the terminal grid.
    ///
    /// Called when the PTY child process exits. The banner appears below
    /// the last output and includes the exit code (if available) plus a
    /// hint to press Enter to restart.
    pub fn write_exit_banner(&mut self, code: Option<u32>) {
        // Move to next line after any existing output
        let row = self.grid.cursor.row;
        if row < self.grid.rows.saturating_sub(1) {
            self.grid.cursor.row = row + 1;
        }
        self.grid.cursor.col = 0;

        // Build banner text
        let line1 = match code {
            Some(c) => format!("[Process exited with code {}]", c),
            None => "[Process exited]".to_string(),
        };
        let line2 = "Press Enter to restart or close the tab.";

        // Write an empty line, then the banner lines
        self.write_string_to_grid("");
        self.write_string_to_grid(&line1);
        self.write_string_to_grid(line2);
        self.dirty = true;
    }

    /// Write a plain string to the current cursor position, advancing the
    /// cursor. Each character is placed in its own cell, and the cursor
    /// moves to the next row at the end.
    fn write_string_to_grid(&mut self, s: &str) {
        let cols = self.grid.cols;
        let max_row = self.grid.rows.saturating_sub(1);
        for ch in s.chars() {
            let col = self.grid.cursor.col;
            if col < cols {
                if let Some(cell) = self.grid.cell_mut(col, self.grid.cursor.row) {
                    cell.content.clear();
                    cell.content.push(ch);
                }
                self.grid.cursor.col += 1;
            }
        }
        // Move to next row
        if self.grid.cursor.row < max_row {
            self.grid.cursor.row += 1;
        }
        self.grid.cursor.col = 0;
    }

    /// Extract text from a range of absolute rows (spanning scrollback + grid).
    ///
    /// Absolute row 0 is the first scrollback row. Rows beyond the scrollback
    /// are grid rows (grid row = absolute_row - scrollback.len()).
    /// Each row is joined with `\n`. Trailing whitespace per row is trimmed.
    pub fn extract_row_text(&self, start_row: usize, end_row: usize) -> String {
        let scrollback_len = self.scrollback.len();
        let mut text = String::new();
        for abs_row in start_row..=end_row {
            if abs_row > start_row {
                text.push('\n');
            }
            let mut line = String::new();
            if abs_row < scrollback_len {
                // Row is in the scrollback buffer
                if let Some(row) = self.scrollback.get(abs_row) {
                    for cell in row.cells.iter() {
                        if cell.width > 0 {
                            line.push_str(&cell.content);
                        }
                    }
                }
            } else {
                // Row is in the active grid
                let grid_row = abs_row - scrollback_len;
                if grid_row < self.grid.rows {
                    for col in 0..self.grid.cols {
                        if let Some(cell) = self.grid.cell(col, grid_row)
                            && cell.width > 0
                        {
                            line.push_str(&cell.content);
                        }
                    }
                }
            }
            text.push_str(line.trim_end());
        }
        text
    }

    /// Extract all visible text from the active grid as a single string.
    ///
    /// Each row is joined with `\n`. Trailing whitespace per row is trimmed
    /// so screen readers don't announce blank cells.
    pub fn visible_text(&self) -> String {
        let mut text = String::with_capacity(self.grid.cols * self.grid.rows);
        for row in 0..self.grid.rows {
            if row > 0 {
                text.push('\n');
            }
            let line_start = text.len();
            for col in 0..self.grid.cols {
                if let Some(cell) = self.grid.cell(col, row) {
                    text.push_str(&cell.content);
                }
            }
            // Trim trailing whitespace in-place
            let trimmed_len = text[line_start..].trim_end().len();
            text.truncate(line_start + trimmed_len);
        }
        text
    }

    /// Export the entire terminal buffer (scrollback + live grid) as a string.
    ///
    /// Each row is joined with `\n`. Trailing whitespace per row is trimmed.
    pub fn export_buffer_text(&self) -> String {
        let sb_lines = self.scrollback.len();
        let total_rows = sb_lines + self.grid.rows;
        let mut text = String::with_capacity(self.grid.cols * total_rows);

        // Scrollback rows
        for i in 0..sb_lines {
            if i > 0 || !text.is_empty() {
                text.push('\n');
            }
            if let Some(row) = self.scrollback.get(i) {
                let line_start = text.len();
                for cell in row.iter() {
                    if cell.width > 0 {
                        text.push_str(&cell.content);
                    }
                }
                let trimmed_len = text[line_start..].trim_end().len();
                text.truncate(line_start + trimmed_len);
            }
        }

        // Live grid rows
        for row in 0..self.grid.rows {
            if !text.is_empty() {
                text.push('\n');
            }
            let line_start = text.len();
            for col in 0..self.grid.cols {
                if let Some(cell) = self.grid.cell(col, row) {
                    text.push_str(&cell.content);
                }
            }
            let trimmed_len = text[line_start..].trim_end().len();
            text.truncate(line_start + trimmed_len);
        }

        text
    }

    /// Start a new text selection at the given grid point.
    pub fn start_selection(&mut self, col: usize, row: usize, sel_type: SelectionType) {
        let point = GridPoint::new(
            col.min(self.grid.cols.saturating_sub(1)),
            row.min(self.grid.rows.saturating_sub(1)),
        );
        self.selection = Some(Selection::new(point, sel_type));
        self.dirty = true;
    }

    /// Update the moving end of the current selection.
    pub fn update_selection(&mut self, col: usize, row: usize) {
        if let Some(ref mut sel) = self.selection {
            let new_end = GridPoint::new(
                col.min(self.grid.cols.saturating_sub(1)),
                row.min(self.grid.rows.saturating_sub(1)),
            );
            if sel.end != new_end {
                sel.end = new_end;
                self.dirty = true;
            }
        }
    }

    /// Select all visible text (entire grid).
    pub fn select_all(&mut self) {
        self.selection = Some(Selection::new(GridPoint::new(0, 0), SelectionType::Normal));
        if let Some(sel) = &mut self.selection {
            sel.end = GridPoint::new(
                self.grid.cols.saturating_sub(1),
                self.grid.rows.saturating_sub(1),
            );
        }
        self.dirty = true;
    }

    /// Clear the scrollback history buffer.
    pub fn clear_scrollback(&mut self) {
        self.scrollback.clear();
        self.viewport_offset = 0;
        self.dirty = true;
    }

    /// Clear the current selection.
    pub fn clear_selection(&mut self) {
        if self.selection.is_some() {
            self.selection = None;
            self.dirty = true;
        }
    }

    /// Extract the selected text as a String.
    pub fn selected_text(&self) -> Option<String> {
        let sel = self.selection.as_ref()?;
        let (start, end) = sel.ordered();
        let mut result = String::new();

        // Block selection: extract the same column range from every row.
        if sel.sel_type == SelectionType::Block {
            let min_col = sel.anchor.col.min(sel.end.col);
            let max_col = sel.anchor.col.max(sel.end.col);

            for row in start.row..=end.row.min(self.grid.rows.saturating_sub(1)) {
                for col in min_col..=max_col.min(self.grid.cols.saturating_sub(1)) {
                    if let Some(cell) = self.grid.cell(col, row)
                        && cell.width > 0
                    {
                        result.push_str(&cell.content);
                    }
                }

                // Trim trailing spaces from this row's block
                let trimmed_len = result.trim_end_matches(' ').len();
                result.truncate(trimmed_len);

                if row < end.row {
                    result.push('\n');
                }
            }
        } else {
            // Normal / Word / Line selection
            for row in start.row..=end.row.min(self.grid.rows.saturating_sub(1)) {
                let col_start = if row == start.row && sel.sel_type != SelectionType::Line {
                    start.col
                } else {
                    0
                };
                let col_end = if row == end.row && sel.sel_type != SelectionType::Line {
                    end.col
                } else {
                    self.grid.cols.saturating_sub(1)
                };

                for col in col_start..=col_end.min(self.grid.cols.saturating_sub(1)) {
                    if let Some(cell) = self.grid.cell(col, row)
                        && cell.width > 0
                    {
                        result.push_str(&cell.content);
                    }
                }

                // Trim trailing spaces from each line
                let trimmed_len = result.trim_end_matches(' ').len();
                result.truncate(trimmed_len);

                if row < end.row {
                    result.push('\n');
                }
            }
        }

        if result.is_empty() {
            None
        } else {
            Some(result)
        }
    }

    /// Write a printable character at the current cursor position.
    #[inline]
    pub fn print(&mut self, ch: char) {
        // Apply character set mapping (G0/G1 line drawing)
        let ch = if self.gl == Charset::DecLineDrawing {
            map_line_drawing(ch)
        } else {
            ch
        };
        self.snap_to_bottom();
        // Handle pending wrap (auto-wrap at right margin)
        if self.pending_wrap {
            if self.modes.auto_wrap {
                // Mark current row as soft-wrapped before moving to next line
                if let Some(row) = self.grid.row_mut(self.grid.cursor.row) {
                    row.wrapped = true;
                }
                self.carriage_return();
                self.linefeed();
            }
            self.pending_wrap = false;
        }

        let col = self.grid.cursor.col;
        let row = self.grid.cursor.row;

        if self.modes.insert_mode {
            self.grid.insert_blank_cells(col, row, 1);
        }

        // Determine character width
        let char_width = unicode_width::UnicodeWidthChar::width(ch).unwrap_or(1);

        if char_width == 2 && col + 1 >= self.grid.cols {
            // Wide char doesn't fit — fill current cell with space, wrap
            if let Some(cell) = self.grid.cell_mut(col, row) {
                cell.clear();
            }
            if self.modes.auto_wrap {
                self.carriage_return();
                self.linefeed();
                self.pending_wrap = false;
            }
        }

        let col = self.grid.cursor.col;
        let row = self.grid.cursor.row;
        let mut attrs = self.grid.current_attrs;
        // Stamp the active OSC 8 hyperlink onto printed cells.
        attrs.hyperlink_id = self.hyperlinks.active_id;

        if col < self.grid.cols && row < self.grid.rows {
            if let Some(cell) = self.grid.cell_mut(col, row) {
                cell.content.clear();
                cell.content.push(ch);
                cell.attrs = attrs;
                cell.width = char_width as u8;
            }

            // For wide characters, mark the next cell as continuation
            if char_width == 2
                && col + 1 < self.grid.cols
                && let Some(next) = self.grid.cell_mut(col + 1, row)
            {
                next.content.clear();
                next.attrs = attrs;
                next.width = 0; // continuation
            }

            let advance = char_width.max(1);
            if self.grid.cursor.col + advance >= self.grid.cols {
                // At right margin — set pending wrap, don't advance past
                self.grid.cursor.col = self.grid.cols - 1;
                self.pending_wrap = true;
            } else {
                self.grid.cursor.col += advance;
            }
        }

        self.dirty = true;
    }

    /// Execute a C0 control character.
    pub fn execute(&mut self, byte: u8) {
        self.snap_to_bottom();
        match byte {
            0x07 => self.bell(),
            0x08 => self.backspace(),
            0x09 => self.horizontal_tab(),
            0x0a..=0x0c => self.linefeed(),
            0x0d => self.carriage_return(),
            0x0e => self.gl = self.g1, // SO — shift out (activate G1)
            0x0f => self.gl = self.g0, // SI — shift in (activate G0)
            _ => {
                trace!(byte, "Unhandled C0 control");
            }
        }
    }

    /// Handle a CSI dispatch.
    pub fn csi_dispatch(
        &mut self,
        params: &[u16],
        intermediates: &[u8],
        action: char,
        subparams: &[Vec<u16>],
    ) {
        // Check for private mode marker
        let private = intermediates.first().copied() == Some(b'?');

        match action {
            // Cursor movement
            'A' => self.cursor_up(param(params, 0, 1) as usize),
            'B' | 'e' => self.cursor_down(param(params, 0, 1) as usize),
            'C' | 'a' => self.cursor_forward(param(params, 0, 1) as usize),
            'D' => self.cursor_backward(param(params, 0, 1) as usize),
            'E' => {
                self.cursor_down(param(params, 0, 1) as usize);
                self.carriage_return();
            }
            'F' => {
                self.cursor_up(param(params, 0, 1) as usize);
                self.carriage_return();
            }
            'G' | '`' => self.cursor_to_col(param(params, 0, 1) as usize - 1),
            'H' | 'f' => {
                let row = param(params, 0, 1) as usize - 1;
                let col = param(params, 1, 1) as usize - 1;
                self.cursor_to(col, row);
            }
            'd' => self.cursor_to_row(param(params, 0, 1) as usize - 1),

            // Erase
            'J' => {
                let mode = param(params, 0, 0);
                if private {
                    // Selective erase — not common, treat as normal
                }
                self.erase_in_display(mode);
            }
            'K' => {
                let mode = param(params, 0, 0);
                self.erase_in_line(mode);
            }

            // Insert/delete
            'L' => self.insert_lines(param(params, 0, 1) as usize),
            'M' => self.delete_lines(param(params, 0, 1) as usize),
            'P' => self.delete_chars(param(params, 0, 1) as usize),
            '@' => {
                let n = param(params, 0, 1) as usize;
                let col = self.grid.cursor.col;
                let row = self.grid.cursor.row;
                self.grid.insert_blank_cells(col, row, n);
                self.dirty = true;
            }
            'X' => self.erase_chars(param(params, 0, 1) as usize),

            // Scroll
            'S' => {
                if !private {
                    self.scroll_up_n(param(params, 0, 1) as usize);
                }
            }
            'T' => self.scroll_down_n(param(params, 0, 1) as usize),

            // SGR
            'm' => self.handle_sgr(params, subparams),

            // Mode set/reset
            'h' => {
                if private {
                    self.set_private_mode(params, true);
                } else {
                    self.set_ansi_mode(params, true);
                }
            }
            'l' => {
                if private {
                    self.set_private_mode(params, false);
                } else {
                    self.set_ansi_mode(params, false);
                }
            }

            // Scroll region
            'r' => {
                let top = param(params, 0, 1) as usize - 1;
                let bottom = param(params, 1, self.grid.rows as u16) as usize - 1;
                self.set_scroll_region(top, bottom);
            }

            // Cursor save/restore (ANSI)
            's' => {
                if !private {
                    self.save_cursor();
                }
            }
            'u' => self.restore_cursor(),

            // Tab clear
            'g' => {
                let mode = param(params, 0, 0);
                match mode {
                    0 => {
                        let col = self.grid.cursor.col;
                        if col < self.tab_stops.len() {
                            self.tab_stops[col] = false;
                        }
                    }
                    3 => {
                        self.tab_stops.fill(false);
                    }
                    _ => {}
                }
            }

            // Device status report
            'n' => {
                let mode = param(params, 0, 0);
                match mode {
                    5 => {
                        // Device status: respond "OK"
                        self.pending_responses.push(b"\x1b[0n".to_vec());
                    }
                    6 => {
                        // Cursor position report (1-based)
                        let response = format!(
                            "\x1b[{};{}R",
                            self.grid.cursor.row + 1,
                            self.grid.cursor.col + 1
                        );
                        self.pending_responses.push(response.into_bytes());
                    }
                    _ => {
                        trace!(mode, "Unhandled DSR mode");
                    }
                }
            }

            // Cursor style (DECSCUSR)
            'q' if intermediates.first().copied() == Some(b' ') => {
                let style = param(params, 0, 0);
                match style {
                    0 | 1 => {
                        self.grid.cursor.shape = CursorShape::Block;
                        self.grid.cursor.blinking = true;
                    }
                    2 => {
                        self.grid.cursor.shape = CursorShape::Block;
                        self.grid.cursor.blinking = false;
                    }
                    3 => {
                        self.grid.cursor.shape = CursorShape::Underline;
                        self.grid.cursor.blinking = true;
                    }
                    4 => {
                        self.grid.cursor.shape = CursorShape::Underline;
                        self.grid.cursor.blinking = false;
                    }
                    5 => {
                        self.grid.cursor.shape = CursorShape::Bar;
                        self.grid.cursor.blinking = true;
                    }
                    6 => {
                        self.grid.cursor.shape = CursorShape::Bar;
                        self.grid.cursor.blinking = false;
                    }
                    _ => {}
                }
                self.dirty = true;
            }

            _ => {
                trace!(action = %action, ?params, ?intermediates, "Unhandled CSI sequence");
            }
        }
    }

    /// Handle an ESC dispatch.
    pub fn esc_dispatch(&mut self, intermediates: &[u8], action: char) {
        match (intermediates, action) {
            ([], '7') => self.save_cursor(),    // DECSC
            ([], '8') => self.restore_cursor(), // DECRC
            ([], 'D') => self.linefeed(),       // IND (index)
            ([], 'E') => {
                // NEL (next line)
                self.linefeed();
                self.carriage_return();
            }
            ([], 'H') => {
                // HTS (set tab stop)
                let col = self.grid.cursor.col;
                if col < self.tab_stops.len() {
                    self.tab_stops[col] = true;
                }
            }
            ([], 'M') => self.reverse_index(), // RI (reverse index)
            ([], 'c') => self.reset(),         // RIS (full reset)
            (b"#", '8') => self.fill_with_e(), // DECALN
            // Designate G0 character set
            (b"(", '0') => {
                self.g0 = Charset::DecLineDrawing;
                self.gl = self.g0; // G0 is active by default via GL
            }
            (b"(", 'B') => {
                self.g0 = Charset::Ascii;
                self.gl = self.g0;
            }
            // Designate G1 character set
            (b")", '0') => {
                self.g1 = Charset::DecLineDrawing;
            }
            (b")", 'B') => {
                self.g1 = Charset::Ascii;
            }
            _ => {
                trace!(action = %action, ?intermediates, "Unhandled ESC sequence");
            }
        }
    }

    /// Handle an OSC dispatch.
    pub fn osc_dispatch(&mut self, params: &[Vec<u8>]) {
        if params.is_empty() {
            return;
        }

        let cmd = std::str::from_utf8(&params[0]).unwrap_or("");
        match cmd {
            "0" | "2" => {
                // Set window title
                if let Some(title) = params.get(1) {
                    self.title = String::from_utf8_lossy(title).into_owned();
                    self.title_dirty = true;
                    debug!(title = %self.title, "Window title set");
                }
            }
            "1" => {
                // Set icon name (ignored — same as title on modern systems)
            }
            "7" => {
                // Current working directory (OSC 7)
                if let Some(uri) = params.get(1) {
                    let uri = String::from_utf8_lossy(uri);
                    self.shell_integ.handle_osc7(&uri);
                    self.title_dirty = true;
                }
            }
            "8" => {
                // OSC 8 hyperlinks: `OSC 8 ; params ; uri ST`
                // params can contain `id=value` pairs separated by `:`.
                // Empty URI closes the current hyperlink.
                let params_raw = params
                    .get(1)
                    .map(|p| String::from_utf8_lossy(p).into_owned())
                    .unwrap_or_default();
                let uri = params
                    .get(2)
                    .map(|p| String::from_utf8_lossy(p).into_owned())
                    .unwrap_or_default();

                if uri.is_empty() {
                    // Close current hyperlink.
                    self.hyperlinks.close();
                    trace!("OSC 8: hyperlink closed");
                } else {
                    // Extract optional `id=` from params (params are `:` or `;` separated key=value).
                    let link_id = params_raw
                        .split([':', ';'])
                        .find_map(|kv| kv.strip_prefix("id="));
                    self.hyperlinks.open(&uri, link_id);
                    debug!(uri = %uri, "OSC 8: hyperlink opened");
                }
            }
            "9" => {
                // ConEmu-compatible OSC 9: extended functionality
                // Sub-command 4: progress bar — `OSC 9 ; 4 ; <state> ; <value> ST`
                //   state: 0 = hidden, 1 = normal, 2 = error, 3 = indeterminate, 4 = paused
                //   value: 0–100 (ignored for state 0 and 3)
                if let Some(sub) = params.get(1) {
                    let sub_str = std::str::from_utf8(sub).unwrap_or("");
                    // The VT parser may split "4;1;50" into separate params or keep as one.
                    let parts: Vec<&str> = if sub_str.contains(';') {
                        sub_str.splitn(3, ';').collect()
                    } else {
                        let mut v: Vec<&str> = vec![sub_str];
                        for p in params.iter().skip(2) {
                            v.push(std::str::from_utf8(p).unwrap_or(""));
                        }
                        v
                    };
                    if parts.first() == Some(&"4") {
                        let state: u8 = parts.get(1).and_then(|s| s.parse().ok()).unwrap_or(0);
                        let value: u8 = parts
                            .get(2)
                            .and_then(|s| s.parse().ok())
                            .unwrap_or(0)
                            .min(100);
                        self.progress = match state {
                            1 => ProgressState::Normal(value),
                            2 => ProgressState::Error(value),
                            3 => ProgressState::Indeterminate,
                            4 => ProgressState::Paused(value),
                            _ => ProgressState::Hidden,
                        };
                        debug!(state, value, "OSC 9;4: progress updated");
                    }
                }
            }
            "52" => {
                // OSC 52: Clipboard access
                // Format: OSC 52 ; <selection> ; <base64-data> ST
                //   selection: c = clipboard, p = primary (treated as clipboard on Windows)
                //   data: ? = query, empty = clear, base64 = write
                //
                // The VT parser splits on semicolons, so the selection is params[1]
                // and the data is params[2]. Some terminals embed both in one param
                // as "selection;data", so handle both layouts.
                let (selection, data) = if params.len() >= 3 {
                    // Parser already split: params = ["52", "c", "dGVzdA=="]
                    (
                        String::from_utf8_lossy(params[1].as_ref()).to_string(),
                        String::from_utf8_lossy(params[2].as_ref()).to_string(),
                    )
                } else if let Some(payload) = params.get(1) {
                    // Single payload: "c;dGVzdA=="
                    let payload_str = String::from_utf8_lossy(payload);
                    if let Some(idx) = payload_str.find(';') {
                        (
                            payload_str[..idx].to_string(),
                            payload_str[idx + 1..].to_string(),
                        )
                    } else {
                        (payload_str.to_string(), String::new())
                    }
                } else {
                    (String::new(), String::new())
                };

                if !selection.is_empty() || !data.is_empty() {
                    if data == "?" {
                        // Read query — only honor if policy permits
                        if self.osc52_policy == Osc52Policy::ReadWrite {
                            self.clipboard_read_pending = true;
                            debug!("OSC 52: clipboard read query");
                        } else {
                            trace!("OSC 52: clipboard read blocked by policy");
                        }
                    } else if self.osc52_policy == Osc52Policy::Disabled {
                        trace!("OSC 52: clipboard write/clear blocked by policy");
                    } else if data.is_empty() {
                        // Clear clipboard
                        self.clipboard_writes.push(String::new());
                        debug!("OSC 52: clipboard clear");
                    } else {
                        // Write: decode base64
                        use base64::Engine;
                        match base64::engine::general_purpose::STANDARD.decode(&data) {
                            Ok(bytes) => {
                                let text = String::from_utf8_lossy(&bytes).into_owned();
                                debug!(len = text.len(), "OSC 52: clipboard write");
                                self.clipboard_writes.push(text);
                            }
                            Err(_) => {
                                trace!("OSC 52: invalid base64 — ignored");
                            }
                        }
                    }
                }
            }
            "133" => {
                // Shell integration (OSC 133)
                // Format: OSC 133 ; <marker> [; <params>] ST
                // The VT parser splits on semicolons, so the marker is params[1]
                // and any extra parameters (e.g. exit code) are in params[2..].
                if let Some(sub) = params.get(1) {
                    let sub_str = String::from_utf8_lossy(sub);
                    // The marker may contain embedded semicolons if the parser
                    // didn't split them, or the extra params may be in params[2..].
                    let (marker, extra) = if let Some(idx) = sub_str.find(';') {
                        (&sub_str[..idx], sub_str[idx + 1..].to_string())
                    } else if params.len() > 2 {
                        // Extra params split by the parser into separate entries
                        let extra_parts: Vec<String> = params[2..]
                            .iter()
                            .map(|p| String::from_utf8_lossy(p).to_string())
                            .collect();
                        (sub_str.as_ref(), extra_parts.join(";"))
                    } else {
                        (sub_str.as_ref(), String::new())
                    };
                    let abs_row = self.grid.cursor.row + self.scrollback.len();
                    self.shell_integ.handle_osc133(marker, &extra, abs_row);

                    // On execution start (C), extract command text from the input rows
                    if marker == "C"
                        && let Some(input_range) =
                            self.shell_integ.current_block().and_then(|b| b.input)
                    {
                        let cmd_text = self.extract_row_text(input_range.start, input_range.end);
                        if !cmd_text.is_empty()
                            && let Some(block) = self.shell_integ.current_block_mut()
                        {
                            block.command_text = Some(cmd_text);
                        }
                    }
                }
            }
            "1337" => {
                // iTerm2 proprietary sequences.
                // Currently supported: File= (inline images).
                // Format: OSC 1337 ; File=[params]:<base64-data> ST
                if let Some(payload) = params.get(1) {
                    let payload_str = String::from_utf8_lossy(payload);
                    if let Some(rest) = payload_str.strip_prefix("File=") {
                        self.handle_iterm2_file(rest);
                    }
                }
            }
            "4" => {
                // OSC 4: Set/query indexed color
                // Format: OSC 4 ; <index> ; <color-spec-or-?> ST
                if let (Some(idx_raw), Some(spec_raw)) = (params.get(1), params.get(2)) {
                    let idx_str = std::str::from_utf8(idx_raw).unwrap_or("");
                    let spec_str = std::str::from_utf8(spec_raw).unwrap_or("");
                    if let Ok(idx) = idx_str.parse::<u8>() {
                        if spec_str == "?" {
                            // Query: respond with current color
                            let color = self.effective_palette_color(idx);
                            let resp =
                                format!("\x1b]4;{};{}\x07", idx, Self::format_xterm_rgb(color));
                            self.pending_responses.push(resp.into_bytes());
                        } else if let Some(color) = Self::parse_color_spec(spec_str) {
                            // Set palette override
                            self.palette_overrides.insert(idx, color);
                            self.dirty = true;
                        }
                    }
                }
            }
            "10" => {
                // OSC 10: Foreground color query/set
                if let Some(spec_raw) = params.get(1) {
                    let spec = std::str::from_utf8(spec_raw).unwrap_or("");
                    if spec == "?" {
                        let resp =
                            format!("\x1b]10;{}\x07", Self::format_xterm_rgb(self.default_fg));
                        self.pending_responses.push(resp.into_bytes());
                    }
                    // Setting fg via OSC 10 is ignored (renderer-managed)
                }
            }
            "11" => {
                // OSC 11: Background color query/set
                if let Some(spec_raw) = params.get(1) {
                    let spec = std::str::from_utf8(spec_raw).unwrap_or("");
                    if spec == "?" {
                        let resp =
                            format!("\x1b]11;{}\x07", Self::format_xterm_rgb(self.default_bg));
                        self.pending_responses.push(resp.into_bytes());
                    }
                }
            }
            "12" => {
                // OSC 12: Cursor color query/set
                if let Some(spec_raw) = params.get(1) {
                    let spec = std::str::from_utf8(spec_raw).unwrap_or("");
                    if spec == "?" {
                        let resp = format!(
                            "\x1b]12;{}\x07",
                            Self::format_xterm_rgb(self.default_cursor_color)
                        );
                        self.pending_responses.push(resp.into_bytes());
                    }
                }
            }
            "104" => {
                // OSC 104: Reset color(s)
                // Format: OSC 104 ; <index> ST — reset specific index
                // Format: OSC 104 ST — reset all palette overrides
                if let Some(idx_raw) = params.get(1) {
                    let idx_str = std::str::from_utf8(idx_raw).unwrap_or("");
                    if let Ok(idx) = idx_str.parse::<u8>() {
                        self.palette_overrides.remove(&idx);
                        self.dirty = true;
                    }
                } else {
                    self.palette_overrides.clear();
                    self.dirty = true;
                }
            }
            _ => {
                trace!(cmd, "Unhandled OSC command");
            }
        }
    }

    // --- Cursor movement ---

    fn cursor_up(&mut self, n: usize) {
        self.pending_wrap = false;
        let new_row = self.grid.cursor.row.saturating_sub(n);
        self.grid.cursor.row = new_row.max(self.scroll_region.top);
        self.dirty = true;
    }

    fn cursor_down(&mut self, n: usize) {
        self.pending_wrap = false;
        let new_row = self.grid.cursor.row + n;
        self.grid.cursor.row = new_row.min(self.scroll_region.bottom);
        self.dirty = true;
    }

    fn cursor_forward(&mut self, n: usize) {
        self.pending_wrap = false;
        let new_col = self.grid.cursor.col + n;
        self.grid.cursor.col = new_col.min(self.grid.cols.saturating_sub(1));
        self.dirty = true;
    }

    fn cursor_backward(&mut self, n: usize) {
        self.pending_wrap = false;
        self.grid.cursor.col = self.grid.cursor.col.saturating_sub(n);
        self.dirty = true;
    }

    fn cursor_to_col(&mut self, col: usize) {
        self.pending_wrap = false;
        self.grid.cursor.col = col.min(self.grid.cols.saturating_sub(1));
        self.dirty = true;
    }

    fn cursor_to_row(&mut self, row: usize) {
        self.pending_wrap = false;
        self.grid.cursor.row = row.min(self.grid.rows.saturating_sub(1));
        self.dirty = true;
    }

    fn cursor_to(&mut self, col: usize, row: usize) {
        self.pending_wrap = false;
        self.grid.cursor.col = col.min(self.grid.cols.saturating_sub(1));
        let row = if self.modes.origin_mode {
            (self.scroll_region.top + row).min(self.scroll_region.bottom)
        } else {
            row.min(self.grid.rows.saturating_sub(1))
        };
        self.grid.cursor.row = row;
        self.dirty = true;
    }

    fn save_cursor(&mut self) {
        self.saved_cursor = Some(SavedCursor {
            col: self.grid.cursor.col,
            row: self.grid.cursor.row,
            attrs: self.grid.current_attrs,
            auto_wrap: self.modes.auto_wrap,
            origin_mode: self.modes.origin_mode,
            g0: self.g0,
            g1: self.g1,
            gl: self.gl,
        });
    }

    fn restore_cursor(&mut self) {
        if let Some(saved) = self.saved_cursor.clone() {
            self.grid.cursor.col = saved.col.min(self.grid.cols.saturating_sub(1));
            self.grid.cursor.row = saved.row.min(self.grid.rows.saturating_sub(1));
            self.grid.current_attrs = saved.attrs;
            self.modes.auto_wrap = saved.auto_wrap;
            self.modes.origin_mode = saved.origin_mode;
            self.g0 = saved.g0;
            self.g1 = saved.g1;
            self.gl = saved.gl;
            self.pending_wrap = false;
            self.dirty = true;
        }
    }

    // --- Control characters ---

    fn bell(&mut self) {
        self.bell_pending = true;
    }

    fn backspace(&mut self) {
        self.pending_wrap = false;
        if self.grid.cursor.col > 0 {
            self.grid.cursor.col -= 1;
        }
    }

    fn horizontal_tab(&mut self) {
        self.pending_wrap = false;
        let col = self.grid.cursor.col;
        // Find next tab stop
        for i in (col + 1)..self.grid.cols {
            if i < self.tab_stops.len() && self.tab_stops[i] {
                self.grid.cursor.col = i;
                return;
            }
        }
        // No tab stop found — move to last column
        self.grid.cursor.col = self.grid.cols.saturating_sub(1);
    }

    fn carriage_return(&mut self) {
        self.pending_wrap = false;
        self.grid.cursor.col = 0;
    }

    fn linefeed(&mut self) {
        self.pending_wrap = false;
        if self.grid.cursor.row == self.scroll_region.bottom {
            self.scroll_up_region();
        } else if self.grid.cursor.row < self.grid.rows - 1 {
            self.grid.cursor.row += 1;
        }
        // In new-line mode, LF also does CR
        if self.modes.line_feed_new_line {
            self.grid.cursor.col = 0;
        }
        self.dirty = true;
    }

    fn reverse_index(&mut self) {
        self.pending_wrap = false;
        if self.grid.cursor.row == self.scroll_region.top {
            self.scroll_down_region();
        } else if self.grid.cursor.row > 0 {
            self.grid.cursor.row -= 1;
        }
        self.dirty = true;
    }

    // --- Scroll ---

    fn scroll_up_region(&mut self) {
        let top = self.scroll_region.top;
        let bottom = self.scroll_region.bottom;
        let scrolled_row = self.grid.scroll_region_up(top, bottom);

        // Push to scrollback only if scrolling the full screen from top
        if top == 0 {
            self.scrollback.push(scrolled_row);
        }
        self.dirty = true;
    }

    fn scroll_down_region(&mut self) {
        let top = self.scroll_region.top;
        let bottom = self.scroll_region.bottom;
        self.grid.scroll_region_down(top, bottom);
        self.dirty = true;
    }

    fn scroll_up_n(&mut self, n: usize) {
        for _ in 0..n {
            self.scroll_up_region();
        }
    }

    fn scroll_down_n(&mut self, n: usize) {
        for _ in 0..n {
            self.scroll_down_region();
        }
    }

    // --- Erase ---

    fn erase_in_display(&mut self, mode: u16) {
        let col = self.grid.cursor.col;
        let row = self.grid.cursor.row;
        match mode {
            0 => {
                // Erase from cursor to end of display
                self.grid.clear_range_in_row(col, self.grid.cols, row);
                for r in (row + 1)..self.grid.rows {
                    self.grid.clear_row(r);
                }
            }
            1 => {
                // Erase from start of display to cursor
                for r in 0..row {
                    self.grid.clear_row(r);
                }
                self.grid.clear_range_in_row(0, col + 1, row);
            }
            2 => {
                // Erase entire display
                self.grid.clear();
            }
            3 => {
                // Erase scrollback buffer
                self.scrollback.clear();
                self.grid.clear();
            }
            _ => {}
        }
        self.dirty = true;
    }

    fn erase_in_line(&mut self, mode: u16) {
        let col = self.grid.cursor.col;
        let row = self.grid.cursor.row;
        match mode {
            0 => self.grid.clear_range_in_row(col, self.grid.cols, row),
            1 => self.grid.clear_range_in_row(0, col + 1, row),
            2 => self.grid.clear_row(row),
            _ => {}
        }
        self.dirty = true;
    }

    fn erase_chars(&mut self, n: usize) {
        let col = self.grid.cursor.col;
        let row = self.grid.cursor.row;
        let end = (col + n).min(self.grid.cols);
        self.grid.clear_range_in_row(col, end, row);
        self.dirty = true;
    }

    fn insert_lines(&mut self, n: usize) {
        let row = self.grid.cursor.row;
        if row >= self.scroll_region.top && row <= self.scroll_region.bottom {
            for _ in 0..n {
                self.grid.insert_line(row, self.scroll_region.bottom);
            }
        }
        self.dirty = true;
    }

    fn delete_lines(&mut self, n: usize) {
        let row = self.grid.cursor.row;
        if row >= self.scroll_region.top && row <= self.scroll_region.bottom {
            for _ in 0..n {
                self.grid.delete_line(row, self.scroll_region.bottom);
            }
        }
        self.dirty = true;
    }

    fn delete_chars(&mut self, n: usize) {
        let col = self.grid.cursor.col;
        let row = self.grid.cursor.row;
        self.grid.delete_cells(col, row, n);
        self.dirty = true;
    }

    // --- Scroll region ---

    fn set_scroll_region(&mut self, top: usize, bottom: usize) {
        let top = top.min(self.grid.rows.saturating_sub(1));
        let bottom = bottom.min(self.grid.rows.saturating_sub(1));
        if top < bottom {
            self.scroll_region = ScrollRegion { top, bottom };
            self.cursor_to(0, 0);
        }
    }

    // --- SGR (Select Graphic Rendition) ---

    fn handle_sgr(&mut self, params: &[u16], subparams: &[Vec<u16>]) {
        if params.is_empty() {
            self.grid.current_attrs = CellAttributes::default();
            return;
        }

        let mut i = 0;
        while i < params.len() {
            match params[i] {
                0 => self.grid.current_attrs = CellAttributes::default(),
                1 => self.grid.current_attrs.bold = true,
                2 => self.grid.current_attrs.dim = true,
                3 => self.grid.current_attrs.italic = true,
                4 => {
                    // SGR 4 — check for sub-params (4:0 none, 4:1 single, 4:3 curly, etc.)
                    let subs = subparams.get(i).filter(|s| !s.is_empty());
                    if let Some(sub) = subs {
                        self.grid.current_attrs.underline = match sub[0] {
                            0 => UnderlineStyle::None,
                            1 => UnderlineStyle::Single,
                            2 => UnderlineStyle::Double,
                            3 => UnderlineStyle::Curly,
                            4 => UnderlineStyle::Dotted,
                            5 => UnderlineStyle::Dashed,
                            _ => UnderlineStyle::Single,
                        };
                    } else {
                        self.grid.current_attrs.underline = UnderlineStyle::Single;
                    }
                }
                5 => self.grid.current_attrs.blink = true,
                7 => self.grid.current_attrs.inverse = true,
                8 => self.grid.current_attrs.hidden = true,
                9 => self.grid.current_attrs.strikethrough = true,
                21 => self.grid.current_attrs.underline = UnderlineStyle::Double,
                22 => {
                    self.grid.current_attrs.bold = false;
                    self.grid.current_attrs.dim = false;
                }
                23 => self.grid.current_attrs.italic = false,
                24 => self.grid.current_attrs.underline = UnderlineStyle::None,
                25 => self.grid.current_attrs.blink = false,
                27 => self.grid.current_attrs.inverse = false,
                28 => self.grid.current_attrs.hidden = false,
                29 => self.grid.current_attrs.strikethrough = false,
                53 => self.grid.current_attrs.overline = true,
                55 => self.grid.current_attrs.overline = false,

                // Standard foreground colors (30-37)
                n @ 30..=37 => {
                    self.grid.current_attrs.fg = Color::Indexed((n - 30) as u8);
                }
                38 => {
                    // Check for colon sub-params first (e.g. 38:2:R:G:B)
                    let subs = subparams.get(i).filter(|s| !s.is_empty());
                    if let Some(sub) = subs {
                        self.grid.current_attrs.fg = Self::parse_color_subparams(sub);
                    } else {
                        i += 1;
                        self.grid.current_attrs.fg = self.parse_color(params, &mut i);
                        continue; // parse_color advances i
                    }
                }
                39 => self.grid.current_attrs.fg = Color::Default,

                // Standard background colors (40-47)
                n @ 40..=47 => {
                    self.grid.current_attrs.bg = Color::Indexed((n - 40) as u8);
                }
                48 => {
                    let subs = subparams.get(i).filter(|s| !s.is_empty());
                    if let Some(sub) = subs {
                        self.grid.current_attrs.bg = Self::parse_color_subparams(sub);
                    } else {
                        i += 1;
                        self.grid.current_attrs.bg = self.parse_color(params, &mut i);
                        continue;
                    }
                }
                49 => self.grid.current_attrs.bg = Color::Default,

                // Bright foreground colors (90-97)
                n @ 90..=97 => {
                    self.grid.current_attrs.fg = Color::Indexed((n - 90 + 8) as u8);
                }
                // Bright background colors (100-107)
                n @ 100..=107 => {
                    self.grid.current_attrs.bg = Color::Indexed((n - 100 + 8) as u8);
                }

                // Underline color (SGR 58)
                58 => {
                    let subs = subparams.get(i).filter(|s| !s.is_empty());
                    if let Some(sub) = subs {
                        self.grid.current_attrs.underline_color = Self::parse_color_subparams(sub);
                    } else {
                        i += 1;
                        self.grid.current_attrs.underline_color = self.parse_color(params, &mut i);
                        continue;
                    }
                }
                59 => {
                    self.grid.current_attrs.underline_color = Color::Default;
                }

                _ => {
                    trace!(sgr = params[i], "Unhandled SGR parameter");
                }
            }
            i += 1;
        }
    }

    fn parse_color(&self, params: &[u16], i: &mut usize) -> Color {
        if *i >= params.len() {
            return Color::Default;
        }
        match params[*i] {
            2 => {
                // True color: 2;r;g;b
                if *i + 3 < params.len() {
                    let r = params[*i + 1] as u8;
                    let g = params[*i + 2] as u8;
                    let b = params[*i + 3] as u8;
                    *i += 4;
                    Color::Rgb(Rgb::new(r, g, b))
                } else {
                    *i = params.len();
                    Color::Default
                }
            }
            5 => {
                // 256-color: 5;n
                if *i + 1 < params.len() {
                    let idx = params[*i + 1] as u8;
                    *i += 2;
                    Color::Indexed(idx)
                } else {
                    *i = params.len();
                    Color::Default
                }
            }
            _ => {
                *i += 1;
                Color::Default
            }
        }
    }

    /// Parse a color from colon-separated sub-params (e.g., [2, 255, 0, 0] for true color).
    fn parse_color_subparams(subs: &[u16]) -> Color {
        if subs.is_empty() {
            return Color::Default;
        }
        match subs[0] {
            2 => {
                // True color: 2:R:G:B (may also have colorspace ID: 2:cs:R:G:B)
                if subs.len() >= 4 {
                    let r = subs[subs.len() - 3] as u8;
                    let g = subs[subs.len() - 2] as u8;
                    let b = subs[subs.len() - 1] as u8;
                    Color::Rgb(Rgb::new(r, g, b))
                } else {
                    Color::Default
                }
            }
            5 => {
                // 256-color: 5:N
                if subs.len() >= 2 {
                    Color::Indexed(subs[1] as u8)
                } else {
                    Color::Default
                }
            }
            _ => Color::Default,
        }
    }

    // --- DEC Private Modes ---

    fn set_private_mode(&mut self, params: &[u16], enabled: bool) {
        for &p in params {
            match p {
                1 => self.modes.cursor_keys_application = enabled,
                3 => {
                    // DECCOLM: 132/80 column mode switch
                    // Per spec: switching clears the screen and resets cursor
                    self.cursor_to(0, 0);
                    self.erase_in_display(2); // clear entire screen
                    self.dirty = true;
                }
                5 => {
                    // DECSCNM: Reverse video
                    self.modes.reverse_video = enabled;
                    self.dirty = true;
                }
                6 => {
                    self.modes.origin_mode = enabled;
                    self.cursor_to(0, 0);
                }
                7 => self.modes.auto_wrap = enabled,
                9 => {
                    // X10 mouse tracking (button press only, no modifiers)
                    self.modes.mouse_tracking = if enabled {
                        MouseMode::X10
                    } else {
                        MouseMode::None
                    };
                }
                12 => self.grid.cursor.blinking = enabled,
                25 => {
                    self.grid.cursor.visible = enabled;
                    self.dirty = true;
                }
                47 | 1047 => self.switch_screen(enabled),
                1000 => {
                    self.modes.mouse_tracking = if enabled {
                        MouseMode::Normal
                    } else {
                        MouseMode::None
                    };
                }
                1002 => {
                    self.modes.mouse_tracking = if enabled {
                        MouseMode::Button
                    } else {
                        MouseMode::None
                    };
                }
                1003 => {
                    self.modes.mouse_tracking = if enabled {
                        MouseMode::Any
                    } else {
                        MouseMode::None
                    };
                }
                1004 => self.modes.focus_reporting = enabled,
                1006 => self.modes.mouse_sgr = enabled,
                1049 => {
                    // Save cursor + switch to alt screen
                    if enabled {
                        self.save_cursor();
                    }
                    self.switch_screen(enabled);
                    if !enabled {
                        self.restore_cursor();
                    }
                }
                1048 => {
                    // Save/restore cursor (without alt screen switch)
                    if enabled {
                        self.save_cursor();
                    } else {
                        self.restore_cursor();
                    }
                }
                2004 => self.modes.bracketed_paste = enabled,
                2026 => self.modes.synchronized_output = enabled,
                _ => {
                    trace!(mode = p, enabled, "Unhandled DEC private mode");
                }
            }
        }
    }

    fn set_ansi_mode(&mut self, params: &[u16], enabled: bool) {
        for &p in params {
            match p {
                4 => self.modes.insert_mode = enabled,
                20 => self.modes.line_feed_new_line = enabled,
                _ => {
                    trace!(mode = p, enabled, "Unhandled ANSI mode");
                }
            }
        }
    }

    fn switch_screen(&mut self, to_alt: bool) {
        if to_alt && !self.modes.alternate_screen {
            // Switch to alt screen
            let alt = Grid::new(self.grid.cols, self.grid.rows);
            self.alt_grid = Some(std::mem::replace(&mut self.grid, alt));
            self.modes.alternate_screen = true;
            self.pending_wrap = false;
            self.dirty = true;
        } else if !to_alt && self.modes.alternate_screen {
            // Switch back to primary
            if let Some(primary) = self.alt_grid.take() {
                self.grid = primary;
            }
            self.modes.alternate_screen = false;
            self.pending_wrap = false;
            self.dirty = true;
        }
    }

    // --- Misc ---

    fn fill_with_e(&mut self) {
        for row in 0..self.grid.rows {
            for col in 0..self.grid.cols {
                if let Some(cell) = self.grid.cell_mut(col, row) {
                    cell.content.clear();
                    cell.content.push('E');
                    cell.attrs = CellAttributes::default();
                    cell.width = 1;
                }
            }
        }
        self.dirty = true;
    }

    pub fn reset(&mut self) {
        let cols = self.grid.cols;
        let rows = self.grid.rows;
        *self = Terminal::new(cols, rows);
    }

    /// Resize the terminal.
    pub fn resize(&mut self, cols: usize, rows: usize) {
        // Primary screen: reflow wrapped lines at the new width
        if !self.modes.alternate_screen {
            self.grid
                .resize_with_reflow(cols, rows, &mut self.scrollback);
        } else {
            // Alt screen: simple resize, no reflow
            self.grid.resize(cols, rows);
        }

        if let Some(alt) = &mut self.alt_grid {
            alt.resize(cols, rows);
        }
        self.scroll_region = ScrollRegion {
            top: 0,
            bottom: rows.saturating_sub(1),
        };
        // Resize tab stops
        self.tab_stops.resize(cols, false);
        for i in (0..cols).step_by(8) {
            self.tab_stops[i] = true;
        }
        self.pending_wrap = false;
        self.dirty = true;
    }

    // --- Tmux control mode ---

    /// Whether tmux control mode is active.
    pub fn tmux_control_active(&self) -> bool {
        self.tmux_control
    }

    /// Enable or disable tmux control mode.
    pub fn set_tmux_control(&mut self, active: bool) {
        self.tmux_control = active;
        if !active {
            self.tmux_lines.clear();
        }
    }

    /// Push a line into the tmux control mode buffer.
    pub fn push_tmux_line(&mut self, line: String) {
        self.tmux_lines.push(line);
    }

    /// Drain all buffered tmux control mode lines.
    pub fn drain_tmux_lines(&mut self) -> Vec<String> {
        std::mem::take(&mut self.tmux_lines)
    }

    // --- Session export/import ---

    /// Export the terminal state as a serializable `SessionState`.
    pub fn export_session(&self) -> crate::session::SessionState {
        let grid_rows: Vec<String> = (0..self.grid.rows)
            .map(|r| {
                let mut line = String::new();
                for c in 0..self.grid.cols {
                    if let Some(cell) = self.grid.cell(c, r) {
                        line.push_str(&cell.content);
                    }
                }
                line.trim_end().to_string()
            })
            .collect();

        crate::session::SessionState {
            grid_rows,
            scrollback_text: Vec::new(), // text-only scrollback omitted for now
            cursor_col: self.grid.cursor.col,
            cursor_row: self.grid.cursor.row,
            title: self.title.clone(),
            cwd: self.shell_integ.cwd().unwrap_or("").to_string(),
            cols: self.grid.cols,
            rows: self.grid.rows,
        }
    }

    /// Import a saved session state into this terminal.
    ///
    /// Overwrites grid content, cursor position, and title.
    pub fn import_session(&mut self, state: &crate::session::SessionState) {
        self.title = state.title.clone();

        // Write grid content — each row as text placed into cells
        for (row_idx, row_text) in state.grid_rows.iter().enumerate() {
            if row_idx >= self.grid.rows {
                break;
            }
            for (col_idx, ch) in row_text.chars().enumerate() {
                if col_idx >= self.grid.cols {
                    break;
                }
                if let Some(cell) = self.grid.cell_mut(col_idx, row_idx) {
                    cell.content = ch.to_string();
                }
            }
        }

        // Restore cursor (clamped to grid bounds)
        self.grid.cursor.col = state.cursor_col.min(self.grid.cols.saturating_sub(1));
        self.grid.cursor.row = state.cursor_row.min(self.grid.rows.saturating_sub(1));
        self.dirty = true;
    }

    // --- DCS (Device Control String) handling ---

    /// Begin a DCS sequence — store header and prepare to accumulate data bytes.
    pub fn dcs_hook(&mut self, params: &[u16], intermediates: &[u8], action: char) {
        self.dcs_params = params.to_vec();
        self.dcs_intermediates = intermediates.to_vec();
        self.dcs_action = action;
        self.dcs_data.clear();
    }

    /// Accumulate a data byte during a DCS passthrough.
    pub fn dcs_put(&mut self, byte: u8) {
        // Cap at 16MB for safety (Sixel images can be several MB)
        if self.dcs_data.len() < 16 * 1024 * 1024 {
            self.dcs_data.push(byte);
        }
    }

    /// End DCS passthrough — dispatch the accumulated sequence.
    pub fn dcs_unhook(&mut self) {
        let action = self.dcs_action;
        let intermediates = std::mem::take(&mut self.dcs_intermediates);
        let _params = std::mem::take(&mut self.dcs_params);
        let data = std::mem::take(&mut self.dcs_data);

        match (intermediates.as_slice(), action) {
            // DECRQSS: DCS $ q <data> ST → request status string
            (b"$", 'q') => self.handle_decrqss(&data),
            // Sixel: DCS P1;P2;P3 q <data> ST (no intermediates)
            (b"", 'q') => self.handle_sixel(&data),
            _ => {
                trace!(
                    action = %action,
                    intermediates = ?intermediates,
                    data_len = data.len(),
                    "Unhandled DCS sequence"
                );
            }
        }
    }

    /// Handle an iTerm2 `File=` inline image sequence.
    ///
    /// `rest` is everything after `File=`, formatted as `[params]:<base64-data>`.
    fn handle_iterm2_file(&mut self, rest: &str) {
        // Split on the first ':' — left is params, right is base64 data
        let (params_str, base64_data) = match rest.split_once(':') {
            Some((p, d)) => (p, d),
            None => return, // Malformed — no colon separator
        };

        let params = crate::image::parse_iterm2_params(params_str);

        // Only display if inline=1
        if !params.inline {
            debug!("iTerm2 File: inline=0, ignoring (download not supported)");
            return;
        }

        if let Some((width, height, pixels)) = crate::image::decode_iterm2_image(base64_data) {
            let col = self.grid.cursor.col;
            let row = self.grid.cursor.row;
            self.images.add(
                pixels,
                width,
                height,
                col,
                row + self.scrollback.len(),
                (self.cell_pixel_width, self.cell_pixel_height),
            );
            // Advance cursor past the image
            let cell_rows = (height as f32 / self.cell_pixel_height).ceil() as usize;
            let cell_cols = (width as f32 / self.cell_pixel_width).ceil() as usize;
            for _ in 0..cell_rows {
                self.linefeed();
            }
            self.pending_image_announcements.push(ImageAnnouncement {
                width,
                height,
                cell_cols,
                cell_rows,
                protocol: ImageProtocol::ITerm2,
                filename: params.name.clone(),
            });
            self.dirty = true;
            debug!(width, height, "iTerm2 inline image placed");
        }
    }

    /// Handle an APC (Application Program Command) dispatch.
    ///
    /// Currently supports the Kitty graphics protocol (`G...`).
    pub fn apc_dispatch(&mut self, data: &[u8]) {
        if data.is_empty() {
            return;
        }
        // Kitty graphics protocol: starts with 'G'
        if data[0] == b'G' {
            self.handle_kitty_graphics(data);
        } else {
            trace!("Unhandled APC sequence");
        }
    }

    /// Handle a Kitty graphics protocol command.
    fn handle_kitty_graphics(&mut self, data: &[u8]) {
        use crate::image::{KittyAction, decode_kitty_payload, parse_kitty_command};

        let cmd = match parse_kitty_command(data) {
            Some(c) => c,
            None => return,
        };

        // Handle query action — respond with OK to signal support
        if cmd.action == KittyAction::Query {
            let resp = format!("\x1b_Gi={};OK\x1b\\", cmd.image_id);
            self.pending_responses.push(resp.into_bytes());
            return;
        }

        // Handle delete action
        if cmd.action == KittyAction::Delete {
            use crate::image::KittyDeleteType;
            match cmd.delete_type {
                KittyDeleteType::ById => {
                    if cmd.image_id > 0 {
                        self.images.remove_by_id(cmd.image_id as u64);
                        self.transmitted_images.remove(&cmd.image_id);
                    }
                }
                KittyDeleteType::AtCursor => {
                    let col = self.grid.cursor.col;
                    let row = self.grid.cursor.row;
                    self.images.remove_at_position(col, row);
                }
                _ => {
                    // All, ByNumber, InColumn, InRow, OnZLayer — clear all
                    self.images.clear();
                    self.transmitted_images.clear();
                }
            }
            self.dirty = true;
            return;
        }

        // Handle Put action — display a previously transmitted image
        if cmd.action == KittyAction::Put {
            if let Some(img) = self.transmitted_images.get(&cmd.image_id) {
                let col = self.grid.cursor.col;
                let row = self.grid.cursor.row;
                let cell_rows = (img.height as f32 / self.cell_pixel_height).ceil() as usize;
                let cell_cols = (img.width as f32 / self.cell_pixel_width).ceil() as usize;
                let (w, h) = (img.width, img.height);
                self.images.add(
                    img.pixels.clone(),
                    img.width,
                    img.height,
                    col,
                    row + self.scrollback.len(),
                    (self.cell_pixel_width, self.cell_pixel_height),
                );
                for _ in 0..cell_rows {
                    self.linefeed();
                }
                self.pending_image_announcements.push(ImageAnnouncement {
                    width: w,
                    height: h,
                    cell_cols,
                    cell_rows,
                    protocol: ImageProtocol::Kitty,
                    filename: None,
                });
                self.dirty = true;
                debug!(image_id = cmd.image_id, "Kitty: put displayed");
            } else {
                trace!(image_id = cmd.image_id, "Kitty: put — image not found");
            }
            return;
        }

        // Transmit / TransmitAndDisplay — may be chunked
        let final_cmd = match self.kitty_chunks.feed(cmd.clone()) {
            Some(c) => c,
            None => return, // More chunks expected
        };

        // Transmit-only: store for later display via Put
        if final_cmd.action == KittyAction::Transmit {
            if let Some((width, height, pixels)) = decode_kitty_payload(&final_cmd)
                && final_cmd.image_id > 0
            {
                self.transmitted_images.insert(
                    final_cmd.image_id,
                    TransmittedImage {
                        width,
                        height,
                        pixels,
                    },
                );
                debug!(
                    image_id = final_cmd.image_id,
                    "Kitty: image transmitted and stored"
                );
            }
            return;
        }

        // Decode the image payload
        if let Some((width, height, pixels)) = decode_kitty_payload(&final_cmd) {
            let col = self.grid.cursor.col;
            let row = self.grid.cursor.row;
            self.images.add(
                pixels,
                width,
                height,
                col,
                row + self.scrollback.len(),
                (self.cell_pixel_width, self.cell_pixel_height),
            );
            // Advance cursor past the image
            let cell_rows = (height as f32 / self.cell_pixel_height).ceil() as usize;
            let cell_cols = (width as f32 / self.cell_pixel_width).ceil() as usize;
            for _ in 0..cell_rows {
                self.linefeed();
            }
            self.pending_image_announcements.push(ImageAnnouncement {
                width,
                height,
                cell_cols,
                cell_rows,
                protocol: ImageProtocol::Kitty,
                filename: None,
            });
            self.dirty = true;
            debug!(width, height, "Kitty image placed");
        }
    }

    /// Handle a Sixel image sequence.
    fn handle_sixel(&mut self, data: &[u8]) {
        if let Some((width, height, pixels)) = crate::image::decode_sixel(data) {
            let col = self.grid.cursor.col;
            let row = self.grid.cursor.row;
            self.images.add(
                pixels,
                width,
                height,
                col,
                row + self.scrollback.len(),
                (self.cell_pixel_width, self.cell_pixel_height),
            );
            // Advance cursor past the image
            let cell_rows = (height as f32 / self.cell_pixel_height).ceil() as usize;
            let cell_cols = (width as f32 / self.cell_pixel_width).ceil() as usize;
            for _ in 0..cell_rows {
                self.linefeed();
            }
            self.pending_image_announcements.push(ImageAnnouncement {
                width,
                height,
                cell_cols,
                cell_rows,
                protocol: ImageProtocol::Sixel,
                filename: None,
            });
            self.dirty = true;
        }
    }

    /// Handle DECRQSS (Request Status String).
    /// Responds with DCS 1 $ r <value> ST for valid requests,
    /// or DCS 0 $ r ST for unrecognized requests.
    fn handle_decrqss(&mut self, data: &[u8]) {
        let query = std::str::from_utf8(data).unwrap_or("");
        let response_value = match query {
            // SGR — report current graphic rendition
            "m" => Some(self.build_sgr_report()),
            // DECSTBM — report scroll region
            "r" => {
                let top = self.scroll_region.top + 1;
                let bottom = self.scroll_region.bottom + 1;
                Some(format!("{};{}r", top, bottom))
            }
            // DECSCL — report conformance level (we report VT500 level 4, 8-bit controls)
            "\"p" => Some("65;1\"p".to_string()),
            // DECSCA — report character protection attribute (0 = not protected)
            "\"q" => Some("0\"q".to_string()),
            // DECSCUSR — report cursor style
            " q" => {
                let code = match (self.grid.cursor.shape, self.grid.cursor.blinking) {
                    (CursorShape::Block, true) => 1,
                    (CursorShape::Block, false) => 2,
                    (CursorShape::Underline, true) => 3,
                    (CursorShape::Underline, false) => 4,
                    (CursorShape::Bar, true) => 5,
                    (CursorShape::Bar, false) => 6,
                };
                Some(format!("{} q", code))
            }
            // DECSLRM — report left and right margins (default: 1 to cols)
            "s" => {
                let cols = self.grid.cols;
                Some(format!("1;{}s", cols))
            }
            // DECSLPP — report lines per page
            "t" => {
                let rows = self.grid.rows;
                Some(format!("{}t", rows))
            }
            _ => {
                trace!(query, "Unrecognized DECRQSS query");
                None
            }
        };

        let response = if let Some(value) = response_value {
            // Valid: DCS 1 $ r <value> ST
            format!("\x1bP1$r{}\x1b\\", value)
        } else {
            // Invalid: DCS 0 $ r ST
            "\x1bP0$r\x1b\\".to_string()
        };
        self.pending_responses.push(response.into_bytes());
    }

    /// Build an SGR parameter string reflecting the current cell attributes.
    fn build_sgr_report(&self) -> String {
        let attrs = &self.grid.current_attrs;
        let mut parts: Vec<String> = Vec::new();

        // Always start with reset
        parts.push("0".to_string());

        if attrs.bold {
            parts.push("1".to_string());
        }
        if attrs.dim {
            parts.push("2".to_string());
        }
        if attrs.italic {
            parts.push("3".to_string());
        }
        match attrs.underline {
            UnderlineStyle::None => {}
            UnderlineStyle::Single => parts.push("4".to_string()),
            UnderlineStyle::Double => parts.push("21".to_string()),
            UnderlineStyle::Curly => parts.push("4:3".to_string()),
            UnderlineStyle::Dotted => parts.push("4:4".to_string()),
            UnderlineStyle::Dashed => parts.push("4:5".to_string()),
        }
        if attrs.blink {
            parts.push("5".to_string());
        }
        if attrs.inverse {
            parts.push("7".to_string());
        }
        if attrs.hidden {
            parts.push("8".to_string());
        }
        if attrs.strikethrough {
            parts.push("9".to_string());
        }
        if attrs.overline {
            parts.push("53".to_string());
        }

        // Foreground color
        match attrs.fg {
            Color::Default => {}
            Color::Indexed(n) if n < 8 => parts.push(format!("{}", 30 + n)),
            Color::Indexed(n) if n < 16 => parts.push(format!("{}", 90 + n - 8)),
            Color::Indexed(n) => parts.push(format!("38;5;{}", n)),
            Color::Rgb(rgb) => parts.push(format!("38;2;{};{};{}", rgb.r, rgb.g, rgb.b)),
        }

        // Background color
        match attrs.bg {
            Color::Default => {}
            Color::Indexed(n) if n < 8 => parts.push(format!("{}", 40 + n)),
            Color::Indexed(n) if n < 16 => parts.push(format!("{}", 100 + n - 8)),
            Color::Indexed(n) => parts.push(format!("48;5;{}", n)),
            Color::Rgb(rgb) => parts.push(format!("48;2;{};{};{}", rgb.r, rgb.g, rgb.b)),
        }

        // Underline color
        match attrs.underline_color {
            Color::Default => {}
            Color::Indexed(n) => parts.push(format!("58;5;{}", n)),
            Color::Rgb(rgb) => parts.push(format!("58;2;{};{};{}", rgb.r, rgb.g, rgb.b)),
        }

        format!("{}m", parts.join(";"))
    }
}

/// Get a parameter with a default value if missing or zero.
fn param(params: &[u16], idx: usize, default: u16) -> u16 {
    params
        .get(idx)
        .copied()
        .filter(|&v| v != 0)
        .unwrap_or(default)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bell_pending_default_false() {
        let term = Terminal::new(80, 24);
        assert!(!term.bell_pending);
    }

    #[test]
    fn test_bell_sets_pending() {
        let mut term = Terminal::new(80, 24);
        term.execute(0x07); // BEL
        assert!(term.bell_pending);
    }

    #[test]
    fn test_print_basic() {
        let mut term = Terminal::new(80, 24);
        term.print('H');
        term.print('i');
        assert_eq!(term.grid.cursor.col, 2);
        assert_eq!(term.grid.cell(0, 0).unwrap().content, "H");
        assert_eq!(term.grid.cell(1, 0).unwrap().content, "i");
    }

    #[test]
    fn test_linefeed_scrolls() {
        let mut term = Terminal::new(80, 3);
        term.grid.cursor.row = 2; // bottom of screen
        term.print('A');
        term.carriage_return();
        term.linefeed(); // should scroll
        assert_eq!(term.scrollback.len(), 1);
    }

    #[test]
    fn test_cursor_movement() {
        let mut term = Terminal::new(80, 24);
        term.cursor_to(10, 5);
        assert_eq!(term.grid.cursor.col, 10);
        assert_eq!(term.grid.cursor.row, 5);

        term.cursor_up(3);
        assert_eq!(term.grid.cursor.row, 2);

        term.cursor_forward(5);
        assert_eq!(term.grid.cursor.col, 15);
    }

    #[test]
    fn test_erase_in_display() {
        let mut term = Terminal::new(10, 3);
        // Fill grid with 'X' directly (bypassing print/wrap logic)
        for r in 0..3 {
            for c in 0..10 {
                if let Some(cell) = term.grid.cell_mut(c, r) {
                    cell.content.clear();
                    cell.content.push('X');
                }
            }
        }
        term.grid.cursor.col = 5;
        term.grid.cursor.row = 1;
        term.erase_in_display(0); // erase from cursor to end

        // Row 0 should be untouched
        assert_eq!(term.grid.cell(0, 0).unwrap().content, "X");
        // Row 1, col 0-4 should be untouched
        assert_eq!(term.grid.cell(4, 1).unwrap().content, "X");
        // Row 1, col 5+ should be cleared
        assert_eq!(term.grid.cell(5, 1).unwrap().content, " ");
        // Row 2 should be cleared
        assert_eq!(term.grid.cell(0, 2).unwrap().content, " ");
    }

    #[test]
    fn test_sgr_colors() {
        let mut term = Terminal::new(80, 24);
        // SGR 31 = red foreground
        term.handle_sgr(&[31], &[]);
        assert_eq!(term.grid.current_attrs.fg, Color::Indexed(1));

        // SGR 0 = reset
        term.handle_sgr(&[0], &[]);
        assert_eq!(term.grid.current_attrs.fg, Color::Default);

        // True color: 38;2;255;128;0
        term.handle_sgr(&[38, 2, 255, 128, 0], &[]);
        assert_eq!(
            term.grid.current_attrs.fg,
            Color::Rgb(Rgb::new(255, 128, 0))
        );
    }

    #[test]
    fn test_sgr_4_subparam_curly() {
        let mut term = Terminal::new(80, 24);
        // SGR 4:3 = curly underline
        term.handle_sgr(&[4], &[vec![3]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Curly);
    }

    #[test]
    fn test_sgr_4_subparam_all_styles() {
        let mut term = Terminal::new(80, 24);

        // 4:0 = no underline
        term.handle_sgr(&[4], &[vec![0]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::None);

        // 4:1 = single
        term.handle_sgr(&[4], &[vec![1]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Single);

        // 4:2 = double
        term.handle_sgr(&[4], &[vec![2]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Double);

        // 4:3 = curly
        term.handle_sgr(&[4], &[vec![3]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Curly);

        // 4:4 = dotted
        term.handle_sgr(&[4], &[vec![4]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Dotted);

        // 4:5 = dashed
        term.handle_sgr(&[4], &[vec![5]]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Dashed);
    }

    #[test]
    fn test_sgr_4_no_subparam_is_single() {
        let mut term = Terminal::new(80, 24);
        // SGR 4 without sub-params = single underline (backward compat)
        term.handle_sgr(&[4], &[]);
        assert_eq!(term.grid.current_attrs.underline, UnderlineStyle::Single);
    }

    #[test]
    fn test_sgr_color_subparams() {
        let mut term = Terminal::new(80, 24);
        // SGR 38 with colon sub-params: 38:2:255:0:128
        term.handle_sgr(&[38], &[vec![2, 255, 0, 128]]);
        assert_eq!(
            term.grid.current_attrs.fg,
            Color::Rgb(Rgb::new(255, 0, 128))
        );

        // SGR 58 with colon sub-params: 58:2:0:255:0 (underline color)
        term.handle_sgr(&[58], &[vec![2, 0, 255, 0]]);
        assert_eq!(
            term.grid.current_attrs.underline_color,
            Color::Rgb(Rgb::new(0, 255, 0))
        );

        // SGR 48 with colon sub-params: 48:5:196 (256-color bg)
        term.handle_sgr(&[48], &[vec![5, 196]]);
        assert_eq!(term.grid.current_attrs.bg, Color::Indexed(196));
    }

    #[test]
    fn test_dsr_cursor_position() {
        let mut term = Terminal::new(80, 24);
        // New terminal: cursor at (0,0) → report should be \x1b[1;1R
        term.csi_dispatch(&[6], &[], 'n', &[]);
        assert_eq!(term.pending_responses.len(), 1);
        assert_eq!(term.pending_responses[0], b"\x1b[1;1R");
    }

    #[test]
    fn test_dsr_device_status() {
        let mut term = Terminal::new(80, 24);
        // CSI 5n → device OK → \x1b[0n
        term.csi_dispatch(&[5], &[], 'n', &[]);
        assert_eq!(term.pending_responses.len(), 1);
        assert_eq!(term.pending_responses[0], b"\x1b[0n");
    }

    #[test]
    fn test_dsr_after_cursor_move() {
        let mut term = Terminal::new(80, 24);
        // Move cursor to col=9, row=4 (0-based)
        term.cursor_to(9, 4);
        term.csi_dispatch(&[6], &[], 'n', &[]);
        assert_eq!(term.pending_responses.len(), 1);
        // 1-based: row=5, col=10
        assert_eq!(term.pending_responses[0], b"\x1b[5;10R");
    }

    #[test]
    fn test_drain_responses() {
        let mut term = Terminal::new(80, 24);
        term.csi_dispatch(&[5], &[], 'n', &[]);
        assert_eq!(term.pending_responses.len(), 1);
        let drained = term.drain_responses();
        assert_eq!(drained.len(), 1);
        assert!(term.pending_responses.is_empty());
    }

    #[test]
    fn test_auto_wrap() {
        let mut term = Terminal::new(5, 2);
        for ch in "Hello".chars() {
            term.print(ch);
        }
        // Cursor at col 4 (last column), pending_wrap set
        assert!(term.pending_wrap);
        // Print one more character — should wrap to next line
        term.print('!');
        assert_eq!(term.grid.cursor.row, 1);
        assert_eq!(term.grid.cursor.col, 1);
        assert_eq!(term.grid.cell(0, 1).unwrap().content, "!");
    }

    #[test]
    fn test_alternate_screen() {
        let mut term = Terminal::new(80, 24);
        term.print('A');
        assert!(!term.modes.alternate_screen);

        // Switch to alt screen
        term.set_private_mode(&[1049], true);
        assert!(term.modes.alternate_screen);
        assert_eq!(term.grid.cell(0, 0).unwrap().content, " "); // alt screen is blank

        term.print('B');

        // Switch back
        term.set_private_mode(&[1049], false);
        assert!(!term.modes.alternate_screen);
        assert_eq!(term.grid.cell(0, 0).unwrap().content, "A"); // primary restored
    }

    #[test]
    fn test_extract_row_text_grid_only() {
        let mut term = Terminal::new(10, 3);
        // Write "Hello" on row 0
        for ch in "Hello".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed();
        // Write "World" on row 1
        for ch in "World".chars() {
            term.print(ch);
        }

        // Extract row 0 only (absolute row = scrollback + grid_row = 0)
        let text = term.extract_row_text(0, 0);
        assert_eq!(text, "Hello");

        // Extract rows 0-1
        let text = term.extract_row_text(0, 1);
        assert_eq!(text, "Hello\nWorld");
    }

    #[test]
    fn test_extract_row_text_with_scrollback() {
        let mut term = Terminal::new(10, 2);
        // Fill rows and force scrollback
        for ch in "AAAAAAAAAA".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed();
        for ch in "BBBBBBBBBB".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed(); // This triggers scroll — "AAAAAAAAAA" goes to scrollback
        for ch in "CCCCCCCCCC".chars() {
            term.print(ch);
        }

        assert_eq!(term.scrollback.len(), 1);
        // Absolute row 0 = scrollback row 0 = "AAAAAAAAAA"
        assert_eq!(term.extract_row_text(0, 0), "AAAAAAAAAA");
        // Absolute row 1 = grid row 0 = "BBBBBBBBBB"
        assert_eq!(term.extract_row_text(1, 1), "BBBBBBBBBB");
        // Absolute row 2 = grid row 1 = "CCCCCCCCCC"
        assert_eq!(term.extract_row_text(2, 2), "CCCCCCCCCC");
        // Cross scrollback + grid boundary
        assert_eq!(
            term.extract_row_text(0, 2),
            "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC"
        );
    }

    #[test]
    fn test_osc133_command_text_extraction() {
        let mut term = Terminal::new(20, 5);

        // Simulate OSC 133;A (prompt start at row 0)
        term.osc_dispatch(&[b"133".to_vec(), b"A".to_vec()]);
        // Write prompt text
        for ch in "C:\\> ".chars() {
            term.print(ch);
        }
        // Simulate OSC 133;B (input start — user typing starts at row 0)
        term.osc_dispatch(&[b"133".to_vec(), b"B".to_vec()]);
        // Write command text
        for ch in "dir".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed();
        // Simulate OSC 133;C (execution start — output starts at row 1)
        term.osc_dispatch(&[b"133".to_vec(), b"C".to_vec()]);

        // command_text should have been auto-populated
        let block = term.shell_integ.current_block().unwrap();
        assert_eq!(block.state, wixen_shell_integ::BlockState::Executing);
        // The command text should contain "dir" (the input row text)
        assert!(block.command_text.is_some());
        let cmd = block.command_text.as_ref().unwrap();
        assert!(
            cmd.contains("dir"),
            "Expected command text to contain 'dir', got: {}",
            cmd
        );
    }

    #[test]
    fn test_viewport_cell_live() {
        let mut term = Terminal::new(10, 3);
        for ch in "Hello".chars() {
            term.print(ch);
        }
        // Live mode: viewport_cell == grid.cell
        assert_eq!(term.viewport_cell(0, 0).unwrap().content, "H");
        assert_eq!(term.viewport_cell(4, 0).unwrap().content, "o");
        assert!(term.cursor_in_viewport());
    }

    #[test]
    fn test_viewport_cell_scrolled() {
        let mut term = Terminal::new(10, 2);
        // Write 4 rows into a 2-row terminal → 2 go to scrollback
        // Row A
        for ch in "AAAAAAAAAA".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed();
        // Row B
        for ch in "BBBBBBBBBB".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed(); // A scrolls off → scrollback [A], grid [B, _]
        // Row C
        for ch in "CCCCCCCCCC".chars() {
            term.print(ch);
        }
        term.carriage_return();
        term.linefeed(); // B scrolls off → scrollback [A, B], grid [C, _]
        // Row D
        for ch in "DDDDDDDDDD".chars() {
            term.print(ch);
        }

        // scrollback = [A, B], grid = [C, D]
        assert_eq!(term.scrollback.len(), 2);

        // Live mode — viewport shows grid [C, D]
        assert_eq!(term.viewport_cell(0, 0).unwrap().content, "C");
        assert_eq!(term.viewport_cell(0, 1).unwrap().content, "D");
        assert!(term.cursor_in_viewport());

        // Scroll back 1 line — viewport shows [B, C]
        term.scroll_viewport(1);
        assert_eq!(term.viewport_offset, 1);
        assert!(!term.cursor_in_viewport());
        assert_eq!(term.viewport_cell(0, 0).unwrap().content, "B");
        assert_eq!(term.viewport_cell(0, 1).unwrap().content, "C");

        // Scroll back 1 more — viewport shows [A, B]
        term.scroll_viewport(1);
        assert_eq!(term.viewport_offset, 2);
        assert_eq!(term.viewport_cell(0, 0).unwrap().content, "A");
        assert_eq!(term.viewport_cell(0, 1).unwrap().content, "B");

        // Can't scroll further (clamped at max)
        term.scroll_viewport(1);
        assert_eq!(term.viewport_offset, 2);

        // Scroll back down to live
        term.scroll_viewport(-2);
        assert_eq!(term.viewport_offset, 0);
        assert!(term.cursor_in_viewport());
        assert_eq!(term.viewport_cell(0, 0).unwrap().content, "C");
    }

    #[test]
    fn test_osc8_open_close() {
        let mut term = Terminal::new(20, 3);

        // Open a hyperlink: OSC 8 ; ; https://example.com ST
        term.osc_dispatch(&[b"8".to_vec(), b"".to_vec(), b"https://example.com".to_vec()]);

        // Print characters — they should carry the hyperlink ID.
        for ch in "link".chars() {
            term.print(ch);
        }

        // Close the hyperlink: OSC 8 ; ; ST
        term.osc_dispatch(&[b"8".to_vec(), b"".to_vec(), b"".to_vec()]);

        // Print more — these should NOT carry the hyperlink.
        for ch in " end".chars() {
            term.print(ch);
        }

        // Verify: "link" cells have hyperlink_id > 0
        let link_id = term.grid.cell(0, 0).unwrap().attrs.hyperlink_id;
        assert!(link_id > 0, "Expected hyperlink_id > 0 for linked cell");

        // All 4 chars of "link" should share the same ID.
        for col in 0..4 {
            assert_eq!(term.grid.cell(col, 0).unwrap().attrs.hyperlink_id, link_id);
        }

        // " end" cells should have hyperlink_id == 0.
        for col in 4..8 {
            assert_eq!(term.grid.cell(col, 0).unwrap().attrs.hyperlink_id, 0);
        }

        // hyperlink_at should return the URI for linked cells.
        assert_eq!(
            term.hyperlink_at(0, 0),
            Some("https://example.com".to_string())
        );
        assert_eq!(
            term.hyperlink_at(3, 0),
            Some("https://example.com".to_string())
        );
    }

    #[test]
    fn test_osc8_with_id_param() {
        let mut term = Terminal::new(20, 3);

        // Open with explicit id: OSC 8 ; id=mylink ; https://example.com ST
        term.osc_dispatch(&[
            b"8".to_vec(),
            b"id=mylink".to_vec(),
            b"https://example.com".to_vec(),
        ]);
        term.print('A');
        term.osc_dispatch(&[b"8".to_vec(), b"".to_vec(), b"".to_vec()]);

        let id = term.grid.cell(0, 0).unwrap().attrs.hyperlink_id;
        let link = term.hyperlinks.get(id).unwrap();
        assert_eq!(link.uri, "https://example.com");
        assert_eq!(link.id.as_deref(), Some("mylink"));
    }

    #[test]
    fn test_osc8_nested() {
        let mut term = Terminal::new(20, 3);

        // Open first link
        term.osc_dispatch(&[b"8".to_vec(), b"".to_vec(), b"https://first.com".to_vec()]);
        term.print('A');

        // Open second link (overrides first without explicit close)
        term.osc_dispatch(&[b"8".to_vec(), b"".to_vec(), b"https://second.com".to_vec()]);
        term.print('B');
        term.osc_dispatch(&[b"8".to_vec(), b"".to_vec(), b"".to_vec()]);

        let id_a = term.grid.cell(0, 0).unwrap().attrs.hyperlink_id;
        let id_b = term.grid.cell(1, 0).unwrap().attrs.hyperlink_id;
        assert_ne!(id_a, id_b);
        assert_eq!(term.hyperlinks.get(id_a).unwrap().uri, "https://first.com");
        assert_eq!(term.hyperlinks.get(id_b).unwrap().uri, "https://second.com");
    }

    #[test]
    fn test_hyperlink_at_regex_fallback() {
        let mut term = Terminal::new(40, 3);

        // Write a plain-text URL (no OSC 8).
        for ch in "Visit https://rust-lang.org for info".chars() {
            term.print(ch);
        }

        // hyperlink_at should detect the URL via regex.
        assert_eq!(
            term.hyperlink_at(6, 0),
            Some("https://rust-lang.org".to_string())
        );
        // Position before the URL should return None.
        assert_eq!(term.hyperlink_at(0, 0), None);
    }

    #[test]
    fn test_decrqss_sgr_default() {
        let mut term = Terminal::new(80, 24);
        // DECRQSS for SGR: DCS $ q m ST
        term.dcs_hook(&[], b"$", 'q');
        term.dcs_put(b'm');
        term.dcs_unhook();
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Default attrs → "0m"
        assert_eq!(r, "\x1bP1$r0m\x1b\\");
    }

    #[test]
    fn test_decrqss_sgr_bold_red() {
        let mut term = Terminal::new(80, 24);
        // Set bold + red foreground
        term.handle_sgr(&[1], &[]);
        term.handle_sgr(&[31], &[]);
        // Query SGR
        term.dcs_hook(&[], b"$", 'q');
        term.dcs_put(b'm');
        term.dcs_unhook();
        let responses = term.drain_responses();
        let r = String::from_utf8(responses[0].clone()).unwrap();
        assert_eq!(r, "\x1bP1$r0;1;31m\x1b\\");
    }

    #[test]
    fn test_decrqss_decstbm() {
        let mut term = Terminal::new(80, 24);
        // Query DECSTBM: DCS $ q r ST
        term.dcs_hook(&[], b"$", 'q');
        term.dcs_put(b'r');
        term.dcs_unhook();
        let responses = term.drain_responses();
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Default scroll region = 1;24 (1-based)
        assert_eq!(r, "\x1bP1$r1;24r\x1b\\");
    }

    #[test]
    fn test_decrqss_unknown() {
        let mut term = Terminal::new(80, 24);
        // Query unknown: DCS $ q x ST
        term.dcs_hook(&[], b"$", 'q');
        term.dcs_put(b'x');
        term.dcs_unhook();
        let responses = term.drain_responses();
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Invalid → DCS 0 $ r ST
        assert_eq!(r, "\x1bP0$r\x1b\\");
    }

    #[test]
    fn test_dcs_hook_put_unhook_lifecycle() {
        let mut term = Terminal::new(80, 24);
        // Simulate a DCS sequence we don't recognize
        term.dcs_hook(&[1, 2], b"+", 'p');
        term.dcs_put(b'A');
        term.dcs_put(b'B');
        term.dcs_unhook();
        // No crash, no responses for unknown DCS
        assert!(term.drain_responses().is_empty());
    }

    #[test]
    fn test_block_selection_text() {
        use crate::selection::{GridPoint, Selection, SelectionType};

        let mut term = Terminal::new(10, 4);
        // Write a grid:
        // Row 0: "ABCDEFGHIJ"
        // Row 1: "0123456789"
        // Row 2: "abcdefghij"
        // Row 3: "KLMNOPQRST"
        for ch in "ABCDEFGHIJ".chars() {
            term.print(ch);
        }
        for ch in "0123456789".chars() {
            term.print(ch);
        }
        for ch in "abcdefghij".chars() {
            term.print(ch);
        }
        for ch in "KLMNOPQRST".chars() {
            term.print(ch);
        }

        // Block select cols 2..5, rows 1..2 → "2345\ncdef"
        let mut sel = Selection::new(GridPoint::new(2, 1), SelectionType::Block);
        sel.end = GridPoint::new(5, 2);
        term.selection = Some(sel);

        let text = term.selected_text().unwrap();
        assert_eq!(text, "2345\ncdef");
    }

    // --- OSC 52 clipboard tests ---

    #[test]
    fn test_osc52_write_clipboard() {
        let mut term = Terminal::new(80, 24);
        // OSC 52 ; c ; SGVsbG8= ST → "Hello" in base64
        term.osc_dispatch(&[b"52".to_vec(), b"c;SGVsbG8=".to_vec()]);
        let writes = term.drain_clipboard_writes();
        assert_eq!(writes.len(), 1);
        assert_eq!(writes[0], "Hello");
    }

    #[test]
    fn test_osc52_write_primary_selection() {
        let mut term = Terminal::new(80, 24);
        // OSC 52 ; p ; d29ybGQ= ST → "world" in base64
        term.osc_dispatch(&[b"52".to_vec(), b"p;d29ybGQ=".to_vec()]);
        let writes = term.drain_clipboard_writes();
        // Primary selection maps to clipboard on Windows
        assert_eq!(writes.len(), 1);
        assert_eq!(writes[0], "world");
    }

    #[test]
    fn test_osc52_write_empty_clears() {
        let mut term = Terminal::new(80, 24);
        // Empty data = clear clipboard
        term.osc_dispatch(&[b"52".to_vec(), b"c;".to_vec()]);
        let writes = term.drain_clipboard_writes();
        assert_eq!(writes.len(), 1);
        assert_eq!(writes[0], ""); // empty string signals "clear"
    }

    #[test]
    fn test_osc52_read_query() {
        let mut term = Terminal::new(80, 24);
        // Enable read-write so the read query is honored
        term.osc52_policy = Osc52Policy::ReadWrite;
        // OSC 52 ; c ; ? ST → query clipboard
        term.osc_dispatch(&[b"52".to_vec(), b"c;?".to_vec()]);
        assert!(term.clipboard_read_pending);
    }

    #[test]
    fn test_osc52_inject_clipboard_response() {
        let mut term = Terminal::new(80, 24);
        term.inject_clipboard_response("Hello World");
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        // Response format: OSC 52 ; c ; <base64> ST
        let resp = String::from_utf8(responses[0].clone()).unwrap();
        assert!(resp.starts_with("\x1b]52;c;"));
        assert!(resp.ends_with("\x07"));
        // Decode the base64 payload
        let payload = &resp[7..resp.len() - 1]; // strip "\x1b]52;c;" and "\x07"
        use base64::Engine;
        let decoded = base64::engine::general_purpose::STANDARD
            .decode(payload)
            .unwrap();
        assert_eq!(String::from_utf8(decoded).unwrap(), "Hello World");
    }

    #[test]
    fn test_osc52_invalid_base64_ignored() {
        let mut term = Terminal::new(80, 24);
        // Invalid base64 should be silently ignored
        term.osc_dispatch(&[b"52".to_vec(), b"c;!!!not-base64!!!".to_vec()]);
        let writes = term.drain_clipboard_writes();
        assert!(writes.is_empty());
    }

    #[test]
    fn test_osc52_multiple_writes() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"52".to_vec(), b"c;SGVsbG8=".to_vec()]); // Hello
        term.osc_dispatch(&[b"52".to_vec(), b"c;V29ybGQ=".to_vec()]); // World
        let writes = term.drain_clipboard_writes();
        assert_eq!(writes.len(), 2);
        assert_eq!(writes[0], "Hello");
        assert_eq!(writes[1], "World");
        // drain should clear
        let writes2 = term.drain_clipboard_writes();
        assert!(writes2.is_empty());
    }

    // -----------------------------------------------------------------------
    // OSC 52 policy enforcement tests
    // -----------------------------------------------------------------------

    #[test]
    fn test_osc52_read_blocked_by_default() {
        let mut term = Terminal::new(80, 24);
        // Default policy is WriteOnly — read should be blocked
        assert_eq!(term.osc52_policy, Osc52Policy::WriteOnly);
        term.osc_dispatch(&[b"52".to_vec(), b"c;?".to_vec()]);
        assert!(
            !term.clipboard_read_pending,
            "clipboard read must be blocked under WriteOnly policy"
        );
    }

    #[test]
    fn test_osc52_write_allowed_by_default() {
        let mut term = Terminal::new(80, 24);
        assert_eq!(term.osc52_policy, Osc52Policy::WriteOnly);
        term.osc_dispatch(&[b"52".to_vec(), b"c;SGVsbG8=".to_vec()]);
        let writes = term.drain_clipboard_writes();
        assert_eq!(
            writes.len(),
            1,
            "clipboard write must succeed under WriteOnly policy"
        );
        assert_eq!(writes[0], "Hello");
    }

    #[test]
    fn test_osc52_read_allowed_with_readwrite_policy() {
        let mut term = Terminal::new(80, 24);
        term.osc52_policy = Osc52Policy::ReadWrite;
        term.osc_dispatch(&[b"52".to_vec(), b"c;?".to_vec()]);
        assert!(
            term.clipboard_read_pending,
            "clipboard read must be allowed under ReadWrite policy"
        );
    }

    #[test]
    fn test_osc52_disabled_blocks_read_and_write() {
        let mut term = Terminal::new(80, 24);
        term.osc52_policy = Osc52Policy::Disabled;
        // Read should be blocked
        term.osc_dispatch(&[b"52".to_vec(), b"c;?".to_vec()]);
        assert!(
            !term.clipboard_read_pending,
            "read must be blocked when disabled"
        );
        // Write should be blocked
        term.osc_dispatch(&[b"52".to_vec(), b"c;SGVsbG8=".to_vec()]);
        let writes = term.drain_clipboard_writes();
        assert!(writes.is_empty(), "write must be blocked when disabled");
    }

    // -----------------------------------------------------------------------
    // OSC 9;4 progress tests
    // -----------------------------------------------------------------------

    #[test]
    fn test_osc9_progress_normal() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"1".to_vec(), b"75".to_vec()]);
        assert_eq!(term.progress, ProgressState::Normal(75));
    }

    #[test]
    fn test_osc9_progress_error() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"2".to_vec(), b"50".to_vec()]);
        assert_eq!(term.progress, ProgressState::Error(50));
    }

    #[test]
    fn test_osc9_progress_indeterminate() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"3".to_vec()]);
        assert_eq!(term.progress, ProgressState::Indeterminate);
    }

    #[test]
    fn test_osc9_progress_paused() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"4".to_vec(), b"30".to_vec()]);
        assert_eq!(term.progress, ProgressState::Paused(30));
    }

    #[test]
    fn test_osc9_progress_hidden() {
        let mut term = Terminal::new(80, 24);
        // First set to normal, then hide
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"1".to_vec(), b"50".to_vec()]);
        assert_eq!(term.progress, ProgressState::Normal(50));
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"0".to_vec()]);
        assert_eq!(term.progress, ProgressState::Hidden);
    }

    #[test]
    fn test_osc9_progress_clamps_to_100() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"9".to_vec(), b"4".to_vec(), b"1".to_vec(), b"200".to_vec()]);
        assert_eq!(term.progress, ProgressState::Normal(100));
    }

    #[test]
    fn test_osc9_progress_default_hidden() {
        let term = Terminal::new(80, 24);
        assert_eq!(term.progress, ProgressState::Hidden);
    }

    // -----------------------------------------------------------------------
    // visible_images tests
    // -----------------------------------------------------------------------

    #[test]
    fn test_visible_images_in_viewport() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Add a 16x16 image at grid col 5, absolute row = scrollback.len() + 2
        let pixels = vec![255u8; 16 * 16 * 4];
        term.images.add(pixels, 16, 16, 5, 2, (8.0, 16.0));
        let vis = term.visible_images(8.0, 16.0);
        assert_eq!(vis.len(), 1);
        assert_eq!(vis[0].width, 16);
        assert_eq!(vis[0].height, 16);
        assert_eq!(vis[0].px_x, 40.0); // col 5 * 8.0
        assert_eq!(vis[0].px_y, 32.0); // row 2 * 16.0
    }

    #[test]
    fn test_visible_images_off_screen() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Image at row 30 — beyond viewport (rows 0..24)
        let pixels = vec![0u8; 8 * 8 * 4];
        term.images.add(pixels, 8, 8, 0, 30, (8.0, 16.0));
        let vis = term.visible_images(8.0, 16.0);
        assert!(vis.is_empty());
    }

    #[test]
    fn test_visible_images_partially_above_viewport() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Simulate scrollback: push lines so the viewport can scroll
        for _ in 0..10 {
            term.grid.cursor.row = term.rows() - 1;
            term.linefeed();
        }
        // Image at absolute row 5, 2 cell_rows tall
        let pixels = vec![0u8; 16 * 32 * 4];
        term.images.add(pixels, 16, 32, 0, 5, (8.0, 16.0));
        // Scroll viewport to see row 5 at the top
        term.viewport_offset = term.scrollback.len() - 5;
        let vis = term.visible_images(8.0, 16.0);
        assert_eq!(vis.len(), 1);
        assert_eq!(vis[0].px_y, 0.0); // row 5 maps to viewport row 0
    }

    #[test]
    fn test_visible_images_scrolled_past() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Image at row 0
        let pixels = vec![0u8; 8 * 16 * 4];
        term.images.add(pixels, 8, 16, 0, 0, (8.0, 16.0));
        // Push scrollback beyond the image
        for _ in 0..30 {
            term.grid.cursor.row = term.rows() - 1;
            term.linefeed();
        }
        // Viewport at live position — image at row 0 is far above
        let vis = term.visible_images(8.0, 16.0);
        assert!(vis.is_empty());
    }

    // --- OSC 1337 iTerm2 inline image tests ---

    /// Helper: create a minimal 2x2 red PNG as base64.
    fn make_tiny_png_base64() -> String {
        use image::{ImageBuffer, Rgba};
        let img: ImageBuffer<Rgba<u8>, Vec<u8>> =
            ImageBuffer::from_pixel(2, 2, Rgba([255, 0, 0, 255]));
        let mut png_bytes = Vec::new();
        {
            use std::io::Cursor;
            img.write_to(&mut Cursor::new(&mut png_bytes), image::ImageFormat::Png)
                .unwrap();
        }
        use base64::Engine;
        base64::engine::general_purpose::STANDARD.encode(&png_bytes)
    }

    #[test]
    fn test_osc_1337_inline_image() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        let b64 = make_tiny_png_base64();
        // OSC 1337 ; File=inline=1:<base64> ST
        // Parser splits on semicolons: params = ["1337", "File=inline=1:<base64>"]
        let payload = format!("File=inline=1:{}", b64);
        term.osc_dispatch(&[b"1337".to_vec(), payload.into_bytes()]);
        assert_eq!(term.images.len(), 1);
        let img = &term.images.images()[0];
        assert_eq!(img.width, 2);
        assert_eq!(img.height, 2);
    }

    #[test]
    fn test_osc_1337_non_inline_ignored() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        let b64 = make_tiny_png_base64();
        // inline=0 → download, not display
        let payload = format!("File=inline=0:{}", b64);
        term.osc_dispatch(&[b"1337".to_vec(), payload.into_bytes()]);
        assert_eq!(term.images.len(), 0); // Not displayed
    }

    #[test]
    fn test_osc_1337_invalid_base64() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Invalid base64 data
        let payload = b"File=inline=1:!!!invalid!!!".to_vec();
        term.osc_dispatch(&[b"1337".to_vec(), payload]);
        assert_eq!(term.images.len(), 0); // Gracefully ignored
    }

    #[test]
    fn test_osc_1337_cursor_advance() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        let initial_row = term.grid.cursor.row;
        let b64 = make_tiny_png_base64();
        // 2x2 PNG with 16px cell height → ceil(2/16) = 1 cell row
        let payload = format!("File=inline=1:{}", b64);
        term.osc_dispatch(&[b"1337".to_vec(), payload.into_bytes()]);
        // Cursor should advance past the image
        assert!(term.grid.cursor.row > initial_row);
    }

    // --- Kitty graphics protocol tests ---

    #[test]
    fn test_kitty_apc_rgba_image() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // 2x2 RGBA image = 16 bytes
        let pixels = [255u8, 0, 0, 255].repeat(4);
        let b64 = base64::engine::general_purpose::STANDARD.encode(&pixels);
        let apc = format!("Ga=T,f=32,s=2,v=2,i=1;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert_eq!(term.images.len(), 1);
        let img = &term.images.images()[0];
        assert_eq!(img.width, 2);
        assert_eq!(img.height, 2);
    }

    #[test]
    fn test_kitty_apc_png_image() {
        use base64::Engine;
        use image::{ImageBuffer, Rgba};
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Create a tiny PNG
        let img: ImageBuffer<Rgba<u8>, Vec<u8>> =
            ImageBuffer::from_pixel(3, 3, Rgba([0, 128, 255, 255]));
        let mut png_bytes = Vec::new();
        {
            use std::io::Cursor;
            img.write_to(&mut Cursor::new(&mut png_bytes), image::ImageFormat::Png)
                .unwrap();
        }
        let b64 = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
        let apc = format!("Ga=T,f=100,i=2;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert_eq!(term.images.len(), 1);
        let stored = &term.images.images()[0];
        assert_eq!(stored.width, 3);
        assert_eq!(stored.height, 3);
    }

    #[test]
    fn test_kitty_query_response() {
        let mut term = Terminal::new(80, 24);
        term.apc_dispatch(b"Ga=q,i=99");
        assert_eq!(term.images.len(), 0); // No image displayed
        // Should have a response queued
        assert_eq!(term.pending_responses.len(), 1);
        let resp = String::from_utf8(term.pending_responses[0].clone()).unwrap();
        assert!(resp.contains("i=99"));
        assert!(resp.contains("OK"));
    }

    #[test]
    fn test_kitty_delete_clears_images() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Add an image first
        let pixels = [255u8, 0, 0, 255].repeat(4);
        let b64 = base64::engine::general_purpose::STANDARD.encode(&pixels);
        let apc = format!("Ga=T,f=32,s=2,v=2;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert_eq!(term.images.len(), 1);
        // Delete
        term.apc_dispatch(b"Ga=d");
        assert_eq!(term.images.len(), 0);
    }

    #[test]
    fn test_kitty_chunked_upload() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // 1x2 RGBA = 8 bytes, split into 2 chunks of 4 bytes each
        let chunk1 = base64::engine::general_purpose::STANDARD.encode(&[255, 0, 0, 255]);
        let chunk2 = base64::engine::general_purpose::STANDARD.encode(&[0, 255, 0, 255]);
        let apc1 = format!("Ga=T,f=32,s=1,v=2,m=1,i=10;{}", chunk1);
        let apc2 = format!("Ga=T,f=32,s=1,v=2,m=0,i=10;{}", chunk2);
        term.apc_dispatch(apc1.as_bytes());
        assert_eq!(term.images.len(), 0); // Not finalized yet
        term.apc_dispatch(apc2.as_bytes());
        assert_eq!(term.images.len(), 1);
    }

    #[test]
    fn test_kitty_transmit_stores_image() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Transmit only (a=t) — should NOT display
        let pixels = [255u8, 0, 0, 255].repeat(4);
        let b64 = base64::engine::general_purpose::STANDARD.encode(&pixels);
        let apc = format!("Ga=t,f=32,s=2,v=2,i=7;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert_eq!(term.images.len(), 0, "Transmit-only should not display");
        assert!(
            term.has_transmitted_image(7),
            "Image should be stored for later put"
        );
    }

    #[test]
    fn test_kitty_put_displays_transmitted_image() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // First transmit
        let pixels = [255u8, 0, 0, 255].repeat(4);
        let b64 = base64::engine::general_purpose::STANDARD.encode(&pixels);
        let apc = format!("Ga=t,f=32,s=2,v=2,i=7;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert_eq!(term.images.len(), 0);
        // Then put (display)
        term.apc_dispatch(b"Ga=p,i=7");
        assert_eq!(term.images.len(), 1, "Put should display the image");
    }

    #[test]
    fn test_kitty_put_nonexistent_id_does_nothing() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        term.apc_dispatch(b"Ga=p,i=999");
        assert_eq!(term.images.len(), 0, "Put with unknown ID should be no-op");
    }

    #[test]
    fn test_kitty_delete_by_id_also_clears_transmitted() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        let pixels = [255u8, 0, 0, 255].repeat(4);
        let b64 = base64::engine::general_purpose::STANDARD.encode(&pixels);
        let apc = format!("Ga=t,f=32,s=2,v=2,i=7;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert!(term.has_transmitted_image(7));
        // Delete by id
        term.apc_dispatch(b"Ga=d,d=i,i=7");
        assert!(
            !term.has_transmitted_image(7),
            "Delete should also clear transmitted storage"
        );
    }

    // --- Command-block navigation tests ---

    /// Helper: set up a terminal with two completed command blocks in scrollback.
    fn setup_command_blocks() -> Terminal {
        let mut term = Terminal::new(80, 5);

        // Block 1
        let abs1 = term.scrollback.len();
        term.shell_integ.handle_osc133("A", "", abs1);
        term.shell_integ.handle_osc133("B", "", abs1 + 1);
        term.shell_integ.handle_osc133("C", "", abs1 + 2);
        term.grid.cursor.row = 2;
        term.grid.cursor.col = 0;
        for ch in "hello world".chars() {
            term.print(ch);
        }
        term.shell_integ.handle_osc133("D", "0", abs1 + 4);
        if let Some(block) = term.shell_integ.current_block_mut() {
            block.command_text = Some("echo hello world".to_string());
        }

        // Push into scrollback
        for _ in 0..8 {
            term.grid.cursor.row = term.rows() - 1;
            term.linefeed();
        }

        // Block 2
        let abs2 = term.scrollback.len() + term.grid.cursor.row;
        term.shell_integ.handle_osc133("A", "", abs2);
        term.shell_integ.handle_osc133("B", "", abs2 + 1);
        term.shell_integ.handle_osc133("C", "", abs2 + 2);
        term.grid.cursor.row = 2;
        term.grid.cursor.col = 0;
        for ch in "second output".chars() {
            term.print(ch);
        }
        term.shell_integ.handle_osc133("D", "1", abs2 + 4);
        if let Some(block) = term.shell_integ.current_block_mut() {
            block.command_text = Some("ls -la".to_string());
        }

        // Push more into scrollback
        for _ in 0..8 {
            term.grid.cursor.row = term.rows() - 1;
            term.linefeed();
        }

        term
    }

    #[test]
    fn test_jump_to_previous_prompt() {
        let mut term = setup_command_blocks();
        assert_eq!(term.viewport_offset, 0);
        assert!(term.jump_to_previous_prompt());
        assert!(term.viewport_offset > 0);
    }

    #[test]
    fn test_jump_to_previous_prompt_again() {
        let mut term = setup_command_blocks();
        term.jump_to_previous_prompt();
        let first_offset = term.viewport_offset;
        assert!(term.jump_to_previous_prompt());
        assert!(term.viewport_offset > first_offset);
    }

    #[test]
    fn test_jump_to_previous_prompt_no_blocks() {
        let mut term = Terminal::new(80, 24);
        assert!(!term.jump_to_previous_prompt());
    }

    #[test]
    fn test_jump_to_next_prompt() {
        let mut term = setup_command_blocks();
        let scrollback_len = term.scrollback.len();
        term.viewport_offset = scrollback_len;
        assert!(term.jump_to_next_prompt());
        assert!(term.viewport_offset < scrollback_len);
    }

    #[test]
    fn test_jump_to_next_prompt_returns_to_live() {
        let mut term = setup_command_blocks();
        term.viewport_offset = 1;
        for _ in 0..10 {
            if term.viewport_offset == 0 {
                break;
            }
            term.jump_to_next_prompt();
        }
        assert_eq!(term.viewport_offset, 0);
    }

    #[test]
    fn test_last_command_output() {
        // Use a simple terminal without scrollback for output extraction
        let mut term = Terminal::new(80, 24);
        let abs = term.scrollback.len();
        term.shell_integ.handle_osc133("A", "", abs);
        term.shell_integ.handle_osc133("B", "", abs + 1);
        term.shell_integ.handle_osc133("C", "", abs + 2);
        // Write output at grid row 2
        term.grid.cursor.row = 2;
        term.grid.cursor.col = 0;
        for ch in "test output here".chars() {
            term.print(ch);
        }
        term.shell_integ.handle_osc133("D", "0", abs + 3);
        if let Some(block) = term.shell_integ.current_block_mut() {
            block.command_text = Some("my cmd".to_string());
        }
        let output = term.last_command_output();
        assert!(output.is_some());
        assert!(output.unwrap().contains("test output here"));
    }

    #[test]
    fn test_last_command_output_no_blocks() {
        let term = Terminal::new(80, 24);
        assert!(term.last_command_output().is_none());
    }

    #[test]
    fn test_jump_to_next_prompt_no_blocks() {
        let mut term = Terminal::new(80, 24);
        assert!(!term.jump_to_next_prompt());
    }

    #[test]
    fn test_last_command_text() {
        let term = setup_command_blocks();
        let cmd = term.last_command_text();
        assert_eq!(cmd.as_deref(), Some("ls -la"));
    }

    #[test]
    fn test_last_command_text_no_blocks() {
        let term = Terminal::new(80, 24);
        assert!(term.last_command_text().is_none());
    }

    // ── Tier 3B: URL hover highlighting tests ──

    #[test]
    fn test_hover_url_detected() {
        let mut term = Terminal::new(80, 24);
        // Write "Visit https://example.com now" starting at row 0
        let text = "Visit https://example.com now";
        for (i, ch) in text.chars().enumerate() {
            if let Some(cell) = term.grid.cell_mut(i, 0) {
                cell.content.clear();
                cell.content.push(ch);
            }
        }
        // Hover over column 10 (within the URL)
        term.update_hover(10, 0);
        let hover = term.hover_state();
        assert!(hover.is_some());
        let h = hover.unwrap();
        assert_eq!(h.url, "https://example.com");
        assert_eq!(h.viewport_row, 0);
        assert_eq!(h.col_start, 6);
        assert_eq!(h.col_end, 25);
    }

    #[test]
    fn test_hover_no_url() {
        let mut term = Terminal::new(80, 24);
        let text = "Just plain text here";
        for (i, ch) in text.chars().enumerate() {
            if let Some(cell) = term.grid.cell_mut(i, 0) {
                cell.content.clear();
                cell.content.push(ch);
            }
        }
        term.update_hover(5, 0);
        assert!(term.hover_state().is_none());
    }

    #[test]
    fn test_hover_clears_on_move_away() {
        let mut term = Terminal::new(80, 24);
        let text = "Visit https://example.com now";
        for (i, ch) in text.chars().enumerate() {
            if let Some(cell) = term.grid.cell_mut(i, 0) {
                cell.content.clear();
                cell.content.push(ch);
            }
        }
        term.update_hover(10, 0);
        assert!(term.hover_state().is_some());

        // Move away from URL
        term.update_hover(0, 0);
        assert!(term.hover_state().is_none());
    }

    #[test]
    fn test_clear_hover() {
        let mut term = Terminal::new(80, 24);
        let text = "Visit https://example.com now";
        for (i, ch) in text.chars().enumerate() {
            if let Some(cell) = term.grid.cell_mut(i, 0) {
                cell.content.clear();
                cell.content.push(ch);
            }
        }
        term.update_hover(10, 0);
        assert!(term.hover_state().is_some());

        term.clear_hover();
        assert!(term.hover_state().is_none());
    }

    // ── Tier 2B: PTY exit banner tests ──

    #[test]
    fn test_exit_banner_code_zero() {
        let mut term = Terminal::new(80, 24);
        term.write_exit_banner(Some(0));
        let text = term.visible_text();
        assert!(text.contains("[Process exited with code 0]"));
        assert!(text.contains("Press Enter to restart"));
    }

    #[test]
    fn test_exit_banner_code_nonzero() {
        let mut term = Terminal::new(80, 24);
        term.write_exit_banner(Some(1));
        let text = term.visible_text();
        assert!(text.contains("[Process exited with code 1]"));
    }

    #[test]
    fn test_exit_banner_no_code() {
        let mut term = Terminal::new(80, 24);
        term.write_exit_banner(None);
        let text = term.visible_text();
        assert!(text.contains("[Process exited]"));
    }

    #[test]
    fn test_exit_banner_marks_dirty() {
        let mut term = Terminal::new(80, 24);
        term.dirty = false;
        term.write_exit_banner(Some(0));
        assert!(term.dirty);
    }

    // ── Tier M: Effective title tests ──

    #[test]
    fn test_effective_title_default() {
        let term = Terminal::new(80, 24);
        assert_eq!(term.effective_title(), "Wixen Terminal");
    }

    #[test]
    fn test_effective_title_osc_0_2() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"0".to_vec(), b"My Shell".to_vec()]);
        assert_eq!(term.effective_title(), "My Shell");
        assert!(term.title_dirty);
    }

    #[test]
    fn test_effective_title_osc7_cwd() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[
            b"7".to_vec(),
            b"file://host/C:/Users/Pratik/Projects".to_vec(),
        ]);
        assert_eq!(term.effective_title(), "Projects");
        assert!(term.title_dirty);
    }

    #[test]
    fn test_effective_title_osc_overrides_cwd() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[
            b"7".to_vec(),
            b"file://host/C:/Users/Pratik/Projects".to_vec(),
        ]);
        term.title_dirty = false;
        term.osc_dispatch(&[b"0".to_vec(), b"Custom Title".to_vec()]);
        assert_eq!(term.effective_title(), "Custom Title");
    }

    #[test]
    fn test_effective_title_cwd_trailing_slash() {
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"7".to_vec(), b"file://host/C:/Users/".to_vec()]);
        assert_eq!(term.effective_title(), "Users");
    }

    #[test]
    fn test_select_all() {
        let mut term = Terminal::new(10, 5);
        term.select_all();
        assert!(term.selection.is_some());
        let sel = term.selection.as_ref().unwrap();
        assert_eq!(sel.anchor, GridPoint::new(0, 0));
        assert_eq!(sel.end, GridPoint::new(9, 4));
        assert_eq!(sel.sel_type, SelectionType::Normal);
    }

    #[test]
    fn test_clear_scrollback() {
        let mut term = Terminal::new(10, 5);
        // Push some lines into scrollback
        for _ in 0..10 {
            let row = term.grid.scroll_up();
            term.scrollback.push(row);
        }
        assert!(!term.scrollback.is_empty());
        term.viewport_offset = 5;
        term.clear_scrollback();
        assert!(term.scrollback.is_empty());
        assert_eq!(term.viewport_offset, 0);
    }

    #[test]
    fn test_scroll_to_selection_no_selection() {
        let mut term = Terminal::new(10, 5);
        assert!(!term.scroll_to_selection());
    }

    #[test]
    fn test_scroll_to_selection_at_bottom() {
        let mut term = Terminal::new(10, 5);
        // Create a selection on row 2 of the live grid
        use crate::selection::{GridPoint, Selection, SelectionType};
        term.selection = Some(Selection::new(GridPoint::new(0, 2), SelectionType::Normal));
        // Already at bottom — no scroll needed
        assert!(!term.scroll_to_selection());
        assert_eq!(term.viewport_offset, 0);
    }

    #[test]
    fn test_scroll_to_selection_snaps_to_bottom() {
        let mut term = Terminal::new(10, 5);
        use crate::selection::{GridPoint, Selection, SelectionType};
        term.selection = Some(Selection::new(GridPoint::new(0, 2), SelectionType::Normal));
        // Scroll up into history
        for _ in 0..10 {
            let row = term.grid.scroll_up();
            term.scrollback.push(row);
        }
        term.viewport_offset = 3;
        // The selection is on the live grid — scroll_to_selection should snap to bottom
        assert!(term.scroll_to_selection());
        assert_eq!(term.viewport_offset, 0);
    }

    #[test]
    fn test_scroll_to_cursor_already_at_bottom() {
        let mut term = Terminal::new(10, 5);
        assert!(!term.scroll_to_cursor());
    }

    #[test]
    fn test_scroll_to_cursor_snaps_to_bottom() {
        let mut term = Terminal::new(10, 5);
        for _ in 0..10 {
            let row = term.grid.scroll_up();
            term.scrollback.push(row);
        }
        term.viewport_offset = 5;
        assert!(term.scroll_to_cursor());
        assert_eq!(term.viewport_offset, 0);
    }

    #[test]
    fn test_export_buffer_text_empty() {
        let term = Terminal::new(10, 5);
        let text = term.export_buffer_text();
        // All cells are spaces — trimmed to empty lines
        for line in text.lines() {
            assert!(line.is_empty(), "expected empty trimmed lines");
        }
    }

    #[test]
    fn test_export_buffer_text_with_content() {
        let mut term = Terminal::new(10, 3);
        // Write "Hi" on row 0 via cell_mut
        term.grid.cell_mut(0, 0).unwrap().content = "H".into();
        term.grid.cell_mut(1, 0).unwrap().content = "i".into();
        // Push row 0 into scrollback and write "Bye" on the new row 0
        let row = term.grid.scroll_up();
        term.scrollback.push(row);
        term.grid.cell_mut(0, 0).unwrap().content = "B".into();
        term.grid.cell_mut(1, 0).unwrap().content = "y".into();
        term.grid.cell_mut(2, 0).unwrap().content = "e".into();

        let text = term.export_buffer_text();
        let lines: Vec<&str> = text.lines().collect();
        assert!(lines.iter().any(|l| l.contains("Hi")));
        assert!(lines.iter().any(|l| l.contains("Bye")));
    }

    // --- DCS Dispatch Expansion Tests ---

    #[test]
    fn test_decrqss_decscl() {
        let mut term = Terminal::new(80, 24);
        // DECRQSS for DECSCL: DCS $ q " p ST
        term.dcs_hook(&[], b"$", 'q');
        for b in b"\"p" {
            term.dcs_put(*b);
        }
        term.dcs_unhook();
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Report VT500 level: 65;1 " p
        assert_eq!(r, "\x1bP1$r65;1\"p\x1b\\");
    }

    #[test]
    fn test_decrqss_decscusr() {
        let mut term = Terminal::new(80, 24);
        // Default cursor is blinking block → DECSCUSR code 1
        term.dcs_hook(&[], b"$", 'q');
        for b in b" q" {
            term.dcs_put(*b);
        }
        term.dcs_unhook();
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Blinking block → 1 SP q
        assert_eq!(r, "\x1bP1$r1 q\x1b\\");
    }

    #[test]
    fn test_decrqss_decscusr_bar() {
        let mut term = Terminal::new(80, 24);
        // Set cursor to steady bar
        term.grid.cursor.shape = crate::cursor::CursorShape::Bar;
        term.grid.cursor.blinking = false;
        term.dcs_hook(&[], b"$", 'q');
        for b in b" q" {
            term.dcs_put(*b);
        }
        term.dcs_unhook();
        let responses = term.drain_responses();
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Steady bar → 6 SP q
        assert_eq!(r, "\x1bP1$r6 q\x1b\\");
    }

    #[test]
    fn test_decrqss_decslrm() {
        let mut term = Terminal::new(80, 24);
        term.dcs_hook(&[], b"$", 'q');
        for b in b"s" {
            term.dcs_put(*b);
        }
        term.dcs_unhook();
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Default left/right margins: 1;80
        assert_eq!(r, "\x1bP1$r1;80s\x1b\\");
    }

    #[test]
    fn test_decrqss_decslpp() {
        let mut term = Terminal::new(120, 40);
        term.dcs_hook(&[], b"$", 'q');
        for b in b"t" {
            term.dcs_put(*b);
        }
        term.dcs_unhook();
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Lines per page: 40
        assert_eq!(r, "\x1bP1$r40t\x1b\\");
    }

    #[test]
    fn test_decrqss_decslrm_narrow() {
        let mut term = Terminal::new(40, 10);
        term.dcs_hook(&[], b"$", 'q');
        for b in b"s" {
            term.dcs_put(*b);
        }
        term.dcs_unhook();
        let responses = term.drain_responses();
        let r = String::from_utf8(responses[0].clone()).unwrap();
        // Narrow terminal: 1;40
        assert_eq!(r, "\x1bP1$r1;40s\x1b\\");
    }

    // --- G0/G1 Character Set Tests ---

    #[test]
    fn test_dec_line_drawing_mapping() {
        // 'q' → '─' (horizontal line, U+2500)
        assert_eq!(map_line_drawing('q'), '─');
        // 'x' → '│' (vertical line, U+2502)
        assert_eq!(map_line_drawing('x'), '│');
        // 'l' → '┌' (top-left corner)
        assert_eq!(map_line_drawing('l'), '┌');
        // 'k' → '┐' (top-right corner)
        assert_eq!(map_line_drawing('k'), '┐');
        // 'm' → '└' (bottom-left corner)
        assert_eq!(map_line_drawing('m'), '└');
        // 'j' → '┘' (bottom-right corner)
        assert_eq!(map_line_drawing('j'), '┘');
        // 'a' → '▒' (checkerboard)
        assert_eq!(map_line_drawing('a'), '▒');
        // Characters outside 0x60-0x7E range pass through unchanged
        assert_eq!(map_line_drawing('A'), 'A');
        assert_eq!(map_line_drawing('Z'), 'Z');
    }

    #[test]
    fn test_designate_g0_line_drawing() {
        let mut term = Terminal::new(80, 24);
        // ESC ( 0 — designate DEC line drawing to G0
        term.esc_dispatch(b"(", '0');
        // G0 is active by default (gl = g0), so printing 'q' should yield '─'
        term.print('q');
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "─");
    }

    #[test]
    fn test_so_si_shifts_charset() {
        let mut term = Terminal::new(80, 24);
        // Designate G1 as line drawing: ESC ) 0
        term.esc_dispatch(b")", '0');
        // SO (0x0e) — shift out to G1
        term.execute(0x0e);
        // Now printing 'q' should yield '─'
        term.print('q');
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "─");
        // SI (0x0f) — shift in to G0 (ASCII)
        term.execute(0x0f);
        // Now printing 'q' should yield 'q'
        term.print('q');
        let cell = term.grid.cell(1, 0).unwrap();
        assert_eq!(cell.content, "q");
    }

    #[test]
    fn test_designate_g1_line_drawing() {
        let mut term = Terminal::new(80, 24);
        // ESC ) 0 — designate DEC line drawing to G1
        term.esc_dispatch(b")", '0');
        // SO — activate G1
        term.execute(0x0e);
        // Print 'x' → should yield '│'
        term.print('x');
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "│");
    }

    #[test]
    fn test_save_restore_preserves_charset() {
        let mut term = Terminal::new(80, 24);
        // Designate G0 as line drawing
        term.esc_dispatch(b"(", '0');
        // Save cursor (DECSC)
        term.esc_dispatch(&[], '7');
        // Designate G0 back to ASCII
        term.esc_dispatch(b"(", 'B');
        // Verify G0 is ASCII now
        term.print('q');
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "q");
        // Restore cursor (DECRC) — should restore G0=LineDrawing
        term.esc_dispatch(&[], '8');
        term.print('q');
        // cursor was saved at col 0, so after restore we're at col 0 again
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "─");
    }

    #[test]
    fn test_reset_clears_charset() {
        let mut term = Terminal::new(80, 24);
        // Designate G0 as line drawing
        term.esc_dispatch(b"(", '0');
        // Verify line drawing is active
        term.print('q');
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "─");
        // Reset (RIS)
        term.reset();
        // After reset, G0 should be ASCII
        term.print('q');
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "q");
    }

    // --- Tier 8: Tmux control mode ---

    #[test]
    fn test_tmux_control_mode_activation() {
        let mut term = Terminal::new(80, 24);
        assert!(!term.tmux_control_active());
        term.set_tmux_control(true);
        assert!(term.tmux_control_active());
    }

    #[test]
    fn test_tmux_line_buffering() {
        let mut term = Terminal::new(80, 24);
        term.set_tmux_control(true);
        term.push_tmux_line("%window-add @1".to_string());
        term.push_tmux_line("%window-renamed @1 bash".to_string());
        let lines = term.drain_tmux_lines();
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0], "%window-add @1");
        assert_eq!(lines[1], "%window-renamed @1 bash");
        // After drain, buffer is empty
        assert!(term.drain_tmux_lines().is_empty());
    }

    #[test]
    fn test_tmux_control_mode_reset() {
        let mut term = Terminal::new(80, 24);
        term.set_tmux_control(true);
        term.push_tmux_line("some line".to_string());
        term.reset();
        assert!(!term.tmux_control_active());
        assert!(term.drain_tmux_lines().is_empty());
    }

    #[test]
    fn test_tmux_no_buffering_when_inactive() {
        let mut term = Terminal::new(80, 24);
        // tmux mode is off by default
        assert!(!term.tmux_control_active());
        // drain should be empty even without pushing anything
        assert!(term.drain_tmux_lines().is_empty());
    }

    // --- Tier 11: Session restore ---

    #[test]
    fn test_export_import_roundtrip() {
        let mut term = Terminal::new(80, 24);
        // Write some content
        for ch in "hello world".chars() {
            term.print(ch);
        }
        term.title = "Test Title".to_string();

        let state = term.export_session();
        assert_eq!(state.cols, 80);
        assert_eq!(state.rows, 24);
        assert_eq!(state.title, "Test Title");
        assert!(state.grid_rows[0].starts_with("hello world"));

        // Import into a fresh terminal
        let mut term2 = Terminal::new(80, 24);
        term2.import_session(&state);
        assert_eq!(term2.title, "Test Title");
        let cell = term2.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "h");
    }

    #[test]
    fn test_export_preserves_cursor() {
        let mut term = Terminal::new(80, 24);
        for ch in "abc".chars() {
            term.print(ch);
        }
        let state = term.export_session();
        assert_eq!(state.cursor_col, 3);
        assert_eq!(state.cursor_row, 0);
    }

    #[test]
    fn test_export_preserves_title() {
        let mut term = Terminal::new(80, 24);
        term.title = "Custom Title".to_string();
        let state = term.export_session();
        assert_eq!(state.title, "Custom Title");
    }

    #[test]
    fn test_import_session_grid_content() {
        let state = crate::session::SessionState {
            grid_rows: vec!["line one".to_string(), "line two".to_string()],
            scrollback_text: Vec::new(),
            cursor_col: 0,
            cursor_row: 0,
            title: "Imported".to_string(),
            cwd: String::new(),
            cols: 80,
            rows: 24,
        };
        let mut term = Terminal::new(80, 24);
        term.import_session(&state);
        let cell = term.grid.cell(0, 0).unwrap();
        assert_eq!(cell.content, "l");
        let cell = term.grid.cell(4, 1).unwrap();
        assert_eq!(cell.content, " ");
        assert_eq!(term.title, "Imported");
    }

    // ===== OSC 10/11/12/104 Color Query Tests =====

    #[test]
    fn test_osc10_foreground_query() {
        let mut term = Terminal::new(80, 24);
        term.set_default_colors([0xd9, 0xd9, 0xd9], [0x0d, 0x0d, 0x14], [0xcc, 0xcc, 0xcc]);
        term.osc_dispatch(&[b"10".to_vec(), b"?".to_vec()]);
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let resp = String::from_utf8_lossy(&responses[0]);
        // xterm format: OSC 10 ; rgb:RR/GG/BB ST
        assert!(
            resp.contains("rgb:d9d9/d9d9/d9d9"),
            "Expected xterm rgb reply, got: {resp}"
        );
    }

    #[test]
    fn test_osc11_background_query() {
        let mut term = Terminal::new(80, 24);
        term.set_default_colors([0xd9, 0xd9, 0xd9], [0x0d, 0x0d, 0x14], [0xcc, 0xcc, 0xcc]);
        term.osc_dispatch(&[b"11".to_vec(), b"?".to_vec()]);
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let resp = String::from_utf8_lossy(&responses[0]);
        assert!(
            resp.contains("rgb:0d0d/0d0d/1414"),
            "Expected xterm rgb reply, got: {resp}"
        );
    }

    #[test]
    fn test_osc12_cursor_color_query() {
        let mut term = Terminal::new(80, 24);
        term.set_default_colors([0xd9, 0xd9, 0xd9], [0x0d, 0x0d, 0x14], [0xcc, 0xcc, 0xcc]);
        term.osc_dispatch(&[b"12".to_vec(), b"?".to_vec()]);
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let resp = String::from_utf8_lossy(&responses[0]);
        assert!(
            resp.contains("rgb:cccc/cccc/cccc"),
            "Expected xterm rgb reply, got: {resp}"
        );
    }

    #[test]
    fn test_osc104_resets_palette_color() {
        let mut term = Terminal::new(80, 24);
        term.set_default_colors([0xff, 0xff, 0xff], [0x00, 0x00, 0x00], [0xcc, 0xcc, 0xcc]);
        // OSC 4 ; 1 ; rgb:00/ff/00 — set palette index 1 to green
        term.osc_dispatch(&[b"4".to_vec(), b"1".to_vec(), b"rgb:00/ff/00".to_vec()]);
        assert_eq!(term.palette_color(1), Some([0x00, 0xff, 0x00]));

        // OSC 104 ; 1 — reset palette index 1
        term.osc_dispatch(&[b"104".to_vec(), b"1".to_vec()]);
        // After reset, the color should revert to default
        assert_eq!(term.palette_color(1), None);
    }

    #[test]
    fn test_osc4_set_palette_color() {
        let mut term = Terminal::new(80, 24);
        // OSC 4 ; 5 ; rgb:aa/bb/cc — set palette index 5
        term.osc_dispatch(&[b"4".to_vec(), b"5".to_vec(), b"rgb:aa/bb/cc".to_vec()]);
        assert_eq!(term.palette_color(5), Some([0xaa, 0xbb, 0xcc]));
    }

    #[test]
    fn test_osc4_query_palette_color() {
        let mut term = Terminal::new(80, 24);
        term.set_default_colors([0xff, 0xff, 0xff], [0x00, 0x00, 0x00], [0xcc, 0xcc, 0xcc]);
        // OSC 4 ; 5 ; ? — query palette index 5
        term.osc_dispatch(&[b"4".to_vec(), b"5".to_vec(), b"?".to_vec()]);
        let responses = term.drain_responses();
        assert_eq!(responses.len(), 1);
        let resp = String::from_utf8_lossy(&responses[0]);
        // Should respond with the default color for index 5
        assert!(
            resp.starts_with("\x1b]4;5;rgb:"),
            "Expected OSC 4 reply, got: {resp}"
        );
    }

    #[test]
    fn test_osc10_non_query_ignored() {
        // Setting colors is handled by the renderer, not the terminal.
        // For now, OSC 10 with a color value should be a no-op (no crash).
        let mut term = Terminal::new(80, 24);
        term.osc_dispatch(&[b"10".to_vec(), b"rgb:ff/00/00".to_vec()]);
        let responses = term.drain_responses();
        assert!(responses.is_empty());
    }

    // ===== DEC Private Mode Tests =====

    #[test]
    fn test_dec_reverse_video_mode() {
        // DECSCNM (mode 5) — reverse video of entire screen
        let mut term = Terminal::new(80, 24);
        assert!(!term.modes.reverse_video);
        term.set_private_mode(&[5], true);
        assert!(term.modes.reverse_video);
        term.set_private_mode(&[5], false);
        assert!(!term.modes.reverse_video);
    }

    #[test]
    fn test_dec_x10_mouse_mode() {
        // Mode 9 — X10 mouse tracking (button press only)
        let mut term = Terminal::new(80, 24);
        assert_eq!(term.modes.mouse_tracking, MouseMode::None);
        term.set_private_mode(&[9], true);
        assert_eq!(term.modes.mouse_tracking, MouseMode::X10);
        term.set_private_mode(&[9], false);
        assert_eq!(term.modes.mouse_tracking, MouseMode::None);
    }

    #[test]
    fn test_dec_mode_1048_save_restore_cursor() {
        // Mode 1048 — save/restore cursor (without alt screen switch)
        let mut term = Terminal::new(80, 24);
        term.grid.cursor.col = 10;
        term.grid.cursor.row = 5;
        term.set_private_mode(&[1048], true); // save
        term.grid.cursor.col = 0;
        term.grid.cursor.row = 0;
        term.set_private_mode(&[1048], false); // restore
        assert_eq!(term.grid.cursor.col, 10);
        assert_eq!(term.grid.cursor.row, 5);
    }

    #[test]
    fn test_dec_synchronized_output() {
        // Mode 2026 — synchronized output
        let mut term = Terminal::new(80, 24);
        assert!(!term.modes.synchronized_output);
        term.set_private_mode(&[2026], true);
        assert!(term.modes.synchronized_output);
        term.set_private_mode(&[2026], false);
        assert!(!term.modes.synchronized_output);
    }

    #[test]
    fn test_dec_mode_column_132() {
        // DECCOLM (mode 3) — 132-column mode clears screen and resets cursor
        let mut term = Terminal::new(80, 24);
        term.grid.cursor.col = 10;
        term.grid.cursor.row = 5;
        term.print('A');
        term.set_private_mode(&[3], true); // 132-col mode
        // Cursor should be reset to origin
        assert_eq!(term.grid.cursor.col, 0);
        assert_eq!(term.grid.cursor.row, 0);
    }

    // --- Image announcement tests ---

    #[test]
    fn test_image_announcement_message_format() {
        let ann = ImageAnnouncement {
            width: 640,
            height: 480,
            cell_cols: 80,
            cell_rows: 30,
            protocol: ImageProtocol::Sixel,
            filename: None,
        };
        assert_eq!(
            ann.message(),
            "Sixel image: 640\u{00d7}480 pixels, 80\u{00d7}30 cells"
        );
    }

    #[test]
    fn test_sixel_pushes_announcement() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Sixel 0x7E = 1x6 pixel image
        term.dcs_hook(&[], &[], 'q');
        term.dcs_put(0x7E);
        term.dcs_unhook();
        assert_eq!(term.pending_image_announcements.len(), 1);
        let ann = &term.pending_image_announcements[0];
        assert_eq!(ann.width, 1);
        assert_eq!(ann.height, 6);
        assert_eq!(ann.cell_cols, 1); // ceil(1/8) = 1
        assert_eq!(ann.cell_rows, 1); // ceil(6/16) = 1
        assert_eq!(ann.protocol, ImageProtocol::Sixel);
    }

    #[test]
    fn test_iterm2_pushes_announcement() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        let b64 = make_tiny_png_base64(); // 2x2 PNG
        let payload = format!("File=inline=1:{}", b64);
        term.osc_dispatch(&[b"1337".to_vec(), payload.into_bytes()]);
        assert_eq!(term.pending_image_announcements.len(), 1);
        let ann = &term.pending_image_announcements[0];
        assert_eq!(ann.width, 2);
        assert_eq!(ann.height, 2);
        assert_eq!(ann.cell_cols, 1); // ceil(2/8) = 1
        assert_eq!(ann.cell_rows, 1); // ceil(2/16) = 1
        assert_eq!(ann.protocol, ImageProtocol::ITerm2);
    }

    #[test]
    fn test_kitty_pushes_announcement() {
        use base64::Engine;
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // 2x2 RGBA image = 16 bytes
        let pixels = [255u8, 0, 0, 255].repeat(4);
        let b64 = base64::engine::general_purpose::STANDARD.encode(&pixels);
        let apc = format!("Ga=T,f=32,s=2,v=2,i=1;{}", b64);
        term.apc_dispatch(apc.as_bytes());
        assert_eq!(term.pending_image_announcements.len(), 1);
        let ann = &term.pending_image_announcements[0];
        assert_eq!(ann.width, 2);
        assert_eq!(ann.height, 2);
        assert_eq!(ann.cell_cols, 1); // ceil(2/8) = 1
        assert_eq!(ann.cell_rows, 1); // ceil(2/16) = 1
        assert_eq!(ann.protocol, ImageProtocol::Kitty);
    }

    #[test]
    fn test_drain_image_announcements_clears_queue() {
        let mut term = Terminal::new(80, 24);
        term.cell_pixel_width = 8.0;
        term.cell_pixel_height = 16.0;
        // Push two images via Sixel
        term.dcs_hook(&[], &[], 'q');
        term.dcs_put(0x7E);
        term.dcs_unhook();
        term.dcs_hook(&[], &[], 'q');
        term.dcs_put(0x7E);
        term.dcs_unhook();
        assert_eq!(term.pending_image_announcements.len(), 2);
        let drained = term.drain_image_announcements();
        assert_eq!(drained.len(), 2);
        assert!(term.pending_image_announcements.is_empty());
        // Second drain returns empty
        assert!(term.drain_image_announcements().is_empty());
    }

    #[test]
    fn test_echo_suppressed_default_false() {
        let term = Terminal::new(80, 24);
        assert!(!term.echo_suppressed);
        assert!(term.echo_check_pending.is_none());
    }

    #[test]
    fn test_check_echo_timeout_returns_true_on_first_transition() {
        let mut term = Terminal::new(80, 24);
        // Simulate a character typed 300ms ago (well past the 200ms deadline).
        let past = std::time::Instant::now() - std::time::Duration::from_millis(300);
        term.echo_check_pending = Some(('x', past));

        assert!(!term.echo_suppressed);
        let transitioned = term.check_echo_timeout();
        assert!(transitioned, "Should return true on first transition");
        assert!(term.echo_suppressed);
        assert!(term.echo_check_pending.is_none());
    }

    #[test]
    fn test_check_echo_timeout_returns_false_when_already_suppressed() {
        let mut term = Terminal::new(80, 24);
        term.echo_suppressed = true;
        let past = std::time::Instant::now() - std::time::Duration::from_millis(300);
        term.echo_check_pending = Some(('x', past));

        let transitioned = term.check_echo_timeout();
        assert!(!transitioned, "Should return false when already suppressed");
    }

    #[test]
    fn test_check_echo_timeout_returns_false_before_deadline() {
        let mut term = Terminal::new(80, 24);
        // Character typed just now — not yet timed out.
        term.echo_check_pending = Some(('x', std::time::Instant::now()));

        let transitioned = term.check_echo_timeout();
        assert!(!transitioned, "Should not transition before 200ms");
        assert!(!term.echo_suppressed);
        assert!(term.echo_check_pending.is_some());
    }

    #[test]
    fn test_check_echo_timeout_returns_false_when_no_pending() {
        let mut term = Terminal::new(80, 24);
        let transitioned = term.check_echo_timeout();
        assert!(!transitioned);
    }

    #[test]
    fn test_on_char_echoed_clears_state() {
        let mut term = Terminal::new(80, 24);
        term.echo_check_pending = Some(('x', std::time::Instant::now()));

        term.on_char_echoed('x');
        assert!(term.echo_check_pending.is_none());
        assert!(!term.echo_suppressed);
    }

    #[test]
    fn test_on_char_echoed_ignores_wrong_char() {
        let mut term = Terminal::new(80, 24);
        term.echo_check_pending = Some(('x', std::time::Instant::now()));

        term.on_char_echoed('y');
        assert!(
            term.echo_check_pending.is_some(),
            "Should not clear for wrong char"
        );
    }

    #[test]
    fn test_on_char_echoed_resets_suppressed_flag() {
        let mut term = Terminal::new(80, 24);
        term.echo_suppressed = true;
        term.echo_check_pending = Some(('x', std::time::Instant::now()));

        term.on_char_echoed('x');
        assert!(
            !term.echo_suppressed,
            "Echo resumed — should clear suppressed"
        );
    }
}
