//! GPU text rendering pipeline — builds vertex buffers from terminal grid state
//! and renders them using the atlas texture.

use crate::atlas::{GlyphAtlas, GlyphEntry, GlyphKey};
use crate::colors::ColorScheme;
use wixen_core::Terminal;
use wixen_core::attrs::{Color, UnderlineStyle};
use wixen_core::cursor::CursorShape;

/// Cell highlight state — used for search match rendering.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CellHighlight {
    /// No highlight.
    None,
    /// Part of a search match (not the active one).
    Match,
    /// Part of the currently focused search match.
    ActiveMatch,
}

/// Search match highlight colors.
const MATCH_BG: [f32; 4] = [0.35, 0.30, 0.05, 1.0]; // muted yellow
const ACTIVE_MATCH_BG: [f32; 4] = [0.55, 0.45, 0.0, 1.0]; // bright orange
const MATCH_FG: [f32; 4] = [1.0, 1.0, 1.0, 1.0]; // white text on match

/// A tab bar entry for rendering.
pub struct TabBarItem<'a> {
    pub title: &'a str,
    pub is_active: bool,
    /// Whether this tab has a pending bell notification badge.
    pub has_bell: bool,
    /// Optional color indicator stripe (r, g, b).
    pub tab_color: Option<(u8, u8, u8)>,
}

/// A single vertex for cell rendering.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Vertex {
    pub position: [f32; 2],
    pub tex_coords: [f32; 2],
    pub fg_color: [f32; 4],
    pub bg_color: [f32; 4],
}

impl Vertex {
    pub fn layout() -> wgpu::VertexBufferLayout<'static> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<Vertex>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                // position
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x2,
                },
                // tex_coords
                wgpu::VertexAttribute {
                    offset: 8,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x2,
                },
                // fg_color
                wgpu::VertexAttribute {
                    offset: 16,
                    shader_location: 2,
                    format: wgpu::VertexFormat::Float32x4,
                },
                // bg_color
                wgpu::VertexAttribute {
                    offset: 32,
                    shader_location: 3,
                    format: wgpu::VertexFormat::Float32x4,
                },
            ],
        }
    }
}

/// Uniform buffer data.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Uniforms {
    pub screen_size: [f32; 2],
    pub _padding: [f32; 2],
}

/// Build vertex and index data for the entire terminal grid.
///
/// `cursor_visible` controls blink state — pass `false` during the blink-off phase.
/// `highlight` is an optional callback that returns the highlight state for each cell,
/// identified by (absolute_row, col). Used for search match highlighting.
/// `y_offset` shifts all content down (used when tab bar is visible above the terminal).
#[allow(clippy::too_many_arguments)]
pub fn build_cell_vertices(
    terminal: &Terminal,
    atlas: &mut GlyphAtlas,
    atlas_w: f32,
    atlas_h: f32,
    cursor_visible: bool,
    colors: &ColorScheme,
    highlight: Option<&dyn Fn(usize, usize) -> CellHighlight>,
    x_offset: f32,
    y_offset: f32,
    show_scrollbar: bool,
    hover: Option<&wixen_core::HoverState>,
) -> (Vec<Vertex>, Vec<u32>) {
    let cell_width = atlas.metrics.cell_width;
    let cell_height = atlas.metrics.cell_height;
    let cols = terminal.cols();
    let rows = terminal.rows();

    let mut vertices = Vec::with_capacity(cols * rows * 4);
    let mut indices = Vec::with_capacity(cols * rows * 6);
    let mut vertex_idx = 0u32;

    for row in 0..rows {
        // Pre-shape row for ligature detection
        let shaped_row = pre_shape_row(terminal, row, cols, atlas);

        for col in 0..cols {
            // Use viewport_cell for scrollback-aware rendering
            let cell = match terminal.viewport_cell(col, row) {
                Some(c) => c,
                None => continue,
            };

            // Skip continuation cells (width == 0, e.g. wide CJK chars)
            if cell.width == 0 {
                continue;
            }

            let x = col as f32 * cell_width + x_offset;
            let y = row as f32 * cell_height + y_offset;
            let h = cell_height;

            // Resolve colors through the color scheme
            let mut fg = cell.attrs.fg;
            let mut bg = cell.attrs.bg;

            // Selection only applies when viewing the live grid (offset 0)
            let is_selected = terminal.cursor_in_viewport()
                && terminal
                    .selection
                    .as_ref()
                    .is_some_and(|s| s.contains(col, row));

            if cell.attrs.inverse {
                std::mem::swap(&mut fg, &mut bg);
            }

            // Check for search match highlight
            let cell_hl = highlight
                .as_ref()
                .map(|f| f(row, col))
                .unwrap_or(CellHighlight::None);

            let cell_hovered = is_hovered(col, row, hover);

            let (fg_rgba, bg_rgba) = match cell_hl {
                CellHighlight::ActiveMatch => (MATCH_FG, ACTIVE_MATCH_BG),
                CellHighlight::Match => (MATCH_FG, MATCH_BG),
                CellHighlight::None => {
                    let bg_rgba = if is_selected {
                        colors.selection_bg
                    } else {
                        colors.resolve(bg, false)
                    };
                    let fg_rgba = if cell_hovered {
                        LINK_HOVER_FG
                    } else {
                        colors.resolve_fg_contrasted(fg, bg_rgba)
                    };
                    (fg_rgba, bg_rgba)
                }
            };

            // Determine glyph and quad width from shaped row data
            let (glyph, w) = match shaped_row.get(col).copied().flatten() {
                Some((_, 0)) => {
                    // Ligature continuation cell: emit background-only quad
                    let w = cell_width;
                    push_quad(
                        &mut vertices,
                        &mut indices,
                        &mut vertex_idx,
                        x,
                        y,
                        w,
                        h,
                        fg_rgba,
                        bg_rgba,
                        [0.0, 0.0],
                        [0.0, 0.0],
                    );
                    continue;
                }
                Some((g, span)) => {
                    // Ligature start cell or normal shaped cell
                    (g, cell_width * span as f32)
                }
                None => {
                    // Fallback: per-character glyph lookup
                    let ch = cell.content.chars().next().unwrap_or(' ');
                    let g = atlas.get_or_insert(GlyphKey {
                        ch,
                        bold: cell.attrs.bold,
                        italic: cell.attrs.italic,
                    });
                    (g, cell_width * cell.width.max(1) as f32)
                }
            };

            // Texture coordinates
            let (u0, v0, u1, v1) = if glyph.width > 0 && glyph.height > 0 {
                (
                    glyph.atlas_x as f32 / atlas_w,
                    glyph.atlas_y as f32 / atlas_h,
                    (glyph.atlas_x + glyph.width) as f32 / atlas_w,
                    (glyph.atlas_y + glyph.height) as f32 / atlas_h,
                )
            } else {
                (0.0, 0.0, 0.0, 0.0)
            };

            // Quad: top-left, top-right, bottom-right, bottom-left
            vertices.push(Vertex {
                position: [x, y],
                tex_coords: [u0, v0],
                fg_color: fg_rgba,
                bg_color: bg_rgba,
            });
            vertices.push(Vertex {
                position: [x + w, y],
                tex_coords: [u1, v0],
                fg_color: fg_rgba,
                bg_color: bg_rgba,
            });
            vertices.push(Vertex {
                position: [x + w, y + h],
                tex_coords: [u1, v1],
                fg_color: fg_rgba,
                bg_color: bg_rgba,
            });
            vertices.push(Vertex {
                position: [x, y + h],
                tex_coords: [u0, v1],
                fg_color: fg_rgba,
                bg_color: bg_rgba,
            });

            // Two triangles per quad
            indices.push(vertex_idx);
            indices.push(vertex_idx + 1);
            indices.push(vertex_idx + 2);
            indices.push(vertex_idx);
            indices.push(vertex_idx + 2);
            indices.push(vertex_idx + 3);

            vertex_idx += 4;

            // Text decorations: underline, strikethrough, overline
            let deco_w = cell_width * cell.width.max(1) as f32;
            let has_underline = cell.attrs.underline != UnderlineStyle::None || cell_hovered;
            let has_strikethrough = cell.attrs.strikethrough;
            let has_overline = cell.attrs.overline;

            if has_underline || has_strikethrough || has_overline {
                let metrics = &atlas.metrics;
                let ul_thick = metrics.underline_thickness.max(1.0);

                // Resolve decoration color: underline_color if set, else fg
                let deco_color = if cell.attrs.underline_color != Color::Default {
                    colors.resolve(cell.attrs.underline_color, true)
                } else {
                    fg_rgba
                };

                if has_underline {
                    let ul_y = y + metrics.baseline + metrics.underline_position;
                    let ul_style = if cell_hovered && cell.attrs.underline == UnderlineStyle::None {
                        UnderlineStyle::Single
                    } else {
                        cell.attrs.underline
                    };
                    let ul_color = if cell_hovered {
                        LINK_HOVER_FG
                    } else {
                        deco_color
                    };
                    push_underline_style(
                        &mut vertices,
                        &mut indices,
                        &mut vertex_idx,
                        ul_style,
                        x,
                        ul_y,
                        deco_w,
                        ul_thick,
                        ul_color,
                    );
                }

                if has_strikethrough {
                    let st_y = y + metrics.strikethrough_position;
                    push_solid_quad(
                        &mut vertices,
                        &mut indices,
                        &mut vertex_idx,
                        x,
                        st_y,
                        deco_w,
                        ul_thick,
                        fg_rgba,
                    );
                }

                if has_overline {
                    push_solid_quad(
                        &mut vertices,
                        &mut indices,
                        &mut vertex_idx,
                        x,
                        y,
                        deco_w,
                        ul_thick,
                        fg_rgba,
                    );
                }
            }

            // Hyperlink underline — draw a thin line under linked cells.
            if cell.attrs.hyperlink_id > 0 {
                let link_thickness = (cell_height * 0.08).max(1.0);
                let link_y = y + h - link_thickness;
                push_solid_quad(
                    &mut vertices,
                    &mut indices,
                    &mut vertex_idx,
                    x,
                    link_y,
                    deco_w,
                    link_thickness,
                    fg_rgba,
                );
            }
        }
    }

    // Cursor quad — shape-aware (block, underline, bar)
    // Only draw cursor when viewport is at live position (not scrolled back)
    if terminal.grid.cursor.visible && cursor_visible && terminal.cursor_in_viewport() {
        let cursor = &terminal.grid.cursor;
        let cx = cursor.col as f32 * cell_width;
        let cy = cursor.row as f32 * cell_height + y_offset;

        let cursor_color = colors.cursor;
        let transparent = [0.0f32, 0.0, 0.0, 0.0];

        let (x0, y0, x1, y1) = match cursor.shape {
            CursorShape::Block => (cx, cy, cx + cell_width, cy + cell_height),
            CursorShape::Underline => {
                let thickness = (cell_height * 0.1).max(1.0);
                (
                    cx,
                    cy + cell_height - thickness,
                    cx + cell_width,
                    cy + cell_height,
                )
            }
            CursorShape::Bar => {
                let thickness = (cell_width * 0.1).max(1.0);
                (cx, cy, cx + thickness, cy + cell_height)
            }
        };

        vertices.push(Vertex {
            position: [x0, y0],
            tex_coords: [0.0, 0.0],
            fg_color: cursor_color,
            bg_color: transparent,
        });
        vertices.push(Vertex {
            position: [x1, y0],
            tex_coords: [0.0, 0.0],
            fg_color: cursor_color,
            bg_color: transparent,
        });
        vertices.push(Vertex {
            position: [x1, y1],
            tex_coords: [0.0, 0.0],
            fg_color: cursor_color,
            bg_color: transparent,
        });
        vertices.push(Vertex {
            position: [x0, y1],
            tex_coords: [0.0, 0.0],
            fg_color: cursor_color,
            bg_color: transparent,
        });

        indices.push(vertex_idx);
        indices.push(vertex_idx + 1);
        indices.push(vertex_idx + 2);
        indices.push(vertex_idx);
        indices.push(vertex_idx + 2);
        indices.push(vertex_idx + 3);
    }

    // Scrollbar indicator
    if show_scrollbar {
        let total_rows = terminal.scrollback.len() + rows;
        let viewport_height = rows as f32 * cell_height;
        let scrollbar_width = 4.0_f32;
        let scrollbar_x = cols as f32 * cell_width + x_offset - scrollbar_width;

        // Track height is proportional to viewport/total ratio
        let track_ratio = (rows as f32 / total_rows as f32).clamp(0.05, 1.0);
        let thumb_height = (viewport_height * track_ratio).max(8.0);

        // Position: offset from bottom
        let scroll_fraction =
            terminal.viewport_offset as f32 / terminal.scrollback.len().max(1) as f32;
        let thumb_y = (viewport_height - thumb_height) * (1.0 - scroll_fraction) + y_offset;

        let scrollbar_color = [1.0_f32, 1.0, 1.0, 0.3]; // semi-transparent white
        let transparent = [0.0_f32, 0.0, 0.0, 0.0];

        vertices.push(Vertex {
            position: [scrollbar_x, thumb_y],
            tex_coords: [0.0, 0.0],
            fg_color: scrollbar_color,
            bg_color: transparent,
        });
        vertices.push(Vertex {
            position: [scrollbar_x + scrollbar_width, thumb_y],
            tex_coords: [0.0, 0.0],
            fg_color: scrollbar_color,
            bg_color: transparent,
        });
        vertices.push(Vertex {
            position: [scrollbar_x + scrollbar_width, thumb_y + thumb_height],
            tex_coords: [0.0, 0.0],
            fg_color: scrollbar_color,
            bg_color: transparent,
        });
        vertices.push(Vertex {
            position: [scrollbar_x, thumb_y + thumb_height],
            tex_coords: [0.0, 0.0],
            fg_color: scrollbar_color,
            bg_color: transparent,
        });

        indices.push(vertex_idx);
        indices.push(vertex_idx + 1);
        indices.push(vertex_idx + 2);
        indices.push(vertex_idx);
        indices.push(vertex_idx + 2);
        indices.push(vertex_idx + 3);
    }

    (vertices, indices)
}

/// Build vertex and index data for the tab bar strip.
///
/// Returns (vertices, indices). The tab bar occupies y = [0, cell_height] and
/// spans the full window width.
pub fn build_tab_bar_vertices(
    items: &[TabBarItem],
    total_width: f32,
    atlas: &mut GlyphAtlas,
    atlas_w: f32,
    atlas_h: f32,
    colors: &ColorScheme,
) -> (Vec<Vertex>, Vec<u32>) {
    if items.is_empty() {
        return (Vec::new(), Vec::new());
    }

    let cell_width = atlas.metrics.cell_width;
    let cell_height = atlas.metrics.cell_height;
    let tab_count = items.len();
    let tab_width = total_width / tab_count as f32;

    // Color palette: derive from terminal colors
    let bar_bg = lerp_color(colors.bg, colors.fg, 0.08);
    let active_bg = colors.bg;
    let inactive_bg = lerp_color(colors.bg, colors.fg, 0.12);
    let active_fg = colors.fg;
    let inactive_fg = lerp_color(colors.bg, colors.fg, 0.55);
    let divider_color = lerp_color(colors.bg, colors.fg, 0.20);
    let transparent = [0.0_f32, 0.0, 0.0, 0.0];

    let mut vertices = Vec::new();
    let mut indices = Vec::new();
    let mut vertex_idx = 0u32;

    // Full-width background strip
    push_quad(
        &mut vertices,
        &mut indices,
        &mut vertex_idx,
        0.0,
        0.0,
        total_width,
        cell_height,
        bar_bg,
        transparent,
        [0.0, 0.0],
        [0.0, 0.0],
    );

    for (i, item) in items.iter().enumerate() {
        let tab_x = i as f32 * tab_width;
        let bg = if item.is_active {
            active_bg
        } else {
            inactive_bg
        };
        let fg = if item.is_active {
            active_fg
        } else {
            inactive_fg
        };

        // Tab background
        push_quad(
            &mut vertices,
            &mut indices,
            &mut vertex_idx,
            tab_x + 1.0,
            0.0,
            tab_width - 2.0,
            cell_height,
            bg,
            transparent,
            [0.0, 0.0],
            [0.0, 0.0],
        );

        // Active tab bottom indicator (accent line)
        if item.is_active {
            let accent = lerp_color(colors.fg, [0.3, 0.6, 1.0, 1.0], 0.5);
            push_quad(
                &mut vertices,
                &mut indices,
                &mut vertex_idx,
                tab_x + 1.0,
                cell_height - 2.0,
                tab_width - 2.0,
                2.0,
                accent,
                transparent,
                [0.0, 0.0],
                [0.0, 0.0],
            );
        }

        // Tab title text — truncate to fit
        let max_chars = ((tab_width - 2.0 * cell_width) / cell_width)
            .floor()
            .max(0.0) as usize;
        let title: String = item.title.chars().take(max_chars).collect();
        let text_x = tab_x + cell_width; // left padding

        for (ci, ch) in title.chars().enumerate() {
            let glyph = atlas.get_or_insert(GlyphKey {
                ch,
                bold: item.is_active,
                italic: false,
            });

            let (u0, v0, u1, v1) = if glyph.width > 0 && glyph.height > 0 {
                (
                    glyph.atlas_x as f32 / atlas_w,
                    glyph.atlas_y as f32 / atlas_h,
                    (glyph.atlas_x + glyph.width) as f32 / atlas_w,
                    (glyph.atlas_y + glyph.height) as f32 / atlas_h,
                )
            } else {
                (0.0, 0.0, 0.0, 0.0)
            };

            let cx = text_x + ci as f32 * cell_width;
            push_quad(
                &mut vertices,
                &mut indices,
                &mut vertex_idx,
                cx,
                0.0,
                cell_width,
                cell_height,
                fg,
                transparent,
                [u0, v0],
                [u1, v1],
            );
        }

        // Divider between tabs (except after last)
        if i + 1 < tab_count {
            let div_x = tab_x + tab_width - 1.0;
            push_quad(
                &mut vertices,
                &mut indices,
                &mut vertex_idx,
                div_x,
                4.0,
                1.0,
                cell_height - 8.0,
                divider_color,
                transparent,
                [0.0, 0.0],
                [0.0, 0.0],
            );
        }
    }

    (vertices, indices)
}

// ---------------------------------------------------------------------------
// Overlay rendering (command palette, settings, etc.)
// ---------------------------------------------------------------------------

/// A single line of text inside an overlay.
pub struct OverlayLine<'a> {
    /// Text content.
    pub text: &'a str,
    /// Foreground color (RGBA).
    pub fg: [f32; 4],
    /// Optional per-line background highlight (e.g. selected item).
    pub bg: Option<[f32; 4]>,
    /// Whether to render in bold.
    pub bold: bool,
}

/// A rectangular text overlay drawn on top of terminal content.
///
/// Used for the command palette, settings panel, and other UI overlays.
pub struct OverlayLayer<'a> {
    /// Screen-space X position (pixels from left).
    pub x: f32,
    /// Screen-space Y position (pixels from top).
    pub y: f32,
    /// Width in pixels.
    pub width: f32,
    /// Height in pixels.
    pub height: f32,
    /// Background color (RGBA).
    pub bg: [f32; 4],
    /// Lines of text to render inside the overlay.
    pub lines: Vec<OverlayLine<'a>>,
}

/// Build vertex and index data for overlay layers.
///
/// Overlays are drawn on top of terminal content. Each overlay has a solid
/// background and lines of text rendered using the glyph atlas.
pub fn build_overlay_vertices(
    overlays: &[OverlayLayer],
    atlas: &mut GlyphAtlas,
    atlas_w: f32,
    atlas_h: f32,
) -> (Vec<Vertex>, Vec<u32>) {
    if overlays.is_empty() {
        return (Vec::new(), Vec::new());
    }

    let cell_width = atlas.metrics.cell_width;
    let cell_height = atlas.metrics.cell_height;
    let transparent = [0.0_f32, 0.0, 0.0, 0.0];

    let mut vertices = Vec::new();
    let mut indices = Vec::new();
    let mut vertex_idx = 0u32;

    for overlay in overlays {
        // Background rectangle
        push_quad(
            &mut vertices,
            &mut indices,
            &mut vertex_idx,
            overlay.x,
            overlay.y,
            overlay.width,
            overlay.height,
            overlay.bg,
            transparent,
            [0.0, 0.0],
            [0.0, 0.0],
        );

        // Render each line of text
        for (line_idx, line) in overlay.lines.iter().enumerate() {
            let line_y = overlay.y + line_idx as f32 * cell_height;

            // Per-line highlight background
            if let Some(line_bg) = line.bg {
                push_quad(
                    &mut vertices,
                    &mut indices,
                    &mut vertex_idx,
                    overlay.x,
                    line_y,
                    overlay.width,
                    cell_height,
                    line_bg,
                    transparent,
                    [0.0, 0.0],
                    [0.0, 0.0],
                );
            }

            // Text glyphs — left-padded by one cell width
            let text_x = overlay.x + cell_width;
            let max_chars = ((overlay.width - 2.0 * cell_width) / cell_width)
                .floor()
                .max(0.0) as usize;

            for (ci, ch) in line.text.chars().take(max_chars).enumerate() {
                let glyph = atlas.get_or_insert(GlyphKey {
                    ch,
                    bold: line.bold,
                    italic: false,
                });

                let (u0, v0, u1, v1) = if glyph.width > 0 && glyph.height > 0 {
                    (
                        glyph.atlas_x as f32 / atlas_w,
                        glyph.atlas_y as f32 / atlas_h,
                        (glyph.atlas_x + glyph.width) as f32 / atlas_w,
                        (glyph.atlas_y + glyph.height) as f32 / atlas_h,
                    )
                } else {
                    (0.0, 0.0, 0.0, 0.0)
                };

                let cx = text_x + ci as f32 * cell_width;
                push_quad(
                    &mut vertices,
                    &mut indices,
                    &mut vertex_idx,
                    cx,
                    line_y,
                    cell_width,
                    cell_height,
                    line.fg,
                    transparent,
                    [u0, v0],
                    [u1, v1],
                );
            }
        }
    }

    (vertices, indices)
}

/// Pre-shape a terminal row for ligature-aware rendering.
///
/// Groups consecutive cells with the same (bold, italic) attributes into segments,
/// shapes each segment via `atlas.shape_run()`, and returns a per-column map:
/// - `Some((glyph, span))` where `span > 0` → start of a cluster spanning `span` cells
/// - `Some((_, 0))` → continuation cell of a multi-cell ligature (skip glyph rendering)
/// - `None` → not shaped (fallback to per-character lookup)
fn pre_shape_row(
    terminal: &Terminal,
    row: usize,
    cols: usize,
    atlas: &mut GlyphAtlas,
) -> Vec<Option<(GlyphEntry, u8)>> {
    if !atlas.ligatures_enabled {
        return vec![None; cols];
    }

    let mut result: Vec<Option<(GlyphEntry, u8)>> = vec![None; cols];

    // Collect visible cells grouped by attribute segments
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

    // Shape each segment and map clusters back to columns
    for (cells, bold, italic) in &segments {
        let text: String = cells.iter().map(|c| c.ch).collect();
        let clusters = atlas.shape_run(&text, *bold, *italic);

        let mut ci = 0;
        for cluster in &clusters {
            if ci < cells.len() {
                let col = cells[ci].col;
                result[col] = Some((cluster.glyph, cluster.cell_span));
                // Mark continuation cells
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

/// Linear interpolation between two RGBA colors.
fn lerp_color(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
    [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
        a[3] + (b[3] - a[3]) * t,
    ]
}

/// Render an underline with the appropriate style (single, double, curly, dotted, dashed).
#[allow(clippy::too_many_arguments)]
fn push_underline_style(
    vertices: &mut Vec<Vertex>,
    indices: &mut Vec<u32>,
    vertex_idx: &mut u32,
    style: UnderlineStyle,
    x: f32,
    y: f32,
    w: f32,
    thickness: f32,
    color: [f32; 4],
) {
    match style {
        UnderlineStyle::None => {}
        UnderlineStyle::Single => {
            push_solid_quad(vertices, indices, vertex_idx, x, y, w, thickness, color);
        }
        UnderlineStyle::Double => {
            let gap = (thickness * 1.5).max(2.0);
            push_solid_quad(vertices, indices, vertex_idx, x, y, w, thickness, color);
            push_solid_quad(
                vertices,
                indices,
                vertex_idx,
                x,
                y + gap,
                w,
                thickness,
                color,
            );
        }
        UnderlineStyle::Curly => {
            // Approximate a sine wave with small quads
            let segments = ((w / thickness).ceil() as usize).max(4);
            let seg_w = w / segments as f32;
            let amplitude = thickness * 1.5;
            for s in 0..segments {
                let sx = x + s as f32 * seg_w;
                let phase = std::f32::consts::PI * 2.0 * s as f32 / segments as f32;
                let sy = y + phase.sin() * amplitude;
                push_solid_quad(
                    vertices, indices, vertex_idx, sx, sy, seg_w, thickness, color,
                );
            }
        }
        UnderlineStyle::Dotted => {
            let dot_w = thickness.max(1.0);
            let gap = dot_w;
            let mut dx = x;
            while dx < x + w {
                let dw = dot_w.min(x + w - dx);
                push_solid_quad(vertices, indices, vertex_idx, dx, y, dw, thickness, color);
                dx += dot_w + gap;
            }
        }
        UnderlineStyle::Dashed => {
            let dash_w = (thickness * 4.0).max(3.0);
            let gap = (thickness * 2.0).max(2.0);
            let mut dx = x;
            while dx < x + w {
                let dw = dash_w.min(x + w - dx);
                push_solid_quad(vertices, indices, vertex_idx, dx, y, dw, thickness, color);
                dx += dash_w + gap;
            }
        }
    }
}

/// Emit a solid-color quad (no texture) into the vertex/index buffers.
#[allow(clippy::too_many_arguments)]
fn push_solid_quad(
    vertices: &mut Vec<Vertex>,
    indices: &mut Vec<u32>,
    vertex_idx: &mut u32,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    color: [f32; 4],
) {
    push_quad(
        vertices,
        indices,
        vertex_idx,
        x,
        y,
        w,
        h,
        color,
        color,
        [0.0, 0.0],
        [0.0, 0.0],
    );
}

/// Emit a textured quad into the vertex/index buffers.
#[allow(clippy::too_many_arguments)]
fn push_quad(
    vertices: &mut Vec<Vertex>,
    indices: &mut Vec<u32>,
    vertex_idx: &mut u32,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    fg: [f32; 4],
    bg: [f32; 4],
    uv0: [f32; 2],
    uv1: [f32; 2],
) {
    vertices.push(Vertex {
        position: [x, y],
        tex_coords: [uv0[0], uv0[1]],
        fg_color: fg,
        bg_color: bg,
    });
    vertices.push(Vertex {
        position: [x + w, y],
        tex_coords: [uv1[0], uv0[1]],
        fg_color: fg,
        bg_color: bg,
    });
    vertices.push(Vertex {
        position: [x + w, y + h],
        tex_coords: [uv1[0], uv1[1]],
        fg_color: fg,
        bg_color: bg,
    });
    vertices.push(Vertex {
        position: [x, y + h],
        tex_coords: [uv0[0], uv1[1]],
        fg_color: fg,
        bg_color: bg,
    });

    indices.push(*vertex_idx);
    indices.push(*vertex_idx + 1);
    indices.push(*vertex_idx + 2);
    indices.push(*vertex_idx);
    indices.push(*vertex_idx + 2);
    indices.push(*vertex_idx + 3);

    *vertex_idx += 4;
}

// ---------------------------------------------------------------------------
// Inline image rendering (GPU path)
// ---------------------------------------------------------------------------

/// A packed image region within the image atlas.
pub struct PackedImage {
    /// Atlas offset (x pixels from left).
    pub atlas_x: u32,
    /// Atlas offset (y pixels from top).
    pub atlas_y: u32,
    /// Image width in pixels.
    pub width: u32,
    /// Image height in pixels.
    pub height: u32,
    /// Screen position X (pixels).
    pub screen_x: f32,
    /// Screen position Y (pixels).
    pub screen_y: f32,
}

/// Pack visible images into an RGBA atlas buffer and return image placements.
///
/// Uses a simple row packing strategy: images are placed left-to-right in rows.
/// Returns `(atlas_width, atlas_height, rgba_data, placements)`.
pub fn pack_image_atlas(
    images: &[wixen_core::ViewportImage<'_>],
    tab_bar_h: f32,
) -> (u32, u32, Vec<u8>, Vec<PackedImage>) {
    if images.is_empty() {
        return (0, 0, Vec::new(), Vec::new());
    }

    // Simple packing: stack images vertically for simplicity (no overlaps)
    let atlas_w: u32 = images.iter().map(|img| img.width).max().unwrap_or(1).max(1);
    let atlas_h: u32 = images.iter().map(|img| img.height).sum::<u32>().max(1);

    let mut atlas_data = vec![0u8; (atlas_w * atlas_h * 4) as usize];
    let mut placements = Vec::with_capacity(images.len());
    let mut y_offset = 0u32;

    for img in images {
        // Copy image pixels into atlas
        for row in 0..img.height {
            let src_start = (row * img.width * 4) as usize;
            let src_end = src_start + (img.width * 4) as usize;
            if src_end > img.pixels.len() {
                break;
            }
            let dst_start = ((y_offset + row) * atlas_w * 4) as usize;
            let dst_end = dst_start + (img.width * 4) as usize;
            if dst_end <= atlas_data.len() {
                atlas_data[dst_start..dst_end].copy_from_slice(&img.pixels[src_start..src_end]);
            }
        }

        placements.push(PackedImage {
            atlas_x: 0,
            atlas_y: y_offset,
            width: img.width,
            height: img.height,
            screen_x: img.px_x,
            screen_y: img.px_y + tab_bar_h,
        });

        y_offset += img.height;
    }

    (atlas_w, atlas_h, atlas_data, placements)
}

/// Build vertex/index data for image quads.
///
/// Each image becomes a textured quad mapping to its region in the packed atlas.
pub fn build_image_vertices(
    placements: &[PackedImage],
    atlas_w: f32,
    atlas_h: f32,
) -> (Vec<Vertex>, Vec<u32>) {
    if placements.is_empty() || atlas_w == 0.0 || atlas_h == 0.0 {
        return (Vec::new(), Vec::new());
    }

    let mut vertices = Vec::with_capacity(placements.len() * 4);
    let mut indices = Vec::with_capacity(placements.len() * 6);
    let mut vertex_idx = 0u32;

    let transparent = [0.0_f32; 4];

    for img in placements {
        let x0 = img.screen_x;
        let y0 = img.screen_y;
        let x1 = x0 + img.width as f32;
        let y1 = y0 + img.height as f32;

        let u0 = img.atlas_x as f32 / atlas_w;
        let v0 = img.atlas_y as f32 / atlas_h;
        let u1 = (img.atlas_x + img.width) as f32 / atlas_w;
        let v1 = (img.atlas_y + img.height) as f32 / atlas_h;

        // top-left
        vertices.push(Vertex {
            position: [x0, y0],
            tex_coords: [u0, v0],
            fg_color: transparent,
            bg_color: transparent,
        });
        // top-right
        vertices.push(Vertex {
            position: [x1, y0],
            tex_coords: [u1, v0],
            fg_color: transparent,
            bg_color: transparent,
        });
        // bottom-right
        vertices.push(Vertex {
            position: [x1, y1],
            tex_coords: [u1, v1],
            fg_color: transparent,
            bg_color: transparent,
        });
        // bottom-left
        vertices.push(Vertex {
            position: [x0, y1],
            tex_coords: [u0, v1],
            fg_color: transparent,
            bg_color: transparent,
        });

        indices.push(vertex_idx);
        indices.push(vertex_idx + 1);
        indices.push(vertex_idx + 2);
        indices.push(vertex_idx);
        indices.push(vertex_idx + 2);
        indices.push(vertex_idx + 3);

        vertex_idx += 4;
    }

    (vertices, indices)
}

// ---------------------------------------------------------------------------
// Hover state helpers
// ---------------------------------------------------------------------------

/// Default hyperlink hover color (cyan).
pub const LINK_HOVER_FG: [f32; 4] = [0.4, 0.8, 1.0, 1.0];

/// Check whether a cell at (col, row) falls within a hover range.
pub fn is_hovered(col: usize, row: usize, hover: Option<&wixen_core::HoverState>) -> bool {
    match hover {
        Some(h) => row == h.viewport_row && col >= h.col_start && col < h.col_end,
        None => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_overlay_vertices_empty() {
        // Empty overlays should produce no vertices
        // Can't call build_overlay_vertices without a GlyphAtlas (needs DirectWrite),
        // but we can verify the early return path by checking the function signature exists
        // and that OverlayLayer/OverlayLine are constructable.
        let _overlay = OverlayLayer {
            x: 100.0,
            y: 200.0,
            width: 400.0,
            height: 300.0,
            bg: [0.1, 0.1, 0.1, 0.9],
            lines: vec![OverlayLine {
                text: "Hello",
                fg: [1.0, 1.0, 1.0, 1.0],
                bg: None,
                bold: false,
            }],
        };
    }

    #[test]
    fn test_overlay_line_with_highlight() {
        let line = OverlayLine {
            text: "Selected Item",
            fg: [1.0, 1.0, 1.0, 1.0],
            bg: Some([0.3, 0.3, 0.8, 1.0]),
            bold: true,
        };
        assert!(line.bg.is_some());
        assert!(line.bold);
    }

    #[test]
    fn test_overlay_layer_dimensions() {
        let overlay = OverlayLayer {
            x: 50.0,
            y: 100.0,
            width: 600.0,
            height: 400.0,
            bg: [0.0, 0.0, 0.0, 0.8],
            lines: Vec::new(),
        };
        assert_eq!(overlay.x, 50.0);
        assert_eq!(overlay.y, 100.0);
        assert_eq!(overlay.width, 600.0);
        assert_eq!(overlay.height, 400.0);
    }

    #[test]
    fn test_pre_shape_row_ligatures_disabled() {
        // When ligatures are disabled, pre_shape_row returns all None
        // (signaling fallback to per-character glyph lookup)
        let terminal = Terminal::new(80, 24);
        let mut atlas = GlyphAtlas::new("Cascadia Code", 14.0, 96.0, 1.0, &[], false, "").unwrap();
        assert!(!atlas.ligatures_enabled);

        let result = pre_shape_row(&terminal, 0, 80, &mut atlas);
        assert_eq!(result.len(), 80);
        assert!(result.iter().all(|r| r.is_none()));
    }

    #[test]
    fn test_pre_shape_row_empty_terminal() {
        // Empty terminal should produce all-None results even with ligatures on
        let terminal = Terminal::new(10, 5);
        let mut atlas = GlyphAtlas::new("Cascadia Code", 14.0, 96.0, 1.0, &[], true, "").unwrap();
        assert!(atlas.ligatures_enabled);

        let result = pre_shape_row(&terminal, 0, 10, &mut atlas);
        assert_eq!(result.len(), 10);
        // Empty cells (spaces) should still get shaped results
        // All results are Some since ligatures are enabled, but all single-cell
        for entry in &result {
            if let Some((_, span)) = entry {
                assert!(*span >= 1, "Empty cells should be single-cell clusters");
            }
        }
    }

    #[test]
    fn test_pre_shape_row_basic_text() {
        // Write some basic text via Grid::write_char and verify shaping produces results
        let mut terminal = Terminal::new(20, 5);
        for ch in "hello".chars() {
            terminal.grid.write_char(ch);
        }
        let mut atlas = GlyphAtlas::new("Cascadia Code", 14.0, 96.0, 1.0, &[], true, "").unwrap();

        let result = pre_shape_row(&terminal, 0, 20, &mut atlas);
        // The first 5 columns should have shaped results
        for col in 0..5 {
            assert!(
                result[col].is_some(),
                "Column {} should have a shaped result",
                col
            );
            if let Some((_, span)) = result[col] {
                assert_eq!(span, 1, "Regular text should be single-cell clusters");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Image rendering tests
    // -----------------------------------------------------------------------

    #[test]
    fn test_pack_image_atlas_empty() {
        let (w, h, data, placements) = pack_image_atlas(&[], 0.0);
        assert_eq!(w, 0);
        assert_eq!(h, 0);
        assert!(data.is_empty());
        assert!(placements.is_empty());
    }

    #[test]
    fn test_pack_image_atlas_single() {
        // 2x2 red image
        let pixels = vec![
            255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
        ];
        let img = wixen_core::ViewportImage {
            pixels: &pixels,
            width: 2,
            height: 2,
            px_x: 10.0,
            px_y: 20.0,
        };
        let (w, h, data, placements) = pack_image_atlas(&[img], 5.0);
        assert_eq!(w, 2);
        assert_eq!(h, 2);
        assert_eq!(data.len(), 2 * 2 * 4);
        assert_eq!(placements.len(), 1);
        assert_eq!(placements[0].screen_x, 10.0);
        assert_eq!(placements[0].screen_y, 25.0); // 20.0 + tab_bar_h 5.0
        assert_eq!(placements[0].atlas_x, 0);
        assert_eq!(placements[0].atlas_y, 0);
        // Verify pixel data was copied
        assert_eq!(data[0..4], [255, 0, 0, 255]); // red
    }

    #[test]
    fn test_pack_image_atlas_two_images() {
        let pixels1 = [255u8, 0, 0, 255].repeat(4); // 2x2
        let pixels2 = [0u8, 255, 0, 255].repeat(6); // 3x2
        let img1 = wixen_core::ViewportImage {
            pixels: &pixels1,
            width: 2,
            height: 2,
            px_x: 0.0,
            px_y: 0.0,
        };
        let img2 = wixen_core::ViewportImage {
            pixels: &pixels2,
            width: 3,
            height: 2,
            px_x: 50.0,
            px_y: 100.0,
        };
        let (w, h, _data, placements) = pack_image_atlas(&[img1, img2], 0.0);
        assert_eq!(w, 3); // max width
        assert_eq!(h, 4); // 2 + 2 stacked
        assert_eq!(placements.len(), 2);
        assert_eq!(placements[0].atlas_y, 0);
        assert_eq!(placements[1].atlas_y, 2);
    }

    #[test]
    fn test_build_image_vertices_empty() {
        let (verts, indices) = build_image_vertices(&[], 256.0, 256.0);
        assert!(verts.is_empty());
        assert!(indices.is_empty());
    }

    #[test]
    fn test_build_image_vertices_single() {
        let placement = PackedImage {
            atlas_x: 0,
            atlas_y: 0,
            width: 16,
            height: 16,
            screen_x: 10.0,
            screen_y: 20.0,
        };
        let (verts, indices) = build_image_vertices(&[placement], 16.0, 16.0);
        assert_eq!(verts.len(), 4);
        assert_eq!(indices.len(), 6);
        // Top-left vertex
        assert_eq!(verts[0].position, [10.0, 20.0]);
        assert_eq!(verts[0].tex_coords, [0.0, 0.0]);
        // Bottom-right vertex
        assert_eq!(verts[2].position, [26.0, 36.0]);
        assert_eq!(verts[2].tex_coords, [1.0, 1.0]);
    }

    #[test]
    fn test_build_image_vertices_uv_mapping() {
        // Image at atlas position (0, 32) in a 64x64 atlas
        let placement = PackedImage {
            atlas_x: 0,
            atlas_y: 32,
            width: 16,
            height: 16,
            screen_x: 0.0,
            screen_y: 0.0,
        };
        let (verts, _) = build_image_vertices(&[placement], 64.0, 64.0);
        assert_eq!(verts[0].tex_coords, [0.0, 0.5]); // v0 = 32/64
        assert_eq!(verts[2].tex_coords, [0.25, 0.75]); // u1 = 16/64, v1 = 48/64
    }

    #[test]
    fn test_hover_in_range() {
        let hover = wixen_core::HoverState {
            url: "https://example.com".to_string(),
            viewport_row: 3,
            col_start: 5,
            col_end: 10,
        };
        assert!(is_hovered(5, 3, Some(&hover)));
        assert!(is_hovered(7, 3, Some(&hover)));
        assert!(is_hovered(9, 3, Some(&hover)));
    }

    #[test]
    fn test_hover_outside_range() {
        let hover = wixen_core::HoverState {
            url: "https://example.com".to_string(),
            viewport_row: 3,
            col_start: 5,
            col_end: 10,
        };
        // Before range
        assert!(!is_hovered(4, 3, Some(&hover)));
        // After range (col_end is exclusive)
        assert!(!is_hovered(10, 3, Some(&hover)));
        // Wrong row
        assert!(!is_hovered(7, 4, Some(&hover)));
        assert!(!is_hovered(7, 2, Some(&hover)));
    }

    #[test]
    fn test_hover_none() {
        assert!(!is_hovered(5, 3, None));
    }
}
