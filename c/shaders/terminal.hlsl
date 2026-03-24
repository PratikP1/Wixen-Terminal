/* terminal.hlsl — D3D11 vertex + pixel shader for terminal cell rendering */

cbuffer Uniforms : register(b0) {
    float2 screen_size;
    float2 _padding;
};

struct VSInput {
    float2 position : POSITION;
    float2 tex_coord : TEXCOORD0;
    float4 fg_color : COLOR0;
    float4 bg_color : COLOR1;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 tex_coord : TEXCOORD0;
    float4 fg_color : COLOR0;
    float4 bg_color : COLOR1;
};

/* Vertex shader: convert pixel coordinates to NDC */
PSInput VSMain(VSInput input) {
    PSInput output;
    /* Convert pixel position to normalized device coordinates [-1, 1] */
    float2 ndc;
    ndc.x = (input.position.x / screen_size.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (input.position.y / screen_size.y) * 2.0;
    output.position = float4(ndc, 0.0, 1.0);
    output.tex_coord = input.tex_coord;
    output.fg_color = input.fg_color;
    output.bg_color = input.bg_color;
    return output;
}

/* Glyph atlas texture (R8 alpha-only) */
Texture2D atlas_texture : register(t0);
SamplerState atlas_sampler : register(s0);

/* Pixel shader: blend glyph alpha with fg/bg colors */
float4 PSMain(PSInput input) : SV_TARGET {
    float alpha = atlas_texture.Sample(atlas_sampler, input.tex_coord).r;
    /* Mix background and foreground based on glyph alpha */
    float4 color = lerp(input.bg_color, input.fg_color, alpha);
    return color;
}
