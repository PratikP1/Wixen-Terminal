//! Background image loading and scaling for terminal background rendering.

/// Decoded background image data (RGBA pixels).
#[derive(Clone)]
pub struct BgImageData {
    /// RGBA pixel data, row-major, 4 bytes per pixel.
    pub pixels: Vec<u8>,
    /// Image width in pixels.
    pub width: u32,
    /// Image height in pixels.
    pub height: u32,
}

/// Decode an image file's raw bytes into RGBA pixel data.
///
/// Supports PNG, JPEG, and GIF (first frame only).
/// Returns `None` if the data cannot be decoded.
pub fn decode_image(data: &[u8]) -> Option<BgImageData> {
    let img = image::load_from_memory(data).ok()?;
    let rgba = img.to_rgba8();
    let (w, h) = rgba.dimensions();
    Some(BgImageData {
        pixels: rgba.into_raw(),
        width: w,
        height: h,
    })
}

/// Apply opacity to RGBA pixel data by multiplying each pixel's alpha channel.
///
/// `opacity` is clamped to [0.0, 1.0].
pub fn apply_opacity(pixels: &mut [u8], opacity: f32) {
    let opacity = opacity.clamp(0.0, 1.0);
    for chunk in pixels.chunks_exact_mut(4) {
        let a = chunk[3] as f32 * opacity;
        chunk[3] = a.round() as u8;
    }
}

/// Compute a "cover" scaling rect: the image is scaled to fill the surface
/// while maintaining aspect ratio, centered, with overflow cropped.
///
/// Returns `(src_x, src_y, src_w, src_h)` — the source crop rect in image pixels.
pub fn scale_cover_crop(img_w: u32, img_h: u32, surf_w: u32, surf_h: u32) -> (u32, u32, u32, u32) {
    if img_w == 0 || img_h == 0 || surf_w == 0 || surf_h == 0 {
        return (0, 0, img_w, img_h);
    }

    let img_aspect = img_w as f64 / img_h as f64;
    let surf_aspect = surf_w as f64 / surf_h as f64;

    if img_aspect > surf_aspect {
        // Image is wider than surface — crop sides
        let visible_w = (img_h as f64 * surf_aspect) as u32;
        let x_offset = (img_w - visible_w) / 2;
        (x_offset, 0, visible_w, img_h)
    } else {
        // Image is taller than surface — crop top/bottom
        let visible_h = (img_w as f64 / surf_aspect) as u32;
        let y_offset = (img_h - visible_h) / 2;
        (0, y_offset, img_w, visible_h)
    }
}

/// Blit a decoded background image into a u32 framebuffer with alpha blending.
///
/// The image is scaled to cover the surface area (aspect-ratio preserving,
/// centered crop). Pixels are blended over `clear_color`.
///
/// Used by the software renderer path.
pub fn blit_bg_image(
    buffer: &mut [u32],
    surf_w: u32,
    surf_h: u32,
    img: &BgImageData,
    clear_color: u32,
) {
    if img.width == 0 || img.height == 0 || surf_w == 0 || surf_h == 0 {
        return;
    }

    let (src_x, src_y, src_w, src_h) = scale_cover_crop(img.width, img.height, surf_w, surf_h);

    for dy in 0..surf_h {
        for dx in 0..surf_w {
            let idx = (dy * surf_w + dx) as usize;
            if idx >= buffer.len() {
                continue;
            }

            // Map surface pixel to source image pixel
            let sx = src_x + (dx as u64 * src_w as u64 / surf_w as u64) as u32;
            let sy = src_y + (dy as u64 * src_h as u64 / surf_h as u64) as u32;

            if sx >= img.width || sy >= img.height {
                continue;
            }

            let pi = (sy * img.width + sx) as usize * 4;
            if pi + 3 >= img.pixels.len() {
                continue;
            }

            let r = img.pixels[pi] as u32;
            let g = img.pixels[pi + 1] as u32;
            let b = img.pixels[pi + 2] as u32;
            let a = img.pixels[pi + 3] as u32;

            if a == 0 {
                buffer[idx] = clear_color;
                continue;
            }

            let bg_r = (clear_color >> 16) & 0xFF;
            let bg_g = (clear_color >> 8) & 0xFF;
            let bg_b = clear_color & 0xFF;

            let out_r = (r * a + bg_r * (255 - a)) / 255;
            let out_g = (g * a + bg_g * (255 - a)) / 255;
            let out_b = (b * a + bg_b * (255 - a)) / 255;

            buffer[idx] = (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_decode_image_png() {
        // Create a minimal 2x2 PNG in memory using the image crate
        use image::{ImageBuffer, Rgba};
        let mut img: ImageBuffer<Rgba<u8>, Vec<u8>> = ImageBuffer::new(2, 2);
        img.put_pixel(0, 0, Rgba([255u8, 0, 0, 255])); // red
        img.put_pixel(1, 0, Rgba([0u8, 255, 0, 255])); // green
        img.put_pixel(0, 1, Rgba([0u8, 0, 255, 255])); // blue
        img.put_pixel(1, 1, Rgba([255u8, 255, 255, 255])); // white
        let mut buf = Vec::new();
        img.write_to(&mut std::io::Cursor::new(&mut buf), image::ImageFormat::Png)
            .unwrap();

        let decoded = decode_image(&buf).unwrap();
        assert_eq!(decoded.width, 2);
        assert_eq!(decoded.height, 2);
        assert_eq!(decoded.pixels.len(), 2 * 2 * 4); // 4 bytes per pixel
        // First pixel is red
        assert_eq!(&decoded.pixels[0..4], &[255, 0, 0, 255]);
        // Second pixel is green
        assert_eq!(&decoded.pixels[4..8], &[0, 255, 0, 255]);
    }

    #[test]
    fn test_decode_image_invalid_data() {
        let garbage = [0u8, 1, 2, 3, 4, 5];
        assert!(decode_image(&garbage).is_none());
    }

    #[test]
    fn test_apply_opacity_full() {
        let mut pixels = vec![255, 0, 0, 255, 0, 255, 0, 200];
        apply_opacity(&mut pixels, 1.0);
        assert_eq!(pixels[3], 255); // Unchanged
        assert_eq!(pixels[7], 200); // Unchanged
    }

    #[test]
    fn test_apply_opacity_half() {
        let mut pixels = vec![255, 0, 0, 200, 0, 255, 0, 100];
        apply_opacity(&mut pixels, 0.5);
        assert_eq!(pixels[3], 100); // 200 * 0.5
        assert_eq!(pixels[7], 50); // 100 * 0.5
    }

    #[test]
    fn test_apply_opacity_zero() {
        let mut pixels = vec![255, 0, 0, 255];
        apply_opacity(&mut pixels, 0.0);
        assert_eq!(pixels[3], 0);
    }

    #[test]
    fn test_apply_opacity_clamps_above_one() {
        let mut pixels = vec![0, 0, 0, 100];
        apply_opacity(&mut pixels, 2.0);
        assert_eq!(pixels[3], 100); // Clamped to 1.0, so unchanged
    }

    #[test]
    fn test_scale_cover_crop_exact_match() {
        // Image matches surface exactly — no crop needed
        let (x, y, w, h) = scale_cover_crop(800, 600, 800, 600);
        assert_eq!((x, y, w, h), (0, 0, 800, 600));
    }

    #[test]
    fn test_scale_cover_crop_wider_image() {
        // 1600x600 image into 800x600 surface — crop sides
        let (x, y, w, h) = scale_cover_crop(1600, 600, 800, 600);
        // Visible width = 600 * (800/600) = 800, x_offset = (1600-800)/2 = 400
        assert_eq!(x, 400);
        assert_eq!(y, 0);
        assert_eq!(w, 800);
        assert_eq!(h, 600);
    }

    #[test]
    fn test_scale_cover_crop_taller_image() {
        // 800x1200 image into 800x600 surface — crop top/bottom
        let (x, y, w, h) = scale_cover_crop(800, 1200, 800, 600);
        // Visible height = 800 / (800/600) = 600, y_offset = (1200-600)/2 = 300
        assert_eq!(x, 0);
        assert_eq!(y, 300);
        assert_eq!(w, 800);
        assert_eq!(h, 600);
    }

    #[test]
    fn test_scale_cover_crop_zero_dims() {
        let (x, y, w, h) = scale_cover_crop(0, 0, 800, 600);
        assert_eq!((x, y, w, h), (0, 0, 0, 0));
    }

    #[test]
    fn test_blit_bg_image_opaque_1x1() {
        let img = BgImageData {
            pixels: vec![255, 0, 0, 255], // Red, fully opaque
            width: 1,
            height: 1,
        };
        let mut buffer = vec![0u32; 4]; // 2x2
        blit_bg_image(&mut buffer, 2, 2, &img, 0x000000);
        // All 4 pixels should be red (image is stretched to cover)
        assert!(buffer.iter().all(|&p| p == 0x00FF0000));
    }

    #[test]
    fn test_blit_bg_image_with_opacity() {
        let mut pixels = vec![255, 0, 0, 255]; // Red, opaque
        apply_opacity(&mut pixels, 0.5);
        let img = BgImageData {
            pixels,
            width: 1,
            height: 1,
        };
        let mut buffer = vec![0u32; 1]; // 1x1
        blit_bg_image(&mut buffer, 1, 1, &img, 0x00000000);
        let r = (buffer[0] >> 16) & 0xFF;
        // 255 * 128 / 255 ≈ 128
        assert!(r > 120 && r < 135, "r={r}");
    }

    #[test]
    fn test_blit_bg_image_empty() {
        let img = BgImageData {
            pixels: vec![],
            width: 0,
            height: 0,
        };
        let mut buffer = vec![0xABCDEFu32; 4];
        blit_bg_image(&mut buffer, 2, 2, &img, 0);
        assert!(buffer.iter().all(|&p| p == 0xABCDEF));
    }
}
