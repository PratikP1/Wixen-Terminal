use crate::attrs::{CellAttributes, Color, Rgb, UnderlineStyle};
use crate::cell::Cell;
use crate::grid::Row;

/// Two-tier scrollback buffer with zstd compression.
///
/// Recent rows live in a "hot" tier (uncompressed, instant access).
/// When the hot tier exceeds a threshold, the oldest half is compressed
/// into a "cold" tier block using zstd.
#[derive(Debug)]
pub struct ScrollbackBuffer {
    /// Recent rows (uncompressed, instant access).
    hot: Vec<Row>,
    /// Compressed archive blocks (zstd level 3).
    cold: Vec<CompressedBlock>,
    /// Total row count across hot + cold.
    total_len: usize,
    /// Hot tier threshold — when exceeded, compress oldest half.
    hot_threshold: usize,
}

/// A zstd-compressed block of rows.
#[derive(Debug)]
struct CompressedBlock {
    /// Compressed row data.
    data: Vec<u8>,
    /// Number of rows in this block.
    row_count: usize,
}

impl ScrollbackBuffer {
    /// Create a new scrollback buffer with the default hot threshold (10,000 rows).
    pub fn new() -> Self {
        Self::with_threshold(10_000)
    }

    /// Create with a custom hot threshold.
    pub fn with_threshold(hot_threshold: usize) -> Self {
        Self {
            hot: Vec::new(),
            cold: Vec::new(),
            total_len: 0,
            hot_threshold: hot_threshold.max(100), // minimum 100
        }
    }

    /// Push a row into the scrollback.
    pub fn push(&mut self, row: Row) {
        self.hot.push(row);
        self.total_len += 1;
        self.maybe_compress();
    }

    /// Total number of rows in the scrollback (hot + cold).
    pub fn len(&self) -> usize {
        self.total_len
    }

    pub fn is_empty(&self) -> bool {
        self.total_len == 0
    }

    /// Get a row by absolute index (0 = oldest).
    ///
    /// Cold rows are decompressed on demand. For viewport rendering,
    /// only hot rows are typically accessed.
    pub fn get(&self, index: usize) -> Option<&Row> {
        if index >= self.total_len {
            return None;
        }
        let cold_total = self.cold_row_count();
        if index >= cold_total {
            // Index is in the hot tier
            self.hot.get(index - cold_total)
        } else {
            // Index is in the cold tier — we can't return a reference
            // to a decompressed row without allocating. Return None for
            // cold rows accessed by reference; use get_cold() for owned access.
            None
        }
    }

    /// Get a cold-tier row by decompressing on demand.
    /// Returns `None` if the index is in the hot tier or out of bounds.
    pub fn get_cold(&self, index: usize) -> Option<Row> {
        let cold_total = self.cold_row_count();
        if index >= cold_total || index >= self.total_len {
            return None;
        }
        // Find which block contains this index
        let mut offset = 0;
        for block in &self.cold {
            if index < offset + block.row_count {
                let rows = decompress_block(block);
                return rows.into_iter().nth(index - offset);
            }
            offset += block.row_count;
        }
        None
    }

    /// Drain all rows out of the buffer, decompressing cold blocks.
    pub fn drain_all(&mut self) -> Vec<Row> {
        let mut all = Vec::with_capacity(self.total_len);
        for block in self.cold.drain(..) {
            let rows = decompress_block(&block);
            all.extend(rows);
        }
        all.append(&mut self.hot);
        self.total_len = 0;
        all
    }

    /// Clear the entire scrollback.
    pub fn clear(&mut self) {
        self.hot.clear();
        self.cold.clear();
        self.total_len = 0;
    }

    /// Number of rows in the hot (uncompressed) tier.
    pub fn hot_len(&self) -> usize {
        self.hot.len()
    }

    /// Number of compressed blocks.
    pub fn cold_block_count(&self) -> usize {
        self.cold.len()
    }

    /// Total rows across all cold blocks.
    fn cold_row_count(&self) -> usize {
        self.cold.iter().map(|b| b.row_count).sum()
    }

    /// Compress the oldest half of the hot tier when threshold exceeded.
    fn maybe_compress(&mut self) {
        if self.hot.len() <= self.hot_threshold {
            return;
        }
        let compress_count = self.hot_threshold / 2;
        let to_compress: Vec<Row> = self.hot.drain(..compress_count).collect();
        let row_count = to_compress.len();
        let data = serialize_rows(&to_compress);
        let compressed = zstd::bulk::compress(&data, 3).unwrap_or(data);
        self.cold.push(CompressedBlock {
            data: compressed,
            row_count,
        });
    }
}

impl Default for ScrollbackBuffer {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Serialization — compact binary format for rows
// ---------------------------------------------------------------------------

/// Serialize a slice of Rows into a byte buffer.
///
/// Format per row:
///   [wrapped: u8] [cell_count: u32le] [cells...]
/// Format per cell:
///   [content_len: u16le] [content: bytes] [width: u8] [attrs: 16 bytes]
/// Format for attrs:
///   [fg_type: u8] [fg_data: 3 bytes] [bg_type: u8] [bg_data: 3 bytes]
///   [flags: u16le] [underline_color_type: u8] [underline_color_data: 3 bytes]
///   [hyperlink_id: u32le]
fn serialize_rows(rows: &[Row]) -> Vec<u8> {
    let mut buf = Vec::with_capacity(rows.len() * 256);
    for row in rows {
        buf.push(row.wrapped as u8);
        let cell_count = row.cells.len() as u32;
        buf.extend_from_slice(&cell_count.to_le_bytes());
        for cell in &row.cells {
            let content_bytes = cell.content.as_bytes();
            let content_len = content_bytes.len() as u16;
            buf.extend_from_slice(&content_len.to_le_bytes());
            buf.extend_from_slice(content_bytes);
            buf.push(cell.width);
            serialize_attrs(&cell.attrs, &mut buf);
        }
    }
    buf
}

fn serialize_attrs(attrs: &CellAttributes, buf: &mut Vec<u8>) {
    serialize_color(attrs.fg, buf);
    serialize_color(attrs.bg, buf);
    let flags: u16 = (attrs.bold as u16)
        | ((attrs.dim as u16) << 1)
        | ((attrs.italic as u16) << 2)
        | ((underline_to_u8(attrs.underline) as u16) << 3)
        | ((attrs.blink as u16) << 6)
        | ((attrs.inverse as u16) << 7)
        | ((attrs.hidden as u16) << 8)
        | ((attrs.strikethrough as u16) << 9)
        | ((attrs.overline as u16) << 10);
    buf.extend_from_slice(&flags.to_le_bytes());
    serialize_color(attrs.underline_color, buf);
    buf.extend_from_slice(&attrs.hyperlink_id.to_le_bytes());
}

fn serialize_color(color: Color, buf: &mut Vec<u8>) {
    match color {
        Color::Default => {
            buf.push(0);
            buf.extend_from_slice(&[0, 0, 0]);
        }
        Color::Indexed(idx) => {
            buf.push(1);
            buf.push(idx);
            buf.extend_from_slice(&[0, 0]);
        }
        Color::Rgb(rgb) => {
            buf.push(2);
            buf.push(rgb.r);
            buf.push(rgb.g);
            buf.push(rgb.b);
        }
    }
}

fn underline_to_u8(u: UnderlineStyle) -> u8 {
    match u {
        UnderlineStyle::None => 0,
        UnderlineStyle::Single => 1,
        UnderlineStyle::Double => 2,
        UnderlineStyle::Curly => 3,
        UnderlineStyle::Dotted => 4,
        UnderlineStyle::Dashed => 5,
    }
}

// ---------------------------------------------------------------------------
// Deserialization
// ---------------------------------------------------------------------------

fn decompress_block(block: &CompressedBlock) -> Vec<Row> {
    let data = zstd::bulk::decompress(&block.data, 64 * 1024 * 1024).unwrap_or_default();
    deserialize_rows(&data)
}

fn deserialize_rows(data: &[u8]) -> Vec<Row> {
    let mut rows = Vec::new();
    let mut pos = 0;
    while pos < data.len() {
        let Some(row) = deserialize_row(data, &mut pos) else {
            break;
        };
        rows.push(row);
    }
    rows
}

fn deserialize_row(data: &[u8], pos: &mut usize) -> Option<Row> {
    if *pos >= data.len() {
        return None;
    }
    let wrapped = data[*pos] != 0;
    *pos += 1;

    if *pos + 4 > data.len() {
        return None;
    }
    let cell_count = u32::from_le_bytes(data[*pos..*pos + 4].try_into().ok()?) as usize;
    *pos += 4;

    let mut cells = Vec::with_capacity(cell_count);
    for _ in 0..cell_count {
        let cell = deserialize_cell(data, pos)?;
        cells.push(cell);
    }

    Some(Row { cells, wrapped })
}

fn deserialize_cell(data: &[u8], pos: &mut usize) -> Option<Cell> {
    if *pos + 2 > data.len() {
        return None;
    }
    let content_len = u16::from_le_bytes(data[*pos..*pos + 2].try_into().ok()?) as usize;
    *pos += 2;

    if *pos + content_len > data.len() {
        return None;
    }
    let content = String::from_utf8_lossy(&data[*pos..*pos + content_len]).into_owned();
    *pos += content_len;

    if *pos >= data.len() {
        return None;
    }
    let width = data[*pos];
    *pos += 1;

    let attrs = deserialize_attrs(data, pos)?;

    Some(Cell {
        content,
        attrs,
        width,
    })
}

fn deserialize_attrs(data: &[u8], pos: &mut usize) -> Option<CellAttributes> {
    let fg = deserialize_color(data, pos)?;
    let bg = deserialize_color(data, pos)?;

    if *pos + 2 > data.len() {
        return None;
    }
    let flags = u16::from_le_bytes(data[*pos..*pos + 2].try_into().ok()?);
    *pos += 2;

    let underline_color = deserialize_color(data, pos)?;

    if *pos + 4 > data.len() {
        return None;
    }
    let hyperlink_id = u32::from_le_bytes(data[*pos..*pos + 4].try_into().ok()?);
    *pos += 4;

    Some(CellAttributes {
        fg,
        bg,
        bold: flags & 1 != 0,
        dim: flags & 2 != 0,
        italic: flags & 4 != 0,
        underline: u8_to_underline(((flags >> 3) & 7) as u8),
        blink: flags & 64 != 0,
        inverse: flags & 128 != 0,
        hidden: flags & 256 != 0,
        strikethrough: flags & 512 != 0,
        overline: flags & 1024 != 0,
        underline_color,
        hyperlink_id,
    })
}

fn deserialize_color(data: &[u8], pos: &mut usize) -> Option<Color> {
    if *pos + 4 > data.len() {
        return None;
    }
    let color_type = data[*pos];
    let b1 = data[*pos + 1];
    let b2 = data[*pos + 2];
    let b3 = data[*pos + 3];
    *pos += 4;

    Some(match color_type {
        0 => Color::Default,
        1 => Color::Indexed(b1),
        2 => Color::Rgb(Rgb::new(b1, b2, b3)),
        _ => Color::Default,
    })
}

fn u8_to_underline(v: u8) -> UnderlineStyle {
    match v {
        1 => UnderlineStyle::Single,
        2 => UnderlineStyle::Double,
        3 => UnderlineStyle::Curly,
        4 => UnderlineStyle::Dotted,
        5 => UnderlineStyle::Dashed,
        _ => UnderlineStyle::None,
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn make_row(text: &str) -> Row {
        let cells: Vec<Cell> = text
            .chars()
            .map(|ch| Cell {
                content: ch.to_string(),
                attrs: CellAttributes::default(),
                width: 1,
            })
            .collect();
        Row {
            cells,
            wrapped: false,
        }
    }

    #[test]
    fn test_scrollback_push_get() {
        let mut sb = ScrollbackBuffer::new();
        sb.push(make_row("hello"));
        sb.push(make_row("world"));
        assert_eq!(sb.len(), 2);
        let row = sb.get(0).unwrap();
        let text: String = row.cells.iter().map(|c| &*c.content).collect();
        assert_eq!(text, "hello");
    }

    #[test]
    fn test_scrollback_compression_triggers() {
        let mut sb = ScrollbackBuffer::with_threshold(200);
        for i in 0..250 {
            sb.push(make_row(&format!("line {i}")));
        }
        assert_eq!(sb.len(), 250);
        // Compression should have triggered — hot tier reduced
        assert!(sb.hot_len() < 200);
        assert!(sb.cold_block_count() >= 1);
    }

    #[test]
    fn test_scrollback_get_cold_row() {
        let mut sb = ScrollbackBuffer::with_threshold(200);
        for i in 0..250 {
            sb.push(make_row(&format!("line {i}")));
        }
        // First 100 rows should be in cold tier
        let cold_row = sb.get_cold(0).expect("should decompress cold row");
        let text: String = cold_row.cells.iter().map(|c| &*c.content).collect();
        assert_eq!(text, "line 0");
    }

    #[test]
    fn test_scrollback_len_across_tiers() {
        let mut sb = ScrollbackBuffer::with_threshold(200);
        for i in 0..500 {
            sb.push(make_row(&format!("row {i}")));
        }
        assert_eq!(sb.len(), 500);
        let cold_count: usize = sb.cold.iter().map(|b| b.row_count).sum();
        assert_eq!(cold_count + sb.hot_len(), 500);
    }

    #[test]
    fn test_scrollback_clear_both_tiers() {
        let mut sb = ScrollbackBuffer::with_threshold(200);
        for i in 0..300 {
            sb.push(make_row(&format!("row {i}")));
        }
        assert!(!sb.is_empty());
        sb.clear();
        assert!(sb.is_empty());
        assert_eq!(sb.len(), 0);
        assert_eq!(sb.hot_len(), 0);
        assert_eq!(sb.cold_block_count(), 0);
    }

    #[test]
    fn test_scrollback_drain_all_decompresses() {
        let mut sb = ScrollbackBuffer::with_threshold(200);
        for i in 0..300 {
            sb.push(make_row(&format!("line {i}")));
        }
        let all = sb.drain_all();
        assert_eq!(all.len(), 300);
        // Verify order is preserved
        let first_text: String = all[0].cells.iter().map(|c| &*c.content).collect();
        let last_text: String = all[299].cells.iter().map(|c| &*c.content).collect();
        assert_eq!(first_text, "line 0");
        assert_eq!(last_text, "line 299");
        assert_eq!(sb.len(), 0);
    }

    #[test]
    fn test_serialization_roundtrip() {
        let rows = vec![
            make_row("hello world"),
            Row {
                cells: vec![Cell {
                    content: "X".to_string(),
                    attrs: CellAttributes {
                        fg: Color::Rgb(Rgb::new(255, 0, 128)),
                        bg: Color::Indexed(42),
                        bold: true,
                        italic: true,
                        underline: UnderlineStyle::Curly,
                        ..CellAttributes::default()
                    },
                    width: 1,
                }],
                wrapped: true,
            },
        ];
        let data = serialize_rows(&rows);
        let back = deserialize_rows(&data);
        assert_eq!(back.len(), 2);
        let text0: String = back[0].cells.iter().map(|c| &*c.content).collect();
        assert_eq!(text0, "hello world");
        assert!(!back[0].wrapped);
        assert!(back[1].wrapped);
        assert_eq!(back[1].cells[0].attrs.fg, Color::Rgb(Rgb::new(255, 0, 128)));
        assert_eq!(back[1].cells[0].attrs.bg, Color::Indexed(42));
        assert!(back[1].cells[0].attrs.bold);
        assert!(back[1].cells[0].attrs.italic);
        assert_eq!(back[1].cells[0].attrs.underline, UnderlineStyle::Curly);
    }
}
