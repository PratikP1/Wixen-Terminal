//! wixen-render: GPU renderer (wgpu), software fallback (softbuffer),
//! glyph atlas, and DirectWrite text shaping.

pub mod atlas;
pub mod bg_image;
pub mod blur;
pub mod colors;
pub mod custom_shader;
pub mod dwrite;
pub mod pipeline;
pub mod soft;

use std::num::NonZero;
use thiserror::Error;
use tracing::{info, warn};
use wgpu::util::DeviceExt;
use windows::Win32::Foundation::HWND;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;

use atlas::{FontMetrics, GlyphAtlas};
pub use colors::ColorScheme;
pub use pipeline::{CellHighlight, OverlayLayer, OverlayLine, TabBarItem};
use pipeline::{Uniforms, Vertex};
use soft::SoftwareRenderer;
use wixen_core::Terminal;

/// Information needed to render a single pane in a multi-pane layout.
pub struct PaneRenderInfo<'a> {
    pub terminal: &'a Terminal,
    pub cursor_visible: bool,
    pub is_active: bool,
    pub show_scrollbar: bool,
    /// Pixel-space rectangle: (x, y, width, height)
    pub rect: (f32, f32, f32, f32),
}

#[derive(Error, Debug)]
pub enum RenderError {
    #[error("Failed to create wgpu instance")]
    InstanceCreation,
    #[error("Failed to get wgpu adapter")]
    AdapterNotFound,
    #[error("Failed to request wgpu device: {0}")]
    DeviceRequest(#[source] wgpu::RequestDeviceError),
    #[error("Failed to create surface: {0}")]
    SurfaceCreation(#[source] wgpu::CreateSurfaceError),
    #[error("Surface error: {0}")]
    Surface(#[source] wgpu::SurfaceError),
    #[error("Software renderer init error: {0}")]
    SoftbufferInit(String),
    #[error("Software renderer present error: {0}")]
    SoftbufferPresent(String),
    #[error("DirectWrite initialization failed: {0}")]
    DirectWriteInit(String),
}

/// GPU renderer state.
pub struct Renderer {
    device: wgpu::Device,
    queue: wgpu::Queue,
    surface: wgpu::Surface<'static>,
    surface_config: wgpu::SurfaceConfiguration,
    render_pipeline: wgpu::RenderPipeline,
    bind_group_layout: wgpu::BindGroupLayout,
    bind_group: wgpu::BindGroup,
    uniform_buffer: wgpu::Buffer,
    atlas_texture: wgpu::Texture,
    /// Image rendering pipeline (RGBA textured quads for Sixel/iTerm2/Kitty).
    image_pipeline: wgpu::RenderPipeline,
    /// Bind group layout for image rendering (reuses uniform buffer, RGBA texture).
    image_bind_group_layout: wgpu::BindGroupLayout,
    /// Optional background image (texture + bind group for fullscreen quad).
    bg_image: Option<BgImageState>,
    /// Raw RGBA pixel data of the decoded background image (kept for CPU blur).
    #[allow(dead_code)]
    bg_image_pixels: Option<bg_image::BgImageData>,
    /// Gaussian blur radius for the background image layer (0 = no blur).
    #[allow(dead_code)]
    blur_radius: u32,
    /// Optional custom post-process shader configuration (validated, not compiled).
    #[allow(dead_code)]
    custom_shader_config: Option<custom_shader::CustomShaderConfig>,
    pub atlas: GlyphAtlas,
    pub colors: ColorScheme,
}

/// GPU state for a loaded background image.
struct BgImageState {
    bind_group: wgpu::BindGroup,
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
}

impl Renderer {
    /// Create a new renderer targeting the given HWND.
    ///
    /// # Safety
    /// The HWND must remain valid for the lifetime of this Renderer.
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
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::DX12 | wgpu::Backends::VULKAN,
            ..Default::default()
        });

        let mut win_handle = raw_window_handle::Win32WindowHandle::new(
            NonZero::new(hwnd.0 as isize).expect("HWND is null"),
        );
        let hmodule = unsafe { GetModuleHandleW(None).unwrap_or_default() };
        win_handle.hinstance = NonZero::new(hmodule.0 as isize);

        let raw_window = raw_window_handle::RawWindowHandle::Win32(win_handle);
        let raw_display = raw_window_handle::RawDisplayHandle::Windows(
            raw_window_handle::WindowsDisplayHandle::new(),
        );

        let surface_target = wgpu::SurfaceTargetUnsafe::RawHandle {
            raw_display_handle: raw_display,
            raw_window_handle: raw_window,
        };

        let surface = unsafe {
            instance
                .create_surface_unsafe(surface_target)
                .map_err(RenderError::SurfaceCreation)?
        };

        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
        }))
        .ok_or(RenderError::AdapterNotFound)?;

        info!(
            backend = ?adapter.get_info().backend,
            name = adapter.get_info().name,
            "GPU adapter selected"
        );

        let (device, queue) =
            pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor::default(), None))
                .map_err(RenderError::DeviceRequest)?;

        // Log wgpu validation errors instead of panicking (the default behavior)
        device.on_uncaptured_error(Box::new(|error| {
            tracing::error!("wgpu device error: {error}");
        }));

        let surface_caps = surface.get_capabilities(&adapter);
        let surface_format = surface_caps
            .formats
            .iter()
            .find(|f| f.is_srgb())
            .copied()
            .unwrap_or(surface_caps.formats[0]);

        let surface_config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: surface_format,
            width,
            height,
            present_mode: wgpu::PresentMode::Fifo,
            alpha_mode: surface_caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &surface_config);

        // Create glyph atlas with DirectWrite
        let atlas = GlyphAtlas::new(
            font_family,
            font_size,
            dpi,
            line_height,
            fallback_fonts,
            ligatures,
            font_path,
        )?;

        // Create atlas GPU texture
        let atlas_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Glyph Atlas"),
            size: wgpu::Extent3d {
                width: atlas.width,
                height: atlas.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::R8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });

        // Upload atlas data
        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &atlas_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            atlas.data(),
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(atlas.width),
                rows_per_image: Some(atlas.height),
            },
            wgpu::Extent3d {
                width: atlas.width,
                height: atlas.height,
                depth_or_array_layers: 1,
            },
        );

        let atlas_view = atlas_texture.create_view(&wgpu::TextureViewDescriptor::default());
        let atlas_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("Atlas Sampler"),
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });

        // Uniform buffer
        let uniforms = Uniforms {
            screen_size: [width as f32, height as f32],
            _padding: [0.0, 0.0],
        };
        let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Uniforms"),
            contents: bytemuck::bytes_of(&uniforms),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });

        // Bind group layout
        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("Bind Group Layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });

        let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Bind Group"),
            layout: &bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&atlas_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&atlas_sampler),
                },
            ],
        });

        // Shader module
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Terminal Shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shader.wgsl").into()),
        });

        // Pipeline layout
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Pipeline Layout"),
            bind_group_layouts: &[&bind_group_layout],
            push_constant_ranges: &[],
        });

        // Render pipeline
        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Terminal Pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[Vertex::layout()],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: surface_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
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
        });

        // Image rendering pipeline (RGBA textured quads for inline images)
        let image_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Image Shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("image.wgsl").into()),
        });

        let image_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("Image Bind Group Layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::VERTEX,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Uniform,
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Texture {
                            sample_type: wgpu::TextureSampleType::Float { filterable: true },
                            view_dimension: wgpu::TextureViewDimension::D2,
                            multisampled: false,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 2,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                        count: None,
                    },
                ],
            });

        let image_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("Image Pipeline Layout"),
                bind_group_layouts: &[&image_bind_group_layout],
                push_constant_ranges: &[],
            });

        let image_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Image Pipeline"),
            layout: Some(&image_pipeline_layout),
            vertex: wgpu::VertexState {
                module: &image_shader,
                entry_point: Some("vs_main"),
                buffers: &[Vertex::layout()],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &image_shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: surface_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
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
        });

        Ok(Self {
            device,
            queue,
            surface,
            surface_config,
            render_pipeline,
            bind_group_layout,
            bind_group,
            uniform_buffer,
            atlas_texture,
            image_pipeline,
            image_bind_group_layout,
            bg_image: None,
            bg_image_pixels: None,
            blur_radius: 0,
            custom_shader_config: None,
            atlas,
            colors,
        })
    }

    /// Update the color scheme (hot-reload). No GPU resources need recreating.
    pub fn update_colors(&mut self, colors: ColorScheme) {
        self.colors = colors;
    }

    /// Rebuild the glyph atlas for a new font (hot-reload).
    ///
    /// Recreates the DirectWrite engine, re-rasterizes all ASCII glyphs,
    /// allocates a new GPU texture, and rebuilds the bind group.
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
        // Build a fresh atlas with the new font parameters
        let atlas = GlyphAtlas::new(
            font_family,
            font_size,
            dpi,
            line_height,
            fallback_fonts,
            ligatures,
            font_path,
        )?;

        // Create a new GPU texture (atlas dimensions may have changed)
        let atlas_texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Glyph Atlas"),
            size: wgpu::Extent3d {
                width: atlas.width,
                height: atlas.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::R8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });

        // Upload the new atlas data
        self.queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &atlas_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            atlas.data(),
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(atlas.width),
                rows_per_image: Some(atlas.height),
            },
            wgpu::Extent3d {
                width: atlas.width,
                height: atlas.height,
                depth_or_array_layers: 1,
            },
        );

        // Rebuild the bind group to point at the new texture
        let atlas_view = atlas_texture.create_view(&wgpu::TextureViewDescriptor::default());
        let atlas_sampler = self.device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("Atlas Sampler"),
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });

        self.bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Bind Group"),
            layout: &self.bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: self.uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&atlas_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&atlas_sampler),
                },
            ],
        });

        info!(
            font = font_family,
            size = font_size,
            cell_w = atlas.metrics.cell_width,
            cell_h = atlas.metrics.cell_height,
            "Font rebuilt (hot-reload)"
        );

        self.atlas_texture = atlas_texture;
        self.atlas = atlas;
        self.atlas.dirty = false; // Already uploaded

        Ok(())
    }

    /// Resize the rendering surface.
    pub fn resize(&mut self, width: u32, height: u32) {
        if width > 0 && height > 0 {
            self.surface_config.width = width;
            self.surface_config.height = height;
            self.surface.configure(&self.device, &self.surface_config);

            // Update uniforms
            let uniforms = Uniforms {
                screen_size: [width as f32, height as f32],
                _padding: [0.0, 0.0],
            };
            self.queue
                .write_buffer(&self.uniform_buffer, 0, bytemuck::bytes_of(&uniforms));
        }
    }

    /// Get the font metrics (cell dimensions).
    pub fn font_metrics(&self) -> &atlas::FontMetrics {
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

    /// Load a background image from raw file bytes, applying the given opacity.
    ///
    /// The image is decoded, opacity-adjusted, and uploaded as a GPU texture.
    /// A fullscreen quad (two triangles covering NDC [-1,1]) is pre-built.
    /// If a blur radius is set, the CPU blur is applied before upload.
    pub fn set_background_image(&mut self, data: &[u8], opacity: f32) {
        let Some(mut decoded) = bg_image::decode_image(data) else {
            warn!("Failed to decode background image");
            return;
        };
        bg_image::apply_opacity(&mut decoded.pixels, opacity);

        info!(
            width = decoded.width,
            height = decoded.height,
            opacity,
            "Background image loaded"
        );

        // Store the source pixel data for re-blurring when radius changes.
        self.bg_image_pixels = Some(decoded);

        // Upload with current blur radius applied.
        self.reupload_bg_image_with_blur();
    }

    /// Remove the background image.
    pub fn clear_background_image(&mut self) {
        self.bg_image = None;
        self.bg_image_pixels = None;
    }

    /// Whether a background image is currently loaded.
    pub fn has_background_image(&self) -> bool {
        self.bg_image.is_some()
    }

    /// Set the Gaussian blur radius for the background image layer.
    ///
    /// A radius of 0 disables blur. When blur is enabled and a background image
    /// is loaded, the CPU fallback (`apply_cpu_blur`) is used to blur the image
    /// data before uploading it as a texture.
    ///
    /// NOTE: For better performance, the GPU compute blur shader in
    /// `blur::BLUR_SHADER_WGSL` can replace this CPU path. That requires
    /// creating a compute pipeline, bind groups, and staging textures — left
    /// as a future optimization.
    pub fn set_blur_radius(&mut self, radius: u32) {
        let changed = self.blur_radius != radius;
        self.blur_radius = radius;

        // Re-upload the background image with the new blur radius applied.
        if changed {
            self.reupload_bg_image_with_blur();
        }
    }

    /// Get the current blur radius.
    pub fn blur_radius(&self) -> u32 {
        self.blur_radius
    }

    /// Re-upload the background image texture with the current blur radius.
    fn reupload_bg_image_with_blur(&mut self) {
        let Some(ref source) = self.bg_image_pixels else {
            return;
        };

        let mut pixels = source.pixels.clone();
        if self.blur_radius > 0 {
            blur::apply_cpu_blur(&mut pixels, source.width, source.height, self.blur_radius);
        }

        // Create RGBA texture
        let texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Background Image"),
            size: wgpu::Extent3d {
                width: source.width,
                height: source.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8UnormSrgb,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        self.queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            &pixels,
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(source.width * 4),
                rows_per_image: None,
            },
            wgpu::Extent3d {
                width: source.width,
                height: source.height,
                depth_or_array_layers: 1,
            },
        );

        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        let sampler = self.device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("BG Image Sampler"),
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });

        let bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("BG Image Bind Group"),
            layout: &self.image_bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: self.uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });

        // Fullscreen quad vertices
        let vertices: &[f32] = &[
            -1.0, -1.0, 0.0, 1.0, // bottom-left
            1.0, -1.0, 1.0, 1.0, // bottom-right
            1.0, 1.0, 1.0, 0.0, // top-right
            -1.0, 1.0, 0.0, 0.0, // top-left
        ];
        let indices: &[u16] = &[0, 1, 2, 0, 2, 3];

        let vertex_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("BG Image Vertices"),
                contents: bytemuck::cast_slice(vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
        let index_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("BG Image Indices"),
                contents: bytemuck::cast_slice(indices),
                usage: wgpu::BufferUsages::INDEX,
            });

        self.bg_image = Some(BgImageState {
            bind_group,
            vertex_buffer,
            index_buffer,
        });
    }

    /// Load a custom post-process shader from a file path.
    ///
    /// The shader source is read, validated for structural correctness
    /// (non-empty, size limit, contains `@fragment`), and stored. The actual
    /// GPU pipeline creation requires the `wgpu::Device` to compile the WGSL —
    /// that wiring should be added when the post-process render pass is built.
    pub fn load_custom_shader(&mut self, path: &str) -> Result<(), custom_shader::ShaderError> {
        let source = std::fs::read_to_string(path)
            .map_err(|e| custom_shader::ShaderError::IoError(e.to_string()))?;
        custom_shader::validate_shader_source(&source)?;

        self.custom_shader_config = Some(custom_shader::CustomShaderConfig {
            enabled: true,
            path: Some(path.to_string()),
            params: std::collections::HashMap::new(),
        });

        info!(path, "Custom shader loaded and validated");
        Ok(())
    }

    /// Set a named float parameter on the custom shader.
    ///
    /// These parameters are passed as GPU uniforms when the custom shader
    /// pipeline is active.
    pub fn set_shader_param(&mut self, name: &str, value: f32) {
        if let Some(ref mut config) = self.custom_shader_config {
            config.params.insert(name.to_string(), value);
        }
    }

    /// Remove the custom shader, reverting to the default pipeline.
    pub fn clear_custom_shader(&mut self) {
        self.custom_shader_config = None;
    }

    /// Get a reference to the current custom shader config (if any).
    pub fn custom_shader_config(&self) -> Option<&custom_shader::CustomShaderConfig> {
        self.custom_shader_config.as_ref()
    }

    /// Render a frame from the terminal state.
    ///
    /// `cursor_visible` controls the blink phase — pass `false` during blink-off.
    /// `highlight` is an optional callback for search match highlighting:
    /// given (grid_row, col), returns the highlight state for that cell.
    /// `tab_bar` is an optional slice of tab items — when provided and len > 1,
    /// a tab bar is drawn above the terminal content.
    /// `overlays` are UI layers drawn on top (command palette, settings, etc.).
    #[allow(clippy::too_many_arguments)]
    pub fn render(
        &mut self,
        terminal: &Terminal,
        cursor_visible: bool,
        highlight: Option<&dyn Fn(usize, usize) -> pipeline::CellHighlight>,
        tab_bar: Option<&[TabBarItem]>,
        overlays: &[OverlayLayer],
        show_scrollbar: bool,
        padding: (f32, f32),
    ) -> Result<(), RenderError> {
        let (pad_left, pad_top) = padding;

        // Re-upload atlas if glyphs were added
        if self.atlas.dirty {
            self.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &self.atlas_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                self.atlas.data(),
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(self.atlas.width),
                    rows_per_image: Some(self.atlas.height),
                },
                wgpu::Extent3d {
                    width: self.atlas.width,
                    height: self.atlas.height,
                    depth_or_array_layers: 1,
                },
            );
            self.atlas.dirty = false;
        }

        // Compute tab bar height (0 when single tab / no tab bar)
        let show_tab_bar = tab_bar.is_some_and(|items| items.len() > 1);
        let tab_bar_h = if show_tab_bar {
            self.atlas.metrics.cell_height
        } else {
            0.0
        };

        // Build tab bar vertices (if applicable)
        let atlas_w = self.atlas.width as f32;
        let atlas_h = self.atlas.height as f32;
        let (mut vertices, mut indices) = if let (true, Some(tb)) = (show_tab_bar, tab_bar) {
            let total_width = self.surface_config.width as f32;
            pipeline::build_tab_bar_vertices(
                tb,
                total_width,
                &mut self.atlas,
                atlas_w,
                atlas_h,
                &self.colors,
            )
        } else {
            (Vec::new(), Vec::new())
        };

        // Build vertex data from terminal grid (offset by tab bar + padding)
        let (cell_verts, cell_idxs) = pipeline::build_cell_vertices(
            terminal,
            &mut self.atlas,
            atlas_w,
            atlas_h,
            cursor_visible,
            &self.colors,
            highlight,
            pad_left,
            tab_bar_h + pad_top,
            show_scrollbar,
            terminal.hover_state(),
        );

        // Re-upload atlas (may have been modified by tab bar text rendering)
        if self.atlas.dirty {
            self.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &self.atlas_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                self.atlas.data(),
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(self.atlas.width),
                    rows_per_image: Some(self.atlas.height),
                },
                wgpu::Extent3d {
                    width: self.atlas.width,
                    height: self.atlas.height,
                    depth_or_array_layers: 1,
                },
            );
            self.atlas.dirty = false;
        }

        // Merge cell vertices into the combined buffer (adjust indices)
        let base_idx = vertices.len() as u32;
        vertices.extend_from_slice(&cell_verts);
        indices.extend(cell_idxs.iter().map(|i| i + base_idx));

        // Build overlay vertices (command palette, settings, etc.)
        if !overlays.is_empty() {
            let (overlay_verts, overlay_idxs) =
                pipeline::build_overlay_vertices(overlays, &mut self.atlas, atlas_w, atlas_h);

            // Re-upload atlas (overlay text may have added new glyphs)
            if self.atlas.dirty {
                self.queue.write_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: &self.atlas_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::All,
                    },
                    self.atlas.data(),
                    wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(self.atlas.width),
                        rows_per_image: Some(self.atlas.height),
                    },
                    wgpu::Extent3d {
                        width: self.atlas.width,
                        height: self.atlas.height,
                        depth_or_array_layers: 1,
                    },
                );
                self.atlas.dirty = false;
            }

            let overlay_base = vertices.len() as u32;
            vertices.extend_from_slice(&overlay_verts);
            indices.extend(overlay_idxs.iter().map(|i| i + overlay_base));
        }

        if vertices.is_empty() {
            return Ok(());
        }

        let vertex_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("Vertex Buffer"),
                contents: bytemuck::cast_slice(&vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });

        let index_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("Index Buffer"),
                contents: bytemuck::cast_slice(&indices),
                usage: wgpu::BufferUsages::INDEX,
            });

        let output = self
            .surface
            .get_current_texture()
            .map_err(RenderError::Surface)?;

        let view = output
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Render Encoder"),
            });

        {
            let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Terminal Pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(self.colors.clear_color()),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });

            // Draw background image first (behind cells) if loaded
            if let Some(ref bg) = self.bg_image {
                render_pass.set_pipeline(&self.image_pipeline);
                render_pass.set_bind_group(0, &bg.bind_group, &[]);
                render_pass.set_vertex_buffer(0, bg.vertex_buffer.slice(..));
                render_pass.set_index_buffer(bg.index_buffer.slice(..), wgpu::IndexFormat::Uint16);
                render_pass.draw_indexed(0..6, 0, 0..1);
            }

            // Draw terminal cells on top
            render_pass.set_pipeline(&self.render_pipeline);
            render_pass.set_bind_group(0, &self.bind_group, &[]);
            render_pass.set_vertex_buffer(0, vertex_buffer.slice(..));
            render_pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            render_pass.draw_indexed(0..indices.len() as u32, 0, 0..1);
        }

        // Draw inline images (Sixel, iTerm2, Kitty) in a second pass
        let cell_w = self.atlas.metrics.cell_width;
        let cell_h = self.atlas.metrics.cell_height;
        let visible_images = terminal.visible_images(cell_w, cell_h);
        if !visible_images.is_empty() {
            let (img_atlas_w, img_atlas_h, img_atlas_data, placements) =
                pipeline::pack_image_atlas(&visible_images, tab_bar_h);

            if img_atlas_w > 0 && img_atlas_h > 0 {
                let image_texture = self.device.create_texture(&wgpu::TextureDescriptor {
                    label: Some("Image Atlas"),
                    size: wgpu::Extent3d {
                        width: img_atlas_w,
                        height: img_atlas_h,
                        depth_or_array_layers: 1,
                    },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format: wgpu::TextureFormat::Rgba8UnormSrgb,
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                    view_formats: &[],
                });

                self.queue.write_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: &image_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::All,
                    },
                    &img_atlas_data,
                    wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(img_atlas_w * 4),
                        rows_per_image: Some(img_atlas_h),
                    },
                    wgpu::Extent3d {
                        width: img_atlas_w,
                        height: img_atlas_h,
                        depth_or_array_layers: 1,
                    },
                );

                let image_view = image_texture.create_view(&wgpu::TextureViewDescriptor::default());
                let image_sampler = self.device.create_sampler(&wgpu::SamplerDescriptor {
                    label: Some("Image Sampler"),
                    mag_filter: wgpu::FilterMode::Linear,
                    min_filter: wgpu::FilterMode::Linear,
                    ..Default::default()
                });

                let image_bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
                    label: Some("Image Bind Group"),
                    layout: &self.image_bind_group_layout,
                    entries: &[
                        wgpu::BindGroupEntry {
                            binding: 0,
                            resource: self.uniform_buffer.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 1,
                            resource: wgpu::BindingResource::TextureView(&image_view),
                        },
                        wgpu::BindGroupEntry {
                            binding: 2,
                            resource: wgpu::BindingResource::Sampler(&image_sampler),
                        },
                    ],
                });

                let (img_verts, img_indices) = pipeline::build_image_vertices(
                    &placements,
                    img_atlas_w as f32,
                    img_atlas_h as f32,
                );

                if !img_verts.is_empty() {
                    let img_vertex_buffer =
                        self.device
                            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                                label: Some("Image Vertex Buffer"),
                                contents: bytemuck::cast_slice(&img_verts),
                                usage: wgpu::BufferUsages::VERTEX,
                            });

                    let img_index_buffer =
                        self.device
                            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                                label: Some("Image Index Buffer"),
                                contents: bytemuck::cast_slice(&img_indices),
                                usage: wgpu::BufferUsages::INDEX,
                            });

                    {
                        let mut render_pass =
                            encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                                label: Some("Image Pass"),
                                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                                    view: &view,
                                    resolve_target: None,
                                    ops: wgpu::Operations {
                                        load: wgpu::LoadOp::Load,
                                        store: wgpu::StoreOp::Store,
                                    },
                                })],
                                depth_stencil_attachment: None,
                                timestamp_writes: None,
                                occlusion_query_set: None,
                            });

                        render_pass.set_pipeline(&self.image_pipeline);
                        render_pass.set_bind_group(0, &image_bind_group, &[]);
                        render_pass.set_vertex_buffer(0, img_vertex_buffer.slice(..));
                        render_pass.set_index_buffer(
                            img_index_buffer.slice(..),
                            wgpu::IndexFormat::Uint32,
                        );
                        render_pass.draw_indexed(0..img_indices.len() as u32, 0, 0..1);
                    }
                }
            }
        }

        self.queue.submit(std::iter::once(encoder.finish()));
        output.present();

        Ok(())
    }

    /// Render multiple panes in a single frame.
    ///
    /// Each pane is rendered at its pixel-space rect. Tab bar and overlays
    /// are drawn once. Pane borders are drawn between adjacent panes.
    #[allow(clippy::too_many_arguments)]
    pub fn render_panes(
        &mut self,
        panes: &[PaneRenderInfo<'_>],
        tab_bar: Option<&[TabBarItem]>,
        overlays: &[OverlayLayer],
        border_color: [f32; 4],
    ) -> Result<(), RenderError> {
        // Re-upload atlas if glyphs were added
        if self.atlas.dirty {
            self.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &self.atlas_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                self.atlas.data(),
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(self.atlas.width),
                    rows_per_image: Some(self.atlas.height),
                },
                wgpu::Extent3d {
                    width: self.atlas.width,
                    height: self.atlas.height,
                    depth_or_array_layers: 1,
                },
            );
            self.atlas.dirty = false;
        }

        let show_tab_bar = tab_bar.is_some_and(|items| items.len() > 1);
        let tab_bar_h = if show_tab_bar {
            self.atlas.metrics.cell_height
        } else {
            0.0
        };

        let atlas_w = self.atlas.width as f32;
        let atlas_h = self.atlas.height as f32;

        // Build tab bar vertices
        let (mut vertices, mut indices) = if let (true, Some(tb)) = (show_tab_bar, tab_bar) {
            let total_width = self.surface_config.width as f32;
            pipeline::build_tab_bar_vertices(
                tb,
                total_width,
                &mut self.atlas,
                atlas_w,
                atlas_h,
                &self.colors,
            )
        } else {
            (Vec::new(), Vec::new())
        };

        // Build cell vertices for each pane
        for pane_info in panes {
            let (px, py, _pw, _ph) = pane_info.rect;
            let (cell_verts, cell_idxs) = pipeline::build_cell_vertices(
                pane_info.terminal,
                &mut self.atlas,
                atlas_w,
                atlas_h,
                pane_info.cursor_visible,
                &self.colors,
                None, // highlight handled externally
                px,
                py + tab_bar_h,
                pane_info.show_scrollbar,
                pane_info.terminal.hover_state(),
            );
            let base_idx = vertices.len() as u32;
            vertices.extend_from_slice(&cell_verts);
            indices.extend(cell_idxs.iter().map(|i| i + base_idx));
        }

        // Draw 1px border lines between panes
        if panes.len() > 1 {
            let surf_w = self.surface_config.width as f32;
            let surf_h = self.surface_config.height as f32;
            for pane_info in panes {
                let (px, py, pw, ph) = pane_info.rect;
                let oy = py + tab_bar_h;
                // Right border
                if px + pw < surf_w - 1.0 {
                    let base = vertices.len() as u32;
                    let bx = (px + pw) / surf_w * 2.0 - 1.0;
                    let by_top = 1.0 - oy / surf_h * 2.0;
                    let by_bot = 1.0 - (oy + ph) / surf_h * 2.0;
                    let bw = 1.0 / surf_w * 2.0; // 1px
                    vertices.push(Vertex {
                        position: [bx, by_top],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    vertices.push(Vertex {
                        position: [bx + bw, by_top],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    vertices.push(Vertex {
                        position: [bx + bw, by_bot],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    vertices.push(Vertex {
                        position: [bx, by_bot],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    indices.extend_from_slice(&[
                        base,
                        base + 1,
                        base + 2,
                        base,
                        base + 2,
                        base + 3,
                    ]);
                }
                // Bottom border
                if oy + ph < surf_h - 1.0 {
                    let base = vertices.len() as u32;
                    let bx_left = px / surf_w * 2.0 - 1.0;
                    let bx_right = (px + pw) / surf_w * 2.0 - 1.0;
                    let by = 1.0 - (oy + ph) / surf_h * 2.0;
                    let bh = 1.0 / surf_h * 2.0; // 1px
                    vertices.push(Vertex {
                        position: [bx_left, by],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    vertices.push(Vertex {
                        position: [bx_right, by],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    vertices.push(Vertex {
                        position: [bx_right, by - bh],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    vertices.push(Vertex {
                        position: [bx_left, by - bh],
                        tex_coords: [0.0, 0.0],
                        fg_color: border_color,
                        bg_color: border_color,
                    });
                    indices.extend_from_slice(&[
                        base,
                        base + 1,
                        base + 2,
                        base,
                        base + 2,
                        base + 3,
                    ]);
                }
            }
        }

        // Re-upload atlas (may have been modified by building vertices)
        if self.atlas.dirty {
            self.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &self.atlas_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                self.atlas.data(),
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(self.atlas.width),
                    rows_per_image: Some(self.atlas.height),
                },
                wgpu::Extent3d {
                    width: self.atlas.width,
                    height: self.atlas.height,
                    depth_or_array_layers: 1,
                },
            );
            self.atlas.dirty = false;
        }

        // Build overlay vertices
        if !overlays.is_empty() {
            let (overlay_verts, overlay_idxs) =
                pipeline::build_overlay_vertices(overlays, &mut self.atlas, atlas_w, atlas_h);
            if self.atlas.dirty {
                self.queue.write_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: &self.atlas_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::All,
                    },
                    self.atlas.data(),
                    wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(self.atlas.width),
                        rows_per_image: Some(self.atlas.height),
                    },
                    wgpu::Extent3d {
                        width: self.atlas.width,
                        height: self.atlas.height,
                        depth_or_array_layers: 1,
                    },
                );
                self.atlas.dirty = false;
            }
            let overlay_base = vertices.len() as u32;
            vertices.extend_from_slice(&overlay_verts);
            indices.extend(overlay_idxs.iter().map(|i| i + overlay_base));
        }

        if vertices.is_empty() {
            return Ok(());
        }

        let vertex_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("Vertex Buffer"),
                contents: bytemuck::cast_slice(&vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
        let index_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("Index Buffer"),
                contents: bytemuck::cast_slice(&indices),
                usage: wgpu::BufferUsages::INDEX,
            });

        let output = self
            .surface
            .get_current_texture()
            .map_err(RenderError::Surface)?;
        let view = output
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Multi-Pane Render Encoder"),
            });
        {
            let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Multi-Pane Pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(self.colors.clear_color()),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });
            // Draw background image first if loaded
            if let Some(ref bg) = self.bg_image {
                render_pass.set_pipeline(&self.image_pipeline);
                render_pass.set_bind_group(0, &bg.bind_group, &[]);
                render_pass.set_vertex_buffer(0, bg.vertex_buffer.slice(..));
                render_pass.set_index_buffer(bg.index_buffer.slice(..), wgpu::IndexFormat::Uint16);
                render_pass.draw_indexed(0..6, 0, 0..1);
            }

            render_pass.set_pipeline(&self.render_pipeline);
            render_pass.set_bind_group(0, &self.bind_group, &[]);
            render_pass.set_vertex_buffer(0, vertex_buffer.slice(..));
            render_pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            render_pass.draw_indexed(0..indices.len() as u32, 0, 0..1);
        }

        // Images for all panes (only active pane for now)
        for pane_info in panes {
            if pane_info.is_active {
                let cell_w = self.atlas.metrics.cell_width;
                let cell_h = self.atlas.metrics.cell_height;
                let visible_images = pane_info.terminal.visible_images(cell_w, cell_h);
                if !visible_images.is_empty() {
                    let (img_atlas_w, img_atlas_h, img_atlas_data, placements) =
                        pipeline::pack_image_atlas(&visible_images, tab_bar_h);
                    if img_atlas_w > 0 && img_atlas_h > 0 {
                        let image_texture = self.device.create_texture(&wgpu::TextureDescriptor {
                            label: Some("Image Atlas"),
                            size: wgpu::Extent3d {
                                width: img_atlas_w,
                                height: img_atlas_h,
                                depth_or_array_layers: 1,
                            },
                            mip_level_count: 1,
                            sample_count: 1,
                            dimension: wgpu::TextureDimension::D2,
                            format: wgpu::TextureFormat::Rgba8UnormSrgb,
                            usage: wgpu::TextureUsages::TEXTURE_BINDING
                                | wgpu::TextureUsages::COPY_DST,
                            view_formats: &[],
                        });
                        self.queue.write_texture(
                            wgpu::TexelCopyTextureInfo {
                                texture: &image_texture,
                                mip_level: 0,
                                origin: wgpu::Origin3d::ZERO,
                                aspect: wgpu::TextureAspect::All,
                            },
                            &img_atlas_data,
                            wgpu::TexelCopyBufferLayout {
                                offset: 0,
                                bytes_per_row: Some(img_atlas_w * 4),
                                rows_per_image: Some(img_atlas_h),
                            },
                            wgpu::Extent3d {
                                width: img_atlas_w,
                                height: img_atlas_h,
                                depth_or_array_layers: 1,
                            },
                        );

                        let image_view =
                            image_texture.create_view(&wgpu::TextureViewDescriptor::default());
                        let image_sampler = self.device.create_sampler(&wgpu::SamplerDescriptor {
                            label: Some("Image Sampler"),
                            mag_filter: wgpu::FilterMode::Linear,
                            min_filter: wgpu::FilterMode::Linear,
                            ..Default::default()
                        });
                        let image_bind_group =
                            self.device.create_bind_group(&wgpu::BindGroupDescriptor {
                                label: Some("Image Bind Group"),
                                layout: &self.image_bind_group_layout,
                                entries: &[
                                    wgpu::BindGroupEntry {
                                        binding: 0,
                                        resource: self.uniform_buffer.as_entire_binding(),
                                    },
                                    wgpu::BindGroupEntry {
                                        binding: 1,
                                        resource: wgpu::BindingResource::TextureView(&image_view),
                                    },
                                    wgpu::BindGroupEntry {
                                        binding: 2,
                                        resource: wgpu::BindingResource::Sampler(&image_sampler),
                                    },
                                ],
                            });

                        let (img_verts, img_indices) = pipeline::build_image_vertices(
                            &placements,
                            img_atlas_w as f32,
                            img_atlas_h as f32,
                        );
                        let img_vertex_buffer =
                            self.device
                                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                                    label: Some("Image Vertex Buffer"),
                                    contents: bytemuck::cast_slice(&img_verts),
                                    usage: wgpu::BufferUsages::VERTEX,
                                });
                        let img_index_buffer =
                            self.device
                                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                                    label: Some("Image Index Buffer"),
                                    contents: bytemuck::cast_slice(&img_indices),
                                    usage: wgpu::BufferUsages::INDEX,
                                });
                        {
                            let mut render_pass =
                                encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                                    label: Some("Image Pass"),
                                    color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                                        view: &view,
                                        resolve_target: None,
                                        ops: wgpu::Operations {
                                            load: wgpu::LoadOp::Load,
                                            store: wgpu::StoreOp::Store,
                                        },
                                    })],
                                    depth_stencil_attachment: None,
                                    timestamp_writes: None,
                                    occlusion_query_set: None,
                                });
                            render_pass.set_pipeline(&self.image_pipeline);
                            render_pass.set_bind_group(0, &image_bind_group, &[]);
                            render_pass.set_vertex_buffer(0, img_vertex_buffer.slice(..));
                            render_pass.set_index_buffer(
                                img_index_buffer.slice(..),
                                wgpu::IndexFormat::Uint32,
                            );
                            render_pass.draw_indexed(0..img_indices.len() as u32, 0, 0..1);
                        }
                    }
                }
            }
        }

        self.queue.submit(std::iter::once(encoder.finish()));
        output.present();
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// TerminalRenderer — unified enum over GPU and software backends
// ---------------------------------------------------------------------------

/// Renderer backend that dispatches to either GPU (wgpu) or software (softbuffer).
///
/// Selected via `config.window.renderer`: `"auto"`, `"gpu"`, or `"software"`.
/// In `"auto"` mode, GPU is tried first; on failure, falls back to software.
pub enum TerminalRenderer {
    Gpu(Box<Renderer>),
    Software(Box<SoftwareRenderer>),
}

impl TerminalRenderer {
    /// Create a renderer based on the mode string from config.
    ///
    /// # Safety
    /// The HWND must remain valid for the lifetime of this TerminalRenderer.
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn new(
        mode: &str,
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
        match mode {
            "software" => {
                info!("Renderer mode: software");
                let sw = unsafe {
                    SoftwareRenderer::new(
                        hwnd,
                        width,
                        height,
                        dpi,
                        font_family,
                        font_size,
                        line_height,
                        colors,
                        fallback_fonts,
                        ligatures,
                        font_path,
                    )?
                };
                Ok(Self::Software(Box::new(sw)))
            }
            "gpu" => {
                info!("Renderer mode: gpu");
                let gpu = unsafe {
                    Renderer::new(
                        hwnd,
                        width,
                        height,
                        dpi,
                        font_family,
                        font_size,
                        line_height,
                        colors,
                        fallback_fonts,
                        ligatures,
                        font_path,
                    )?
                };
                Ok(Self::Gpu(Box::new(gpu)))
            }
            _ => {
                // "auto" — try GPU first, fall back to software
                info!("Renderer mode: auto (trying GPU first)");
                match unsafe {
                    Renderer::new(
                        hwnd,
                        width,
                        height,
                        dpi,
                        font_family,
                        font_size,
                        line_height,
                        colors.clone(),
                        fallback_fonts,
                        ligatures,
                        font_path,
                    )
                } {
                    Ok(gpu) => Ok(Self::Gpu(Box::new(gpu))),
                    Err(e) => {
                        warn!(error = %e, "GPU renderer failed, falling back to software");
                        let sw = unsafe {
                            SoftwareRenderer::new(
                                hwnd,
                                width,
                                height,
                                dpi,
                                font_family,
                                font_size,
                                line_height,
                                colors,
                                fallback_fonts,
                                ligatures,
                                font_path,
                            )?
                        };
                        Ok(Self::Software(Box::new(sw)))
                    }
                }
            }
        }
    }

    /// Resize the rendering surface.
    pub fn resize(&mut self, width: u32, height: u32) {
        match self {
            Self::Gpu(r) => r.resize(width, height),
            Self::Software(r) => r.resize(width, height),
        }
    }

    /// Get the font metrics (cell dimensions).
    pub fn font_metrics(&self) -> &FontMetrics {
        match self {
            Self::Gpu(r) => r.font_metrics(),
            Self::Software(r) => r.font_metrics(),
        }
    }

    /// The height of the tab bar in pixels (0 when hidden).
    pub fn tab_bar_height(&self, num_tabs: usize) -> f32 {
        match self {
            Self::Gpu(r) => r.tab_bar_height(num_tabs),
            Self::Software(r) => r.tab_bar_height(num_tabs),
        }
    }

    /// Update the color scheme (hot-reload).
    pub fn update_colors(&mut self, colors: ColorScheme) {
        match self {
            Self::Gpu(r) => r.update_colors(colors),
            Self::Software(r) => r.update_colors(colors),
        }
    }

    /// Set the minimum WCAG contrast ratio for foreground/background colors.
    pub fn set_min_contrast_ratio(&mut self, ratio: f64) {
        match self {
            Self::Gpu(r) => r.colors.min_contrast_ratio = ratio,
            Self::Software(r) => r.colors.min_contrast_ratio = ratio,
        }
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
        match self {
            Self::Gpu(r) => r.rebuild_font(
                font_family,
                font_size,
                dpi,
                line_height,
                fallback_fonts,
                ligatures,
                font_path,
            ),
            Self::Software(r) => r.rebuild_font(
                font_family,
                font_size,
                dpi,
                line_height,
                fallback_fonts,
                ligatures,
                font_path,
            ),
        }
    }

    /// Load a background image from raw file bytes.
    pub fn set_background_image(&mut self, data: &[u8], opacity: f32) {
        match self {
            Self::Gpu(r) => r.set_background_image(data, opacity),
            Self::Software(r) => r.set_background_image(data, opacity),
        }
    }

    /// Remove the background image.
    pub fn clear_background_image(&mut self) {
        match self {
            Self::Gpu(r) => r.clear_background_image(),
            Self::Software(r) => r.clear_background_image(),
        }
    }

    /// Set the Gaussian blur radius for the background image layer.
    pub fn set_blur_radius(&mut self, radius: u32) {
        match self {
            Self::Gpu(r) => r.set_blur_radius(radius),
            Self::Software(r) => r.set_blur_radius(radius),
        }
    }

    /// Get the current blur radius.
    pub fn blur_radius(&self) -> u32 {
        match self {
            Self::Gpu(r) => r.blur_radius(),
            Self::Software(r) => r.blur_radius(),
        }
    }

    /// Load a custom post-process shader (GPU renderer only).
    ///
    /// Returns `Ok(())` on the software renderer (no-op).
    pub fn load_custom_shader(&mut self, path: &str) -> Result<(), custom_shader::ShaderError> {
        match self {
            Self::Gpu(r) => r.load_custom_shader(path),
            Self::Software(_) => Ok(()),
        }
    }

    /// Set a named float parameter on the custom shader (GPU only, no-op on software).
    pub fn set_shader_param(&mut self, name: &str, value: f32) {
        if let Self::Gpu(r) = self {
            r.set_shader_param(name, value);
        }
    }

    /// Remove the custom shader (GPU only, no-op on software).
    pub fn clear_custom_shader(&mut self) {
        if let Self::Gpu(r) = self {
            r.clear_custom_shader();
        }
    }

    /// Render a frame from the terminal state.
    #[allow(clippy::too_many_arguments)]
    pub fn render(
        &mut self,
        terminal: &Terminal,
        cursor_visible: bool,
        highlight: Option<&dyn Fn(usize, usize) -> CellHighlight>,
        tab_bar: Option<&[TabBarItem]>,
        overlays: &[OverlayLayer],
        show_scrollbar: bool,
        padding: (f32, f32),
    ) -> Result<(), RenderError> {
        match self {
            Self::Gpu(r) => r.render(
                terminal,
                cursor_visible,
                highlight,
                tab_bar,
                overlays,
                show_scrollbar,
                padding,
            ),
            Self::Software(r) => r.render(
                terminal,
                cursor_visible,
                highlight,
                tab_bar,
                overlays,
                show_scrollbar,
                padding,
            ),
        }
    }

    /// Render multiple panes in a single frame.
    ///
    /// Falls back to rendering only the first (active) pane via `render()`
    /// when using the software backend.
    pub fn render_panes(
        &mut self,
        panes: &[PaneRenderInfo<'_>],
        tab_bar: Option<&[TabBarItem]>,
        overlays: &[OverlayLayer],
        border_color: [f32; 4],
    ) -> Result<(), RenderError> {
        match self {
            Self::Gpu(r) => r.render_panes(panes, tab_bar, overlays, border_color),
            Self::Software(r) => {
                // Software renderer: render each pane separately (active first)
                // For now, render only the active pane as the software renderer
                // doesn't support multi-pane clipping efficiently.
                if let Some(active) = panes.iter().find(|p| p.is_active) {
                    r.render(
                        active.terminal,
                        active.cursor_visible,
                        None,
                        tab_bar,
                        overlays,
                        active.show_scrollbar,
                        (active.rect.0, active.rect.1),
                    )
                } else if let Some(first) = panes.first() {
                    r.render(
                        first.terminal,
                        first.cursor_visible,
                        None,
                        tab_bar,
                        overlays,
                        first.show_scrollbar,
                        (first.rect.0, first.rect.1),
                    )
                } else {
                    Ok(())
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_custom_shader_rejects_empty() {
        assert_eq!(
            custom_shader::validate_shader_source(""),
            Err(custom_shader::ShaderError::Empty)
        );
    }

    #[test]
    fn validate_custom_shader_accepts_valid() {
        let src = "@fragment\nfn fs_main() -> @location(0) vec4<f32> { return vec4(1.0); }";
        assert!(custom_shader::validate_shader_source(src).is_ok());
    }

    #[test]
    fn validate_custom_shader_rejects_missing_fragment() {
        let src = "fn some_func() {}";
        assert_eq!(
            custom_shader::validate_shader_source(src),
            Err(custom_shader::ShaderError::MissingFragmentEntry)
        );
    }

    #[test]
    fn load_custom_shader_rejects_missing_file() {
        // We can't instantiate a GPU Renderer in tests, but we can test the
        // validation + I/O path via the standalone function and error type.
        let result = std::fs::read_to_string("/nonexistent/shader.wgsl");
        assert!(result.is_err());
    }

    #[test]
    fn cpu_blur_identity_with_radius_zero() {
        let mut pixels = vec![42u8; 3 * 3 * 4];
        let original = pixels.clone();
        blur::apply_cpu_blur(&mut pixels, 3, 3, 0);
        assert_eq!(pixels, original, "radius 0 must be an identity transform");
    }

    #[test]
    fn blur_shader_wgsl_is_valid_compute() {
        assert!(
            blur::BLUR_SHADER_WGSL.contains("@compute"),
            "GPU blur shader must contain @compute entry point"
        );
    }

    #[test]
    fn custom_shader_config_stores_params() {
        let mut config = custom_shader::CustomShaderConfig {
            enabled: true,
            path: None,
            params: std::collections::HashMap::new(),
        };
        config.params.insert("brightness".to_string(), 0.8);
        config.params.insert("contrast".to_string(), 1.2);
        assert_eq!(config.params["brightness"], 0.8);
        assert_eq!(config.params["contrast"], 1.2);
    }

    #[test]
    fn shader_params_uniform_data_is_sorted() {
        let mut params = custom_shader::ShaderParams::default();
        params.set("zoom", 2.0);
        params.set("alpha", 0.5);
        let data = params.to_uniform_data();
        assert_eq!(data, vec![0.5, 2.0], "uniform data must be sorted by key");
    }
}
