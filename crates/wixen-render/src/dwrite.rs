//! DirectWrite text shaping — font creation, metrics, and glyph rasterization.
//!
//! Wraps the Windows DirectWrite API to provide:
//! - System font enumeration and fallback
//! - Accurate monospace cell metrics (advance width, ascent, descent, line gap)
//! - Per-glyph rasterization into alpha bitmaps for the GPU atlas

use std::ptr;
use tracing::{debug, warn};
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::DirectWrite::*;
use windows::Win32::Graphics::Gdi::*;
use windows::core::*;

/// Owns the DirectWrite factory and pre-built text formats for normal/bold/italic/bold-italic.
pub struct DWriteEngine {
    #[allow(dead_code)] // kept alive — COM prevent premature release
    factory: IDWriteFactory,
    #[allow(dead_code)] // kept alive — COM prevent premature release
    gdi_interop: IDWriteGdiInterop,
    render_target: IDWriteBitmapRenderTarget,
    rendering_params: IDWriteRenderingParams,
    /// Text formats indexed by style: [normal, bold, italic, bold_italic]
    formats: [IDWriteTextFormat; 4],
    /// Metrics derived from the primary font face.
    pub metrics: DWriteMetrics,
    /// Fallback font faces tried when the primary font lacks a glyph.
    fallback_faces: Vec<IDWriteFontFace>,
}

/// Font metrics extracted from DirectWrite, used to size the terminal grid.
#[derive(Debug, Clone, Copy)]
pub struct DWriteMetrics {
    /// Advance width of a monospace cell in pixels.
    pub cell_width: f32,
    /// Line height in pixels (ascent + descent + line gap, scaled by line_height).
    pub cell_height: f32,
    /// Distance from cell top to baseline in pixels.
    pub baseline: f32,
    /// Underline position below baseline in pixels.
    pub underline_position: f32,
    /// Underline thickness in pixels.
    pub underline_thickness: f32,
    /// Strikethrough position from cell top in pixels.
    pub strikethrough_position: f32,
    /// Font size in DIPs (device-independent pixels).
    pub font_size_dip: f32,
}

/// Style index for format lookup.
const STYLE_NORMAL: usize = 0;
const STYLE_BOLD: usize = 1;
const STYLE_ITALIC: usize = 2;
const STYLE_BOLD_ITALIC: usize = 3;

impl DWriteEngine {
    /// Create a new DirectWrite engine with the given font family, size, and DPI.
    ///
    /// `line_height` is a multiplier (1.0 = default, 1.2 = spacious).
    /// `fallback_fonts` is a list of font family names to try when the primary font lacks a glyph.
    /// Falls back to "Consolas" if the requested family is unavailable.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        family: &str,
        font_size_pt: f32,
        dpi: f32,
        line_height: f32,
        fallback_fonts: &[String],
        font_path: &str,
    ) -> Result<Self> {
        // Resolve effective family: font_path takes priority over family name
        let effective_family = resolve_font_family(font_path, family);
        let family = &effective_family;
        // DIP = points * (96/72) — DirectWrite works in DIPs
        let font_size_dip = font_size_pt * (96.0 / 72.0);
        let pixels_per_dip = dpi / 96.0;

        let factory: IDWriteFactory = unsafe { DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED)? };

        let gdi_interop = unsafe { factory.GetGdiInterop()? };

        // Create a bitmap render target sized for one glyph cell (resized later if needed)
        let render_target = unsafe { gdi_interop.CreateBitmapRenderTarget(None, 256, 256)? };
        unsafe {
            render_target.SetPixelsPerDip(pixels_per_dip)?;
        }

        // Custom rendering params: ClearType natural symmetric for best quality
        let rendering_params = unsafe {
            factory.CreateCustomRenderingParams(
                1.8, // gamma
                0.5, // enhanced contrast
                1.0, // ClearType level
                DWRITE_PIXEL_GEOMETRY_RGB,
                DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
            )?
        };

        // Build the family name as wide string
        let family_wide: Vec<u16> = family.encode_utf16().chain(std::iter::once(0)).collect();
        let locale_wide: Vec<u16> = "en-us\0".encode_utf16().collect();

        // Try creating the requested font; fall back to Consolas then Courier New
        let formats = Self::create_format_set(&factory, &family_wide, &locale_wide, font_size_dip)
            .or_else(|_| {
                warn!(family, "Font not found, falling back to Consolas");
                let fallback: Vec<u16> = "Consolas\0".encode_utf16().collect();
                Self::create_format_set(&factory, &fallback, &locale_wide, font_size_dip)
            })
            .or_else(|_| {
                warn!("Consolas not found, falling back to Courier New");
                let fallback: Vec<u16> = "Courier New\0".encode_utf16().collect();
                Self::create_format_set(&factory, &fallback, &locale_wide, font_size_dip)
            })?;

        // Extract metrics from the normal format
        let metrics = Self::compute_metrics(
            &factory,
            &formats[STYLE_NORMAL],
            font_size_dip,
            pixels_per_dip,
            line_height,
        )?;

        debug!(
            cell_w = metrics.cell_width,
            cell_h = metrics.cell_height,
            baseline = metrics.baseline,
            "DirectWrite metrics computed"
        );

        // Build fallback font faces from the provided list
        let fallback_faces = Self::build_fallback_faces(&factory, fallback_fonts);

        Ok(Self {
            factory,
            gdi_interop,
            render_target,
            rendering_params,
            formats,
            metrics,
            fallback_faces,
        })
    }

    /// Create all four style variants of a text format.
    fn create_format_set(
        factory: &IDWriteFactory,
        family: &[u16],
        locale: &[u16],
        size: f32,
    ) -> Result<[IDWriteTextFormat; 4]> {
        let normal = unsafe {
            factory.CreateTextFormat(
                PCWSTR(family.as_ptr()),
                None, // system font collection
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                PCWSTR(locale.as_ptr()),
            )?
        };
        let bold = unsafe {
            factory.CreateTextFormat(
                PCWSTR(family.as_ptr()),
                None,
                DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                PCWSTR(locale.as_ptr()),
            )?
        };
        let italic = unsafe {
            factory.CreateTextFormat(
                PCWSTR(family.as_ptr()),
                None,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_ITALIC,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                PCWSTR(locale.as_ptr()),
            )?
        };
        let bold_italic = unsafe {
            factory.CreateTextFormat(
                PCWSTR(family.as_ptr()),
                None,
                DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_ITALIC,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                PCWSTR(locale.as_ptr()),
            )?
        };
        Ok([normal, bold, italic, bold_italic])
    }

    /// Build IDWriteFontFace objects for each fallback font family.
    fn build_fallback_faces(factory: &IDWriteFactory, families: &[String]) -> Vec<IDWriteFontFace> {
        let mut collection_opt: Option<IDWriteFontCollection> = None;
        if unsafe { factory.GetSystemFontCollection(&mut collection_opt, false) }.is_err() {
            return Vec::new();
        }
        let collection = match collection_opt {
            Some(c) => c,
            None => return Vec::new(),
        };

        let mut faces = Vec::new();
        for family in families {
            let wide: Vec<u16> = family.encode_utf16().chain(std::iter::once(0)).collect();
            let mut idx = 0u32;
            let mut exists = BOOL::default();
            let found =
                unsafe { collection.FindFamilyName(PCWSTR(wide.as_ptr()), &mut idx, &mut exists) };
            if found.is_err() || !exists.as_bool() {
                debug!(family = %family, "Fallback font not found, skipping");
                continue;
            }
            if let Ok(font_family) = unsafe { collection.GetFontFamily(idx) }
                && let Ok(font) = unsafe {
                    font_family.GetFirstMatchingFont(
                        DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        DWRITE_FONT_STYLE_NORMAL,
                    )
                }
                && let Ok(face) = unsafe { font.CreateFontFace() }
            {
                debug!(family = %family, "Loaded fallback font face");
                faces.push(face);
            }
        }
        faces
    }

    /// Rebuild fallback font faces (called on config hot-reload).
    pub fn rebuild_fallback_fonts(&mut self, families: &[String]) {
        self.fallback_faces = Self::build_fallback_faces(&self.factory, families);
    }

    /// Compute cell metrics by measuring a reference character ('M') with a text layout.
    fn compute_metrics(
        factory: &IDWriteFactory,
        format: &IDWriteTextFormat,
        font_size_dip: f32,
        pixels_per_dip: f32,
        line_height_mult: f32,
    ) -> Result<DWriteMetrics> {
        // Measure 'M' for advance width, full line for height
        let reference: Vec<u16> = "M".encode_utf16().collect();

        let layout = unsafe {
            factory.CreateTextLayout(
                &reference, format, 1000.0, // large max width
                1000.0,
            )?
        };

        let mut text_metrics = DWRITE_TEXT_METRICS::default();
        unsafe {
            layout.GetMetrics(&mut text_metrics)?;
        }

        // For monospace fonts, width of 'M' should equal the advance width
        let cell_width = (text_metrics.width * pixels_per_dip).round();

        // Get font metrics for precise ascent/descent/line gap
        let mut line_buf = [DWRITE_LINE_METRICS::default()];
        let mut line_count = 0u32;
        unsafe {
            layout.GetLineMetrics(Some(&mut line_buf), &mut line_count)?;
        }

        let raw_height = line_buf[0].height * pixels_per_dip;
        let cell_height = if raw_height > 0.0 {
            (raw_height * line_height_mult).ceil()
        } else {
            // Fallback: use font size * 1.2 as line height
            (font_size_dip * pixels_per_dip * 1.2 * line_height_mult).ceil()
        };
        let baseline = if line_buf[0].baseline > 0.0 {
            (line_buf[0].baseline * pixels_per_dip).round()
        } else {
            (cell_height * 0.8).round()
        };

        // Get underline/strikethrough info from font metrics via the collection
        let collection = unsafe { format.GetFontCollection()? };
        let mut idx = 0u32;
        let mut exists = BOOL::default();
        let family_name_len = unsafe { format.GetFontFamilyNameLength() };
        let mut family_name = vec![0u16; (family_name_len + 1) as usize];
        unsafe {
            format.GetFontFamilyName(&mut family_name)?;
        }
        unsafe {
            collection.FindFamilyName(PCWSTR(family_name.as_ptr()), &mut idx, &mut exists)?;
        }

        let (underline_pos, underline_thick, strikethrough_pos) = if exists.as_bool() {
            let font_family = unsafe { collection.GetFontFamily(idx)? };
            let font = unsafe {
                font_family.GetFirstMatchingFont(
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                )?
            };
            let face = unsafe { font.CreateFontFace()? };
            let mut fm = DWRITE_FONT_METRICS::default();
            unsafe {
                face.GetMetrics(&mut fm);
            }

            let design_to_px = font_size_dip * pixels_per_dip / fm.designUnitsPerEm as f32;
            let ul_pos = fm.underlinePosition as f32 * design_to_px;
            let ul_thick = (fm.underlineThickness as f32 * design_to_px).max(1.0);
            let st_pos = fm.strikethroughPosition as f32 * design_to_px;

            (ul_pos.abs(), ul_thick, baseline - st_pos)
        } else {
            // Fallback: approximate
            let scale = pixels_per_dip;
            (2.0 * scale, (1.0 * scale).max(1.0), cell_height * 0.35)
        };

        Ok(DWriteMetrics {
            cell_width,
            cell_height,
            baseline,
            underline_position: underline_pos,
            underline_thickness: underline_thick,
            strikethrough_position: strikethrough_pos,
            font_size_dip,
        })
    }

    /// Rasterize a single character into an alpha bitmap.
    ///
    /// Returns `(width, height, alpha_data)` where alpha_data is row-major,
    /// one byte per pixel (0 = transparent, 255 = fully opaque).
    ///
    /// The bitmap is sized to exactly one cell (`cell_width × cell_height`).
    pub fn rasterize(&mut self, ch: char, bold: bool, italic: bool) -> (u32, u32, Vec<u8>) {
        let cw = self.metrics.cell_width as u32;
        let ch_height = self.metrics.cell_height as u32;

        if cw == 0 || ch_height == 0 {
            return (0, 0, Vec::new());
        }

        // Ensure render target is large enough
        let rt_size = unsafe { self.render_target.GetSize() };
        if let Ok(size) = rt_size
            && ((size.cx as u32) < cw || (size.cy as u32) < ch_height)
        {
            let new_w = cw.max(size.cx as u32);
            let new_h = ch_height.max(size.cy as u32);
            let _ = unsafe { self.render_target.Resize(new_w, new_h) };
        }

        // Clear the render target to black
        let hdc = unsafe { self.render_target.GetMemoryDC() };
        let rect = RECT {
            left: 0,
            top: 0,
            right: cw as i32,
            bottom: ch_height as i32,
        };
        unsafe {
            let brush = CreateSolidBrush(COLORREF(0x00000000)); // black
            FillRect(hdc, &rect, brush);
            let _ = DeleteObject(brush.into());
        }

        // Pick the right text format
        let style_idx = match (bold, italic) {
            (false, false) => STYLE_NORMAL,
            (true, false) => STYLE_BOLD,
            (false, true) => STYLE_ITALIC,
            (true, true) => STYLE_BOLD_ITALIC,
        };

        let result = self.draw_glyph_via_layout(ch, style_idx, cw, ch_height);
        if let Some(alpha) = result {
            return (cw, ch_height, alpha);
        }

        // Try fallback font faces
        for fallback_face in &self.fallback_faces {
            if let Some(alpha) = self.draw_glyph_with_face(ch, fallback_face, cw, ch_height) {
                return (cw, ch_height, alpha);
            }
        }

        // Fallback: empty cell
        (cw, ch_height, vec![0u8; (cw * ch_height) as usize])
    }

    /// Draw a glyph using IDWriteFontFace::GetGlyphIndices + DrawGlyphRun.
    fn draw_glyph_via_layout(
        &self,
        ch: char,
        style_idx: usize,
        width: u32,
        height: u32,
    ) -> Option<Vec<u8>> {
        let format = &self.formats[style_idx];

        // Get the font collection and find the font face
        let collection = unsafe { format.GetFontCollection().ok()? };
        let family_name_len = unsafe { format.GetFontFamilyNameLength() };
        let mut family_name = vec![0u16; (family_name_len + 1) as usize];
        unsafe {
            format.GetFontFamilyName(&mut family_name).ok()?;
        }

        let mut family_idx = 0u32;
        let mut exists = BOOL::default();
        unsafe {
            collection
                .FindFamilyName(PCWSTR(family_name.as_ptr()), &mut family_idx, &mut exists)
                .ok()?;
        }
        if !exists.as_bool() {
            return None;
        }

        let font_family = unsafe { collection.GetFontFamily(family_idx).ok()? };
        let weight = unsafe { format.GetFontWeight() };
        let stretch = unsafe { format.GetFontStretch() };
        let style = unsafe { format.GetFontStyle() };
        let font = unsafe {
            font_family
                .GetFirstMatchingFont(weight, stretch, style)
                .ok()?
        };
        let face = unsafe { font.CreateFontFace().ok()? };

        // Get glyph index for this character
        let codepoint = ch as u32;
        let mut glyph_index = 0u16;
        unsafe {
            face.GetGlyphIndices(&codepoint, 1, &mut glyph_index).ok()?;
        }

        // If glyph index is 0 (.notdef), the font doesn't have this character
        if glyph_index == 0 && ch != '\0' {
            return None;
        }

        // Build a DWRITE_GLYPH_RUN
        let advance = self.metrics.cell_width / (unsafe { self.render_target.GetPixelsPerDip() });
        let glyph_run = DWRITE_GLYPH_RUN {
            fontFace: std::mem::ManuallyDrop::new(Some(face.clone())),
            fontEmSize: self.metrics.font_size_dip,
            glyphCount: 1,
            glyphIndices: &glyph_index,
            glyphAdvances: &advance,
            glyphOffsets: ptr::null(),
            isSideways: BOOL(0),
            bidiLevel: 0,
        };

        // Draw at the baseline position
        let pixels_per_dip = unsafe { self.render_target.GetPixelsPerDip() };
        let baseline_x = 0.0f32;
        let baseline_y = self.metrics.baseline / pixels_per_dip;

        unsafe {
            self.render_target
                .DrawGlyphRun(
                    baseline_x,
                    baseline_y,
                    DWRITE_MEASURING_MODE_NATURAL,
                    &glyph_run,
                    &self.rendering_params,
                    COLORREF(0x00FFFFFF), // white on black
                    None,
                )
                .ok()?;
        }

        // Read pixels from the memory DC
        let hdc = unsafe { self.render_target.GetMemoryDC() };
        let alpha = self.extract_alpha_from_hdc(hdc, width, height);

        // Clear for next use (fill with black again)
        let rect = RECT {
            left: 0,
            top: 0,
            right: width as i32,
            bottom: height as i32,
        };
        unsafe {
            let brush = CreateSolidBrush(COLORREF(0x00000000));
            FillRect(hdc, &rect, brush);
            let _ = DeleteObject(brush.into());
        }

        // Don't drop the font face from the glyph run — ManuallyDrop handles it
        // We cloned the face above, so the original is still alive
        // The ManuallyDrop in the glyph_run won't be dropped (that's the point).

        Some(alpha)
    }

    /// Draw a glyph using a specific fallback IDWriteFontFace.
    fn draw_glyph_with_face(
        &self,
        ch: char,
        face: &IDWriteFontFace,
        width: u32,
        height: u32,
    ) -> Option<Vec<u8>> {
        let codepoint = ch as u32;
        let mut glyph_index = 0u16;
        unsafe {
            face.GetGlyphIndices(&codepoint, 1, &mut glyph_index).ok()?;
        }

        if glyph_index == 0 && ch != '\0' {
            return None; // This fallback font also lacks the glyph
        }

        let advance = self.metrics.cell_width / (unsafe { self.render_target.GetPixelsPerDip() });
        let glyph_run = DWRITE_GLYPH_RUN {
            fontFace: std::mem::ManuallyDrop::new(Some(face.clone())),
            fontEmSize: self.metrics.font_size_dip,
            glyphCount: 1,
            glyphIndices: &glyph_index,
            glyphAdvances: &advance,
            glyphOffsets: ptr::null(),
            isSideways: BOOL(0),
            bidiLevel: 0,
        };

        let pixels_per_dip = unsafe { self.render_target.GetPixelsPerDip() };
        let baseline_x = 0.0f32;
        let baseline_y = self.metrics.baseline / pixels_per_dip;

        unsafe {
            self.render_target
                .DrawGlyphRun(
                    baseline_x,
                    baseline_y,
                    DWRITE_MEASURING_MODE_NATURAL,
                    &glyph_run,
                    &self.rendering_params,
                    COLORREF(0x00FFFFFF),
                    None,
                )
                .ok()?;
        }

        let hdc = unsafe { self.render_target.GetMemoryDC() };
        let alpha = self.extract_alpha_from_hdc(hdc, width, height);

        // Clear for next use
        let rect = RECT {
            left: 0,
            top: 0,
            right: width as i32,
            bottom: height as i32,
        };
        unsafe {
            let brush = CreateSolidBrush(COLORREF(0x00000000));
            FillRect(hdc, &rect, brush);
            let _ = DeleteObject(brush.into());
        }

        Some(alpha)
    }

    /// Extract alpha values from an HDC bitmap using GetDIBits for bulk reads.
    /// DirectWrite renders white-on-black; we extract the luminance as alpha.
    fn extract_alpha_from_hdc(&self, hdc: HDC, width: u32, height: u32) -> Vec<u8> {
        let mut alpha = vec![0u8; (width * height) as usize];

        // Get the bitmap handle from the DC
        let hbitmap = unsafe { GetCurrentObject(hdc, OBJ_BITMAP) };
        if hbitmap.is_invalid() {
            // Fallback: use GetPixel (slow but always works)
            return self.extract_alpha_getpixel(hdc, width, height);
        }

        // Set up BITMAPINFO for 32-bit BGRA
        let mut bmi = BITMAPINFO {
            bmiHeader: BITMAPINFOHEADER {
                biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: width as i32,
                biHeight: -(height as i32), // top-down
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB.0,
                ..Default::default()
            },
            ..Default::default()
        };

        let stride = width as usize * 4; // 4 bytes per pixel (BGRA)
        let mut pixels = vec![0u8; stride * height as usize];

        let result = unsafe {
            GetDIBits(
                hdc,
                HBITMAP(hbitmap.0),
                0,
                height,
                Some(pixels.as_mut_ptr() as *mut _),
                &mut bmi,
                DIB_RGB_COLORS,
            )
        };

        if result == 0 {
            return self.extract_alpha_getpixel(hdc, width, height);
        }

        // Convert BGRA → luminance alpha
        for row in 0..height as usize {
            for col in 0..width as usize {
                let px = row * stride + col * 4;
                let b = pixels[px] as u32;
                let g = pixels[px + 1] as u32;
                let r = pixels[px + 2] as u32;
                let lum = (r * 77 + g * 150 + b * 29) >> 8;
                alpha[row * width as usize + col] = lum.min(255) as u8;
            }
        }

        alpha
    }

    /// Slow fallback using GetPixel (one syscall per pixel).
    fn extract_alpha_getpixel(&self, hdc: HDC, width: u32, height: u32) -> Vec<u8> {
        let mut alpha = vec![0u8; (width * height) as usize];
        for row in 0..height {
            for col in 0..width {
                let color = unsafe { GetPixel(hdc, col as i32, row as i32) };
                let r = (color.0 & 0xFF) as u32;
                let g = ((color.0 >> 8) & 0xFF) as u32;
                let b = ((color.0 >> 16) & 0xFF) as u32;
                let lum = (r * 77 + g * 150 + b * 29) >> 8;
                alpha[(row * width + col) as usize] = lum.min(255) as u8;
            }
        }
        alpha
    }

    /// Get the style index for cache key construction.
    pub fn style_index(bold: bool, italic: bool) -> usize {
        match (bold, italic) {
            (false, false) => STYLE_NORMAL,
            (true, false) => STYLE_BOLD,
            (false, true) => STYLE_ITALIC,
            (true, true) => STYLE_BOLD_ITALIC,
        }
    }

    /// Shape a text run and return per-cluster cell spans.
    ///
    /// Each returned `ClusterInfo` tells the caller how many UTF-16 code units
    /// and how many terminal cells the cluster occupies. A ligature like `=>`
    /// in a ligature-supporting font produces a single cluster with `cell_span = 2`.
    pub fn get_cluster_metrics(&self, text: &str, bold: bool, italic: bool) -> Vec<ClusterInfo> {
        let wide: Vec<u16> = text.encode_utf16().collect();
        if wide.is_empty() {
            return Vec::new();
        }

        let style_idx = Self::style_index(bold, italic);
        let format = &self.formats[style_idx];

        let layout = match unsafe {
            self.factory
                .CreateTextLayout(&wide, format, 100_000.0, 1000.0)
        } {
            Ok(l) => l,
            Err(_) => {
                // Fallback: every character is 1 cell
                return text
                    .chars()
                    .map(|_| ClusterInfo {
                        char_count: 1,
                        cell_span: 1,
                    })
                    .collect();
            }
        };

        let max_clusters = wide.len();
        let mut dw_clusters = vec![DWRITE_CLUSTER_METRICS::default(); max_clusters];
        let mut actual_count = 0u32;
        if unsafe { layout.GetClusterMetrics(Some(&mut dw_clusters), &mut actual_count) }.is_err() {
            return text
                .chars()
                .map(|_| ClusterInfo {
                    char_count: 1,
                    cell_span: 1,
                })
                .collect();
        }
        dw_clusters.truncate(actual_count as usize);

        let pixels_per_dip = unsafe { self.render_target.GetPixelsPerDip() };
        let cell_width = self.metrics.cell_width;

        dw_clusters
            .iter()
            .map(|c| {
                let advance_px = c.width * pixels_per_dip;
                let cell_span = (advance_px / cell_width).round().max(1.0) as u8;
                ClusterInfo {
                    char_count: c.length,
                    cell_span,
                }
            })
            .collect()
    }

    /// Rasterize a multi-character text cluster (for ligatures).
    ///
    /// Uses `IDWriteTextAnalyzer::GetGlyphs` for OpenType shaping (GSUB) to
    /// produce the ligature glyph, then `DrawGlyphRun` to render it.
    /// The bitmap is sized to `target_width × cell_height`.
    pub fn rasterize_cluster(
        &mut self,
        text: &str,
        bold: bool,
        italic: bool,
        target_width: u32,
    ) -> (u32, u32, Vec<u8>) {
        let ch_height = self.metrics.cell_height as u32;
        if target_width == 0 || ch_height == 0 || text.is_empty() {
            return (0, 0, Vec::new());
        }

        let empty = || {
            (
                target_width,
                ch_height,
                vec![0u8; (target_width * ch_height) as usize],
            )
        };

        // Get font face for the requested style
        let style_idx = Self::style_index(bold, italic);
        let face = match self.get_font_face_for_style(style_idx) {
            Some(f) => f,
            None => return empty(),
        };

        // Create text analyzer
        let analyzer: IDWriteTextAnalyzer = match unsafe { self.factory.CreateTextAnalyzer() } {
            Ok(a) => a,
            Err(_) => return empty(),
        };

        // UTF-16 encode
        let wide: Vec<u16> = text.encode_utf16().collect();
        let text_len = wide.len() as u32;

        // Script analysis: common/Latin for programming ligatures
        let script_analysis = DWRITE_SCRIPT_ANALYSIS {
            script: 0,
            shapes: DWRITE_SCRIPT_SHAPES_DEFAULT,
        };

        // Allocate output buffers
        let max_glyphs = (text_len * 3 / 2 + 16) as usize;
        let mut cluster_map = vec![0u16; wide.len()];
        let mut text_props = vec![DWRITE_SHAPING_TEXT_PROPERTIES::default(); wide.len()];
        let mut glyph_indices = vec![0u16; max_glyphs];
        let mut glyph_props = vec![DWRITE_SHAPING_GLYPH_PROPERTIES::default(); max_glyphs];
        let mut actual_glyph_count = 0u32;

        // GetGlyphs: applies GSUB (ligature substitution)
        let hr = unsafe {
            analyzer.GetGlyphs(
                PCWSTR(wide.as_ptr()),
                text_len,
                &face,
                false,
                false,
                &script_analysis,
                PCWSTR::null(),
                None, // number substitution
                None, // features
                None, // feature range lengths
                0,    // feature ranges count
                max_glyphs as u32,
                cluster_map.as_mut_ptr(),
                text_props.as_mut_ptr(),
                glyph_indices.as_mut_ptr(),
                glyph_props.as_mut_ptr(),
                &mut actual_glyph_count,
            )
        };
        if hr.is_err() {
            return empty();
        }

        let glyph_count = actual_glyph_count as usize;

        // GetGlyphPlacements: compute advances and offsets
        let mut advances = vec![0.0f32; glyph_count];
        let mut offsets = vec![DWRITE_GLYPH_OFFSET::default(); glyph_count];

        let hr = unsafe {
            analyzer.GetGlyphPlacements(
                PCWSTR(wide.as_ptr()),
                cluster_map.as_ptr(),
                text_props.as_mut_ptr(),
                text_len,
                glyph_indices.as_ptr(),
                glyph_props.as_ptr(),
                actual_glyph_count,
                &face,
                self.metrics.font_size_dip,
                false,
                false,
                &script_analysis,
                PCWSTR::null(),
                None,
                None,
                0,
                advances.as_mut_ptr(),
                offsets.as_mut_ptr(),
            )
        };
        if hr.is_err() {
            return empty();
        }

        // Ensure render target is large enough
        let rt_size = unsafe { self.render_target.GetSize() };
        if let Ok(size) = rt_size
            && ((size.cx as u32) < target_width || (size.cy as u32) < ch_height)
        {
            let new_w = target_width.max(size.cx as u32);
            let new_h = ch_height.max(size.cy as u32);
            let _ = unsafe { self.render_target.Resize(new_w, new_h) };
        }

        // Clear render target to black
        let hdc = unsafe { self.render_target.GetMemoryDC() };
        let rect = RECT {
            left: 0,
            top: 0,
            right: target_width as i32,
            bottom: ch_height as i32,
        };
        unsafe {
            let brush = CreateSolidBrush(COLORREF(0x00000000));
            FillRect(hdc, &rect, brush);
            let _ = DeleteObject(brush.into());
        }

        // Build and draw the shaped glyph run
        let pixels_per_dip = unsafe { self.render_target.GetPixelsPerDip() };
        let baseline_y = self.metrics.baseline / pixels_per_dip;

        let glyph_run = DWRITE_GLYPH_RUN {
            fontFace: std::mem::ManuallyDrop::new(Some(face.clone())),
            fontEmSize: self.metrics.font_size_dip,
            glyphCount: actual_glyph_count,
            glyphIndices: glyph_indices.as_ptr(),
            glyphAdvances: advances.as_ptr(),
            glyphOffsets: offsets.as_ptr(),
            isSideways: BOOL(0),
            bidiLevel: 0,
        };

        let draw_result = unsafe {
            self.render_target.DrawGlyphRun(
                0.0,
                baseline_y,
                DWRITE_MEASURING_MODE_NATURAL,
                &glyph_run,
                &self.rendering_params,
                COLORREF(0x00FFFFFF),
                None,
            )
        };
        if draw_result.is_err() {
            return empty();
        }

        // Extract alpha from the rendered bitmap
        let alpha = self.extract_alpha_from_hdc(hdc, target_width, ch_height);

        // Clear for next use
        unsafe {
            let brush = CreateSolidBrush(COLORREF(0x00000000));
            FillRect(hdc, &rect, brush);
            let _ = DeleteObject(brush.into());
        }

        (target_width, ch_height, alpha)
    }

    /// Get the `IDWriteFontFace` for a given style index.
    fn get_font_face_for_style(&self, style_idx: usize) -> Option<IDWriteFontFace> {
        let format = &self.formats[style_idx];
        let collection = unsafe { format.GetFontCollection().ok()? };
        let family_name_len = unsafe { format.GetFontFamilyNameLength() };
        let mut family_name = vec![0u16; (family_name_len + 1) as usize];
        unsafe {
            format.GetFontFamilyName(&mut family_name).ok()?;
        }
        let mut family_idx = 0u32;
        let mut exists = BOOL::default();
        unsafe {
            collection
                .FindFamilyName(PCWSTR(family_name.as_ptr()), &mut family_idx, &mut exists)
                .ok()?;
        }
        if !exists.as_bool() {
            return None;
        }
        let font_family = unsafe { collection.GetFontFamily(family_idx).ok()? };
        let weight = unsafe { format.GetFontWeight() };
        let stretch = unsafe { format.GetFontStretch() };
        let style = unsafe { format.GetFontStyle() };
        let font = unsafe {
            font_family
                .GetFirstMatchingFont(weight, stretch, style)
                .ok()?
        };
        unsafe { font.CreateFontFace().ok() }
    }
}

/// Cluster information from DirectWrite text shaping.
#[derive(Debug, Clone, Copy)]
pub struct ClusterInfo {
    /// Number of UTF-16 code units in this cluster.
    pub char_count: u16,
    /// How many terminal cells this cluster occupies.
    pub cell_span: u8,
}

/// Resolve the effective font family given a font file path and a fallback family.
///
/// If `font_path` is non-empty and the file exists, attempts to extract the
/// font family name from the file using DirectWrite. If that fails (or the
/// path is empty), returns the `fallback_family` unchanged.
pub fn resolve_font_family(font_path: &str, fallback_family: &str) -> String {
    if font_path.is_empty() {
        return fallback_family.to_string();
    }

    let path = std::path::Path::new(font_path);
    if !path.exists() {
        warn!(
            font_path,
            "Font file not found, falling back to family name"
        );
        return fallback_family.to_string();
    }

    // Try to extract the family name from the font file via DirectWrite
    match extract_family_from_file(font_path) {
        Some(name) => {
            debug!(font_path, family = %name, "Resolved font family from file");
            name
        }
        None => {
            warn!(
                font_path,
                "Could not extract family from font file, using fallback"
            );
            fallback_family.to_string()
        }
    }
}

/// Extract the font family name from a .ttf/.otf file using DirectWrite.
///
/// Registers the font temporarily via GDI `AddFontResourceExW` and then
/// queries its family name through DirectWrite font file APIs.
fn extract_family_from_file(font_path: &str) -> Option<String> {
    use windows::Win32::Graphics::Gdi::{AddFontResourceExW, FR_PRIVATE, RemoveFontResourceExW};

    let path_wide: Vec<u16> = font_path.encode_utf16().chain(std::iter::once(0)).collect();

    // Register font privately (only visible to this process)
    let added = unsafe { AddFontResourceExW(PCWSTR(path_wide.as_ptr()), FR_PRIVATE, None) };
    if added == 0 {
        return None;
    }

    // Use DirectWrite to read the font file and extract the family name
    let result = (|| -> Option<String> {
        let factory: IDWriteFactory =
            unsafe { DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED).ok()? };
        let font_file = unsafe {
            factory
                .CreateFontFileReference(PCWSTR(path_wide.as_ptr()), None)
                .ok()?
        };

        let mut is_supported = BOOL::default();
        let mut file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
        let mut face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
        let mut num_faces = 0u32;
        unsafe {
            font_file
                .Analyze(
                    &mut is_supported,
                    &mut file_type,
                    Some(&mut face_type),
                    &mut num_faces,
                )
                .ok()?;
        }
        if !is_supported.as_bool() || num_faces == 0 {
            return None;
        }

        let font_face = unsafe {
            factory
                .CreateFontFace(
                    face_type,
                    &[Some(font_file.clone())],
                    0,
                    DWRITE_FONT_SIMULATIONS_NONE,
                )
                .ok()?
        };

        // Use the system font collection (with the privately loaded font) to find
        // the family name by matching metrics against our font face.
        let mut collection_opt: Option<IDWriteFontCollection> = None;
        unsafe {
            factory
                .GetSystemFontCollection(&mut collection_opt, false)
                .ok()?;
        }
        let collection = collection_opt?;
        let family_count = unsafe { collection.GetFontFamilyCount() };
        for i in 0..family_count {
            let family = unsafe { collection.GetFontFamily(i).ok()? };
            let family_names = unsafe { family.GetFamilyNames().ok()? };
            let font_count = unsafe { family.GetFontCount() };
            for j in 0..font_count {
                let font = unsafe { family.GetFont(j).ok()? };
                if let Ok(face) = unsafe { font.CreateFontFace() } {
                    // Compare metrics as a heuristic
                    let mut m1 = DWRITE_FONT_METRICS::default();
                    let mut m2 = DWRITE_FONT_METRICS::default();
                    unsafe {
                        font_face.GetMetrics(&mut m1 as *mut _);
                        face.GetMetrics(&mut m2 as *mut _);
                    }
                    if m1.designUnitsPerEm == m2.designUnitsPerEm
                        && m1.ascent == m2.ascent
                        && m1.descent == m2.descent
                    {
                        // Found a match — extract the family name
                        let name_count = unsafe { family_names.GetCount() };
                        if name_count > 0 {
                            let len = unsafe { family_names.GetStringLength(0).ok()? };
                            let mut buf = vec![0u16; (len + 1) as usize];
                            unsafe {
                                family_names.GetString(0, &mut buf).ok()?;
                            }
                            let name = String::from_utf16_lossy(&buf[..len as usize]);
                            return Some(name);
                        }
                    }
                }
            }
        }
        None
    })();

    // Always unregister the privately loaded font
    unsafe {
        let _ = RemoveFontResourceExW(PCWSTR(path_wide.as_ptr()), FR_PRIVATE.0, None);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::atlas::GlyphAtlas;

    #[test]
    fn test_resolve_empty_path_returns_family() {
        let result = resolve_font_family("", "Consolas");
        assert_eq!(result, "Consolas");
    }

    #[test]
    fn test_resolve_nonexistent_path_returns_family() {
        let result = resolve_font_family("C:/nonexistent/font.ttf", "Cascadia Code");
        assert_eq!(result, "Cascadia Code");
    }

    #[test]
    fn test_resolve_empty_path_empty_family() {
        let result = resolve_font_family("", "");
        assert_eq!(result, "");
    }

    /// DirectWrite must return nonzero cell dimensions for any valid system font.
    /// This test caught a real bug: GetLineMetrics wrote into a temporary copy
    /// of DWRITE_LINE_METRICS instead of the actual variable, returning height=0.
    #[test]
    fn test_glyph_atlas_has_nonzero_cell_dimensions() {
        let atlas = GlyphAtlas::new("Consolas", 14.0, 96.0, 1.0, &[], false, "")
            .expect("Consolas must be available on Windows");
        assert!(
            atlas.metrics.cell_width > 0.0,
            "Cell width must be positive, got {}",
            atlas.metrics.cell_width
        );
        assert!(
            atlas.metrics.cell_height > 0.0,
            "Cell height must be positive, got {}",
            atlas.metrics.cell_height
        );
        assert!(
            atlas.metrics.baseline > 0.0,
            "Baseline must be positive, got {}",
            atlas.metrics.baseline
        );
        assert!(
            atlas.metrics.baseline < atlas.metrics.cell_height,
            "Baseline ({}) must be less than cell height ({})",
            atlas.metrics.baseline,
            atlas.metrics.cell_height
        );
    }

    /// Cascadia Code is the default font. Verify it produces sane metrics.
    #[test]
    fn test_cascadia_code_metrics() {
        let atlas = GlyphAtlas::new("Cascadia Code", 14.0, 96.0, 1.0, &[], false, "")
            .expect("Cascadia Code should be available");
        assert!(
            atlas.metrics.cell_width >= 5.0 && atlas.metrics.cell_width <= 30.0,
            "Cascadia Code cell width {} out of expected range",
            atlas.metrics.cell_width
        );
        assert!(
            atlas.metrics.cell_height >= 10.0 && atlas.metrics.cell_height <= 50.0,
            "Cascadia Code cell height {} out of expected range",
            atlas.metrics.cell_height
        );
    }

    /// Font metrics must scale with font size.
    #[test]
    fn test_metrics_scale_with_size() {
        let small = GlyphAtlas::new("Consolas", 10.0, 96.0, 1.0, &[], false, "")
            .expect("Consolas must load");
        let large = GlyphAtlas::new("Consolas", 20.0, 96.0, 1.0, &[], false, "")
            .expect("Consolas must load");
        assert!(
            large.metrics.cell_height > small.metrics.cell_height,
            "Larger font must produce taller cells: {} vs {}",
            large.metrics.cell_height,
            small.metrics.cell_height
        );
    }

    /// Line height multiplier must affect cell height.
    #[test]
    fn test_line_height_multiplier() {
        let normal = GlyphAtlas::new("Consolas", 14.0, 96.0, 1.0, &[], false, "")
            .expect("Consolas must load");
        let tall = GlyphAtlas::new("Consolas", 14.0, 96.0, 1.5, &[], false, "")
            .expect("Consolas must load");
        assert!(
            tall.metrics.cell_height > normal.metrics.cell_height,
            "Line height 1.5 must produce taller cells: {} vs {}",
            tall.metrics.cell_height,
            normal.metrics.cell_height
        );
    }

    /// High DPI must produce larger cells.
    #[test]
    fn test_metrics_scale_with_dpi() {
        let lo = GlyphAtlas::new("Consolas", 14.0, 96.0, 1.0, &[], false, "")
            .expect("Consolas must load");
        let hi = GlyphAtlas::new("Consolas", 14.0, 192.0, 1.0, &[], false, "")
            .expect("Consolas must load");
        assert!(
            hi.metrics.cell_height > lo.metrics.cell_height,
            "192 DPI must produce taller cells than 96 DPI: {} vs {}",
            hi.metrics.cell_height,
            lo.metrics.cell_height
        );
    }

    /// Missing font must fall back without panicking.
    #[test]
    fn test_missing_font_does_not_panic() {
        let result = GlyphAtlas::new("NonexistentFont_12345", 14.0, 96.0, 1.0, &[], false, "");
        // May succeed (DirectWrite falls back) or fail, but must not panic
        if let Ok(atlas) = result {
            assert!(atlas.metrics.cell_width > 0.0);
            assert!(atlas.metrics.cell_height > 0.0);
        }
    }
}
