// Terminal cell rendering shader.
// Each cell is a textured quad with foreground/background colors.

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) tex_coords: vec2<f32>,
    @location(2) fg_color: vec4<f32>,
    @location(3) bg_color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) tex_coords: vec2<f32>,
    @location(1) fg_color: vec4<f32>,
    @location(2) bg_color: vec4<f32>,
};

struct Uniforms {
    screen_size: vec2<f32>,
    _padding: vec2<f32>,
};

@group(0) @binding(0)
var<uniform> uniforms: Uniforms;

@group(0) @binding(1)
var atlas_texture: texture_2d<f32>;

@group(0) @binding(2)
var atlas_sampler: sampler;

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Convert pixel coordinates to clip space (-1..1)
    let x = (input.position.x / uniforms.screen_size.x) * 2.0 - 1.0;
    let y = 1.0 - (input.position.y / uniforms.screen_size.y) * 2.0;

    output.clip_position = vec4<f32>(x, y, 0.0, 1.0);
    output.tex_coords = input.tex_coords;
    output.fg_color = input.fg_color;
    output.bg_color = input.bg_color;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let alpha = textureSample(atlas_texture, atlas_sampler, input.tex_coords).r;
    // Mix background and foreground based on glyph alpha
    return mix(input.bg_color, input.fg_color, alpha);
}
