//! Custom post-process shader pass.
//!
//! The terminal frame is rendered into an offscreen "scene" texture, and this
//! module runs a final fullscreen pass that samples that texture through either
//! a user-supplied WGSL fragment shader or a built-in passthrough. User float
//! parameters (`set_shader_param`) are packed into a uniform buffer each frame.
//!
//! ## Shader contract
//!
//! A custom shader supplies **only the fragment stage** — the fullscreen vertex
//! shader, bindings, and uniform struct are provided by [`POST_PROCESS_PRELUDE`]
//! and prepended automatically. The fragment entry point must be named
//! `fs_main` with the signature:
//!
//! ```wgsl
//! @fragment
//! fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> { ... }
//! ```
//!
//! Available bindings inside a fragment shader:
//! - `t_screen: texture_2d<f32>` — the rendered terminal frame
//! - `s_screen: sampler`
//! - `params: PostParams` — `resolution`, `time`, `param_count`, and up to
//!   [`MAX_USER_PARAMS`] user floats (sorted by name) in `params.user`.

use wgpu::util::DeviceExt;

use crate::custom_shader::{DEFAULT_POST_PROCESS_SHADER, ShaderError, ShaderParams};

/// Maximum number of named user parameters exposed to a custom shader.
pub const MAX_USER_PARAMS: usize = 16;

/// Number of `f32`s in the post-process uniform: `resolution` (2), `time` (1),
/// `param_count` (1), then [`MAX_USER_PARAMS`] user slots.
pub const PARAM_FLOATS: usize = 4 + MAX_USER_PARAMS;

/// WGSL prepended to every post-process fragment shader.
///
/// Declares the bindings, the [`PostParams`] uniform, and a fullscreen-triangle
/// vertex shader (`vs_main`) so user shaders only write a fragment stage.
pub const POST_PROCESS_PRELUDE: &str = r#"
struct PostParams {
    resolution: vec2<f32>,
    time: f32,
    param_count: f32,
    user: array<vec4<f32>, 4>,
};

@group(0) @binding(0) var t_screen: texture_2d<f32>;
@group(0) @binding(1) var s_screen: sampler;
@group(0) @binding(2) var<uniform> params: PostParams;

struct PostVertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> PostVertexOutput {
    // Oversized fullscreen triangle covering the viewport.
    var out: PostVertexOutput;
    let x = f32((vertex_index << 1u) & 2u);
    let y = f32(vertex_index & 2u);
    out.uv = vec2<f32>(x, y);
    out.clip_position = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    return out;
}
"#;

/// Combine the shared prelude with a user (or default) fragment shader into a
/// single compilable WGSL module.
pub fn compose_shader(fragment_source: &str) -> String {
    format!("{POST_PROCESS_PRELUDE}\n{fragment_source}")
}

/// Pack the post-process uniform into a fixed-size `f32` array matching the
/// `PostParams` WGSL layout.
///
/// `user` values beyond [`MAX_USER_PARAMS`] are dropped; `param_count` records
/// how many were actually stored.
pub fn pack_params(resolution: [f32; 2], time: f32, user: &[f32]) -> [f32; PARAM_FLOATS] {
    let mut out = [0.0_f32; PARAM_FLOATS];
    out[0] = resolution[0];
    out[1] = resolution[1];
    out[2] = time;
    let count = user.len().min(MAX_USER_PARAMS);
    out[3] = count as f32;
    for (slot, &value) in out[4..].iter_mut().zip(user.iter().take(MAX_USER_PARAMS)) {
        *slot = value;
    }
    out
}

/// GPU resources for the offscreen scene texture and post-process pass.
pub struct PostProcess {
    format: wgpu::TextureFormat,
    scene_view: wgpu::TextureView,
    sampler: wgpu::Sampler,
    bind_group_layout: wgpu::BindGroupLayout,
    bind_group: wgpu::BindGroup,
    uniform_buffer: wgpu::Buffer,
    passthrough_pipeline: wgpu::RenderPipeline,
    custom_pipeline: Option<wgpu::RenderPipeline>,
    params: ShaderParams,
    start: std::time::Instant,
}

impl PostProcess {
    /// Create the post-process resources for a surface of the given size/format.
    pub fn new(
        device: &wgpu::Device,
        format: wgpu::TextureFormat,
        width: u32,
        height: u32,
    ) -> Self {
        let scene_view = Self::create_scene_view(device, format, width, height);

        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("Post Process Sampler"),
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });

        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("Post Process Bind Group Layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });

        let empty = pack_params([width as f32, height as f32], 0.0, &[]);
        let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Post Process Uniforms"),
            contents: bytemuck::bytes_of(&empty),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });

        let bind_group = Self::create_bind_group(
            device,
            &bind_group_layout,
            &scene_view,
            &sampler,
            &uniform_buffer,
        );

        let passthrough_pipeline = build_pipeline(
            device,
            format,
            &bind_group_layout,
            &compose_shader(DEFAULT_POST_PROCESS_SHADER),
            "Passthrough Post Process",
        );

        Self {
            format,
            scene_view,
            sampler,
            bind_group_layout,
            bind_group,
            uniform_buffer,
            passthrough_pipeline,
            custom_pipeline: None,
            params: ShaderParams::default(),
            start: std::time::Instant::now(),
        }
    }

    /// Recreate the scene texture (and its bind group) for a new surface size.
    pub fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        if width == 0 || height == 0 {
            return;
        }
        self.scene_view = Self::create_scene_view(device, self.format, width, height);
        self.bind_group = Self::create_bind_group(
            device,
            &self.bind_group_layout,
            &self.scene_view,
            &self.sampler,
            &self.uniform_buffer,
        );
    }

    /// Compile and install a custom fragment shader.
    ///
    /// Returns [`ShaderError::CompileError`] if wgpu rejects the composed WGSL.
    pub fn set_shader(
        &mut self,
        device: &wgpu::Device,
        fragment_source: &str,
    ) -> Result<(), ShaderError> {
        let full = compose_shader(fragment_source);
        device.push_error_scope(wgpu::ErrorFilter::Validation);
        let pipeline = build_pipeline(
            device,
            self.format,
            &self.bind_group_layout,
            &full,
            "Custom Post Process",
        );
        if let Some(error) = pollster::block_on(device.pop_error_scope()) {
            return Err(ShaderError::CompileError(error.to_string()));
        }
        self.custom_pipeline = Some(pipeline);
        Ok(())
    }

    /// Remove the custom shader, reverting to the passthrough pipeline.
    pub fn clear_shader(&mut self) {
        self.custom_pipeline = None;
    }

    /// Whether a custom shader pipeline is currently installed.
    pub fn has_custom_shader(&self) -> bool {
        self.custom_pipeline.is_some()
    }

    /// Set (or overwrite) a named user parameter.
    pub fn set_param(&mut self, name: &str, value: f32) {
        self.params.set(name, value);
    }

    /// Upload the current uniform (resolution, elapsed time, user params).
    pub fn update_uniform(&self, queue: &wgpu::Queue, resolution: [f32; 2]) {
        let time = self.start.elapsed().as_secs_f32();
        let packed = pack_params(resolution, time, &self.params.to_uniform_data());
        queue.write_buffer(&self.uniform_buffer, 0, bytemuck::bytes_of(&packed));
    }

    /// The offscreen view the terminal frame renders into.
    pub fn scene_view(&self) -> &wgpu::TextureView {
        &self.scene_view
    }

    /// The bind group for the post-process pass.
    pub fn bind_group(&self) -> &wgpu::BindGroup {
        &self.bind_group
    }

    /// The pipeline to run: the custom shader if loaded, otherwise passthrough.
    pub fn active_pipeline(&self) -> &wgpu::RenderPipeline {
        self.custom_pipeline
            .as_ref()
            .unwrap_or(&self.passthrough_pipeline)
    }

    fn create_scene_view(
        device: &wgpu::Device,
        format: wgpu::TextureFormat,
        width: u32,
        height: u32,
    ) -> wgpu::TextureView {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Scene Texture"),
            size: wgpu::Extent3d {
                width: width.max(1),
                height: height.max(1),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        texture.create_view(&wgpu::TextureViewDescriptor::default())
    }

    fn create_bind_group(
        device: &wgpu::Device,
        layout: &wgpu::BindGroupLayout,
        scene_view: &wgpu::TextureView,
        sampler: &wgpu::Sampler,
        uniform_buffer: &wgpu::Buffer,
    ) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Post Process Bind Group"),
            layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(scene_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: uniform_buffer.as_entire_binding(),
                },
            ],
        })
    }
}

fn build_pipeline(
    device: &wgpu::Device,
    format: wgpu::TextureFormat,
    bind_group_layout: &wgpu::BindGroupLayout,
    full_source: &str,
    label: &str,
) -> wgpu::RenderPipeline {
    let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some(label),
        source: wgpu::ShaderSource::Wgsl(full_source.into()),
    });
    let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Post Process Pipeline Layout"),
        bind_group_layouts: &[bind_group_layout],
        push_constant_ranges: &[],
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some(label),
        layout: Some(&pipeline_layout),
        vertex: wgpu::VertexState {
            module: &module,
            entry_point: Some("vs_main"),
            buffers: &[],
            compilation_options: Default::default(),
        },
        fragment: Some(wgpu::FragmentState {
            module: &module,
            entry_point: Some("fs_main"),
            targets: &[Some(wgpu::ColorTargetState {
                format,
                blend: Some(wgpu::BlendState::REPLACE),
                write_mask: wgpu::ColorWrites::ALL,
            })],
            compilation_options: Default::default(),
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            ..Default::default()
        },
        depth_stencil: None,
        multisample: wgpu::MultisampleState::default(),
        multiview: None,
        cache: None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compose_prepends_prelude_to_fragment() {
        let composed = compose_shader("@fragment\nfn fs_main() {}");
        assert!(composed.contains("@vertex"), "must include prelude vertex");
        assert!(composed.contains("fn vs_main"), "must include vs_main");
        assert!(
            composed.contains("fn fs_main"),
            "must include user fragment"
        );
        assert!(composed.contains("t_screen"), "must declare screen texture");
    }

    #[test]
    fn default_shader_composes_into_complete_module() {
        let composed = compose_shader(DEFAULT_POST_PROCESS_SHADER);
        assert!(composed.contains("fn vs_main"));
        assert!(composed.contains("fn fs_main"));
        assert!(composed.contains("textureSample(t_screen, s_screen"));
    }

    #[test]
    fn pack_params_layout_matches_postparams() {
        let packed = pack_params([1920.0, 1080.0], 3.5, &[0.1, 0.2, 0.3]);
        assert_eq!(packed.len(), PARAM_FLOATS);
        assert_eq!(packed[0], 1920.0);
        assert_eq!(packed[1], 1080.0);
        assert_eq!(packed[2], 3.5);
        assert_eq!(packed[3], 3.0, "param_count records stored user count");
        assert_eq!(&packed[4..7], &[0.1, 0.2, 0.3]);
        assert!(packed[7..].iter().all(|&v| v == 0.0), "unused slots zeroed");
    }

    #[test]
    fn pack_params_truncates_excess_user_values() {
        let user: Vec<f32> = (0..(MAX_USER_PARAMS + 5)).map(|i| i as f32).collect();
        let packed = pack_params([0.0, 0.0], 0.0, &user);
        assert_eq!(packed[3], MAX_USER_PARAMS as f32);
        assert_eq!(packed[4], 0.0);
        assert_eq!(
            packed[4 + MAX_USER_PARAMS - 1],
            (MAX_USER_PARAMS - 1) as f32
        );
    }

    #[test]
    fn pack_params_empty_user_reports_zero_count() {
        let packed = pack_params([800.0, 600.0], 0.0, &[]);
        assert_eq!(packed[3], 0.0);
        assert!(packed[4..].iter().all(|&v| v == 0.0));
    }

    #[test]
    fn param_floats_matches_postparams_byte_size() {
        // PostParams: vec2 + f32 + f32 (16 bytes) + array<vec4<f32>, 4> (64 bytes).
        assert_eq!(PARAM_FLOATS * 4, 80);
    }

    #[test]
    fn packed_params_are_sorted_by_name_via_shader_params() {
        // Mirrors the runtime path: params are packed from ShaderParams, which
        // sorts by key for a deterministic uniform layout.
        let mut params = ShaderParams::default();
        params.set("zoom", 2.0);
        params.set("alpha", 0.5);
        params.set("brightness", 1.0);
        let packed = pack_params([0.0, 0.0], 0.0, &params.to_uniform_data());
        // Sorted keys: alpha, brightness, zoom
        assert_eq!(&packed[4..7], &[0.5, 1.0, 2.0]);
    }
}
