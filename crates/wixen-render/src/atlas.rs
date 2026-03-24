//! Glyph atlas — a texture containing DirectWrite-rasterized glyphs.
//!
//! Each glyph is rendered once via DirectWrite and packed into a GPU texture atlas.
//! The atlas uses R8Unorm format (one byte per pixel, alpha only). Glyphs are
//! rasterized on demand and cached for the lifetime of the atlas.

use std::collections::HashMap;
use tracing::{debug, info, warn};

use crate::dwrite::{ClusterInfo, DWriteEngine};

/// A single glyph's location in the atlas.
#[derive(Debug, Clone, Copy)]
pub struct GlyphEntry {
    /// X offset in atlas texture (pixels)
    pub atlas_x: u32,
    /// Y offset in atlas texture (pixels)
    pub atlas_y: u32,
    /// Glyph width in pixels
    pub width: u32,
    /// Glyph height in pixels
    pub height: u32,
    /// Horizontal bearing (offset from cursor to left edge)
    pub bearing_x: i32,
    /// Vertical bearing (offset from baseline to top edge)
    pub bearing_y: i32,
}

/// Font metrics for the terminal grid.
#[derive(Debug, Clone, Copy)]
pub struct FontMetrics {
    /// Cell width in pixels (advance width of a monospace character)
    pub cell_width: f32,
    /// Cell height in pixels (line height)
    pub cell_height: f32,
    /// Baseline offset from top of cell
    pub baseline: f32,
    /// Underline position (offset below baseline)
    pub underline_position: f32,
    /// Underline thickness
    pub underline_thickness: f32,
    /// Strikethrough position
    pub strikethrough_position: f32,
}

/// The glyph atlas.
pub struct GlyphAtlas {
    /// Atlas texture data (1 byte per pixel — alpha only)
    data: Vec<u8>,
    /// Atlas texture width
    pub width: u32,
    /// Atlas texture height
    pub height: u32,
    /// Current packing cursor X
    cursor_x: u32,
    /// Current packing cursor Y
    cursor_y: u32,
    /// Current row height
    row_height: u32,
    /// Cached glyph entries: key → GlyphEntry
    cache: HashMap<GlyphKey, GlyphEntry>,
    /// Ligature run cache: text+style → shaped clusters.
    ligature_cache: HashMap<LigatureKey, Vec<ShapedCluster>>,
    /// LRU order for ligature cache eviction (most recent at the end).
    ligature_lru: Vec<LigatureKey>,
    /// Font metrics
    pub metrics: FontMetrics,
    /// Whether the atlas texture needs re-uploading to GPU
    pub dirty: bool,
    /// DirectWrite engine for glyph rasterization
    dwrite: DWriteEngine,
    /// Whether ligatures are enabled.
    pub ligatures_enabled: bool,
}

/// Key for glyph cache — character + style flags.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct GlyphKey {
    pub ch: char,
    pub bold: bool,
    pub italic: bool,
}

/// Key for the ligature run cache.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct LigatureKey {
    pub text: String,
    pub bold: bool,
    pub italic: bool,
}

/// A shaped cluster from DirectWrite run shaping.
#[derive(Debug, Clone)]
pub struct ShapedCluster {
    /// The text characters in this cluster.
    pub text: String,
    /// How many terminal cells this cluster spans.
    pub cell_span: u8,
    /// Atlas glyph entry (may be wider than a single cell for ligatures).
    pub glyph: GlyphEntry,
}

impl GlyphAtlas {
    /// Create a new atlas with DirectWrite font rendering.
    ///
    /// `font_family` — DirectWrite font family name (e.g. "Cascadia Code").
    /// `font_size` — Font size in points.
    /// `dpi` — Display DPI (96 = 100% scaling).
    /// `line_height` — Line height multiplier (1.0 = default).
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        font_family: &str,
        font_size: f32,
        dpi: f32,
        line_height: f32,
        fallback_fonts: &[String],
        ligatures: bool,
        font_path: &str,
    ) -> Result<Self, crate::RenderError> {
        let dwrite = DWriteEngine::new(
            font_family,
            font_size,
            dpi,
            line_height,
            fallback_fonts,
            font_path,
        )
        .map_err(|e| crate::RenderError::DirectWriteInit(e.to_string()))?;

        let dw_metrics = dwrite.metrics;
        let metrics = FontMetrics {
            cell_width: dw_metrics.cell_width,
            cell_height: dw_metrics.cell_height,
            baseline: dw_metrics.baseline,
            underline_position: dw_metrics.underline_position,
            underline_thickness: dw_metrics.underline_thickness,
            strikethrough_position: dw_metrics.strikethrough_position,
        };

        // Start with a 2048x2048 atlas (plenty of room for DirectWrite glyphs)
        let width = 2048;
        let height = 2048;

        let mut atlas = Self {
            data: vec![0u8; (width * height) as usize],
            width,
            height,
            cursor_x: 1, // 1px padding
            cursor_y: 1,
            row_height: 0,
            cache: HashMap::new(),
            ligature_cache: HashMap::new(),
            ligature_lru: Vec::new(),
            metrics,
            dirty: true,
            dwrite,
            ligatures_enabled: ligatures,
        };

        // Pre-rasterize printable ASCII (normal + bold)
        atlas.rasterize_ascii_range();

        info!(
            font = font_family,
            cell_w = metrics.cell_width,
            cell_h = metrics.cell_height,
            baseline = metrics.baseline,
            atlas_w = width,
            atlas_h = height,
            glyphs = atlas.cache.len(),
            "Glyph atlas initialized (DirectWrite)"
        );

        Ok(atlas)
    }

    /// Look up a glyph, rasterizing it on demand if needed.
    pub fn get_or_insert(&mut self, key: GlyphKey) -> GlyphEntry {
        if let Some(&entry) = self.cache.get(&key) {
            return entry;
        }
        self.rasterize_glyph(key)
    }

    /// Get the atlas texture data as a byte slice.
    pub fn data(&self) -> &[u8] {
        &self.data
    }

    /// Pre-rasterize printable ASCII in normal and bold variants.
    fn rasterize_ascii_range(&mut self) {
        for ch in ' '..='~' {
            self.rasterize_glyph(GlyphKey {
                ch,
                bold: false,
                italic: false,
            });
        }
        for ch in ' '..='~' {
            self.rasterize_glyph(GlyphKey {
                ch,
                bold: true,
                italic: false,
            });
        }
    }

    /// Shape a text run into clusters, detecting ligatures via DirectWrite.
    ///
    /// Returns one `ShapedCluster` per cluster. Ligature clusters have
    /// `cell_span > 1` and a wider atlas glyph. Results are cached with
    /// LRU eviction at 1024 entries.
    pub fn shape_run(&mut self, text: &str, bold: bool, italic: bool) -> Vec<ShapedCluster> {
        if !self.ligatures_enabled || text.is_empty() {
            return self.fallback_per_char(text, bold, italic);
        }

        let key = LigatureKey {
            text: text.to_string(),
            bold,
            italic,
        };
        if let Some(clusters) = self.ligature_cache.get(&key) {
            // Move to end of LRU
            if let Some(pos) = self.ligature_lru.iter().position(|k| k == &key) {
                self.ligature_lru.remove(pos);
            }
            self.ligature_lru.push(key);
            return clusters.clone();
        }

        // Query DirectWrite for cluster metrics
        let cluster_infos: Vec<ClusterInfo> = self.dwrite.get_cluster_metrics(text, bold, italic);

        // Check if any cluster is a multi-cell ligature
        let has_ligature = cluster_infos.iter().any(|c| c.cell_span > 1);

        if !has_ligature {
            // No ligatures in this run — use per-character rendering (faster)
            let result = self.fallback_per_char(text, bold, italic);
            self.insert_ligature_cache(key, result.clone());
            return result;
        }

        // Build shaped clusters, rasterizing multi-cell ligatures
        let cell_w = self.metrics.cell_width;
        let mut clusters = Vec::with_capacity(cluster_infos.len());
        let mut char_iter = text.chars().peekable();

        for info in &cluster_infos {
            // Collect the characters for this cluster
            let mut cluster_text = String::new();
            let mut consumed_u16 = 0u16;
            while consumed_u16 < info.char_count {
                if let Some(ch) = char_iter.next() {
                    cluster_text.push(ch);
                    consumed_u16 += ch.len_utf16() as u16;
                } else {
                    break;
                }
            }

            let glyph = if info.cell_span > 1 {
                // Ligature: rasterize the cluster text at the wider size
                let target_w = (info.cell_span as f32 * cell_w) as u32;
                self.rasterize_cluster_glyph(&cluster_text, bold, italic, target_w)
            } else {
                // Single cell: use normal per-character rasterization
                let ch = cluster_text.chars().next().unwrap_or(' ');
                self.get_or_insert(GlyphKey { ch, bold, italic })
            };

            clusters.push(ShapedCluster {
                text: cluster_text,
                cell_span: info.cell_span,
                glyph,
            });
        }

        self.insert_ligature_cache(key, clusters.clone());
        clusters
    }

    /// Fallback: each character is its own cluster with cell_span = 1.
    fn fallback_per_char(&mut self, text: &str, bold: bool, italic: bool) -> Vec<ShapedCluster> {
        text.chars()
            .map(|ch| {
                let glyph = self.get_or_insert(GlyphKey { ch, bold, italic });
                ShapedCluster {
                    text: ch.to_string(),
                    cell_span: 1,
                    glyph,
                }
            })
            .collect()
    }

    /// Rasterize a multi-character ligature cluster and pack it into the atlas.
    fn rasterize_cluster_glyph(
        &mut self,
        text: &str,
        bold: bool,
        italic: bool,
        target_w: u32,
    ) -> GlyphEntry {
        let glyph_h = self.metrics.cell_height as u32;

        // Row wrap
        if self.cursor_x + target_w + 1 > self.width {
            self.cursor_x = 1;
            self.cursor_y += self.row_height + 1;
            self.row_height = 0;
        }

        // Atlas full check
        if self.cursor_y + glyph_h + 1 > self.height {
            warn!(text, "Atlas full — ligature glyph not cached");
            return GlyphEntry {
                atlas_x: 0,
                atlas_y: 0,
                width: 0,
                height: 0,
                bearing_x: 0,
                bearing_y: 0,
            };
        }

        // Rasterize via DirectWrite text analyzer (with GSUB shaping)
        let (bmp_w, bmp_h, alpha) = self.dwrite.rasterize_cluster(text, bold, italic, target_w);

        // Blit the alpha bitmap into the atlas
        let dst_x = self.cursor_x;
        let dst_y = self.cursor_y;
        let copy_w = bmp_w.min(target_w);
        let copy_h = bmp_h.min(glyph_h);

        for row in 0..copy_h {
            for col in 0..copy_w {
                let src_idx = (row * bmp_w + col) as usize;
                let dst_idx = ((dst_y + row) * self.width + (dst_x + col)) as usize;
                if src_idx < alpha.len() && dst_idx < self.data.len() {
                    self.data[dst_idx] = alpha[src_idx];
                }
            }
        }

        let entry = GlyphEntry {
            atlas_x: dst_x,
            atlas_y: dst_y,
            width: target_w,
            height: glyph_h,
            bearing_x: 0,
            bearing_y: self.metrics.baseline as i32,
        };

        self.cursor_x += target_w + 1;
        self.row_height = self.row_height.max(glyph_h);
        self.dirty = true;

        debug!(
            text,
            cell_span = target_w / (self.metrics.cell_width as u32).max(1),
            "Ligature glyph rasterized"
        );

        entry
    }

    /// Insert into ligature cache with LRU eviction at 1024 entries.
    fn insert_ligature_cache(&mut self, key: LigatureKey, clusters: Vec<ShapedCluster>) {
        // Evict oldest if over capacity
        while self.ligature_cache.len() >= 1024 {
            if let Some(oldest) = self.ligature_lru.first().cloned() {
                self.ligature_cache.remove(&oldest);
                self.ligature_lru.remove(0);
            } else {
                break;
            }
        }
        self.ligature_lru.push(key.clone());
        self.ligature_cache.insert(key, clusters);
    }

    /// Rasterize a single glyph and pack it into the atlas.
    fn rasterize_glyph(&mut self, key: GlyphKey) -> GlyphEntry {
        if let Some(&entry) = self.cache.get(&key) {
            return entry;
        }

        let glyph_w = self.metrics.cell_width as u32;
        let glyph_h = self.metrics.cell_height as u32;

        // Row wrap
        if self.cursor_x + glyph_w + 1 > self.width {
            self.cursor_x = 1;
            self.cursor_y += self.row_height + 1;
            self.row_height = 0;
        }

        // Atlas full check
        if self.cursor_y + glyph_h + 1 > self.height {
            warn!(ch = %key.ch, "Atlas full — glyph not cached");
            return GlyphEntry {
                atlas_x: 0,
                atlas_y: 0,
                width: 0,
                height: 0,
                bearing_x: 0,
                bearing_y: 0,
            };
        }

        // Rasterize via DirectWrite
        let (bmp_w, bmp_h, alpha) = self.dwrite.rasterize(key.ch, key.bold, key.italic);

        // Blit the alpha bitmap into the atlas at the current cursor position
        let dst_x = self.cursor_x;
        let dst_y = self.cursor_y;
        let copy_w = bmp_w.min(glyph_w);
        let copy_h = bmp_h.min(glyph_h);

        for row in 0..copy_h {
            for col in 0..copy_w {
                let src_idx = (row * bmp_w + col) as usize;
                let dst_idx = ((dst_y + row) * self.width + (dst_x + col)) as usize;
                if src_idx < alpha.len() && dst_idx < self.data.len() {
                    self.data[dst_idx] = alpha[src_idx];
                }
            }
        }

        let entry = GlyphEntry {
            atlas_x: dst_x,
            atlas_y: dst_y,
            width: glyph_w,
            height: glyph_h,
            bearing_x: 0,
            bearing_y: self.metrics.baseline as i32,
        };

        self.cursor_x += glyph_w + 1;
        self.row_height = self.row_height.max(glyph_h);
        self.cache.insert(key, entry);
        self.dirty = true;

        debug!(ch = %key.ch, bold = key.bold, italic = key.italic, "Glyph rasterized");

        entry
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ligature_key_equality() {
        let a = LigatureKey {
            text: "=>".to_string(),
            bold: false,
            italic: false,
        };
        let b = LigatureKey {
            text: "=>".to_string(),
            bold: false,
            italic: false,
        };
        let c = LigatureKey {
            text: "=>".to_string(),
            bold: true,
            italic: false,
        };
        assert_eq!(a, b);
        assert_ne!(a, c); // Different bold
    }

    #[test]
    fn test_shaped_cluster_construction() {
        let glyph = GlyphEntry {
            atlas_x: 10,
            atlas_y: 20,
            width: 24,
            height: 18,
            bearing_x: 0,
            bearing_y: 14,
        };

        // Single-cell cluster
        let single = ShapedCluster {
            text: "a".to_string(),
            cell_span: 1,
            glyph,
        };
        assert_eq!(single.cell_span, 1);
        assert_eq!(single.glyph.width, 24);

        // Multi-cell ligature cluster
        let ligature = ShapedCluster {
            text: "=>".to_string(),
            cell_span: 2,
            glyph: GlyphEntry {
                atlas_x: 100,
                atlas_y: 20,
                width: 48, // 2 cells wide
                height: 18,
                bearing_x: 0,
                bearing_y: 14,
            },
        };
        assert_eq!(ligature.cell_span, 2);
        assert_eq!(ligature.glyph.width, 48);
    }

    #[test]
    fn test_ligature_cache_eviction() {
        // GlyphAtlas requires DirectWrite, so we test the LRU logic
        // by verifying the data structures.
        let mut cache: HashMap<LigatureKey, Vec<ShapedCluster>> = HashMap::new();
        let mut lru: Vec<LigatureKey> = Vec::new();
        let capacity = 4; // small capacity for testing

        // Insert entries up to capacity
        for i in 0..capacity {
            let key = LigatureKey {
                text: format!("lig{}", i),
                bold: false,
                italic: false,
            };
            lru.push(key.clone());
            cache.insert(key, vec![]);
        }
        assert_eq!(cache.len(), 4);
        assert_eq!(lru.len(), 4);

        // Insert one more — should evict oldest (lig0)
        while cache.len() >= capacity {
            if let Some(oldest) = lru.first().cloned() {
                cache.remove(&oldest);
                lru.remove(0);
            } else {
                break;
            }
        }
        let new_key = LigatureKey {
            text: "lig4".to_string(),
            bold: false,
            italic: false,
        };
        lru.push(new_key.clone());
        cache.insert(new_key, vec![]);

        assert_eq!(cache.len(), 4);
        // "lig0" should be evicted
        assert!(!cache.contains_key(&LigatureKey {
            text: "lig0".to_string(),
            bold: false,
            italic: false,
        }));
        // "lig4" should be present
        assert!(cache.contains_key(&LigatureKey {
            text: "lig4".to_string(),
            bold: false,
            italic: false,
        }));
    }

    #[test]
    fn test_glyph_key_hash_consistency() {
        use std::collections::hash_map::DefaultHasher;
        use std::hash::{Hash, Hasher};

        let key1 = GlyphKey {
            ch: 'A',
            bold: true,
            italic: false,
        };
        let key2 = GlyphKey {
            ch: 'A',
            bold: true,
            italic: false,
        };

        let mut h1 = DefaultHasher::new();
        let mut h2 = DefaultHasher::new();
        key1.hash(&mut h1);
        key2.hash(&mut h2);
        assert_eq!(h1.finish(), h2.finish());
    }

    #[test]
    fn test_font_metrics_fields() {
        let metrics = FontMetrics {
            cell_width: 9.6,
            cell_height: 20.0,
            baseline: 16.0,
            underline_position: 2.0,
            underline_thickness: 1.0,
            strikethrough_position: 10.0,
        };
        assert!(metrics.cell_width > 0.0);
        assert!(metrics.cell_height > metrics.baseline);
        assert!(metrics.underline_thickness >= 1.0);
    }
}
