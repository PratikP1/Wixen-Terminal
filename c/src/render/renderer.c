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
#include <stdlib.h>
#include <string.h>

#include <d3dcompiler.h>
#include "wixen/render/pipeline.h"
#include "wixen/render/atlas.h"

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
};

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
        free(r);
        return NULL;
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
    if (r->atlas) wixen_atlas_destroy(r->atlas);
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

void wixen_renderer_resize(WixenRenderer *r, uint32_t width, uint32_t height) {
    if (!r || (width == r->width && height == r->height)) return;
    r->width = width;
    r->height = height;

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
    if (!r || !r->rtv) return;

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

#endif /* _WIN32 */
