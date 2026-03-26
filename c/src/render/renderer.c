/* renderer.c — D3D11 GPU renderer (skeleton)
 *
 * Full implementation will add:
 * - D3D11 device/swapchain/pipeline creation
 * - HLSL shader compilation
 * - DirectWrite glyph atlas
 * - Vertex buffer building per frame
 * - Background image, blur, cursor rendering
 */
#ifdef _WIN32

#include "wixen/render/renderer.h"
#define COBJMACROS
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <stdlib.h>
#include <string.h>

#include <d3dcompiler.h>
#include "wixen/render/pipeline.h"
#include "wixen/render/atlas.h"
#include "wixen/render/software.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

/* Embedded HLSL source (avoids needing external file at runtime) */
static const char hlsl_source[] =
    "cbuffer Uniforms : register(b0) {\n"
    "  float2 screen_size;\n"
    "  float2 _padding;\n"
    "};\n"
    "struct VSInput {\n"
    "  float2 position : POSITION;\n"
    "  float2 tex_coord : TEXCOORD0;\n"
    "  float4 fg_color : COLOR0;\n"
    "  float4 bg_color : COLOR1;\n"
    "};\n"
    "struct PSInput {\n"
    "  float4 position : SV_POSITION;\n"
    "  float2 tex_coord : TEXCOORD0;\n"
    "  float4 fg_color : COLOR0;\n"
    "  float4 bg_color : COLOR1;\n"
    "};\n"
    "PSInput VSMain(VSInput input) {\n"
    "  PSInput output;\n"
    "  float2 ndc;\n"
    "  ndc.x = (input.position.x / screen_size.x) * 2.0 - 1.0;\n"
    "  ndc.y = 1.0 - (input.position.y / screen_size.y) * 2.0;\n"
    "  output.position = float4(ndc, 0.0, 1.0);\n"
    "  output.tex_coord = input.tex_coord;\n"
    "  output.fg_color = input.fg_color;\n"
    "  output.bg_color = input.bg_color;\n"
    "  return output;\n"
    "}\n"
    "Texture2D atlas_texture : register(t0);\n"
    "SamplerState atlas_sampler : register(s0);\n"
    "float4 PSMain(PSInput input) : SV_TARGET {\n"
    "  float alpha = atlas_texture.Sample(atlas_sampler, input.tex_coord).r;\n"
    "  float4 color = lerp(input.bg_color, input.fg_color, alpha);\n"
    "  return color;\n"
    "}\n";

/* HLSL for image quads (samples RGBA texture directly) */
static const char image_hlsl_source[] =
    "cbuffer Uniforms : register(b0) {\n"
    "  float2 screen_size;\n"
    "  float2 _padding;\n"
    "};\n"
    "struct VSInput {\n"
    "  float2 position : POSITION;\n"
    "  float2 tex_coord : TEXCOORD0;\n"
    "};\n"
    "struct PSInput {\n"
    "  float4 position : SV_POSITION;\n"
    "  float2 tex_coord : TEXCOORD0;\n"
    "};\n"
    "PSInput ImgVS(VSInput input) {\n"
    "  PSInput output;\n"
    "  float2 ndc;\n"
    "  ndc.x = (input.position.x / screen_size.x) * 2.0 - 1.0;\n"
    "  ndc.y = 1.0 - (input.position.y / screen_size.y) * 2.0;\n"
    "  output.position = float4(ndc, 0.0, 1.0);\n"
    "  output.tex_coord = input.tex_coord;\n"
    "  return output;\n"
    "}\n"
    "Texture2D img_texture : register(t0);\n"
    "SamplerState img_sampler : register(s0);\n"
    "float4 ImgPS(PSInput input) : SV_TARGET {\n"
    "  return img_texture.Sample(img_sampler, input.tex_coord);\n"
    "}\n";

/* Image vertex: position + UV only */
typedef struct {
    float position[2];
    float tex_coord[2];
} WixenImageVertex;

#define MAX_VERTICES (256 * 64 * 6) /* 256 cols * 64 rows * 6 verts per cell */

struct WixenRenderer {
    /* D3D11 state */
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swapchain;
    ID3D11RenderTargetView *rtv;

    /* Shaders */
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *input_layout;

    /* Buffers */
    ID3D11Buffer *vertex_buffer;
    ID3D11Buffer *uniform_buffer;

    /* Atlas texture */
    ID3D11Texture2D *atlas_tex;
    ID3D11ShaderResourceView *atlas_srv;
    ID3D11SamplerState *sampler;

    /* DirectWrite glyph atlas */
    WixenGlyphAtlas *atlas;
    uint32_t atlas_width;
    uint32_t atlas_height;

    /* State */
    uint32_t width;
    uint32_t height;
    WixenColorScheme colors;
    WixenFontMetrics metrics;

    /* Image rendering pipeline (Sixel etc.) */
    ID3D11VertexShader *img_vs;
    ID3D11PixelShader *img_ps;
    ID3D11InputLayout *img_input_layout;
    ID3D11Buffer *img_vertex_buffer;

    /* GDI software fallback */
    bool use_software;
    WixenSoftwareRenderer *soft;
    HWND hwnd;  /* Needed for GDI fallback resize */
};

/* --- Image shader compilation helper --- */
static void renderer_compile_image_shaders(WixenRenderer *r) {
    if (!r || !r->device) return;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *err_blob = NULL;
    HRESULT hr = D3DCompile(image_hlsl_source, strlen(image_hlsl_source), "image.hlsl",
                             NULL, NULL, "ImgVS", "vs_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &vs_blob, &err_blob);
    if (err_blob) { ID3D10Blob_Release(err_blob); err_blob = NULL; }
    if (FAILED(hr)) return;

    hr = D3DCompile(image_hlsl_source, strlen(image_hlsl_source), "image.hlsl",
                     NULL, NULL, "ImgPS", "ps_5_0",
                     D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &ps_blob, &err_blob);
    if (err_blob) ID3D10Blob_Release(err_blob);
    if (FAILED(hr)) { ID3D10Blob_Release(vs_blob); return; }

    ID3D11Device_CreateVertexShader(r->device,
        ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob),
        NULL, &r->img_vs);
    ID3D11Device_CreatePixelShader(r->device,
        ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob),
        NULL, &r->img_ps);

    D3D11_INPUT_ELEMENT_DESC img_layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11Device_CreateInputLayout(r->device, img_layout, 2,
        ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob),
        &r->img_input_layout);

    ID3D10Blob_Release(vs_blob);
    ID3D10Blob_Release(ps_blob);

    D3D11_BUFFER_DESC vbd = {0};
    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.ByteWidth = 6 * sizeof(WixenImageVertex);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(r->device, &vbd, NULL, &r->img_vertex_buffer);
}

/* --- Phased Lifecycle (for responsive startup) --- */

WixenRenderer *wixen_renderer_create_begin(HWND hwnd, uint32_t width, uint32_t height,
                                             const WixenColorScheme *colors) {
    WixenRenderer *r = calloc(1, sizeof(WixenRenderer));
    if (!r) return NULL;
    r->width = width;
    r->height = height;
    r->colors = *colors;
    r->metrics.cell_width = 8.0f; /* Placeholder until atlas */
    r->metrics.cell_height = 16.0f;
    r->metrics.baseline = 14.0f;
    r->metrics.underline_pos = 15.0f;
    r->metrics.underline_thickness = 1.0f;

    /* Step 0: D3D11 device + swapchain */
    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        NULL, 0, D3D11_SDK_VERSION,
        &scd, &r->swapchain, &r->device, &feature_level, &r->context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_WARP, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION,
            &scd, &r->swapchain, &r->device, &feature_level, &r->context);
    }
    if (FAILED(hr)) { free(r); return NULL; }

    ID3D11Texture2D *back_buffer = NULL;
    IDXGISwapChain_GetBuffer(r->swapchain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (back_buffer) {
        ID3D11Device_CreateRenderTargetView(r->device, (ID3D11Resource *)back_buffer, NULL, &r->rtv);
        ID3D11Texture2D_Release(back_buffer);
    }
    return r;
}

bool wixen_renderer_create_step(WixenRenderer *r, int step,
                                  const char *font_family, float font_size) {
    if (!r) return false;

    if (step == 0) {
        /* Step 1: Compile shaders */
        ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *err_blob = NULL;
        HRESULT hr = D3DCompile(hlsl_source, strlen(hlsl_source), "terminal.hlsl", NULL, NULL,
                                 "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &vs_blob, &err_blob);
        if (FAILED(hr)) { if (err_blob) ID3D10Blob_Release(err_blob); return true; }
        hr = D3DCompile(hlsl_source, strlen(hlsl_source), "terminal.hlsl", NULL, NULL,
                         "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &ps_blob, &err_blob);
        if (FAILED(hr)) {
            ID3D10Blob_Release(vs_blob);
            if (err_blob) ID3D10Blob_Release(err_blob);
            return true;
        }
        ID3D11Device_CreateVertexShader(r->device,
            ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), NULL, &r->vs);
        ID3D11Device_CreatePixelShader(r->device,
            ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob), NULL, &r->ps);
        D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ID3D11Device_CreateInputLayout(r->device, layout_desc, 4,
            ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), &r->input_layout);
        ID3D10Blob_Release(vs_blob);
        ID3D10Blob_Release(ps_blob);

        /* Create buffers */
        D3D11_BUFFER_DESC vbd = {0};
        vbd.Usage = D3D11_USAGE_DYNAMIC;
        vbd.ByteWidth = MAX_VERTICES * sizeof(WixenVertex);
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device_CreateBuffer(r->device, &vbd, NULL, &r->vertex_buffer);

        D3D11_BUFFER_DESC ubd = {0};
        ubd.Usage = D3D11_USAGE_DYNAMIC;
        ubd.ByteWidth = sizeof(WixenUniforms);
        ubd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ubd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device_CreateBuffer(r->device, &ubd, NULL, &r->uniform_buffer);

        /* Placeholder 1x1 white texture */
        uint8_t white = 255;
        D3D11_TEXTURE2D_DESC td = {0};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA srd = { &white, 1, 0 };
        ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
        if (r->atlas_tex)
            ID3D11Device_CreateShaderResourceView(r->device, (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);

        D3D11_SAMPLER_DESC sd = {0};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ID3D11Device_CreateSamplerState(r->device, &sd, &r->sampler);
        renderer_compile_image_shaders(r);
        return true;
    }

    if (step == 1) {
        /* Step 2: Create DirectWrite atlas (heaviest step) */
        r->atlas = wixen_atlas_create(font_family, font_size, 96);
        if (r->atlas) {
            WixenDWriteMetrics dm = wixen_atlas_metrics(r->atlas);
            r->metrics.cell_width = dm.cell_width;
            r->metrics.cell_height = dm.cell_height;
            r->metrics.baseline = dm.baseline;
            r->metrics.underline_pos = dm.underline_pos;
            r->metrics.underline_thickness = dm.underline_thickness;
            /* Upload atlas texture to GPU */
            uint32_t aw, ah;
            const uint8_t *pixels = wixen_atlas_texture_data(r->atlas, &aw, &ah);
            if (pixels && aw > 0 && ah > 0) {
                if (r->atlas_tex) ID3D11Texture2D_Release(r->atlas_tex);
                if (r->atlas_srv) ID3D11ShaderResourceView_Release(r->atlas_srv);
                r->atlas_tex = NULL; r->atlas_srv = NULL;
                D3D11_TEXTURE2D_DESC td = {0};
                td.Width = aw; td.Height = ah; td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA srd = { pixels, aw, 0 };
                ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
                if (r->atlas_tex)
                    ID3D11Device_CreateShaderResourceView(r->device,
                        (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
            }
        }
        return true;
    }

    return false; /* No more steps */
}

/* --- Background init (no HWND) --- */

WixenRendererBgResult *wixen_renderer_init_background(
        const char *font_family, float font_size, uint32_t dpi) {
    WixenRendererBgResult *bg = calloc(1, sizeof(WixenRendererBgResult));
    if (!bg) return NULL;

    /* 1. Create D3D11 device WITHOUT swapchain (no HWND needed) */
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *ctx = NULL;
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        NULL, 0, D3D11_SDK_VERSION,
        &device, &feature_level, &ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            NULL, D3D_DRIVER_TYPE_WARP, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION,
            &device, &feature_level, &ctx);
    }
    if (FAILED(hr)) { free(bg); return NULL; }
    bg->device = device;
    bg->device_context = ctx;

    /* 2. Compile shaders */
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *err_blob = NULL;
    hr = D3DCompile(hlsl_source, strlen(hlsl_source), "terminal.hlsl", NULL, NULL,
                     "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &vs_blob, &err_blob);
    if (err_blob) ID3D10Blob_Release(err_blob); err_blob = NULL;
    if (SUCCEEDED(hr)) {
        hr = D3DCompile(hlsl_source, strlen(hlsl_source), "terminal.hlsl", NULL, NULL,
                         "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &ps_blob, &err_blob);
        if (err_blob) ID3D10Blob_Release(err_blob);
    }
    bg->vs_blob = vs_blob;
    bg->ps_blob = ps_blob;

    /* 3. Create DirectWrite glyph atlas */
    bg->atlas = wixen_atlas_create(font_family, font_size, (float)dpi);
    if (bg->atlas) {
        WixenDWriteMetrics dm = wixen_atlas_metrics((WixenGlyphAtlas *)bg->atlas);
        bg->metrics.cell_width = dm.cell_width;
        bg->metrics.cell_height = dm.cell_height;
        bg->metrics.baseline = dm.baseline;
        bg->metrics.underline_pos = dm.underline_pos;
        bg->metrics.underline_thickness = dm.underline_thickness;
    } else {
        bg->metrics.cell_width = font_size * 0.6f;
        bg->metrics.cell_height = font_size * 1.2f;
        bg->metrics.baseline = font_size;
    }

    return bg;
}

WixenRenderer *wixen_renderer_finalize(
        WixenRendererBgResult *bg, HWND hwnd, uint32_t width, uint32_t height) {
    if (!bg || !bg->device) return NULL;

    WixenRenderer *r = calloc(1, sizeof(WixenRenderer));
    if (!r) return NULL;
    r->device = (ID3D11Device *)bg->device;
    r->context = (ID3D11DeviceContext *)bg->device_context;
    r->width = width;
    r->height = height;
    r->metrics = bg->metrics;

    /* Create swapchain using DXGI factory from the device (HWND needed) */
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    ID3D11Device_QueryInterface(r->device, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (dxgi_device) {
        IDXGIDevice_GetAdapter(dxgi_device, &adapter);
        IDXGIDevice_Release(dxgi_device);
    }
    if (adapter) {
        IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
        IDXGIAdapter_Release(adapter);
    }
    if (factory) {
        DXGI_SWAP_CHAIN_DESC1 scd = {0};
        scd.Width = width;
        scd.Height = height;
        scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1 *sc1 = NULL;
        IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)r->device,
            hwnd, &scd, NULL, NULL, &sc1);
        if (sc1) {
            sc1->lpVtbl->QueryInterface(sc1, &IID_IDXGISwapChain, (void **)&r->swapchain);
            sc1->lpVtbl->Release(sc1);
        }
        IDXGIFactory2_Release(factory);
    }

    /* Create RTV */
    if (r->swapchain) {
        ID3D11Texture2D *bb = NULL;
        IDXGISwapChain_GetBuffer(r->swapchain, 0, &IID_ID3D11Texture2D, (void **)&bb);
        if (bb) {
            ID3D11Device_CreateRenderTargetView(r->device, (ID3D11Resource *)bb, NULL, &r->rtv);
            ID3D11Texture2D_Release(bb);
        }
    }

    /* Create pipeline objects from pre-compiled shaders */
    ID3DBlob *vs_blob = (ID3DBlob *)bg->vs_blob;
    ID3DBlob *ps_blob = (ID3DBlob *)bg->ps_blob;
    if (vs_blob && ps_blob) {
        ID3D11Device_CreateVertexShader(r->device,
            ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), NULL, &r->vs);
        ID3D11Device_CreatePixelShader(r->device,
            ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob), NULL, &r->ps);

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ID3D11Device_CreateInputLayout(r->device, layout, 4,
            ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), &r->input_layout);
    }

    /* Vertex + uniform buffers */
    D3D11_BUFFER_DESC vbd = { .Usage = D3D11_USAGE_DYNAMIC,
        .ByteWidth = MAX_VERTICES * sizeof(WixenVertex),
        .BindFlags = D3D11_BIND_VERTEX_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE };
    ID3D11Device_CreateBuffer(r->device, &vbd, NULL, &r->vertex_buffer);

    D3D11_BUFFER_DESC ubd = { .Usage = D3D11_USAGE_DYNAMIC,
        .ByteWidth = sizeof(WixenUniforms),
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE };
    ID3D11Device_CreateBuffer(r->device, &ubd, NULL, &r->uniform_buffer);

    /* Sampler */
    D3D11_SAMPLER_DESC sd = {0};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ID3D11Device_CreateSamplerState(r->device, &sd, &r->sampler);
    renderer_compile_image_shaders(r);

    /* Upload atlas texture */
    r->atlas = (WixenGlyphAtlas *)bg->atlas;
    bg->atlas = NULL; /* Transfer ownership */
    if (r->atlas) {
        uint32_t aw, ah;
        const uint8_t *pixels = wixen_atlas_texture_data(r->atlas, &aw, &ah);
        if (pixels && aw > 0) {
            D3D11_TEXTURE2D_DESC td = {0};
            td.Width = aw; td.Height = ah; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA srd = { pixels, aw, 0 };
            ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
            if (r->atlas_tex)
                ID3D11Device_CreateShaderResourceView(r->device,
                    (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
        }
    } else {
        /* Placeholder 1x1 white */
        uint8_t w = 255;
        D3D11_TEXTURE2D_DESC td = {0};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA srd = { &w, 1, 0 };
        ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
        if (r->atlas_tex)
            ID3D11Device_CreateShaderResourceView(r->device,
                (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
    }

    /* Don't free device/context — they're owned by the renderer now */
    bg->device = NULL;
    bg->device_context = NULL;

    return r;
}

void wixen_renderer_bg_result_free(WixenRendererBgResult *bg) {
    if (!bg) return;
    if (bg->vs_blob) ((ID3DBlob *)bg->vs_blob)->lpVtbl->Release((ID3DBlob *)bg->vs_blob);
    if (bg->ps_blob) ((ID3DBlob *)bg->ps_blob)->lpVtbl->Release((ID3DBlob *)bg->ps_blob);
    if (bg->device_context) ((ID3D11DeviceContext *)bg->device_context)->lpVtbl->Release((ID3D11DeviceContext *)bg->device_context);
    if (bg->device) ((ID3D11Device *)bg->device)->lpVtbl->Release((ID3D11Device *)bg->device);
    if (bg->atlas) wixen_atlas_destroy((WixenGlyphAtlas *)bg->atlas);
    free(bg);
}

void wixen_renderer_clear_present(WixenRenderer *r, const WixenColorScheme *colors) {
    if (!r || !r->rtv) return;
    float bg[4];
    wixen_rgb_to_float(colors->default_bg, bg);
    bg[3] = 1.0f;
    ID3D11DeviceContext_ClearRenderTargetView(r->context, r->rtv, bg);
    IDXGISwapChain_Present(r->swapchain, 1, 0);
}

/* --- Lifecycle --- */

WixenRenderer *wixen_renderer_create(HWND hwnd, uint32_t width, uint32_t height,
                                      const char *font_family, float font_size,
                                      const WixenColorScheme *colors) {
    WixenRenderer *r = calloc(1, sizeof(WixenRenderer));
    if (!r) return NULL;

    r->width = width;
    r->height = height;
    r->colors = *colors;

    /* Default font metrics (will be computed from DirectWrite) */
    r->metrics.cell_width = font_size * 0.6f;
    r->metrics.cell_height = font_size * 1.2f;
    r->metrics.baseline = font_size;
    r->metrics.underline_pos = font_size * 1.1f;
    r->metrics.underline_thickness = 1.0f;

    /* Create D3D11 device and swap chain */
    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        NULL, 0, D3D11_SDK_VERSION,
        &scd, &r->swapchain, &r->device, &feature_level, &r->context);

    if (FAILED(hr)) {
        /* Try WARP (software) driver */
        hr = D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_WARP, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION,
            &scd, &r->swapchain, &r->device, &feature_level, &r->context);
    }

    if (FAILED(hr)) {
        /* Both hardware and WARP D3D11 failed — fall back to GDI software renderer */
        r->soft = wixen_soft_create(hwnd, width, height, colors);
        if (!r->soft) {
            free(r);
            return NULL;
        }
        r->use_software = true;
        r->hwnd = hwnd;
        return r;
    }

    /* Create render target view from back buffer */
    ID3D11Texture2D *back_buffer = NULL;
    IDXGISwapChain_GetBuffer(r->swapchain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (back_buffer) {
        ID3D11Device_CreateRenderTargetView(r->device, (ID3D11Resource *)back_buffer, NULL, &r->rtv);
        ID3D11Texture2D_Release(back_buffer);
    }

    /* Pump messages between heavy init stages to prevent "Not Responding".
     * NVDA's watchdog triggers after ~1s without message pump activity. */
    { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

    /* Compile vertex shader */
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *err_blob = NULL;
    /* D3DCOMPILE_OPTIMIZATION_LEVEL0 for fastest compile time.
     * The shader is trivial — no benefit from optimization. */
    hr = D3DCompile(hlsl_source, strlen(hlsl_source), "terminal.hlsl", NULL, NULL,
                     "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &vs_blob, &err_blob);
    if (FAILED(hr)) { if (err_blob) ID3D10Blob_Release(err_blob); goto shader_fail; }

    hr = D3DCompile(hlsl_source, strlen(hlsl_source), "terminal.hlsl", NULL, NULL,
                     "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &ps_blob, &err_blob);
    if (FAILED(hr)) { if (err_blob) ID3D10Blob_Release(err_blob); goto shader_fail; }

    ID3D11Device_CreateVertexShader(r->device,
        ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob),
        NULL, &r->vs);
    ID3D11Device_CreatePixelShader(r->device,
        ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob),
        NULL, &r->ps);

    /* Input layout matching WixenVertex */
    D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11Device_CreateInputLayout(r->device, layout_desc, 4,
        ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob),
        &r->input_layout);

    ID3D10Blob_Release(vs_blob);
    ID3D10Blob_Release(ps_blob);

    /* Create vertex buffer (dynamic, updated each frame) */
    {
        D3D11_BUFFER_DESC bd = {0};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = MAX_VERTICES * sizeof(WixenVertex);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device_CreateBuffer(r->device, &bd, NULL, &r->vertex_buffer);
    }

    /* Create uniform buffer */
    {
        D3D11_BUFFER_DESC bd = {0};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = sizeof(WixenUniforms);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device_CreateBuffer(r->device, &bd, NULL, &r->uniform_buffer);
    }

    /* Pump again after shader compile, before atlas (another heavy op) */
    { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

    /* Create DirectWrite glyph atlas */
    r->atlas = wixen_atlas_create(font_family, font_size, 96);
    if (r->atlas) {
        WixenDWriteMetrics dm = wixen_atlas_metrics(r->atlas);
        r->metrics.cell_width = dm.cell_width;
        r->metrics.cell_height = dm.cell_height;
        r->metrics.baseline = dm.baseline;
        r->metrics.underline_pos = dm.underline_pos;
        r->metrics.underline_thickness = dm.underline_thickness;

        /* Pre-cache ASCII glyphs */
        for (uint32_t cp = 0x20; cp < 0x7F; cp++) {
            wixen_atlas_get_glyph(r->atlas, cp, false, false);
        }

        /* Upload atlas texture */
        uint32_t aw = 0, ah = 0;
        const uint8_t *pixels = wixen_atlas_texture_data(r->atlas, &aw, &ah);
        if (pixels && aw > 0 && ah > 0) {
            D3D11_TEXTURE2D_DESC td = {0};
            td.Width = aw; td.Height = ah;
            td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA srd = { pixels, aw, 0 };
            ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
            if (r->atlas_tex)
                ID3D11Device_CreateShaderResourceView(r->device,
                    (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
            r->atlas_width = aw;
            r->atlas_height = ah;
            wixen_atlas_clear_dirty(r->atlas);
        }
    }
    if (!r->atlas_tex) {
        /* Fallback: 1x1 white pixel if DirectWrite failed */
        uint8_t white_pixel = 255;
        D3D11_TEXTURE2D_DESC td = {0};
        td.Width = 1; td.Height = 1;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA srd = { &white_pixel, 1, 0 };
        ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
        if (r->atlas_tex)
            ID3D11Device_CreateShaderResourceView(r->device,
                (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
        r->atlas_width = 1;
        r->atlas_height = 1;
    }

    /* Create sampler */
    {
        D3D11_SAMPLER_DESC sd = {0};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ID3D11Device_CreateSamplerState(r->device, &sd, &r->sampler);
    }
    renderer_compile_image_shaders(r);

    (void)font_family;
    return r;

shader_fail:
    if (vs_blob) ID3D10Blob_Release(vs_blob);
    if (ps_blob) ID3D10Blob_Release(ps_blob);
    /* Fall through — renderer still usable for clear+present, just no text */
    (void)font_family;
    return r;
}

void wixen_renderer_destroy(WixenRenderer *r) {
    if (!r) return;
    if (r->use_software) {
        wixen_soft_destroy(r->soft);
        free(r);
        return;
    }
    if (r->atlas) wixen_atlas_destroy(r->atlas);
    if (r->img_vertex_buffer) ID3D11Buffer_Release(r->img_vertex_buffer);
    if (r->img_input_layout) ID3D11InputLayout_Release(r->img_input_layout);
    if (r->img_ps) ID3D11PixelShader_Release(r->img_ps);
    if (r->img_vs) ID3D11VertexShader_Release(r->img_vs);
    if (r->sampler) ID3D11SamplerState_Release(r->sampler);
    if (r->atlas_srv) ID3D11ShaderResourceView_Release(r->atlas_srv);
    if (r->atlas_tex) ID3D11Texture2D_Release(r->atlas_tex);
    if (r->uniform_buffer) ID3D11Buffer_Release(r->uniform_buffer);
    if (r->vertex_buffer) ID3D11Buffer_Release(r->vertex_buffer);
    if (r->input_layout) ID3D11InputLayout_Release(r->input_layout);
    if (r->ps) ID3D11PixelShader_Release(r->ps);
    if (r->vs) ID3D11VertexShader_Release(r->vs);
    if (r->rtv) ID3D11RenderTargetView_Release(r->rtv);
    if (r->swapchain) IDXGISwapChain_Release(r->swapchain);
    if (r->context) ID3D11DeviceContext_Release(r->context);
    if (r->device) ID3D11Device_Release(r->device);
    free(r);
}

bool wixen_renderer_update_font(WixenRenderer *r, const char *font_family, float font_size) {
    if (!r) return false;

    /* Destroy old atlas and its GPU resources */
    if (r->atlas) { wixen_atlas_destroy(r->atlas); r->atlas = NULL; }
    if (r->atlas_srv) { ID3D11ShaderResourceView_Release(r->atlas_srv); r->atlas_srv = NULL; }
    if (r->atlas_tex) { ID3D11Texture2D_Release(r->atlas_tex); r->atlas_tex = NULL; }

    /* Create new atlas with the new font */
    float dpi = 96.0f; /* Default DPI — atlas uses DIPs internally */
    r->atlas = wixen_atlas_create(font_family, font_size, dpi);
    if (!r->atlas) return false;

    /* Update font metrics from the new atlas */
    WixenDWriteMetrics dm = wixen_atlas_metrics(r->atlas);
    r->metrics.cell_width = dm.cell_width;
    r->metrics.cell_height = dm.cell_height;
    r->metrics.baseline = dm.baseline;
    r->metrics.underline_pos = dm.underline_pos;
    r->metrics.underline_thickness = dm.underline_thickness;

    /* Upload new atlas texture to GPU */
    uint32_t aw = 0, ah = 0;
    const uint8_t *pixels = wixen_atlas_texture_data(r->atlas, &aw, &ah);
    if (pixels && aw > 0 && ah > 0) {
        D3D11_TEXTURE2D_DESC td = {0};
        td.Width = aw; td.Height = ah; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA srd = { pixels, aw, 0 };
        ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
        if (r->atlas_tex)
            ID3D11Device_CreateShaderResourceView(r->device,
                (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
        r->atlas_width = aw;
        r->atlas_height = ah;
        wixen_atlas_clear_dirty(r->atlas);
    }

    return true;
}

bool wixen_renderer_is_software(const WixenRenderer *r) {
    if (!r) return false;
    return r->use_software;
}

void wixen_renderer_resize(WixenRenderer *r, uint32_t width, uint32_t height) {
    if (!r || (width == r->width && height == r->height)) return;
    r->width = width;
    r->height = height;

    if (r->use_software) {
        wixen_soft_resize(r->soft, width, height);
        return;
    }

    /* Release old RTV, resize swap chain, create new RTV */
    if (r->rtv) { ID3D11RenderTargetView_Release(r->rtv); r->rtv = NULL; }
    ID3D11DeviceContext_OMSetRenderTargets(r->context, 0, NULL, NULL);

    IDXGISwapChain_ResizeBuffers(r->swapchain, 0, width, height,
                                  DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D *back_buffer = NULL;
    IDXGISwapChain_GetBuffer(r->swapchain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (back_buffer) {
        ID3D11Device_CreateRenderTargetView(r->device, (ID3D11Resource *)back_buffer, NULL, &r->rtv);
        ID3D11Texture2D_Release(back_buffer);
    }
}

void wixen_renderer_render_frame(WixenRenderer *r,
                                  const WixenPaneRenderInfo *panes, size_t pane_count) {
    if (!r) return;

    /* GDI software fallback path */
    if (r->use_software) {
        if (r->soft && panes && pane_count > 0) {
            wixen_soft_render(r->soft, panes[0].grid);
        }
        return;
    }

    if (!r->rtv) return;

    /* Clear to background color */
    float bg[4];
    wixen_rgb_to_float(r->colors.default_bg, bg);
    bg[3] = 1.0f;
    ID3D11DeviceContext_ClearRenderTargetView(r->context, r->rtv, bg);

    /* Set render target */
    ID3D11DeviceContext_OMSetRenderTargets(r->context, 1, &r->rtv, NULL);

    /* Set viewport */
    D3D11_VIEWPORT vp = { 0, 0, (FLOAT)r->width, (FLOAT)r->height, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(r->context, 1, &vp);

    /* Update uniforms */
    if (r->uniform_buffer) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ID3D11DeviceContext_Map(r->context,
            (ID3D11Resource *)r->uniform_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            WixenUniforms *u = (WixenUniforms *)mapped.pData;
            u->screen_size[0] = (float)r->width;
            u->screen_size[1] = (float)r->height;
            ID3D11DeviceContext_Unmap(r->context, (ID3D11Resource *)r->uniform_buffer, 0);
        }
    }

    /* Re-upload atlas if glyphs were added since last frame */
    if (r->atlas && wixen_atlas_is_dirty(r->atlas)) {
        uint32_t aw = 0, ah = 0;
        const uint8_t *pixels = wixen_atlas_texture_data(r->atlas, &aw, &ah);
        if (pixels && aw > 0 && ah > 0) {
            /* Recreate texture if size changed */
            if (aw != r->atlas_width || ah != r->atlas_height) {
                if (r->atlas_srv) { ID3D11ShaderResourceView_Release(r->atlas_srv); r->atlas_srv = NULL; }
                if (r->atlas_tex) { ID3D11Texture2D_Release(r->atlas_tex); r->atlas_tex = NULL; }
                D3D11_TEXTURE2D_DESC td = {0};
                td.Width = aw; td.Height = ah;
                td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA srd = { pixels, aw, 0 };
                ID3D11Device_CreateTexture2D(r->device, &td, &srd, &r->atlas_tex);
                if (r->atlas_tex)
                    ID3D11Device_CreateShaderResourceView(r->device,
                        (ID3D11Resource *)r->atlas_tex, NULL, &r->atlas_srv);
                r->atlas_width = aw;
                r->atlas_height = ah;
            } else {
                /* Same size — just update the data */
                ID3D11DeviceContext_UpdateSubresource(r->context,
                    (ID3D11Resource *)r->atlas_tex, 0, NULL, pixels, aw, 0);
            }
        }
        wixen_atlas_clear_dirty(r->atlas);
    }

    /* Build and draw vertices for each pane */
    if (r->vs && r->ps && r->input_layout && r->vertex_buffer) {
        /* Set pipeline state */
        ID3D11DeviceContext_IASetInputLayout(r->context, r->input_layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(r->context,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(WixenVertex), offset = 0;
        ID3D11DeviceContext_IASetVertexBuffers(r->context, 0, 1,
            &r->vertex_buffer, &stride, &offset);
        ID3D11DeviceContext_VSSetShader(r->context, r->vs, NULL, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(r->context, 0, 1, &r->uniform_buffer);
        ID3D11DeviceContext_PSSetShader(r->context, r->ps, NULL, 0);
        if (r->atlas_srv)
            ID3D11DeviceContext_PSSetShaderResources(r->context, 0, 1, &r->atlas_srv);
        if (r->sampler)
            ID3D11DeviceContext_PSSetSamplers(r->context, 0, 1, &r->sampler);

        for (size_t p = 0; p < pane_count; p++) {
            const WixenPaneRenderInfo *pane = &panes[p];
            if (!pane->grid) continue;

            /* Build vertices using pipeline module */
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = ID3D11DeviceContext_Map(r->context,
                (ID3D11Resource *)r->vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr)) continue;

            size_t vert_count = wixen_build_cell_vertices(
                pane->grid,
                r->metrics.cell_width, r->metrics.cell_height,
                pane->x, pane->y,
                &r->colors,
                NULL, NULL, /* No highlight callback yet */
                pane->has_focus,
                pane->grid->cursor.col, pane->grid->cursor.row,
                (WixenVertex *)mapped.pData, MAX_VERTICES);

            ID3D11DeviceContext_Unmap(r->context, (ID3D11Resource *)r->vertex_buffer, 0);

            if (vert_count > 0) {
                ID3D11DeviceContext_Draw(r->context, (UINT)vert_count, 0);
            }
        }
    }

    /* Draw Sixel/terminal images on top of cell grid */
    if (r->img_vs && r->img_ps && r->img_input_layout && r->img_vertex_buffer) {
        for (size_t p = 0; p < pane_count; p++) {
            const WixenPaneRenderInfo *pane = &panes[p];
            if (!pane->image_store || pane->image_store->count == 0) continue;

            /* Switch to image pipeline */
            ID3D11DeviceContext_IASetInputLayout(r->context, r->img_input_layout);
            UINT img_stride = sizeof(WixenImageVertex), img_offset = 0;
            ID3D11DeviceContext_IASetVertexBuffers(r->context, 0, 1,
                &r->img_vertex_buffer, &img_stride, &img_offset);
            ID3D11DeviceContext_VSSetShader(r->context, r->img_vs, NULL, 0);
            ID3D11DeviceContext_VSSetConstantBuffers(r->context, 0, 1, &r->uniform_buffer);
            ID3D11DeviceContext_PSSetShader(r->context, r->img_ps, NULL, 0);
            if (r->sampler)
                ID3D11DeviceContext_PSSetSamplers(r->context, 0, 1, &r->sampler);

            /* Enable alpha blending for transparent images */
            ID3D11BlendState *blend_state = NULL;
            D3D11_BLEND_DESC bd = {0};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            ID3D11Device_CreateBlendState(r->device, &bd, &blend_state);
            if (blend_state) {
                float blend_factor[4] = {0};
                ID3D11DeviceContext_OMSetBlendState(r->context, blend_state, blend_factor, 0xFFFFFFFF);
            }

            for (size_t i = 0; i < pane->image_store->count; i++) {
                const WixenTerminalImage *img = &pane->image_store->images[i];
                if (!img->pixels || img->width == 0 || img->height == 0) continue;

                /* Compute pixel rect from grid position */
                float px = pane->x + (float)img->col * r->metrics.cell_width;
                float py = pane->y + (float)img->row * r->metrics.cell_height;
                float pw = (float)img->cell_cols * r->metrics.cell_width;
                float ph = (float)img->cell_rows * r->metrics.cell_height;

                /* Upload image pixels as RGBA texture */
                void *srv = wixen_renderer_upload_image(r, img->pixels, img->width, img->height);
                if (!srv) continue;

                /* Build quad vertices */
                D3D11_MAPPED_SUBRESOURCE mapped;
                HRESULT hr = ID3D11DeviceContext_Map(r->context,
                    (ID3D11Resource *)r->img_vertex_buffer, 0,
                    D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                if (SUCCEEDED(hr)) {
                    WixenImageVertex *v = (WixenImageVertex *)mapped.pData;
                    /* Triangle 1: top-left, top-right, bottom-left */
                    v[0] = (WixenImageVertex){{ px, py },      { 0, 0 }};
                    v[1] = (WixenImageVertex){{ px + pw, py },  { 1, 0 }};
                    v[2] = (WixenImageVertex){{ px, py + ph },  { 0, 1 }};
                    /* Triangle 2: top-right, bottom-right, bottom-left */
                    v[3] = (WixenImageVertex){{ px + pw, py },      { 1, 0 }};
                    v[4] = (WixenImageVertex){{ px + pw, py + ph }, { 1, 1 }};
                    v[5] = (WixenImageVertex){{ px, py + ph },      { 0, 1 }};
                    ID3D11DeviceContext_Unmap(r->context,
                        (ID3D11Resource *)r->img_vertex_buffer, 0);

                    ID3D11DeviceContext_PSSetShaderResources(r->context, 0, 1,
                        (ID3D11ShaderResourceView **)&srv);
                    ID3D11DeviceContext_Draw(r->context, 6, 0);
                }

                wixen_renderer_release_image(srv);
            }

            /* Restore default blend state */
            if (blend_state) {
                ID3D11DeviceContext_OMSetBlendState(r->context, NULL, NULL, 0xFFFFFFFF);
                ID3D11BlendState_Release(blend_state);
            }
        }
    }

    /* Present */
    IDXGISwapChain_Present(r->swapchain, 1, 0);
}

WixenFontMetrics wixen_renderer_font_metrics(const WixenRenderer *r) {
    return r->metrics;
}

void wixen_renderer_set_colors(WixenRenderer *r, const WixenColorScheme *colors) {
    r->colors = *colors;
}

uint32_t wixen_renderer_width(const WixenRenderer *r) { return r->width; }
uint32_t wixen_renderer_height(const WixenRenderer *r) { return r->height; }

/* --- DPI helpers --- */

float wixen_dpi_scale_factor(uint32_t dpi) {
    if (dpi == 0) return 1.0f;
    return (float)dpi / 96.0f;
}

void wixen_dpi_grid_dimensions(uint32_t window_width, uint32_t window_height,
                                float base_cell_width, float base_cell_height,
                                uint32_t dpi,
                                uint32_t *out_cols, uint32_t *out_rows) {
    float scale = wixen_dpi_scale_factor(dpi);
    float cell_w = base_cell_width * scale;
    float cell_h = base_cell_height * scale;
    *out_cols = (cell_w > 0) ? (uint32_t)(window_width / cell_w) : 1;
    *out_rows = (cell_h > 0) ? (uint32_t)(window_height / cell_h) : 1;
}

/* --- Image upload/draw API --- */

void *wixen_renderer_upload_image(WixenRenderer *r, const uint8_t *pixels,
                                   uint32_t width, uint32_t height) {
    if (!r || !r->device || !pixels || width == 0 || height == 0) return NULL;

    D3D11_TEXTURE2D_DESC td = {0};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd = {0};
    srd.pSysMem = pixels;
    srd.SysMemPitch = width * 4;

    ID3D11Texture2D *tex = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(r->device, &td, &srd, &tex);
    if (FAILED(hr) || !tex) return NULL;

    ID3D11ShaderResourceView *srv = NULL;
    hr = ID3D11Device_CreateShaderResourceView(r->device, (ID3D11Resource *)tex, NULL, &srv);
    ID3D11Texture2D_Release(tex);
    if (FAILED(hr)) return NULL;

    return srv;
}

void wixen_renderer_release_image(void *srv) {
    if (srv) {
        ID3D11ShaderResourceView_Release((ID3D11ShaderResourceView *)srv);
    }
}

void wixen_renderer_draw_image(WixenRenderer *r, const WixenImagePlacement *placement,
                                void *texture_srv) {
    if (!r || !placement || !texture_srv) return;
    if (!r->img_vs || !r->img_ps || !r->img_input_layout || !r->img_vertex_buffer) return;

    /* Set image pipeline state */
    ID3D11DeviceContext_IASetInputLayout(r->context, r->img_input_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(r->context,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(WixenImageVertex), offset = 0;
    ID3D11DeviceContext_IASetVertexBuffers(r->context, 0, 1,
        &r->img_vertex_buffer, &stride, &offset);
    ID3D11DeviceContext_VSSetShader(r->context, r->img_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(r->context, 0, 1, &r->uniform_buffer);
    ID3D11DeviceContext_PSSetShader(r->context, r->img_ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(r->context, 0, 1,
        (ID3D11ShaderResourceView **)&texture_srv);
    if (r->sampler)
        ID3D11DeviceContext_PSSetSamplers(r->context, 0, 1, &r->sampler);

    /* Build quad */
    float px = placement->x, py = placement->y;
    float pw = placement->width, ph = placement->height;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ID3D11DeviceContext_Map(r->context,
        (ID3D11Resource *)r->img_vertex_buffer, 0,
        D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    WixenImageVertex *v = (WixenImageVertex *)mapped.pData;
    v[0] = (WixenImageVertex){{ px, py },      { 0, 0 }};
    v[1] = (WixenImageVertex){{ px + pw, py },  { 1, 0 }};
    v[2] = (WixenImageVertex){{ px, py + ph },  { 0, 1 }};
    v[3] = (WixenImageVertex){{ px + pw, py },      { 1, 0 }};
    v[4] = (WixenImageVertex){{ px + pw, py + ph }, { 1, 1 }};
    v[5] = (WixenImageVertex){{ px, py + ph },      { 0, 1 }};
    ID3D11DeviceContext_Unmap(r->context, (ID3D11Resource *)r->img_vertex_buffer, 0);

    ID3D11DeviceContext_Draw(r->context, 6, 0);
}

#endif /* _WIN32 */
