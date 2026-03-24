//! Terminal image support — Sixel, iTerm2 inline images, and Kitty graphics protocol.
//!
//! Images are decoded into RGBA pixel buffers and associated with grid cell positions.
//! The renderer draws them as textured quads overlaid on the terminal grid.

use tracing::debug;

/// A decoded terminal image ready for rendering.
#[derive(Debug, Clone)]
pub struct TerminalImage {
    /// Unique image ID (sequential)
    pub id: u64,
    /// Width in pixels
    pub width: u32,
    /// Height in pixels
    pub height: u32,
    /// RGBA pixel data (row-major, 4 bytes per pixel)
    pub pixels: Vec<u8>,
    /// Grid position: column where the image starts
    pub col: usize,
    /// Grid position: row where the image starts
    pub row: usize,
    /// Width in grid cells (calculated from pixel width / cell width)
    pub cell_cols: usize,
    /// Height in grid cells (calculated from pixel height / cell height)
    pub cell_rows: usize,
}

/// Manages all images placed in the terminal.
pub struct ImageStore {
    images: Vec<TerminalImage>,
    next_id: u64,
}

impl ImageStore {
    pub fn new() -> Self {
        Self {
            images: Vec::new(),
            next_id: 1,
        }
    }

    /// Add an image at the given grid position.
    ///
    /// `cell_size` is `(cell_width, cell_height)` in pixels for grid placement.
    pub fn add(
        &mut self,
        pixels: Vec<u8>,
        width: u32,
        height: u32,
        col: usize,
        row: usize,
        cell_size: (f32, f32),
    ) -> u64 {
        let cell_cols = (width as f32 / cell_size.0).ceil().max(1.0) as usize;
        let cell_rows = (height as f32 / cell_size.1).ceil().max(1.0) as usize;
        let id = self.next_id;
        self.next_id += 1;
        debug!(id, width, height, cell_cols, cell_rows, "Image added");
        self.images.push(TerminalImage {
            id,
            width,
            height,
            pixels,
            col,
            row,
            cell_cols,
            cell_rows,
        });
        id
    }

    /// Get all images.
    pub fn images(&self) -> &[TerminalImage] {
        &self.images
    }

    /// Remove images that have scrolled off-screen.
    pub fn prune(&mut self, min_row: usize) {
        self.images.retain(|img| img.row + img.cell_rows > min_row);
    }

    /// Remove all images.
    pub fn clear(&mut self) {
        self.images.clear();
    }

    /// Remove a specific image by its ID. Returns true if found and removed.
    pub fn remove_by_id(&mut self, id: u64) -> bool {
        let before = self.images.len();
        self.images.retain(|img| img.id != id);
        self.images.len() < before
    }

    /// Remove images at a specific grid position (col, row).
    pub fn remove_at_position(&mut self, col: usize, row: usize) {
        self.images.retain(|img| {
            !(img.col <= col
                && col < img.col + img.cell_cols
                && img.row <= row
                && row < img.row + img.cell_rows)
        });
    }

    /// Number of stored images.
    pub fn len(&self) -> usize {
        self.images.len()
    }

    pub fn is_empty(&self) -> bool {
        self.images.is_empty()
    }
}

impl Default for ImageStore {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Sixel decoder
// ---------------------------------------------------------------------------

/// Sixel color register.
#[derive(Debug, Clone, Copy, Default)]
struct SixelColor {
    r: u8,
    g: u8,
    b: u8,
}

/// Decode a Sixel data stream into an RGBA pixel buffer.
///
/// Sixel format: each data byte (0x3F..0x7E) encodes 6 vertical pixels.
/// Colors are defined with `#<index>;<type>;<v1>;<v2>;<v3>` sequences.
/// `$` = carriage return (move to column 0), `–` = newline (advance 6 rows).
///
/// Returns `(width, height, rgba_pixels)` or `None` on invalid data.
pub fn decode_sixel(data: &[u8]) -> Option<(u32, u32, Vec<u8>)> {
    if data.is_empty() {
        return None;
    }

    // Default palette (VT340-compatible 16 colors)
    let mut palette = vec![SixelColor::default(); 256];
    init_default_palette(&mut palette);

    let mut current_color: usize = 0;
    let mut x: u32 = 0;
    let mut y: u32 = 0;
    let mut max_x: u32 = 0;
    let mut max_y: u32 = 0;

    // First pass: determine dimensions
    {
        let mut px = 0u32;
        let mut py = 0u32;
        let mut i = 0;
        while i < data.len() {
            let b = data[i];
            match b {
                // Sixel data byte
                0x3F..=0x7E => {
                    px += 1;
                    if px > max_x {
                        max_x = px;
                    }
                    let row_bottom = py + 6;
                    if row_bottom > max_y {
                        max_y = row_bottom;
                    }
                    i += 1;
                }
                // Carriage return
                b'$' => {
                    px = 0;
                    i += 1;
                }
                // New line (advance 6 rows)
                b'-' => {
                    px = 0;
                    py += 6;
                    i += 1;
                }
                // Color definition or selection: # followed by digits/semicolons
                b'#' => {
                    i += 1;
                    // Skip over the color definition
                    while i < data.len() && (data[i].is_ascii_digit() || data[i] == b';') {
                        i += 1;
                    }
                }
                // Repeat introducer: ! <count> <sixel_byte>
                b'!' => {
                    i += 1;
                    let mut count = 0u32;
                    while i < data.len() && data[i].is_ascii_digit() {
                        count = count * 10 + (data[i] - b'0') as u32;
                        i += 1;
                    }
                    if i < data.len() && (0x3F..=0x7E).contains(&data[i]) {
                        px += count;
                        if px > max_x {
                            max_x = px;
                        }
                        let row_bottom = py + 6;
                        if row_bottom > max_y {
                            max_y = row_bottom;
                        }
                        i += 1;
                    }
                }
                _ => {
                    i += 1;
                }
            }
        }
    }

    if max_x == 0 || max_y == 0 {
        return None;
    }

    // Allocate RGBA buffer
    let width = max_x;
    let height = max_y;
    let mut pixels = vec![0u8; (width * height * 4) as usize];

    // Second pass: decode pixels
    let mut i = 0;
    while i < data.len() {
        let b = data[i];
        match b {
            // Sixel data byte
            0x3F..=0x7E => {
                let sixel = b - 0x3F;
                let color = &palette[current_color];
                for bit in 0..6u32 {
                    if sixel & (1 << bit) != 0 {
                        let px = x;
                        let py = y + bit;
                        if px < width && py < height {
                            let offset = ((py * width + px) * 4) as usize;
                            pixels[offset] = color.r;
                            pixels[offset + 1] = color.g;
                            pixels[offset + 2] = color.b;
                            pixels[offset + 3] = 255;
                        }
                    }
                }
                x += 1;
                i += 1;
            }
            // Carriage return
            b'$' => {
                x = 0;
                i += 1;
            }
            // New line (advance 6 rows)
            b'-' => {
                x = 0;
                y += 6;
                i += 1;
            }
            // Color definition or selection
            b'#' => {
                i += 1;
                let mut index = 0u32;
                while i < data.len() && data[i].is_ascii_digit() {
                    index = index * 10 + (data[i] - b'0') as u32;
                    i += 1;
                }
                if i < data.len() && data[i] == b';' {
                    // Color definition: #index;type;v1;v2;v3
                    i += 1;
                    let mut vals = [0u32; 4];
                    let mut vi = 0;
                    while i < data.len() && vi < 4 {
                        if data[i].is_ascii_digit() {
                            vals[vi] = vals[vi] * 10 + (data[i] - b'0') as u32;
                            i += 1;
                        } else if data[i] == b';' {
                            vi += 1;
                            i += 1;
                        } else {
                            break;
                        }
                    }
                    let idx = (index as usize).min(255);
                    if vals[0] == 2 {
                        // RGB: percentages 0-100
                        palette[idx] = SixelColor {
                            r: (vals[1] * 255 / 100) as u8,
                            g: (vals[2] * 255 / 100) as u8,
                            b: (vals[3] * 255 / 100) as u8,
                        };
                    } else if vals[0] == 1 {
                        // HLS: convert to RGB (simplified)
                        let (h, l, s) = (vals[1], vals[2], vals[3]);
                        let (r, g, b_val) = hls_to_rgb(h, l, s);
                        palette[idx] = SixelColor { r, g, b: b_val };
                    }
                } else {
                    // Color selection only
                    current_color = (index as usize).min(255);
                }
            }
            // Repeat introducer
            b'!' => {
                i += 1;
                let mut count = 0u32;
                while i < data.len() && data[i].is_ascii_digit() {
                    count = count * 10 + (data[i] - b'0') as u32;
                    i += 1;
                }
                if i < data.len() && (0x3F..=0x7E).contains(&data[i]) {
                    let sixel = data[i] - 0x3F;
                    let color = &palette[current_color];
                    for rep in 0..count {
                        for bit in 0..6u32 {
                            if sixel & (1 << bit) != 0 {
                                let px = x + rep;
                                let py = y + bit;
                                if px < width && py < height {
                                    let offset = ((py * width + px) * 4) as usize;
                                    pixels[offset] = color.r;
                                    pixels[offset + 1] = color.g;
                                    pixels[offset + 2] = color.b;
                                    pixels[offset + 3] = 255;
                                }
                            }
                        }
                    }
                    x += count;
                    i += 1;
                }
            }
            _ => {
                i += 1;
            }
        }
    }

    debug!(width, height, "Sixel decoded");
    Some((width, height, pixels))
}

/// Initialize the default VT340 16-color Sixel palette.
fn init_default_palette(palette: &mut [SixelColor]) {
    let defaults = [
        (0, 0, 0),    // 0: black
        (20, 20, 80), // 1: blue
        (80, 13, 13), // 2: red
        (20, 80, 20), // 3: green
        (80, 20, 80), // 4: magenta
        (20, 80, 80), // 5: cyan
        (80, 80, 20), // 6: yellow
        (53, 53, 53), // 7: gray 50%
        (26, 26, 26), // 8: gray 25%
        (33, 33, 60), // 9: light blue
        (60, 26, 26), // 10: light red
        (33, 60, 33), // 11: light green
        (60, 33, 60), // 12: light magenta
        (33, 60, 60), // 13: light cyan
        (60, 60, 33), // 14: light yellow
        (80, 80, 80), // 15: white
    ];
    for (i, (r, g, b)) in defaults.iter().enumerate() {
        if i < palette.len() {
            palette[i] = SixelColor {
                r: (*r as u32 * 255 / 100) as u8,
                g: (*g as u32 * 255 / 100) as u8,
                b: (*b as u32 * 255 / 100) as u8,
            };
        }
    }
}

/// Simplified HLS to RGB conversion (percentages 0-100).
fn hls_to_rgb(h: u32, l: u32, s: u32) -> (u8, u8, u8) {
    if s == 0 {
        let v = (l * 255 / 100) as u8;
        return (v, v, v);
    }
    let h = h % 360;
    let l_f = l as f64 / 100.0;
    let s_f = s as f64 / 100.0;
    let c = (1.0 - (2.0 * l_f - 1.0).abs()) * s_f;
    let x = c * (1.0 - ((h as f64 / 60.0) % 2.0 - 1.0).abs());
    let m = l_f - c / 2.0;
    let (r1, g1, b1) = match h {
        0..=59 => (c, x, 0.0),
        60..=119 => (x, c, 0.0),
        120..=179 => (0.0, c, x),
        180..=239 => (0.0, x, c),
        240..=299 => (x, 0.0, c),
        _ => (c, 0.0, x),
    };
    (
        ((r1 + m) * 255.0) as u8,
        ((g1 + m) * 255.0) as u8,
        ((b1 + m) * 255.0) as u8,
    )
}

// ---------------------------------------------------------------------------
// iTerm2 inline image protocol (OSC 1337)
// ---------------------------------------------------------------------------

/// Parsed parameters from an iTerm2 inline image sequence.
#[derive(Debug, Clone, Default)]
pub struct Iterm2ImageParams {
    /// Base64-encoded filename (optional).
    pub name: Option<String>,
    /// Expected file size in bytes (optional).
    pub size: Option<u64>,
    /// Width specification (e.g., "auto", "80px", "50%", "10" for cells).
    pub width: Option<String>,
    /// Height specification (e.g., "auto", "24px", "50%", "5" for cells).
    pub height: Option<String>,
    /// Whether to preserve aspect ratio (default true).
    pub preserve_aspect_ratio: bool,
    /// Whether to display inline (1) or as a downloadable file (0, default).
    pub inline: bool,
}

/// Parse the parameter string from an iTerm2 `File=` directive.
///
/// The input `params_str` is everything between `File=` and the `:` separator,
/// formatted as semicolon-separated `key=value` pairs (e.g., `name=dGVzdA==;size=1234;inline=1`).
pub fn parse_iterm2_params(params_str: &str) -> Iterm2ImageParams {
    let mut params = Iterm2ImageParams {
        preserve_aspect_ratio: true,
        ..Default::default()
    };

    for kv in params_str.split(';') {
        if let Some((key, value)) = kv.split_once('=') {
            match key {
                "name" => {
                    // Name is base64-encoded
                    use base64::Engine;
                    if let Ok(bytes) = base64::engine::general_purpose::STANDARD.decode(value) {
                        params.name = Some(String::from_utf8_lossy(&bytes).into_owned());
                    }
                }
                "size" => {
                    params.size = value.parse().ok();
                }
                "width" => {
                    params.width = Some(value.to_string());
                }
                "height" => {
                    params.height = Some(value.to_string());
                }
                "preserveAspectRatio" => {
                    params.preserve_aspect_ratio = value != "0";
                }
                "inline" => {
                    params.inline = value == "1";
                }
                _ => {} // Ignore unknown keys
            }
        }
    }

    params
}

/// Decode an iTerm2 inline image from base64-encoded image data.
///
/// Supports PNG, JPEG, and GIF formats. Returns `(width, height, rgba_pixels)` or `None`
/// if the data is invalid or the format is unsupported.
pub fn decode_iterm2_image(base64_data: &str) -> Option<(u32, u32, Vec<u8>)> {
    use base64::Engine;
    let raw = base64::engine::general_purpose::STANDARD
        .decode(base64_data)
        .ok()?;
    decode_image_bytes(&raw)
}

/// Decode raw image bytes (PNG/JPEG/GIF) into an RGBA pixel buffer.
///
/// Returns `(width, height, rgba_pixels)` or `None` on failure.
pub fn decode_image_bytes(data: &[u8]) -> Option<(u32, u32, Vec<u8>)> {
    use image::GenericImageView;
    let img = image::load_from_memory(data).ok()?;
    let rgba = img.to_rgba8();
    let (w, h) = img.dimensions();
    debug!(w, h, "Image decoded from bytes");
    Some((w, h, rgba.into_raw()))
}

// ---------------------------------------------------------------------------
// Kitty graphics protocol
// ---------------------------------------------------------------------------

/// Kitty graphics action type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KittyAction {
    /// Transmit image data (a=t)
    Transmit,
    /// Transmit and display (a=T)
    TransmitAndDisplay,
    /// Display a previously transmitted image (a=p)
    Put,
    /// Delete images (a=d)
    Delete,
    /// Query terminal support (a=q)
    Query,
}

/// Kitty graphics data format.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KittyFormat {
    /// 32-bit RGBA raw pixels (f=32)
    Rgba,
    /// 24-bit RGB raw pixels (f=24)
    Rgb,
    /// PNG compressed (f=100)
    Png,
}

/// Kitty graphics delete target type.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum KittyDeleteType {
    /// Delete all images (d=a or d=A, default)
    #[default]
    All,
    /// Delete by image ID (d=i or d=I)
    ById,
    /// Delete by image number (d=n or d=N)
    ByNumber,
    /// Delete all images at cursor position (d=c or d=C)
    AtCursor,
    /// Delete all images in the specified column (d=x or d=X)
    InColumn,
    /// Delete all images in the specified row (d=y or d=Y)
    InRow,
    /// Delete all images on the specified z-layer (d=z or d=Z)
    OnZLayer,
}

/// Parsed Kitty graphics command.
#[derive(Debug, Clone)]
pub struct KittyCommand {
    /// Action (default: transmit)
    pub action: KittyAction,
    /// Data format (default: RGBA)
    pub format: KittyFormat,
    /// Image ID (i=)
    pub image_id: u32,
    /// Image number for display (I=), used in a=p
    pub image_number: u32,
    /// Width in pixels (s=)
    pub width: u32,
    /// Height in pixels (v=)
    pub height: u32,
    /// Whether more data chunks follow (m=1)
    pub more_chunks: bool,
    /// Compression: z=zlib
    pub compressed: bool,
    /// Quiet mode (q=1 suppress OK, q=2 suppress all responses)
    pub quiet: u8,
    /// The base64 payload (decoded to raw bytes by the caller)
    pub payload: Vec<u8>,
    /// Delete target type (d=) — only used when action is Delete
    pub delete_type: KittyDeleteType,
}

/// Parse a Kitty graphics protocol command from raw APC data.
///
/// The format is `G<key=value,...>;<base64-payload>` or `G<key=value,...>`
/// when there is no payload.
pub fn parse_kitty_command(data: &[u8]) -> Option<KittyCommand> {
    let s = std::str::from_utf8(data).ok()?;

    // Must start with 'G'
    let rest = s.strip_prefix('G')?;

    // Split on first ';' — left is params, right is base64 payload
    let (params_str, payload_b64) = match rest.split_once(';') {
        Some((p, d)) => (p, d),
        None => (rest, ""),
    };

    let mut cmd = KittyCommand {
        action: KittyAction::TransmitAndDisplay,
        format: KittyFormat::Rgba,
        image_id: 0,
        image_number: 0,
        width: 0,
        height: 0,
        more_chunks: false,
        compressed: false,
        quiet: 0,
        payload: Vec::new(),
        delete_type: KittyDeleteType::All,
    };

    // Parse key=value pairs
    for kv in params_str.split(',') {
        if let Some((key, value)) = kv.split_once('=') {
            match key {
                "a" => {
                    cmd.action = match value {
                        "t" => KittyAction::Transmit,
                        "T" => KittyAction::TransmitAndDisplay,
                        "p" => KittyAction::Put,
                        "d" => KittyAction::Delete,
                        "q" => KittyAction::Query,
                        _ => KittyAction::TransmitAndDisplay,
                    };
                }
                "f" => {
                    cmd.format = match value {
                        "24" => KittyFormat::Rgb,
                        "32" => KittyFormat::Rgba,
                        "100" => KittyFormat::Png,
                        _ => KittyFormat::Rgba,
                    };
                }
                "i" => cmd.image_id = value.parse().unwrap_or(0),
                "I" => cmd.image_number = value.parse().unwrap_or(0),
                "s" => cmd.width = value.parse().unwrap_or(0),
                "v" => cmd.height = value.parse().unwrap_or(0),
                "m" => cmd.more_chunks = value == "1",
                "o" => cmd.compressed = value == "z",
                "q" => cmd.quiet = value.parse().unwrap_or(0),
                "d" => {
                    cmd.delete_type = match value {
                        "a" | "A" => KittyDeleteType::All,
                        "i" | "I" => KittyDeleteType::ById,
                        "n" | "N" => KittyDeleteType::ByNumber,
                        "c" | "C" => KittyDeleteType::AtCursor,
                        "x" | "X" => KittyDeleteType::InColumn,
                        "y" | "Y" => KittyDeleteType::InRow,
                        "z" | "Z" => KittyDeleteType::OnZLayer,
                        _ => KittyDeleteType::All,
                    };
                }
                _ => {} // Ignore unknown keys
            }
        }
    }

    // Decode base64 payload
    if !payload_b64.is_empty() {
        use base64::Engine;
        cmd.payload = base64::engine::general_purpose::STANDARD
            .decode(payload_b64)
            .ok()?;
    }

    Some(cmd)
}

/// Decode a Kitty image payload into RGBA pixels.
///
/// Handles raw RGBA (f=32), raw RGB (f=24), and PNG (f=100) formats.
/// For raw formats, `width` and `height` must be provided (from the command).
pub fn decode_kitty_payload(cmd: &KittyCommand) -> Option<(u32, u32, Vec<u8>)> {
    match cmd.format {
        KittyFormat::Png => decode_image_bytes(&cmd.payload),
        KittyFormat::Rgba => {
            let w = cmd.width;
            let h = cmd.height;
            if w == 0 || h == 0 {
                return None;
            }
            let expected = (w * h * 4) as usize;
            if cmd.payload.len() != expected {
                return None;
            }
            Some((w, h, cmd.payload.clone()))
        }
        KittyFormat::Rgb => {
            let w = cmd.width;
            let h = cmd.height;
            if w == 0 || h == 0 {
                return None;
            }
            let expected = (w * h * 3) as usize;
            if cmd.payload.len() != expected {
                return None;
            }
            // Convert RGB to RGBA
            let mut rgba = Vec::with_capacity((w * h * 4) as usize);
            for chunk in cmd.payload.chunks_exact(3) {
                rgba.push(chunk[0]);
                rgba.push(chunk[1]);
                rgba.push(chunk[2]);
                rgba.push(255);
            }
            Some((w, h, rgba))
        }
    }
}

/// Manages chunked Kitty image uploads.
///
/// When `m=1`, data is accumulated across multiple APC sequences.
/// When `m=0`, the full image is finalized.
pub struct KittyChunkState {
    /// Accumulated base64-decoded payload bytes
    pub data: Vec<u8>,
    /// Command from the first chunk (carries format, dimensions, etc.)
    pub command: Option<KittyCommand>,
}

impl KittyChunkState {
    pub fn new() -> Self {
        Self {
            data: Vec::new(),
            command: None,
        }
    }

    /// Feed a parsed command. Returns `Some(finalized_command)` when complete.
    pub fn feed(&mut self, mut cmd: KittyCommand) -> Option<KittyCommand> {
        if self.command.is_none() {
            // First chunk — store the command metadata
            self.command = Some(cmd.clone());
            self.data.clear();
        }

        self.data.extend_from_slice(&cmd.payload);

        if cmd.more_chunks {
            None
        } else {
            // Final chunk — produce the complete command
            let mut final_cmd = self.command.take().unwrap_or(cmd.clone());
            final_cmd.payload = std::mem::take(&mut self.data);
            final_cmd.more_chunks = false;
            cmd.payload.clear(); // consumed
            self.command = None;
            Some(final_cmd)
        }
    }

    /// Reset chunking state.
    pub fn reset(&mut self) {
        self.data.clear();
        self.command = None;
    }
}

impl Default for KittyChunkState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_image_store_add() {
        let mut store = ImageStore::new();
        let pixels = vec![255u8; 40 * 20 * 4]; // 40x20 white image
        let id = store.add(pixels, 40, 20, 0, 0, (8.0, 16.0));
        assert_eq!(id, 1);
        assert_eq!(store.len(), 1);
        let img = &store.images()[0];
        assert_eq!(img.width, 40);
        assert_eq!(img.height, 20);
        assert_eq!(img.cell_cols, 5); // 40/8 = 5
        assert_eq!(img.cell_rows, 2); // ceil(20/16) = 2
    }

    #[test]
    fn test_image_store_prune() {
        let mut store = ImageStore::new();
        store.add(vec![0; 4 * 4], 1, 1, 0, 0, (8.0, 16.0));
        store.add(vec![0; 4 * 4], 1, 1, 0, 10, (8.0, 16.0));
        assert_eq!(store.len(), 2);
        store.prune(5); // Remove images with row + cell_rows <= 5
        assert_eq!(store.len(), 1);
        assert_eq!(store.images()[0].row, 10);
    }

    #[test]
    fn test_image_store_clear() {
        let mut store = ImageStore::new();
        store.add(vec![0; 4], 1, 1, 0, 0, (8.0, 16.0));
        store.add(vec![0; 4], 1, 1, 0, 0, (8.0, 16.0));
        assert_eq!(store.len(), 2);
        store.clear();
        assert!(store.is_empty());
    }

    #[test]
    fn test_decode_sixel_empty() {
        assert!(decode_sixel(b"").is_none());
    }

    #[test]
    fn test_decode_sixel_single_pixel_column() {
        // A single Sixel byte 0x7E = 0x3F + 0x3F = bits 0-5 all set
        // This means 6 pixels vertically at column 0
        let (w, h, pixels) = decode_sixel(b"\x7e").unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 6);
        // All 6 pixels should be opaque (using default color 0 = black-ish)
        for row in 0..6 {
            let offset = (row * 4) as usize;
            assert_eq!(pixels[offset + 3], 255, "Row {row} should be opaque");
        }
    }

    #[test]
    fn test_decode_sixel_color_definition() {
        // Define color 1 as pure red (RGB type=2, 100;0;0), select it, draw one column
        let data = b"#1;2;100;0;0#1\x7e";
        let (w, h, pixels) = decode_sixel(data).unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 6);
        // First pixel should be red
        assert_eq!(pixels[0], 255); // R
        assert_eq!(pixels[1], 0); // G
        assert_eq!(pixels[2], 0); // B
        assert_eq!(pixels[3], 255); // A
    }

    #[test]
    fn test_decode_sixel_repeat() {
        // Repeat 0x7e 5 times: !5~
        let data = b"!5\x7e";
        let (w, h, pixels) = decode_sixel(data).unwrap();
        assert_eq!(w, 5);
        assert_eq!(h, 6);
        // All columns should be filled
        for col in 0..5u32 {
            let offset = (col * 4) as usize; // row 0, col
            assert_eq!(pixels[offset + 3], 255, "Col {col} row 0 should be opaque");
        }
    }

    #[test]
    fn test_decode_sixel_newline() {
        // Two rows: first 6-pixel high, then newline, then another 6-pixel high
        // ~$-~ means: draw column, CR, newline, draw column
        let data = b"\x7e-\x7e";
        let (w, h, _) = decode_sixel(data).unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 12); // 6 + 6
    }

    #[test]
    fn test_decode_sixel_carriage_return() {
        // Draw at col 0, then CR resets to col 0 (overdraw)
        let data = b"\x7e$\x7e";
        let (w, h, _) = decode_sixel(data).unwrap();
        assert_eq!(w, 1); // Only 1 column wide (CR reset to 0)
        assert_eq!(h, 6);
    }

    #[test]
    fn test_decode_sixel_partial_bits() {
        // 0x40 = 0x3F + 1 = only bit 0 set
        // This means only the top pixel in each 6-pixel column
        let data = b"\x40";
        let (w, h, pixels) = decode_sixel(data).unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 6);
        // Row 0 should be opaque
        assert_eq!(pixels[3], 255);
        // Row 1 should be transparent (alpha = 0)
        assert_eq!(pixels[4 + 3], 0);
    }

    #[test]
    fn test_decode_sixel_two_colors() {
        // Color 0 (default), draw, CR, color 1 (green), draw
        // This overlays two colors on the same column
        let data = b"#1;2;0;100;0\x7e$#1\x7e";
        let (w, h, pixels) = decode_sixel(data).unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 6);
        // The second draw overwrites: should be green
        assert_eq!(pixels[0], 0); // R
        assert_eq!(pixels[1], 255); // G
        assert_eq!(pixels[2], 0); // B
    }

    #[test]
    fn test_hls_to_rgb_achromatic() {
        // 0% saturation = gray
        let (r, g, b) = hls_to_rgb(0, 50, 0);
        assert_eq!(r, 127);
        assert_eq!(g, 127);
        assert_eq!(b, 127);
    }

    // --- iTerm2 inline image tests ---

    #[test]
    fn test_parse_iterm2_params_inline_only() {
        let params = parse_iterm2_params("inline=1");
        assert!(params.inline);
        assert!(params.preserve_aspect_ratio); // default true
        assert!(params.name.is_none());
        assert!(params.size.is_none());
    }

    #[test]
    fn test_parse_iterm2_params_full() {
        // "test.png" base64 = "dGVzdC5wbmc="
        let params = parse_iterm2_params(
            "name=dGVzdC5wbmc=;size=1234;width=80px;height=24px;preserveAspectRatio=0;inline=1",
        );
        assert_eq!(params.name.as_deref(), Some("test.png"));
        assert_eq!(params.size, Some(1234));
        assert_eq!(params.width.as_deref(), Some("80px"));
        assert_eq!(params.height.as_deref(), Some("24px"));
        assert!(!params.preserve_aspect_ratio);
        assert!(params.inline);
    }

    #[test]
    fn test_parse_iterm2_params_defaults() {
        let params = parse_iterm2_params("");
        assert!(!params.inline); // default false
        assert!(params.preserve_aspect_ratio); // default true
        assert!(params.name.is_none());
    }

    #[test]
    fn test_parse_iterm2_params_no_inline() {
        let params = parse_iterm2_params("inline=0;size=100");
        assert!(!params.inline);
        assert_eq!(params.size, Some(100));
    }

    #[test]
    fn test_decode_iterm2_image_valid_png() {
        // Create a minimal 1x1 red PNG using the image crate
        use image::{ImageBuffer, Rgba};
        let img: ImageBuffer<Rgba<u8>, Vec<u8>> =
            ImageBuffer::from_pixel(1, 1, Rgba([255, 0, 0, 255]));
        let mut png_bytes = Vec::new();
        {
            use std::io::Cursor;
            img.write_to(&mut Cursor::new(&mut png_bytes), image::ImageFormat::Png)
                .unwrap();
        }
        use base64::Engine;
        let b64 = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
        let (w, h, pixels) = decode_iterm2_image(&b64).unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 1);
        assert_eq!(pixels, vec![255, 0, 0, 255]); // RGBA red
    }

    #[test]
    fn test_decode_iterm2_image_valid_jpeg() {
        // Create a minimal 2x2 JPEG
        use image::{ImageBuffer, Rgb};
        let img: ImageBuffer<Rgb<u8>, Vec<u8>> = ImageBuffer::from_pixel(2, 2, Rgb([0, 128, 255]));
        let mut jpeg_bytes = Vec::new();
        {
            use std::io::Cursor;
            img.write_to(&mut Cursor::new(&mut jpeg_bytes), image::ImageFormat::Jpeg)
                .unwrap();
        }
        use base64::Engine;
        let b64 = base64::engine::general_purpose::STANDARD.encode(&jpeg_bytes);
        let (w, h, pixels) = decode_iterm2_image(&b64).unwrap();
        assert_eq!(w, 2);
        assert_eq!(h, 2);
        // JPEG is lossy so just check we got 4 bytes per pixel (RGBA)
        assert_eq!(pixels.len(), 2 * 2 * 4);
    }

    #[test]
    fn test_decode_iterm2_image_invalid_base64() {
        assert!(decode_iterm2_image("!!!not-base64!!!").is_none());
    }

    #[test]
    fn test_decode_iterm2_image_invalid_image_data() {
        use base64::Engine;
        let b64 = base64::engine::general_purpose::STANDARD.encode(b"not an image");
        assert!(decode_iterm2_image(&b64).is_none());
    }

    #[test]
    fn test_decode_image_bytes_empty() {
        assert!(decode_image_bytes(b"").is_none());
    }

    #[test]
    fn test_decode_image_bytes_valid_png() {
        use image::{ImageBuffer, Rgba};
        let img: ImageBuffer<Rgba<u8>, Vec<u8>> =
            ImageBuffer::from_pixel(3, 2, Rgba([10, 20, 30, 255]));
        let mut png_bytes = Vec::new();
        {
            use std::io::Cursor;
            img.write_to(&mut Cursor::new(&mut png_bytes), image::ImageFormat::Png)
                .unwrap();
        }
        let (w, h, pixels) = decode_image_bytes(&png_bytes).unwrap();
        assert_eq!(w, 3);
        assert_eq!(h, 2);
        assert_eq!(pixels.len(), 3 * 2 * 4);
        // First pixel RGBA
        assert_eq!(pixels[0], 10);
        assert_eq!(pixels[1], 20);
        assert_eq!(pixels[2], 30);
        assert_eq!(pixels[3], 255);
    }

    // --- Kitty graphics protocol tests ---

    #[test]
    fn test_parse_kitty_command_basic() {
        use base64::Engine;
        let payload = base64::engine::general_purpose::STANDARD.encode(b"raw-data");
        let data = format!("Ga=T,f=32,s=2,v=2,i=1;{}", payload);
        let cmd = parse_kitty_command(data.as_bytes()).unwrap();
        assert_eq!(cmd.action, KittyAction::TransmitAndDisplay);
        assert_eq!(cmd.format, KittyFormat::Rgba);
        assert_eq!(cmd.width, 2);
        assert_eq!(cmd.height, 2);
        assert_eq!(cmd.image_id, 1);
        assert_eq!(cmd.payload, b"raw-data");
    }

    #[test]
    fn test_parse_kitty_command_png() {
        let cmd = parse_kitty_command(b"Ga=T,f=100,i=5").unwrap();
        assert_eq!(cmd.action, KittyAction::TransmitAndDisplay);
        assert_eq!(cmd.format, KittyFormat::Png);
        assert_eq!(cmd.image_id, 5);
        assert!(cmd.payload.is_empty());
    }

    #[test]
    fn test_parse_kitty_command_query() {
        let cmd = parse_kitty_command(b"Ga=q,i=42").unwrap();
        assert_eq!(cmd.action, KittyAction::Query);
        assert_eq!(cmd.image_id, 42);
    }

    #[test]
    fn test_parse_kitty_command_delete() {
        let cmd = parse_kitty_command(b"Ga=d").unwrap();
        assert_eq!(cmd.action, KittyAction::Delete);
    }

    #[test]
    fn test_parse_kitty_command_chunked() {
        let cmd = parse_kitty_command(b"Ga=T,m=1,f=32,s=4,v=4").unwrap();
        assert!(cmd.more_chunks);
    }

    #[test]
    fn test_parse_kitty_command_no_g_prefix() {
        assert!(parse_kitty_command(b"a=T,f=32").is_none());
    }

    #[test]
    fn test_decode_kitty_payload_rgba() {
        // 2x2 RGBA image: 16 bytes
        let pixels = [
            255u8, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 128, 128, 255,
        ];
        let cmd = KittyCommand {
            action: KittyAction::TransmitAndDisplay,
            format: KittyFormat::Rgba,
            image_id: 1,
            image_number: 0,
            width: 2,
            height: 2,
            more_chunks: false,
            compressed: false,
            quiet: 0,
            payload: pixels.to_vec(),
            delete_type: KittyDeleteType::All,
        };
        let (w, h, data) = decode_kitty_payload(&cmd).unwrap();
        assert_eq!(w, 2);
        assert_eq!(h, 2);
        assert_eq!(data, pixels);
    }

    #[test]
    fn test_decode_kitty_payload_rgb() {
        // 1x2 RGB image: 6 bytes → 8 bytes RGBA
        let pixels = vec![255, 0, 0, 0, 255, 0];
        let cmd = KittyCommand {
            action: KittyAction::TransmitAndDisplay,
            format: KittyFormat::Rgb,
            image_id: 1,
            image_number: 0,
            width: 1,
            height: 2,
            more_chunks: false,
            compressed: false,
            quiet: 0,
            payload: pixels,
            delete_type: KittyDeleteType::All,
        };
        let (w, h, data) = decode_kitty_payload(&cmd).unwrap();
        assert_eq!(w, 1);
        assert_eq!(h, 2);
        assert_eq!(data, vec![255, 0, 0, 255, 0, 255, 0, 255]);
    }

    #[test]
    fn test_decode_kitty_payload_wrong_size() {
        let cmd = KittyCommand {
            action: KittyAction::TransmitAndDisplay,
            format: KittyFormat::Rgba,
            image_id: 1,
            image_number: 0,
            width: 2,
            height: 2,
            more_chunks: false,
            compressed: false,
            quiet: 0,
            payload: vec![0; 10], // Wrong: 2x2x4 = 16 expected
            delete_type: KittyDeleteType::All,
        };
        assert!(decode_kitty_payload(&cmd).is_none());
    }

    #[test]
    fn test_kitty_chunk_state_single() {
        let mut state = KittyChunkState::new();
        let cmd = KittyCommand {
            action: KittyAction::TransmitAndDisplay,
            format: KittyFormat::Rgba,
            image_id: 1,
            image_number: 0,
            width: 1,
            height: 1,
            more_chunks: false,
            compressed: false,
            quiet: 0,
            payload: vec![255, 0, 0, 255],
            delete_type: KittyDeleteType::All,
        };
        let result = state.feed(cmd);
        assert!(result.is_some());
        let final_cmd = result.unwrap();
        assert_eq!(final_cmd.payload, vec![255, 0, 0, 255]);
    }

    #[test]
    fn test_image_store_remove_by_id() {
        let mut store = ImageStore::new();
        let id1 = store.add(vec![0; 4], 1, 1, 0, 0, (8.0, 16.0));
        let id2 = store.add(vec![0; 4], 1, 1, 5, 5, (8.0, 16.0));
        assert_eq!(store.len(), 2);

        assert!(store.remove_by_id(id1));
        assert_eq!(store.len(), 1);
        assert_eq!(store.images()[0].id, id2);

        // Removing non-existent ID returns false
        assert!(!store.remove_by_id(999));
        assert_eq!(store.len(), 1);
    }

    #[test]
    fn test_image_store_remove_at_position() {
        let mut store = ImageStore::new();
        // Image at (0,0) spanning 5 cols x 2 rows
        store.add(vec![0; 40 * 20 * 4], 40, 20, 0, 0, (8.0, 16.0));
        // Image at (10,10) spanning 1x1
        store.add(vec![0; 4], 1, 1, 10, 10, (8.0, 16.0));
        assert_eq!(store.len(), 2);

        // Remove at (2,1) — inside the first image
        store.remove_at_position(2, 1);
        assert_eq!(store.len(), 1);
        assert_eq!(store.images()[0].col, 10);
    }

    #[test]
    fn test_parse_kitty_delete_by_id() {
        let cmd = parse_kitty_command(b"Ga=d,d=i,i=42").unwrap();
        assert_eq!(cmd.action, KittyAction::Delete);
        assert_eq!(cmd.delete_type, KittyDeleteType::ById);
        assert_eq!(cmd.image_id, 42);
    }

    #[test]
    fn test_parse_kitty_delete_all() {
        let cmd = parse_kitty_command(b"Ga=d,d=a").unwrap();
        assert_eq!(cmd.action, KittyAction::Delete);
        assert_eq!(cmd.delete_type, KittyDeleteType::All);
    }

    #[test]
    fn test_parse_kitty_delete_at_cursor() {
        let cmd = parse_kitty_command(b"Ga=d,d=c").unwrap();
        assert_eq!(cmd.action, KittyAction::Delete);
        assert_eq!(cmd.delete_type, KittyDeleteType::AtCursor);
    }

    #[test]
    fn test_kitty_chunk_state_multi() {
        let mut state = KittyChunkState::new();

        // First chunk (m=1)
        let cmd1 = KittyCommand {
            action: KittyAction::TransmitAndDisplay,
            format: KittyFormat::Rgba,
            image_id: 1,
            image_number: 0,
            width: 1,
            height: 2,
            more_chunks: true,
            compressed: false,
            quiet: 0,
            payload: vec![255, 0, 0, 255],
            delete_type: KittyDeleteType::All,
        };
        assert!(state.feed(cmd1).is_none()); // Not done yet

        // Final chunk (m=0)
        let cmd2 = KittyCommand {
            action: KittyAction::TransmitAndDisplay,
            format: KittyFormat::Rgba,
            image_id: 1,
            image_number: 0,
            width: 1,
            height: 2,
            more_chunks: false,
            compressed: false,
            quiet: 0,
            payload: vec![0, 255, 0, 255],
            delete_type: KittyDeleteType::All,
        };
        let result = state.feed(cmd2);
        assert!(result.is_some());
        let final_cmd = result.unwrap();
        assert_eq!(final_cmd.payload, vec![255, 0, 0, 255, 0, 255, 0, 255]);
        assert_eq!(final_cmd.width, 1);
        assert_eq!(final_cmd.height, 2);
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(5_000))]

        /// decode_sixel must never panic on arbitrary byte input.
        #[test]
        fn sixel_never_panics(data in proptest::collection::vec(any::<u8>(), 0..4096)) {
            let _ = decode_sixel(&data);
        }

        /// decode_sixel with valid-looking Sixel data bytes (0x3F..0x7E) must not panic.
        #[test]
        fn sixel_valid_range_never_panics(
            data in proptest::collection::vec(0x3Fu8..=0x7E, 1..500),
        ) {
            let result = decode_sixel(&data);
            if let Some((w, h, pixels)) = result {
                prop_assert!(w > 0);
                prop_assert!(h > 0);
                prop_assert_eq!(pixels.len(), (w * h * 4) as usize);
            }
        }

        /// Sixel output dimensions must match pixel buffer size.
        #[test]
        fn sixel_output_size_consistent(data in proptest::collection::vec(any::<u8>(), 1..2048)) {
            if let Some((w, h, pixels)) = decode_sixel(&data) {
                prop_assert_eq!(pixels.len(), (w * h * 4) as usize,
                    "Pixel buffer size must be width * height * 4");
            }
        }

        /// parse_iterm2_params must never panic on arbitrary strings.
        #[test]
        fn iterm2_params_never_panics(s in ".*") {
            let _ = parse_iterm2_params(&s);
        }

        /// parse_kitty_command must never panic on arbitrary byte input.
        #[test]
        fn kitty_command_never_panics(data in proptest::collection::vec(any::<u8>(), 0..4096)) {
            let _ = parse_kitty_command(&data);
        }

        /// Valid Kitty commands (starting with 'G') with arbitrary params must not panic.
        #[test]
        fn kitty_valid_prefix_never_panics(
            params in "[a-zA-Z0-9=,]{0,100}",
            payload in "[A-Za-z0-9+/=]{0,200}",
        ) {
            let data = format!("G{params};{payload}");
            let _ = parse_kitty_command(data.as_bytes());
        }

        /// Sixel repeat with large counts must not OOM or panic.
        /// Cap at 10000 to keep memory bounded.
        #[test]
        fn sixel_large_repeat_bounded(count in 1u32..10_000) {
            let data = format!("!{count}\x7e");
            let result = decode_sixel(data.as_bytes());
            if let Some((w, h, pixels)) = result {
                prop_assert_eq!(w, count);
                prop_assert_eq!(h, 6);
                prop_assert_eq!(pixels.len(), (w * h * 4) as usize);
            }
        }
    }
}
