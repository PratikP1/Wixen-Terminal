//! Gaussian blur: GPU compute shader and CPU fallback for background images.

/// WGSL compute shader for single-pass Gaussian blur.
///
/// The GPU path dispatches this shader twice (horizontal then vertical)
/// using a uniform to flip the blur direction.
pub const BLUR_SHADER_WGSL: &str = r#"
struct Params {
    radius: u32,
    horizontal: u32,
    width: u32,
    height: u32,
}

@group(0) @binding(0) var input_tex: texture_2d<f32>;
@group(0) @binding(1) var output_tex: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(2) var<uniform> params: Params;
@group(0) @binding(3) var<storage, read> weights: array<f32>;

@compute @workgroup_size(16, 16)
fn blur_main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let x = gid.x;
    let y = gid.y;
    if x >= params.width || y >= params.height {
        return;
    }

    var color = vec4<f32>(0.0);
    let r = i32(params.radius);

    for (var i = -r; i <= r; i = i + 1) {
        let w = weights[u32(i + r)];
        var sx: i32;
        var sy: i32;
        if params.horizontal != 0u {
            sx = clamp(i32(x) + i, 0, i32(params.width) - 1);
            sy = i32(y);
        } else {
            sx = i32(x);
            sy = clamp(i32(y) + i, 0, i32(params.height) - 1);
        }
        color = color + w * textureLoad(input_tex, vec2<i32>(sx, sy), 0);
    }

    textureStore(output_tex, vec2<i32>(i32(x), i32(y)), color);
}
"#;

/// Compute a 1-D Gaussian kernel of the given radius.
///
/// The kernel has `2 * radius + 1` entries, normalized so they sum to 1.0.
/// A radius of 0 returns `[1.0]`.
pub fn gaussian_kernel(radius: u32) -> Vec<f32> {
    let size = (2 * radius + 1) as usize;
    let sigma = if radius == 0 {
        1.0_f32
    } else {
        radius as f32 / 2.0
    };

    let mut weights = Vec::with_capacity(size);
    let mut sum = 0.0_f32;

    for i in 0..size {
        let x = i as f32 - radius as f32;
        let w = (-x * x / (2.0 * sigma * sigma)).exp();
        weights.push(w);
        sum += w;
    }

    // Normalize so the weights sum to 1.0.
    for w in &mut weights {
        *w /= sum;
    }

    weights
}

/// Apply a separable Gaussian blur to RGBA pixel data on the CPU.
///
/// Performs a horizontal pass followed by a vertical pass for O(n*r) instead
/// of O(n*r^2). Radius 0 is an identity (no-op).
pub fn apply_cpu_blur(pixels: &mut [u8], width: u32, height: u32, radius: u32) {
    if radius == 0 || width == 0 || height == 0 {
        return;
    }

    let kernel = gaussian_kernel(radius);
    let w = width as usize;
    let h = height as usize;
    let stride = w * 4;

    // --- horizontal pass ---
    let mut temp = vec![0u8; pixels.len()];
    let r = radius as i32;
    for y in 0..h {
        for x in 0..w {
            let mut accum = [0.0_f32; 4];
            for (ki, &weight) in kernel.iter().enumerate() {
                let sx = (x as i32 + ki as i32 - r).clamp(0, w as i32 - 1) as usize;
                let base = y * stride + sx * 4;
                for c in 0..4 {
                    accum[c] += pixels[base + c] as f32 * weight;
                }
            }
            let dst = y * stride + x * 4;
            for c in 0..4 {
                temp[dst + c] = accum[c].round().clamp(0.0, 255.0) as u8;
            }
        }
    }

    // --- vertical pass ---
    for y in 0..h {
        for x in 0..w {
            let mut accum = [0.0_f32; 4];
            for (ki, &weight) in kernel.iter().enumerate() {
                let sy = (y as i32 + ki as i32 - r).clamp(0, h as i32 - 1) as usize;
                let base = sy * stride + x * 4;
                for c in 0..4 {
                    accum[c] += temp[base + c] as f32 * weight;
                }
            }
            let dst = y * stride + x * 4;
            for c in 0..4 {
                pixels[dst + c] = accum[c].round().clamp(0.0, 255.0) as u8;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kernel_radius_zero_returns_identity() {
        let k = gaussian_kernel(0);
        assert_eq!(k, vec![1.0]);
    }

    #[test]
    fn kernel_radius_one_has_three_weights_summing_to_one() {
        let k = gaussian_kernel(1);
        assert_eq!(k.len(), 3);
        let sum: f32 = k.iter().sum();
        assert!((sum - 1.0).abs() < 1e-6, "sum was {sum}");
    }

    #[test]
    fn kernel_weights_are_normalized() {
        for radius in [2, 5, 10, 20] {
            let k = gaussian_kernel(radius);
            assert_eq!(k.len(), (2 * radius + 1) as usize);
            let sum: f32 = k.iter().sum();
            assert!((sum - 1.0).abs() < 1e-5, "radius {radius}: sum was {sum}");
        }
    }

    #[test]
    fn cpu_blur_solid_red_stays_red() {
        // 4x4 solid red, fully opaque
        let mut pixels = vec![255, 0, 0, 255].repeat(16);
        let original = pixels.clone();
        apply_cpu_blur(&mut pixels, 4, 4, 2);
        assert_eq!(pixels, original);
    }

    #[test]
    fn cpu_blur_checkerboard_averages() {
        // 4x4 checkerboard: black and white, fully opaque
        let mut pixels = Vec::with_capacity(4 * 4 * 4);
        for y in 0..4u32 {
            for x in 0..4u32 {
                let v = if (x + y) % 2 == 0 { 255u8 } else { 0u8 };
                pixels.extend_from_slice(&[v, v, v, 255]);
            }
        }
        let original = pixels.clone();
        apply_cpu_blur(&mut pixels, 4, 4, 1);

        // The blur should produce values between 0 and 255 (not all extremes).
        let has_mid = pixels.chunks_exact(4).any(|px| px[0] > 10 && px[0] < 245);
        assert!(has_mid, "expected averaged values after blur");
        assert_ne!(pixels, original, "blur must change the checkerboard");
    }

    #[test]
    fn cpu_blur_radius_zero_is_identity() {
        let mut pixels = vec![10, 20, 30, 40, 50, 60, 70, 80];
        let original = pixels.clone();
        apply_cpu_blur(&mut pixels, 2, 1, 0);
        assert_eq!(pixels, original);
    }

    #[test]
    fn shader_source_is_non_empty() {
        assert!(
            !BLUR_SHADER_WGSL.is_empty(),
            "BLUR_SHADER_WGSL must contain a compute shader"
        );
        assert!(
            BLUR_SHADER_WGSL.contains("@compute"),
            "shader must contain a compute entry point"
        );
    }
}
