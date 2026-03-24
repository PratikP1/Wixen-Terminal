// Image rendering shader — draws RGBA textured quads for inline images.
// Uses the same vertex format and uniforms as the text shader,
// but samples an RGBA texture instead of an R8 glyph atlas.

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) tex_coords: vec2<f32>,
    @location(2) fg_color: vec4<f32>,   // unused for images
    @location(3) bg_color: vec4<f32>,   // unused for images
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) tex_coords: vec2<f32>,
};

struct Uniforms {
    screen_size: vec2<f32>,
    _padding: vec2<f32>,
};

@group(0) @binding(0)
var<uniform> uniforms: Uniforms;

@group(0) @binding(1)
var image_texture: texture_2d<f32>;

@group(0) @binding(2)
var image_sampler: sampler;

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let x = (input.position.x / uniforms.screen_size.x) * 2.0 - 1.0;
    let y = 1.0 - (input.position.y / uniforms.screen_size.y) * 2.0;
    output.clip_position = vec4<f32>(x, y, 0.0, 1.0);
    output.tex_coords = input.tex_coords;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return textureSample(image_texture, image_sampler, input.tex_coords);
}
