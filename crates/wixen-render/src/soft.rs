//! Software renderer — CPU-based terminal rendering via softbuffer.
//!
//! Fallback for systems without GPU acceleration (VMs, misconfigured drivers,
//! or when the user explicitly selects `renderer = "software"`).
//!
//! Uses the same glyph atlas (DirectWrite) as the GPU renderer, but composites
//! everything on the CPU and blits the final framebuffer via softbuffer (GDI on Win32).

use std::num::NonZero;

use raw_window_handle::{
    DisplayHandle, HandleError, HasDisplayHandle, HasWindowHandle, RawDisplayHandle,
    RawWindowHandle, Win32WindowHandle, WindowHandle, WindowsDisplayHandle,
};
use tracing::info;
use windows::Win32::Foundation::HWND;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;

use crate::RenderError;
use crate::atlas::{FontMetrics, GlyphAtlas, GlyphEntry, GlyphKey};
use crate::colors::ColorScheme;
use crate::pipeline::{CellHighlight, TabBarItem};
use wixen_core::Terminal;
use wixen_core::attrs::{Color, UnderlineStyle};
use wixen_core::cursor::CursorShape;

// ---------------------------------------------------------------------------
// raw-window-handle wrappers
// ---------------------------------------------------------------------------

/// Windows display handle wrapper for softbuffer.
struct WinDisplayHandle;

impl HasDisplayHandle for WinDisplayHandle {
    fn display_handle(&self) -> Result<DisplayHandle<'_>, HandleError> {
        let raw = RawDisplayHandle::Windows(WindowsDisplayHandle::new());
        // Safety: Windows display handle is always valid on Windows.
        Ok(unsafe { DisplayHandle::borrow_raw(raw) })
    }
}

/// Windows window handle wrapper for softbuffer.
struct WinWindowHandle(Win32WindowHandle);

impl HasWindowHandle for WinWindowHandle {
    fn window_handle(&self) -> Result<WindowHandle<'_>, HandleError> {
        let raw = RawWindowHandle::Win32(self.0);
        // Safety: the HWND is guaranteed valid by the caller.
        Ok(unsafe { WindowHandle::borrow_raw(raw) })
    }
}

// ---------------------------------------------------------------------------
// Search match highlight colors (must match GPU pipeline values)
// ---------------------------------------------------------------------------

const MATCH_BG: [f32; 4] = [0.35, 0.30, 0.05, 1.0];
const ACTIVE_MATCH_BG: [f32; 4] = [0.55, 0.45, 0.0, 1.0];
const MATCH_FG: [f32; 4] = [1.0, 1.0, 1.0, 1.0];

// ---------------------------------------------------------------------------
// SoftwareRenderer
// ---------------------------------------------------------------------------

/// CPU-based terminal renderer using softbuffer.
pub struct SoftwareRenderer {
    _context: softbuffer::Context<WinDisplayHandle>,
    surface: softbuffer::Surface<WinDisplayHandle, WinWindowHandle>,
    width: u32,
    height: u32,
    pub atlas: GlyphAtlas,
    pub colors: ColorScheme,
    /// Optional background image (decoded RGBA pixels with opacity applied).
    bg_image: Option<crate::bg_image::BgImageData>,
    /// Gaussian blur radius applied to the background image layer (0 = no blur).
    blur_radius: u32,
}

impl SoftwareRenderer {
    /// Create a new software renderer for the given HWND.
    ///
    /// # Safety
    /// The HWND must remain valid for the lifetime of this SoftwareRenderer.
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn new(
        hwnd: HWND,
        width: u32,
        height: u32,
        dpi: f32,
        font_family: &str,
        font_size: f32,
        line_height: f32,
        colors: ColorScheme,
        fallback_fonts: &[String],
        ligatures: bool,
        font_path: &str,
    ) -> Result<Self, RenderError> {
        let display = WinDisplayHandle;
        let context = softbuffer::Context::new(display)
            .map_err(|e| RenderError::SoftbufferInit(e.to_string()))?;

        let mut win_handle =
            Win32WindowHandle::new(NonZero::new(hwnd.0 as isize).expect("HWND is null"));
        let hmodule = unsafe { GetModuleHandleW(None).unwrap_or_default() };
        win_handle.hinstance = NonZero::new(hmodule.0 as isize);

        let window = WinWindowHandle(win_handle);
        let mut surface = softbuffer::Surface::new(&context, window)
            .map_err(|e| RenderError::SoftbufferInit(e.to_string()))?;

        // Resize the surface buffer to match the window
        if let (Some(w), Some(h)) = (NonZero::new(width), NonZero::new(height)) {
            let _ = surface.resize(w, h);
        }

        let atlas = GlyphAtlas::new(
            font_family,
            font_size,
            dpi,
            line_height,
            fallback_fonts,
            ligatures,
            font_path,
        )?;

        info!(
            font = font_family,
            cell_w = atlas.metrics.cell_width,
            cell_h = atlas.metrics.cell_height,
            "Software renderer initialized"
        );

        Ok(Self {
            _context: context,
            surface,
            width,
            height,
            atlas,
            colors,
            bg_image: None,
            blur_radius: 0,
        })
    }

    /// Resize the rendering surface.
    pub fn resize(&mut self, width: u32, height: u32) {
        if width > 0 && height > 0 {
            self.width = width;
            self.height = height;
            if let (Some(w), Some(h)) = (NonZero::new(width), NonZero::new(height)) {
                let _ = self.surface.resize(w, h);
            }
        }
    }

    /// Get the font metrics (cell dimensions).
    pub fn font_metrics(&self) -> &FontMetrics {
        &self.atlas.metrics
    }

    /// The height of the tab bar in pixels (0 when hidden).
    pub fn tab_bar_height(&self, num_tabs: usize) -> f32 {
        if num_tabs > 1 {
            self.atlas.metrics.cell_height
        } else {
            0.0
        }
    }

    /// Update the color scheme (hot-reload).
    pub fn update_colors(&mut self, colors: ColorScheme) {
        self.colors = colors;
    }

    /// Load a background image from raw file bytes.
    pub fn set_background_image(&mut self, data: &[u8], opacity: f32) {
        if let Some(mut decoded) = crate::bg_image::decode_image(data) {
            crate::bg_image::apply_opacity(&mut decoded.pixels, opacity);
            self.bg_image = Some(decoded);
        }
    }

    /// Remove the background image.
    pub fn clear_background_image(&mut self) {
        self.bg_image = None;
    }

    /// Set the Gaussian blur radius for the background image layer.
    ///
    /// A radius of 0 disables blur. The blur is applied to the background image
    /// only — text and UI elements are not affected.
    pub fn set_blur_radius(&mut self, radius: u32) {
        self.blur_radius = radius;
    }

    /// Get the current blur radius.
    pub fn blur_radius(&self) -> u32 {
        self.blur_radius
    }

    /// Rebuild the glyph atlas for a new font (hot-reload).
    #[allow(clippy::too_many_arguments)]
    pub fn rebuild_font(
        &mut self,
        font_family: &str,
        font_size: f32,
        dpi: f32,
        line_height: f32,
        fallback_fonts: &[String],
        ligatures: bool,
        font_path: &str,
    ) -> Result<(), RenderError> {
        self.atlas = GlyphAtlas::new(
            font_family,
            font_size,
            dpi,
            line_height,
            fallback_fonts,
            ligatures,
            font_path,
        )?;
        info!(
            font = font_family,
            size = font_size,
            cell_w = self.atlas.metrics.cell_width,
            cell_h = self.atlas.metrics.cell_height,
            "Font rebuilt (software hot-reload)"
        );
        Ok(())
    }

    /// Render a frame from the terminal state.
    #[allow(clippy::too_many_arguments)]
    pub fn render(
        &mut self,
        terminal: &Terminal,
        cursor_visible: bool,
        highlight: Option<&dyn Fn(usize, usize) -> CellHighlight>,
        tab_bar: Option<&[TabBarItem]>,
        overlays: &[crate::pipeline::OverlayLayer],
        show_scrollbar: bool,
        padding: (f32, f32),
    ) -> Result<(), RenderError> {
        let (pad_left, pad_top) = padding;
        let w = self.width;
        let h = self.height;

        if w == 0 || h == 0 {
            return Ok(());
        }

        // Compute tab bar height before borrowing the surface buffer
        let show_tab_bar = tab_bar.is_some_and(|items| items.len() > 1);
        let tab_bar_h = if show_tab_bar {
            self.atlas.metrics.cell_height
        } else {
            0.0
        };

        let mut buffer = self
            .surface
            .buffer_mut()
            .map_err(|e| RenderError::SoftbufferPresent(e.to_string()))?;

        // Clear with background color
        let bg_xrgb = f32_to_xrgb(self.colors.bg);
        buffer.fill(bg_xrgb);

        // Draw background image behind everything (if loaded)
        if let Some(ref bg_img) = self.bg_image {
            crate::bg_image::blit_bg_image(&mut buffer, w, h, bg_img, bg_xrgb);

            // Apply Gaussian blur to the composited background layer (before text).
            // This operates on the softbuffer pixel data converted to RGBA for the
            // blur, then written back to XRGB. Only runs when blur_radius > 0.
            if self.blur_radius > 0 {
                let mut rgba = xrgb_buffer_to_rgba(&buffer, w, h);
                crate::blur::apply_cpu_blur(&mut rgba, w, h, self.blur_radius);
                write_rgba_to_xrgb_buffer(&mut buffer, &rgba, w, h);
            }
        }

        // Draw tab bar (use free function to avoid &mut self borrow conflict)
        if let (true, Some(tb)) = (show_tab_bar, tab_bar) {
            draw_tab_bar(&mut buffer, tb, w, h, &mut self.atlas, &self.colors);
        }

        let cell_w = self.atlas.metrics.cell_width;
        let cell_h = self.atlas.metrics.cell_height;
        let cols = terminal.cols();
        let rows = terminal.rows();

        // Draw terminal cells
        for row in 0..rows {
            // Pre-shape row for ligature detection
            let shaped_row = pre_shape_row_soft(terminal, row, cols, &mut self.atlas);

            for col in 0..cols {
                let cell = match terminal.viewport_cell(col, row) {
                    Some(c) => c,
                    None => continue,
                };

                if cell.width == 0 {
                    continue;
                }

                let px = (col as f32 * cell_w + pad_left) as i32;
                let py = (row as f32 * cell_h + tab_bar_h + pad_top) as i32;
                let cw = (cell_w * cell.width.max(1) as f32) as u32;
                let ch = cell_h as u32;

                // Resolve colors
                let mut fg = cell.attrs.fg;
                let mut bg = cell.attrs.bg;

                let is_selected = terminal.cursor_in_viewport()
                    && terminal
                        .selection
                        .as_ref()
                        .is_some_and(|s| s.contains(col, row));

                if cell.attrs.inverse {
                    std::mem::swap(&mut fg, &mut bg);
                }

                let cell_hl = highlight
                    .as_ref()
                    .map(|f| f(row, col))
                    .unwrap_or(CellHighlight::None);

                let cell_hovered = crate::pipeline::is_hovered(col, row, terminal.hover_state());

                let (fg_rgba, bg_rgba) = match cell_hl {
                    CellHighlight::ActiveMatch => (MATCH_FG, ACTIVE_MATCH_BG),
                    CellHighlight::Match => (MATCH_FG, MATCH_BG),
                    CellHighlight::None => {
                        let bg_rgba = if is_selected {
                            self.colors.selection_bg
                        } else {
                            self.colors.resolve(bg, false)
                        };
                        let fg_rgba = if cell_hovered {
                            crate::pipeline::LINK_HOVER_FG
                        } else {
                            self.colors.resolve_fg_contrasted(fg, bg_rgba)
                        };
                        (fg_rgba, bg_rgba)
                    }
                };

                // Draw background rectangle
                let bg_xrgb = f32_to_xrgb(bg_rgba);
                fill_rect(&mut buffer, w, h, px, py, cw, ch, bg_xrgb);

                // Determine glyph from shaped row data
                let glyph_to_blit = match shaped_row.get(col).copied().flatten() {
                    Some((_, 0)) => {
                        // Ligature continuation cell: background already drawn, skip glyph
                        continue;
                    }
                    Some((g, _span)) => {
                        // Shaped cell (ligature start or normal)
                        Some(g)
                    }
                    None => {
                        // Fallback: per-character glyph lookup
                        let ch_char = cell.content.chars().next().unwrap_or(' ');
                        if ch_char > ' ' {
                            Some(self.atlas.get_or_insert(GlyphKey {
                                ch: ch_char,
                                bold: cell.attrs.bold,
                                italic: cell.attrs.italic,
                            }))
                        } else {
                            None
                        }
                    }
                };

                // Rasterize and alpha-blend glyph
                if let Some(glyph) = glyph_to_blit
                    && glyph.width > 0
                    && glyph.height > 0
                {
                    blit_glyph(
                        &mut buffer,
                        w,
                        h,
                        self.atlas.data(),
                        self.atlas.width,
                        &glyph,
                        px,
                        py,
                        fg_rgba,
                        bg_rgba,
                    );
                }

                // Text decorations: underline, strikethrough, overline
                let has_underline = cell.attrs.underline != UnderlineStyle::None || cell_hovered;
                let has_strikethrough = cell.attrs.strikethrough;
                let has_overline = cell.attrs.overline;

                if has_underline || has_strikethrough || has_overline {
                    let metrics = &self.atlas.metrics;
                    let ul_thick = metrics.underline_thickness.max(1.0) as u32;

                    // Resolve decoration color: underline_color if set, else fg
                    let deco_color = if cell.attrs.underline_color != Color::Default {
                        f32_to_xrgb(self.colors.resolve(cell.attrs.underline_color, true))
                    } else {
                        f32_to_xrgb(fg_rgba)
                    };

                    if has_underline {
                        let ul_y = py + (metrics.baseline + metrics.underline_position) as i32;
                        let ul_style =
                            if cell_hovered && cell.attrs.underline == UnderlineStyle::None {
                                UnderlineStyle::Single
                            } else {
                                cell.attrs.underline
                            };
                        let ul_color = if cell_hovered {
                            f32_to_xrgb(crate::pipeline::LINK_HOVER_FG)
                        } else {
                            deco_color
                        };
                        draw_underline_style(
                            &mut buffer,
                            w,
                            h,
                            ul_style,
                            px,
                            ul_y,
                            cw,
                            ul_thick,
                            ul_color,
                        );
                    }

                    if has_strikethrough {
                        let st_y = py + metrics.strikethrough_position as i32;
                        fill_rect(
                            &mut buffer,
                            w,
                            h,
                            px,
                            st_y,
                            cw,
                            ul_thick,
                            f32_to_xrgb(fg_rgba),
                        );
                    }

                    if has_overline {
                        fill_rect(
                            &mut buffer,
                            w,
                            h,
                            px,
                            py,
                            cw,
                            ul_thick,
                            f32_to_xrgb(fg_rgba),
                        );
                    }
                }

                // Hyperlink underline — thin line under linked cells.
                if cell.attrs.hyperlink_id > 0 {
                    let link_thickness = (cell_h * 0.08).max(1.0) as u32;
                    let link_y = py + cell_h as i32 - link_thickness as i32;
                    let link_color = f32_to_xrgb(fg_rgba);
                    fill_rect(
                        &mut buffer,
                        w,
                        h,
                        px,
                        link_y,
                        cw,
                        link_thickness,
                        link_color,
                    );
                }
            }
        }

        // Draw cursor
        if terminal.grid.cursor.visible && cursor_visible && terminal.cursor_in_viewport() {
            let cursor = &terminal.grid.cursor;
            let cx = (cursor.col as f32 * cell_w) as i32;
            let cy = (cursor.row as f32 * cell_h + tab_bar_h) as i32;
            let cursor_xrgb = f32_to_xrgb(self.colors.cursor);

            match cursor.shape {
                CursorShape::Block => {
                    fill_rect(
                        &mut buffer,
                        w,
                        h,
                        cx,
                        cy,
                        cell_w as u32,
                        cell_h as u32,
                        cursor_xrgb,
                    );
                }
                CursorShape::Underline => {
                    let thickness = (cell_h * 0.1).max(1.0) as u32;
                    fill_rect(
                        &mut buffer,
                        w,
                        h,
                        cx,
                        cy + cell_h as i32 - thickness as i32,
                        cell_w as u32,
                        thickness,
                        cursor_xrgb,
                    );
                }
                CursorShape::Bar => {
                    let thickness = (cell_w * 0.1).max(1.0) as u32;
                    fill_rect(
                        &mut buffer,
                        w,
                        h,
                        cx,
                        cy,
                        thickness,
                        cell_h as u32,
                        cursor_xrgb,
                    );
                }
            }
        }

        // Draw scrollbar
        if show_scrollbar {
            let total_rows = terminal.scrollback.len() + rows;
            let viewport_height = rows as f32 * cell_h;
            let scrollbar_width = 4u32;
            let scrollbar_x = (cols as f32 * cell_w + pad_left) as i32 - scrollbar_width as i32;

            let track_ratio = (rows as f32 / total_rows as f32).clamp(0.05, 1.0);
            let thumb_height = (viewport_height * track_ratio).max(8.0) as u32;

            let scroll_fraction =
                terminal.viewport_offset as f32 / terminal.scrollback.len().max(1) as f32;
            let thumb_y = ((viewport_height - thumb_height as f32) * (1.0 - scroll_fraction)
                + tab_bar_h
                + pad_top) as i32;

            let scrollbar_color = f32_to_xrgb([1.0, 1.0, 1.0, 0.3]);
            fill_rect(
                &mut buffer,
                w,
                h,
                scrollbar_x,
                thumb_y,
                scrollbar_width,
                thumb_height,
                scrollbar_color,
            );
        }

        // Draw inline images (Sixel, iTerm2, Kitty)
        let visible_images = terminal.visible_images(cell_w, cell_h);
        if !visible_images.is_empty() {
            draw_images(&mut buffer, &visible_images, w, h, tab_bar_h);
        }

        // Draw overlay layers (command palette, settings, etc.)
        if !overlays.is_empty() {
            draw_overlays(&mut buffer, overlays, w, h, &mut self.atlas);
        }

        buffer
            .present()
            .map_err(|e| RenderError::SoftbufferPresent(e.to_string()))?;

        Ok(())
    }
}

/// Pre-shape a terminal row for ligature-aware rendering (software path).
///
/// Same logic as `pre_shape_row` in pipeline.rs — groups cells by attribute,
/// shapes each segment, and returns per-column glyph data.
fn pre_shape_row_soft(
    terminal: &Terminal,
    row: usize,
    cols: usize,
    atlas: &mut GlyphAtlas,
) -> Vec<Option<(GlyphEntry, u8)>> {
    if !atlas.ligatures_enabled {
        return vec![None; cols];
    }

    let mut result: Vec<Option<(GlyphEntry, u8)>> = vec![None; cols];

    struct CellRef {
        col: usize,
        ch: char,
    }

    let mut segments: Vec<(Vec<CellRef>, bool, bool)> = Vec::new();
    let mut seg_cells: Vec<CellRef> = Vec::new();
    let mut seg_bold = false;
    let mut seg_italic = false;

    for col in 0..cols {
        if let Some(cell) = terminal.viewport_cell(col, row) {
            if cell.width == 0 {
                continue;
            }
            let ch = cell.content.chars().next().unwrap_or(' ');
            let bold = cell.attrs.bold;
            let italic = cell.attrs.italic;

            if !seg_cells.is_empty() && (bold != seg_bold || italic != seg_italic) {
                segments.push((std::mem::take(&mut seg_cells), seg_bold, seg_italic));
            }

            if seg_cells.is_empty() {
                seg_bold = bold;
                seg_italic = italic;
            }
            seg_cells.push(CellRef { col, ch });
        }
    }
    if !seg_cells.is_empty() {
        segments.push((seg_cells, seg_bold, seg_italic));
    }

    for (cells, bold, italic) in &segments {
        let text: String = cells.iter().map(|c| c.ch).collect();
        let clusters = atlas.shape_run(&text, *bold, *italic);

        let mut ci = 0;
        for cluster in &clusters {
            if ci < cells.len() {
                let col = cells[ci].col;
                result[col] = Some((cluster.glyph, cluster.cell_span));
                for skip in 1..cluster.cell_span as usize {
                    if ci + skip < cells.len() {
                        result[cells[ci + skip].col] = Some((cluster.glyph, 0));
                    }
                }
                ci += cluster.cell_span.max(1) as usize;
            }
        }
    }

    result
}

/// Draw the tab bar strip at the top of the framebuffer (free function to avoid borrow conflicts).
fn draw_tab_bar(
    buffer: &mut [u32],
    items: &[TabBarItem],
    buf_w: u32,
    buf_h: u32,
    atlas: &mut GlyphAtlas,
    colors: &ColorScheme,
) {
    if items.is_empty() {
        return;
    }

    let cell_w = atlas.metrics.cell_width;
    let cell_h = atlas.metrics.cell_height;
    let tab_count = items.len();
    let total_width = buf_w as f32;
    let tab_width = total_width / tab_count as f32;

    // Color palette derived from terminal colors
    let bar_bg = lerp(colors.bg, colors.fg, 0.08);
    let active_bg = colors.bg;
    let inactive_bg = lerp(colors.bg, colors.fg, 0.12);
    let active_fg = colors.fg;
    let inactive_fg = lerp(colors.bg, colors.fg, 0.55);
    let divider_color = lerp(colors.bg, colors.fg, 0.20);

    // Full-width bar background
    fill_rect(
        buffer,
        buf_w,
        buf_h,
        0,
        0,
        buf_w,
        cell_h as u32,
        f32_to_xrgb(bar_bg),
    );

    for (i, item) in items.iter().enumerate() {
        let tab_x = (i as f32 * tab_width) as i32;
        let bg = if item.is_active {
            active_bg
        } else {
            inactive_bg
        };
        let fg_color = if item.is_active {
            active_fg
        } else {
            inactive_fg
        };

        // Tab background
        fill_rect(
            buffer,
            buf_w,
            buf_h,
            tab_x + 1,
            0,
            (tab_width - 2.0) as u32,
            cell_h as u32,
            f32_to_xrgb(bg),
        );

        // Active tab accent line
        if item.is_active {
            let accent = lerp(colors.fg, [0.3, 0.6, 1.0, 1.0], 0.5);
            fill_rect(
                buffer,
                buf_w,
                buf_h,
                tab_x + 1,
                (cell_h - 2.0) as i32,
                (tab_width - 2.0) as u32,
                2,
                f32_to_xrgb(accent),
            );
        }

        // Tab title text
        let max_chars = ((tab_width - 2.0 * cell_w) / cell_w).floor().max(0.0) as usize;
        let text_x = tab_x + cell_w as i32;

        for (ci, ch) in item.title.chars().take(max_chars).enumerate() {
            let glyph = atlas.get_or_insert(GlyphKey {
                ch,
                bold: item.is_active,
                italic: false,
            });

            if glyph.width > 0 && glyph.height > 0 {
                let cx = text_x + (ci as f32 * cell_w) as i32;
                blit_glyph(
                    buffer,
                    buf_w,
                    buf_h,
                    atlas.data(),
                    atlas.width,
                    &glyph,
                    cx,
                    0,
                    fg_color,
                    bg,
                );
            }
        }

        // Divider between tabs
        if i + 1 < tab_count {
            let div_x = (tab_x as f32 + tab_width - 1.0) as i32;
            fill_rect(
                buffer,
                buf_w,
                buf_h,
                div_x,
                4,
                1,
                (cell_h - 8.0).max(1.0) as u32,
                f32_to_xrgb(divider_color),
            );
        }
    }
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

/// Render an underline with the appropriate style (single, double, curly, dotted, dashed).
#[allow(clippy::too_many_arguments)]
fn draw_underline_style(
    buffer: &mut [u32],
    buf_w: u32,
    buf_h: u32,
    style: UnderlineStyle,
    x: i32,
    y: i32,
    w: u32,
    thickness: u32,
    color: u32,
) {
    match style {
        UnderlineStyle::None => {}
        UnderlineStyle::Single => {
            fill_rect(buffer, buf_w, buf_h, x, y, w, thickness, color);
        }
        UnderlineStyle::Double => {
            let gap = (thickness as i32 * 3 / 2).max(2);
            fill_rect(buffer, buf_w, buf_h, x, y, w, thickness, color);
            fill_rect(buffer, buf_w, buf_h, x, y + gap, w, thickness, color);
        }
        UnderlineStyle::Curly => {
            let segments = (w / thickness.max(1)).max(4);
            let seg_w = w / segments;
            let amplitude = thickness as f32 * 1.5;
            for s in 0..segments {
                let sx = x + (s * seg_w) as i32;
                let phase = std::f32::consts::PI * 2.0 * s as f32 / segments as f32;
                let sy = y + (phase.sin() * amplitude) as i32;
                fill_rect(buffer, buf_w, buf_h, sx, sy, seg_w.max(1), thickness, color);
            }
        }
        UnderlineStyle::Dotted => {
            let dot_w = thickness.max(1);
            let gap = dot_w;
            let mut dx = x;
            let x_end = x + w as i32;
            while dx < x_end {
                let dw = dot_w.min((x_end - dx) as u32);
                fill_rect(buffer, buf_w, buf_h, dx, y, dw, thickness, color);
                dx += (dot_w + gap) as i32;
            }
        }
        UnderlineStyle::Dashed => {
            let dash_w = (thickness * 4).max(3);
            let gap = (thickness * 2).max(2);
            let mut dx = x;
            let x_end = x + w as i32;
            while dx < x_end {
                let dw = dash_w.min((x_end - dx) as u32);
                fill_rect(buffer, buf_w, buf_h, dx, y, dw, thickness, color);
                dx += (dash_w + gap) as i32;
            }
        }
    }
}

/// Convert [f32; 4] RGBA to XRGB u32 (0x00RRGGBB).
fn f32_to_xrgb(c: [f32; 4]) -> u32 {
    let r = (c[0] * 255.0).clamp(0.0, 255.0) as u32;
    let g = (c[1] * 255.0).clamp(0.0, 255.0) as u32;
    let b = (c[2] * 255.0).clamp(0.0, 255.0) as u32;
    (r << 16) | (g << 8) | b
}

/// Fill a rectangle in the pixel buffer with a solid color.
#[allow(clippy::too_many_arguments)]
fn fill_rect(
    buffer: &mut [u32],
    buf_w: u32,
    buf_h: u32,
    x: i32,
    y: i32,
    w: u32,
    h: u32,
    color: u32,
) {
    let x0 = x.max(0) as u32;
    let y0 = y.max(0) as u32;
    let x1 = ((x as i64 + w as i64) as u32).min(buf_w);
    let y1 = ((y as i64 + h as i64) as u32).min(buf_h);

    for row in y0..y1 {
        let row_start = (row * buf_w) as usize;
        for col in x0..x1 {
            let idx = row_start + col as usize;
            if idx < buffer.len() {
                buffer[idx] = color;
            }
        }
    }
}

/// Alpha-blend a glyph from the atlas onto the pixel buffer.
#[allow(clippy::too_many_arguments)]
fn blit_glyph(
    buffer: &mut [u32],
    buf_w: u32,
    buf_h: u32,
    atlas_data: &[u8],
    atlas_w: u32,
    glyph: &crate::atlas::GlyphEntry,
    dst_x: i32,
    dst_y: i32,
    fg: [f32; 4],
    bg: [f32; 4],
) {
    let fg_r = (fg[0] * 255.0) as u32;
    let fg_g = (fg[1] * 255.0) as u32;
    let fg_b = (fg[2] * 255.0) as u32;
    let bg_r = (bg[0] * 255.0) as u32;
    let bg_g = (bg[1] * 255.0) as u32;
    let bg_b = (bg[2] * 255.0) as u32;

    for row in 0..glyph.height {
        let py = dst_y + row as i32;
        if py < 0 || py >= buf_h as i32 {
            continue;
        }

        let buf_row = (py as u32 * buf_w) as usize;
        let atlas_row = ((glyph.atlas_y + row) * atlas_w + glyph.atlas_x) as usize;

        for col in 0..glyph.width {
            let px = dst_x + col as i32;
            if px < 0 || px >= buf_w as i32 {
                continue;
            }

            let atlas_idx = atlas_row + col as usize;
            if atlas_idx >= atlas_data.len() {
                continue;
            }

            let alpha = atlas_data[atlas_idx] as u32;
            if alpha == 0 {
                continue; // Fully transparent — skip
            }

            let buf_idx = buf_row + px as usize;
            if buf_idx >= buffer.len() {
                continue;
            }

            if alpha == 255 {
                // Fully opaque — just write fg
                buffer[buf_idx] = (fg_r << 16) | (fg_g << 8) | fg_b;
            } else {
                // Alpha blend: output = fg * alpha + bg * (255 - alpha)
                let inv = 255 - alpha;
                let r = (fg_r * alpha + bg_r * inv) / 255;
                let g = (fg_g * alpha + bg_g * inv) / 255;
                let b = (fg_b * alpha + bg_b * inv) / 255;
                buffer[buf_idx] = (r << 16) | (g << 8) | b;
            }
        }
    }
}

/// Draw inline images (Sixel, iTerm2, Kitty) onto the framebuffer.
///
/// Each image's RGBA pixels are alpha-blended onto the buffer at the correct
/// viewport position. Images that extend outside the viewport are clipped.
fn draw_images(
    buffer: &mut [u32],
    images: &[wixen_core::ViewportImage<'_>],
    buf_w: u32,
    buf_h: u32,
    tab_bar_h: f32,
) {
    for img in images {
        let dst_x0 = img.px_x as i32;
        let dst_y0 = img.px_y as i32 + tab_bar_h as i32;

        for src_y in 0..img.height {
            let py = dst_y0 + src_y as i32;
            if py < 0 || py >= buf_h as i32 {
                continue;
            }
            let buf_row = py as usize * buf_w as usize;

            for src_x in 0..img.width {
                let px = dst_x0 + src_x as i32;
                if px < 0 || px >= buf_w as i32 {
                    continue;
                }

                let src_offset = ((src_y * img.width + src_x) * 4) as usize;
                if src_offset + 3 >= img.pixels.len() {
                    continue;
                }

                let sr = img.pixels[src_offset] as u32;
                let sg = img.pixels[src_offset + 1] as u32;
                let sb = img.pixels[src_offset + 2] as u32;
                let sa = img.pixels[src_offset + 3] as u32;

                if sa == 0 {
                    continue;
                }

                let buf_idx = buf_row + px as usize;
                if buf_idx >= buffer.len() {
                    continue;
                }

                if sa == 255 {
                    buffer[buf_idx] = (sr << 16) | (sg << 8) | sb;
                } else {
                    // Alpha-blend over existing pixel
                    let dst = buffer[buf_idx];
                    let dr = (dst >> 16) & 0xFF;
                    let dg = (dst >> 8) & 0xFF;
                    let db = dst & 0xFF;
                    let inv = 255 - sa;
                    let r = (sr * sa + dr * inv) / 255;
                    let g = (sg * sa + dg * inv) / 255;
                    let b = (sb * sa + db * inv) / 255;
                    buffer[buf_idx] = (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}

/// Draw overlay layers (command palette, settings panel, etc.) on the framebuffer.
fn draw_overlays(
    buffer: &mut [u32],
    overlays: &[crate::pipeline::OverlayLayer],
    buf_w: u32,
    buf_h: u32,
    atlas: &mut GlyphAtlas,
) {
    let cell_w = atlas.metrics.cell_width;
    let cell_h = atlas.metrics.cell_height;

    for overlay in overlays {
        // Draw the overlay background rectangle
        let bg_xrgb = f32_to_xrgb(overlay.bg);
        fill_rect(
            buffer,
            buf_w,
            buf_h,
            overlay.x as i32,
            overlay.y as i32,
            overlay.width as u32,
            overlay.height as u32,
            bg_xrgb,
        );

        // Draw each line of text
        for (li, line) in overlay.lines.iter().enumerate() {
            let line_y = overlay.y + li as f32 * cell_h;

            // Per-line background highlight (e.g., selected item in palette)
            if let Some(line_bg) = line.bg {
                fill_rect(
                    buffer,
                    buf_w,
                    buf_h,
                    overlay.x as i32,
                    line_y as i32,
                    overlay.width as u32,
                    cell_h as u32,
                    f32_to_xrgb(line_bg),
                );
            }

            let fg_color = line.fg;
            let bg_color = line.bg.unwrap_or(overlay.bg);

            // Render each character
            for (ci, ch) in line.text.chars().enumerate() {
                if ch <= ' ' {
                    continue;
                }

                let glyph = atlas.get_or_insert(GlyphKey {
                    ch,
                    bold: line.bold,
                    italic: false,
                });

                if glyph.width > 0 && glyph.height > 0 {
                    let cx = overlay.x as i32 + (ci as f32 * cell_w) as i32;
                    blit_glyph(
                        buffer,
                        buf_w,
                        buf_h,
                        atlas.data(),
                        atlas.width,
                        &glyph,
                        cx,
                        line_y as i32,
                        fg_color,
                        bg_color,
                    );
                }
            }
        }
    }
}

/// Linear interpolation between two RGBA colors.
fn lerp(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
    [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
        a[3] + (b[3] - a[3]) * t,
    ]
}

/// Convert a softbuffer XRGB `u32` buffer into RGBA `u8` pixel data for CPU blur.
fn xrgb_buffer_to_rgba(buffer: &[u32], width: u32, height: u32) -> Vec<u8> {
    let len = (width * height) as usize;
    let mut rgba = Vec::with_capacity(len * 4);
    for &pixel in &buffer[..len] {
        let r = ((pixel >> 16) & 0xFF) as u8;
        let g = ((pixel >> 8) & 0xFF) as u8;
        let b = (pixel & 0xFF) as u8;
        rgba.extend_from_slice(&[r, g, b, 255]);
    }
    rgba
}

/// Write RGBA `u8` pixel data back into a softbuffer XRGB `u32` buffer.
fn write_rgba_to_xrgb_buffer(buffer: &mut [u32], rgba: &[u8], width: u32, height: u32) {
    let len = (width * height) as usize;
    for (i, pixel) in buffer.iter_mut().enumerate().take(len) {
        let base = i * 4;
        let r = rgba[base] as u32;
        let g = rgba[base + 1] as u32;
        let b = rgba[base + 2] as u32;
        *pixel = (r << 16) | (g << 8) | b;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: create a ViewportImage from RGBA pixel data.
    fn make_viewport_image(
        pixels: &[u8],
        width: u32,
        height: u32,
        px_x: f32,
        px_y: f32,
    ) -> wixen_core::ViewportImage<'_> {
        wixen_core::ViewportImage {
            pixels,
            width,
            height,
            px_x,
            px_y,
        }
    }

    #[test]
    fn test_draw_images_opaque_pixel() {
        // 1x1 red opaque image at (0,0), no tab bar
        let pixels = [255, 0, 0, 255]; // R=255, G=0, B=0, A=255
        let img = make_viewport_image(&pixels, 1, 1, 0.0, 0.0);
        let mut buffer = vec![0u32; 4]; // 2x2 framebuffer
        draw_images(&mut buffer, &[img], 2, 2, 0.0);
        // Pixel at (0,0) should be red: 0x00FF0000
        assert_eq!(buffer[0], 0x00FF0000);
        // Other pixels untouched
        assert_eq!(buffer[1], 0);
    }

    #[test]
    fn test_draw_images_transparent_pixel() {
        // 1x1 fully transparent image — should not modify buffer
        let pixels = [255, 0, 0, 0]; // A=0
        let img = make_viewport_image(&pixels, 1, 1, 0.0, 0.0);
        let mut buffer = vec![0xFFFFFF; 4]; // 2x2 white
        draw_images(&mut buffer, &[img], 2, 2, 0.0);
        assert_eq!(buffer[0], 0xFFFFFF); // Unchanged
    }

    #[test]
    fn test_draw_images_alpha_blend() {
        // 1x1 red at 50% alpha over white background
        let pixels = [255, 0, 0, 128]; // R=255, A=128
        let img = make_viewport_image(&pixels, 1, 1, 0.0, 0.0);
        let mut buffer = vec![0x00FFFFFF; 1]; // 1x1 white
        draw_images(&mut buffer, &[img], 1, 1, 0.0);
        let r = (buffer[0] >> 16) & 0xFF;
        let g = (buffer[0] >> 8) & 0xFF;
        let b = buffer[0] & 0xFF;
        // r = (255*128 + 255*127)/255 ≈ 255, g = (0*128 + 255*127)/255 ≈ 127
        assert!(r > 200, "r={r}"); // Red channel stays high
        assert!(g > 100 && g < 140, "g={g}"); // Green blended
        assert!(b > 100 && b < 140, "b={b}"); // Blue blended
    }

    #[test]
    fn test_draw_images_position_offset() {
        // 1x1 green image at pixel position (1,1) in a 3x3 buffer
        let pixels = [0, 255, 0, 255]; // Green, opaque
        let img = make_viewport_image(&pixels, 1, 1, 1.0, 1.0);
        let mut buffer = vec![0u32; 9]; // 3x3
        draw_images(&mut buffer, &[img], 3, 3, 0.0);
        // Only pixel at (1,1) = index 4 should be green
        assert_eq!(buffer[4], 0x0000FF00);
        assert_eq!(buffer[0], 0);
        assert_eq!(buffer[3], 0);
    }

    #[test]
    fn test_draw_images_tab_bar_offset() {
        // 1x1 blue image at (0,0) with tab_bar_h = 2 pixels
        let pixels = [0, 0, 255, 255]; // Blue, opaque
        let img = make_viewport_image(&pixels, 1, 1, 0.0, 0.0);
        let mut buffer = vec![0u32; 12]; // 3x4
        draw_images(&mut buffer, &[img], 3, 4, 2.0);
        // Should be at pixel row 2, col 0 = index 6
        assert_eq!(buffer[6], 0x000000FF);
        assert_eq!(buffer[0], 0); // Row 0 untouched
        assert_eq!(buffer[3], 0); // Row 1 untouched
    }

    #[test]
    fn test_draw_images_clipping() {
        // 2x2 image placed at (-1, -1) — only bottom-right pixel visible
        let pixels = [
            255, 0, 0, 255, // (0,0) red
            0, 255, 0, 255, // (1,0) green
            0, 0, 255, 255, // (0,1) blue
            255, 255, 0, 255, // (1,1) yellow
        ];
        let img = make_viewport_image(&pixels, 2, 2, -1.0, -1.0);
        let mut buffer = vec![0u32; 4]; // 2x2
        draw_images(&mut buffer, &[img], 2, 2, 0.0);
        // Only (1,1) of the image maps to (0,0) of the buffer
        assert_eq!(buffer[0], 0x00FFFF00); // Yellow
        assert_eq!(buffer[1], 0); // Untouched
    }

    #[test]
    fn test_draw_images_multi_pixel() {
        // 2x2 RGBA image at (0,0)
        let pixels = [
            255, 0, 0, 255, // (0,0) red
            0, 255, 0, 255, // (1,0) green
            0, 0, 255, 255, // (0,1) blue
            255, 255, 255, 255, // (1,1) white
        ];
        let img = make_viewport_image(&pixels, 2, 2, 0.0, 0.0);
        let mut buffer = vec![0u32; 4]; // 2x2
        draw_images(&mut buffer, &[img], 2, 2, 0.0);
        assert_eq!(buffer[0], 0x00FF0000); // Red
        assert_eq!(buffer[1], 0x0000FF00); // Green
        assert_eq!(buffer[2], 0x000000FF); // Blue
        assert_eq!(buffer[3], 0x00FFFFFF); // White
    }

    #[test]
    fn test_draw_images_empty() {
        let mut buffer = vec![0xABCDEFu32; 4];
        draw_images(&mut buffer, &[], 2, 2, 0.0);
        // Nothing should change
        assert!(buffer.iter().all(|&p| p == 0xABCDEF));
    }

    #[test]
    fn xrgb_to_rgba_roundtrip() {
        // 2x2 buffer with known XRGB values
        let buffer: Vec<u32> = vec![0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF];
        let rgba = xrgb_buffer_to_rgba(&buffer, 2, 2);
        assert_eq!(rgba.len(), 16);
        // First pixel: red
        assert_eq!(&rgba[0..4], &[255, 0, 0, 255]);
        // Second pixel: green
        assert_eq!(&rgba[4..8], &[0, 255, 0, 255]);
        // Third pixel: blue
        assert_eq!(&rgba[8..12], &[0, 0, 255, 255]);

        // Round-trip back to XRGB
        let mut out = vec![0u32; 4];
        write_rgba_to_xrgb_buffer(&mut out, &rgba, 2, 2);
        assert_eq!(out, buffer);
    }

    #[test]
    fn blur_radius_zero_preserves_buffer() {
        // Verify that apply_cpu_blur with radius 0 is a no-op (identity).
        let mut rgba = vec![128u8; 4 * 4 * 4]; // 4x4 solid gray
        let original = rgba.clone();
        crate::blur::apply_cpu_blur(&mut rgba, 4, 4, 0);
        assert_eq!(rgba, original);
    }
}
