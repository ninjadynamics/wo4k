#include "video/video_crt.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

/* Fixed glow/bloom pyramid size: 320x240 is 76,800 pixels (~6.25x smaller
 * than 800x600), running both bloom and magic glow in two MRT passes. */
#define CRT_GLOW_WIDTH 320u
#define CRT_GLOW_HEIGHT 240u

/* Internal CRT signal grid caps: matches Dreamcast 640x480 with intres=1.5
 * (640x320 scanlines) on HD sources while keeping 320x192 sources 1:1. */
#define CRT_MAX_SIGNAL_WIDTH 640u
#define CRT_HIGH_RES_TRIGGER 360u
#define CRT_HIGH_RES_LINES 320u

/* Switch from 1080p Flat to 4K Flat mask parameters above 1440p height. */
#define CRT_4K_HEIGHT_THRESHOLD 1600u

static const GUID crt_iid_texture2d = {
    0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c }
};
static const GUID crt_iid_dxgi_device = {
    0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c }
};
static const GUID crt_iid_dxgi_adapter = {
    0x2411e7e1, 0x12ac, 0x4ccf, { 0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0 }
};
static const GUID crt_iid_dxgi_factory = {
    0x7b7166ec, 0x21c7, 0x44ae, { 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69 }
};

typedef HRESULT (WINAPI *D3d11CreateDeviceFn)(
    IDXGIAdapter *adapter,
    D3D_DRIVER_TYPE driver_type,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL *feature_levels,
    UINT levels_count,
    UINT sdk_version,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *feature_level,
    ID3D11DeviceContext **context);

typedef HRESULT (WINAPI *D3dCompileFn)(
    LPCVOID src_data,
    SIZE_T src_size,
    LPCSTR source_name,
    const D3D_SHADER_MACRO *defines,
    ID3DInclude *include,
    LPCSTR entrypoint,
    LPCSTR target,
    UINT flags1,
    UINT flags2,
    ID3DBlob **code,
    ID3DBlob **error_msgs);

typedef void (*CrtAnyFn)(void);

static struct {
    bool attempted;
    bool loaded;
    HMODULE d3d11;
    HMODULE d3dcompiler;
    D3d11CreateDeviceFn create_device;
    D3dCompileFn compile;
} crt_api;

typedef struct CrtConstants {
    float src_size[4];     /* src_w, src_h, 1/src_w, 1/src_h */
    float crt_size[4];     /* crt_w, crt_h, 1/crt_w, 1/crt_h */
    float glow_size[4];    /* glow_w, glow_h, 1/glow_w, 1/glow_h */
    float out_size[4];     /* vp_w, vp_h, 1/vp_w, 1/vp_h */
    float color_params[4]; /* is_nv12, is_bt709, is_full_range, frame_phase */
    float mask_params[4];  /* mask_size, mask_zoom, slot_width, double_slot */
    float crop_params[4];  /* uv_y_offset, uv_y_scale, 0, 0 */
} CrtConstants;

_Static_assert(sizeof(CrtConstants) == 112 && (sizeof(CrtConstants) % 16) == 0,
    "D3D11 constant buffer must be 16-byte aligned");

typedef struct CrtTarget {
    ID3D11Texture2D *tex;
    ID3D11RenderTargetView *rtv;
    ID3D11ShaderResourceView *srv;
    uint32_t width;
    uint32_t height;
} CrtTarget;

struct VideoCrt {
    HWND window;
    bool vsync;
    bool widescreen;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swap_chain;
    ID3D11RenderTargetView *back_rtv;
    uint32_t back_w;
    uint32_t back_h;

    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps_ntsc;
    ID3D11PixelShader *ps_blur_h;
    ID3D11PixelShader *ps_blur_v;
    ID3D11PixelShader *ps_final;
    ID3D11SamplerState *sampler;
    ID3D11Buffer *cbuffer;

    /* Dynamic source textures (BGRA or NV12 Y + UV). */
    ID3D11Texture2D *src_main;
    ID3D11ShaderResourceView *src_main_srv;
    ID3D11Texture2D *src_uv;
    ID3D11ShaderResourceView *src_uv_srv;
    uint32_t src_w;
    uint32_t src_h;
    bool src_is_nv12;
    bool src_bt709;
    bool src_full_range;

    /* Intermediate render targets. */
    DXGI_FORMAT rt_format;
    CrtTarget signal_rt;
    CrtTarget bloom_h;
    CrtTarget glow_h;
    CrtTarget bloom_rt;
    CrtTarget glow_rt;

    bool has_frame;
    uint32_t frame_index;
};

/* Clean-room HLSL shaders (Shader Model 4.0 for broad D3D10/D3D11 support).
 * Split into two string literals to stay well under C11's 4095-byte minimum
 * string literal limit while keeping all entry points in one compilation unit. */
static const char crt_hlsl_part1[] =
"cbuffer CrtParams : register(b0) {\n"
"    float4 src_size;\n"
"    float4 crt_size;\n"
"    float4 glow_size;\n"
"    float4 out_size;\n"
"    float4 color_params;\n"
"    float4 mask_params;\n"
"    float4 crop_params;\n"
"};\n"
"Texture2D tex0 : register(t0);\n"
"Texture2D tex1 : register(t1);\n"
"Texture2D tex2 : register(t2);\n"
"SamplerState samp_lin : register(s0);\n"
"struct VsOut {\n"
"    float4 pos : SV_POSITION;\n"
"    float2 uv  : TEXCOORD0;\n"
"};\n"
"VsOut vs_main(uint vid : SV_VertexID) {\n"
"    VsOut o;\n"
"    o.uv = float2((vid << 1) & 2, vid & 2);\n"
"    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
"    return o;\n"
"}\n"
"float3 decode_source(float2 uv) {\n"
"    if (color_params.x > 0.5) {\n"
"        float y = tex0.SampleLevel(samp_lin, uv, 0.0).r;\n"
"        float2 c = tex1.SampleLevel(samp_lin, uv, 0.0).rg - 0.5;\n"
"        if (color_params.z < 0.5) {\n"
"            y = (y - 16.0 / 255.0) * (255.0 / 219.0);\n"
"            c *= (255.0 / 224.0);\n"
"        }\n"
"        float3 rgb;\n"
"        if (color_params.y > 0.5) {\n"
"            rgb.r = y + 1.574800 * c.y;\n"
"            rgb.g = y - 0.187324 * c.x - 0.468124 * c.y;\n"
"            rgb.b = y + 1.855600 * c.x;\n"
"        } else {\n"
"            rgb.r = y + 1.402000 * c.y;\n"
"            rgb.g = y - 0.344136 * c.x - 0.714136 * c.y;\n"
"            rgb.b = y + 1.772000 * c.x;\n"
"        }\n"
"        return saturate(rgb);\n"
"    }\n"
"    return tex0.SampleLevel(samp_lin, uv, 0.0).rgb;\n"
"}\n"
"float3 rgb_to_yiq(float3 c) {\n"
"    return float3(\n"
"        dot(c, float3(0.2989,  0.5870,  0.1140)),\n"
"        dot(c, float3(0.5959, -0.2744, -0.3216)),\n"
"        dot(c, float3(0.2115, -0.5229,  0.3114))\n"
"    );\n"
"}\n"
"float3 yiq_to_rgb(float3 yiq) {\n"
"    return float3(\n"
"        yiq.x + 0.9560 * yiq.y + 0.6210 * yiq.z,\n"
"        yiq.x - 0.2720 * yiq.y - 0.6474 * yiq.z,\n"
"        yiq.x - 1.1060 * yiq.y + 1.7046 * yiq.z\n"
"    );\n"
"}\n"
"float4 ps_ntsc_linear(VsOut i) : SV_Target {\n"
"    float2 uv = float2(i.uv.x, crop_params.x + i.uv.y * crop_params.y);\n"
"    float2 dx = float2(crt_size.z, 0.0);\n"
"    float3 y_m2 = rgb_to_yiq(decode_source(uv - 2.0 * dx));\n"
"    float3 y_m1 = rgb_to_yiq(decode_source(uv - dx));\n"
"    float3 y_0  = rgb_to_yiq(decode_source(uv));\n"
"    float3 y_p1 = rgb_to_yiq(decode_source(uv + dx));\n"
"    float3 y_p2 = rgb_to_yiq(decode_source(uv + 2.0 * dx));\n"
"    /* Clean high-bandwidth luma (ntsc_scale=2.5) with anti-ringing. */\n"
"    float luma = 0.06 * (y_m1.x + y_p1.x) + 0.88 * y_0.x;\n"
"    float lmin = min(y_0.x, min(y_m1.x, y_p1.x));\n"
"    float lmax = max(y_0.x, max(y_m1.x, y_p1.x));\n"
"    luma = lerp(luma, clamp(luma, lmin, lmax), 0.5);\n"
"    /* Smooth analog chroma band-limit (ntsc_cscale=2.25) + 1.10x sat. */\n"
"    float2 chroma = 0.06 * (y_m2.yz + y_p2.yz) + 0.24 * (y_m1.yz + y_p1.yz) + 0.40 * y_0.yz;\n"
"    chroma *= 1.10;\n"
"    /* Subtle 2-phase NTSC carrier edge warmth (ntsc_fields=0, phase=2). */\n"
"    float row_idx = floor(i.uv.y * crt_size.y);\n"
"    float p_sign = (fmod(row_idx + color_params.w, 2.0) < 0.5) ? 1.0 : -1.0;\n"
"    float edge = y_p1.x - y_m1.x;\n"
"    chroma += float2(0.020, -0.012) * edge * p_sign;\n"
"    float3 rgb = saturate(yiq_to_rgb(float3(luma, chroma)));\n"
"    /* GAMMA_INPUT = 2.0 linearize. */\n"
"    float3 lin = rgb * rgb;\n"
"    float peak = max(lin.r, max(lin.g, lin.b));\n"
"    return float4(lin, peak);\n"
"}\n"
"struct BlurOut {\n"
"    float4 bloom : SV_Target0;\n"
"    float4 glow  : SV_Target1;\n"
"};\n"
"float3 glow_extract(float3 c) {\n"
"    float3 g = max(c - 0.12, 0.0);\n"
"    float m = max(g.r, max(g.g, g.b));\n"
"    float r = max(m - 0.12, 0.0);\n"
"    return g * (r / (m + 1e-5));\n"
"}\n"
"BlurOut ps_blur_h(VsOut i) {\n"
"    static const float bw[4] = { 0.5319, 0.2187, 0.0151, 0.0002 };\n"
"    static const float gw[7] = { 0.3208, 0.2328, 0.0889, 0.0179, 0.0019, 0.0001, 0.00001 };\n"
"    float2 dx = float2(crt_size.z, 0.0);\n"
"    float3 c0 = tex0.SampleLevel(samp_lin, i.uv, 0.0).rgb;\n"
"    float3 bsum = c0 * bw[0];\n"
"    float bpeak = pow(max(c0.r, max(c0.g, c0.b)), 3.0) * bw[0];\n"
"    float bwsum = bw[0];\n"
"    float3 gsum = glow_extract(c0) * gw[0];\n"
"    float gwsum = gw[0];\n"
"    [unroll]\n"
"    for (int k = 1; k <= 6; ++k) {\n"
"        float2 off = dx * (float)k;\n"
"        float3 cl = tex0.SampleLevel(samp_lin, i.uv - off, 0.0).rgb;\n"
"        float3 cr = tex0.SampleLevel(samp_lin, i.uv + off, 0.0).rgb;\n"
"        if (k <= 3) {\n"
"            float w = bw[k];\n"
"            bsum += (cl + cr) * w;\n"
"            float ml = max(cl.r, max(cl.g, cl.b));\n"
"            float mr = max(cr.r, max(cr.g, cr.b));\n"
"            bpeak += (ml * ml * ml + mr * mr * mr) * w;\n"
"            bwsum += 2.0 * w;\n"
"        }\n"
"        float wg = gw[k];\n"
"        gsum += (glow_extract(cl) + glow_extract(cr)) * wg;\n"
"        gwsum += 2.0 * wg;\n"
"    }\n"
"    BlurOut o;\n"
"    o.bloom = float4(bsum / bwsum, pow(max(bpeak / bwsum, 0.0), 0.333333));\n"
"    o.glow = float4(gsum / gwsum, 1.0);\n"
"    return o;\n"
"}\n";

static const char crt_hlsl_part2[] =
"BlurOut ps_blur_v(VsOut i) {\n"
"    static const float bw[4] = { 0.6649, 0.1656, 0.0020, 0.00001 };\n"
"    static const float gw[7] = { 0.3208, 0.2328, 0.0889, 0.0179, 0.0019, 0.0001, 0.00001 };\n"
"    float2 dy = float2(0.0, crt_size.w);\n"
"    float4 b0 = tex0.SampleLevel(samp_lin, i.uv, 0.0);\n"
"    float3 g0 = tex1.SampleLevel(samp_lin, i.uv, 0.0).rgb;\n"
"    float3 bsum = b0.rgb * bw[0];\n"
"    float bpeak = (b0.a * b0.a * b0.a) * bw[0];\n"
"    float bwsum = bw[0];\n"
"    float3 gsum = g0 * gw[0];\n"
"    float gwsum = gw[0];\n"
"    [unroll]\n"
"    for (int k = 1; k <= 6; ++k) {\n"
"        float2 off = dy * (float)k;\n"
"        if (k <= 3) {\n"
"            float4 bu = tex0.SampleLevel(samp_lin, i.uv - off, 0.0);\n"
"            float4 bd = tex0.SampleLevel(samp_lin, i.uv + off, 0.0);\n"
"            float w = bw[k];\n"
"            bsum += (bu.rgb + bd.rgb) * w;\n"
"            bpeak += (bu.a * bu.a * bu.a + bd.a * bd.a * bd.a) * w;\n"
"            bwsum += 2.0 * w;\n"
"        }\n"
"        float3 gu = tex1.SampleLevel(samp_lin, i.uv - off, 0.0).rgb;\n"
"        float3 gd = tex1.SampleLevel(samp_lin, i.uv + off, 0.0).rgb;\n"
"        float wg = gw[k];\n"
"        gsum += (gu + gd) * wg;\n"
"        gwsum += 2.0 * wg;\n"
"    }\n"
"    BlurOut o;\n"
"    o.bloom = float4(bsum / bwsum, pow(max(bpeak / bwsum, 0.0), 0.175));\n"
"    o.glow = float4(gsum / gwsum, 1.0);\n"
"    return o;\n"
"}\n"
"float4 sample_beam_row(float x_base, float fx, float y_uv) {\n"
"    /* 4-tap horizontal CRT Gaussian with mild subtractive sharpening. */\n"
"    float dx = crt_size.z;\n"
"    float3 c0 = tex0.SampleLevel(samp_lin, float2(x_base - dx,       y_uv), 0.0).rgb;\n"
"    float3 c1 = tex0.SampleLevel(samp_lin, float2(x_base,            y_uv), 0.0).rgb;\n"
"    float3 c2 = tex0.SampleLevel(samp_lin, float2(x_base + dx,       y_uv), 0.0).rgb;\n"
"    float3 c3 = tex0.SampleLevel(samp_lin, float2(x_base + 2.0 * dx, y_uv), 0.0).rgb;\n"
"    float d0 = -1.0 - fx;\n"
"    float d1 = -fx;\n"
"    float d2 = 1.0 - fx;\n"
"    float d3 = 2.0 - fx;\n"
"    float inv2s = 1.0 / (2.0 * 0.80 * 0.80);\n"
"    float w0 = exp(-d0 * d0 * inv2s) - 0.14;\n"
"    float w1 = exp(-d1 * d1 * inv2s) - 0.14;\n"
"    float w2 = exp(-d2 * d2 * inv2s) - 0.14;\n"
"    float w3 = exp(-d3 * d3 * inv2s) - 0.14;\n"
"    w0 = max(w0, -0.10);\n"
"    w1 = max(w1, -0.10);\n"
"    w2 = max(w2, -0.10);\n"
"    w3 = max(w3, -0.10);\n"
"    float wsum = w0 + w1 + w2 + w3;\n"
"    float3 col = (c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3) / wsum;\n"
"    float3 cmin = min(min(c1, c2), min(c0, c3));\n"
"    float3 cmax = max(max(c1, c2), max(c0, c3));\n"
"    col = saturate(lerp(clamp(col, cmin, cmax), col, 0.30));\n"
"    float peak = max(col.r, max(col.g, col.b));\n"
"    return float4(col, peak);\n"
"}\n"
"float3 scan_weight(float d, float3 hue) {\n"
"    /* gsl=-1 cubic-quadratic beam, scanline=10, beam=1, scans=0.75. */\n"
"    float3 sat = lerp(float3(1.75, 1.75, 1.75), float3(1.0, 1.0, 1.0), hue);\n"
"    float ex = lerp(d * d, d * d * d, 0.4);\n"
"    return exp2(-10.0 * ex * sat);\n"
"}\n"
"float3 phosphor_mask13(float x, float mx) {\n"
"    float p = floor(fmod(x, 6.0));\n"
"    if (p < 0.0) p += 6.0;\n"
"    float3 m = float3(0.0, 0.0, 0.0);\n"
"    if      (p < 0.5) m = float3(0.0, 0.0, 0.0);\n"
"    else if (p < 1.5) m = float3(1.0, 0.0, 0.0);\n"
"    else if (p < 2.5) m = float3(1.0, 1.0, 0.0);\n"
"    else if (p < 3.5) m = float3(1.0, 1.0, 1.0);\n"
"    else if (p < 4.5) m = float3(0.0, 1.0, 1.0);\n"
"    else              m = float3(0.0, 0.0, 1.0);\n"
"    float3 dark_m = lerp(float3(1.0, 1.0, 1.0), m, 2.0);\n"
"    float3 lite_m = lerp(float3(1.0, 1.0, 1.0), m, 1.0);\n"
"    return saturate(lerp(dark_m, lite_m, mx));\n"
"}\n"
"float4 ps_crt_final(VsOut i) : SV_Target {\n"
"    float2 crt_pos = i.uv * crt_size.xy - 0.5;\n"
"    float2 fp = frac(crt_pos);\n"
"    float x_base = (floor(crt_pos.x) + 0.5) * crt_size.z;\n"
"    float y0 = (floor(crt_pos.y) + 0.5) * crt_size.w;\n"
"    float y1 = y0 + crt_size.w;\n"
"    float4 s1 = sample_beam_row(x_base, fp.x, y0);\n"
"    float4 s2 = sample_beam_row(x_base, fp.x, y1);\n"
"    /* Scanline synthesis (scangamma = 5.0, gamma_in = 2.0 -> pow 2.5 / 0.4). */\n"
"    float3 c1 = pow(max(s1.rgb, 0.0), 2.5);\n"
"    float3 c2 = pow(max(s2.rgb, 0.0), 2.5);\n"
"    float f1 = fp.y;\n"
"    float f2 = 1.0 - f1;\n"
"    float wt1 = exp2(-10.0 * f1 * f1);\n"
"    float wt2 = exp2(-10.0 * f2 * f2);\n"
"    float3 ctmp = (c1 * wt1 + c2 * wt2) / (wt1 + wt2);\n"
"    float mc1 = max(c1.r, max(c1.g, c1.b)) + 1e-8;\n"
"    float mc2 = max(c2.r, max(c2.g, c2.b)) + 1e-8;\n"
"    float3 w1 = scan_weight(f1, c1 / mc1);\n"
"    float3 w2 = scan_weight(f2, c2 / mc2);\n"
"    float3 wsum = w1 + w2;\n"
"    float wf = max(wsum.r, max(wsum.g, wsum.b));\n"
"    if (wf > 1.0) { w1 /= wf; w2 /= wf; }\n"
"    float3 color = pow(saturate(c1 * w1 + c2 * w2), 0.4);\n"
"    float cm = max(color.r, max(color.g, color.b));\n"
"    float colmx = max(max(ctmp.r, max(ctmp.g, ctmp.b)), cm);\n"
"    float w3 = min((cm + 0.0001) / (colmx + 0.0005), 1.0);\n"
"    float mx = pow(max(colmx, 0.0), 0.70);\n"
"    /* Phosphor mask 13 with mask_zoom + staggered slot mask. */\n"
"    float2 px = i.pos.xy;\n"
"    float msize = mask_params.x;\n"
"    float mzoom = mask_params.y;\n"
"    float swidth_cfg = mask_params.z;\n"
"    float dslot = mask_params.w;\n"
"    float mwidth = 6.0 * msize;\n"
"    float mwidth1 = max(mwidth + mzoom, 2.0);\n"
"    float mscale = mwidth1 / mwidth;\n"
"    float mcoord = (px.x / msize) / mscale;\n"
"    float mc0 = floor(mcoord);\n"
"    float mlerp = frac(mcoord);\n"
"    float3 cmask = lerp(phosphor_mask13(mc0, mx), phosphor_mask13(mc0 + 1.0, mx), mlerp);\n"
"    float swidth = max(round(swidth_cfg * mscale), 1.0);\n"
"    float mlen = swidth * 2.0;\n"
"    float spx = floor(fmod(px.x, 0.99999 * mlen));\n"
"    float spy = floor(frac(px.y / (2.0 * dslot)) * 2.0 * dslot);\n"
"    float smask = 1.0;\n"
"    if ((spy < 0.5 && spx < swidth) || (abs(spy - dslot) < 0.5 && spx >= swidth)) {\n"
"        smask = 0.0;\n"
"    }\n"
"    cmask *= smask;\n"
"    float3 cmask1 = cmask;\n"
"    /* Bloom & magic glow textures. */\n"
"    float3 bloom = tex1.SampleLevel(samp_lin, i.uv, 0.0).rgb;\n"
"    float3 glow = tex2.SampleLevel(samp_lin, i.uv, 0.0).rgb;\n"
"    /* Mask bloom (0.50) lifts bright areas through the phosphor & slot mask. */\n"
"    float maxbl = max(max(bloom.r, max(bloom.g, bloom.b)), colmx);\n"
"    cmask = max(min(cmask + maxbl * 0.50, 1.0), cmask);\n"
"    /* Apply mask (mask_gamma == gamma_in == 2.0, so linear multiply). */\n"
"    float3 orig1 = color;\n"
"    color = saturate(color * cmask);\n"
"    /* Brightness boost + dark mask compensation. */\n"
"    float dark_comp = lerp(1.25, 1.0, mx);\n"
"    color *= lerp(1.40, 1.10, mx) * dark_comp;\n"
"    /* Soft phosphor bloom (0.20). */\n"
"    float3 b1 = min(bloom * (orig1 + color), max(0.5 * (colmx + orig1 - color), 0.001 * bloom));\n"
"    b1 = 0.5 * (b1 + lerp(b1, lerp(colmx * orig1, b1, 0.5), saturate(1.0 - color)));\n"
"    color += 0.20 * b1;\n"
"    /* Declip saturated highlights softly. */\n"
"    float cap = lerp(1.0, w3, 0.6);\n"
"    float m_c = max(color.r, max(color.g, color.b));\n"
"    if (m_c > cap) color *= cap / m_c;\n"
"    /* Magic glow highlight halo (glow = 0.08, m_glow_mask = 1.0). */\n"
"    float g_hi = pow(max(colmx, 0.0), 0.5);\n"
"    float3 mglow = lerp(0.35 * glow, 2.5 * glow, g_hi);\n"
"    color += 0.08 * saturate(cmask1) * mglow;\n"
"    /* Output gamma (gamma_out = 1.95). */\n"
"    color = pow(saturate(color), 1.0 / 1.95);\n"
"    return float4(color, 1.0);\n"
"}\n";

static CrtAnyFn crt_symbol(HMODULE module, const char *name)
{
    if (module == NULL) {
        return NULL;
    }
    return (CrtAnyFn)GetProcAddress(module, name);
}

static bool crt_load_apis(void)
{
    if (crt_api.attempted) {
        return crt_api.loaded;
    }
    crt_api.attempted = true;
    crt_api.d3d11 = LoadLibraryExW(L"d3d11.dll", NULL,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    crt_api.d3dcompiler = LoadLibraryExW(L"d3dcompiler_47.dll", NULL,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    crt_api.create_device = (D3d11CreateDeviceFn)crt_symbol(crt_api.d3d11,
        "D3D11CreateDevice");
    crt_api.compile = (D3dCompileFn)crt_symbol(crt_api.d3dcompiler,
        "D3DCompile");
    crt_api.loaded = crt_api.create_device != NULL && crt_api.compile != NULL;
    return crt_api.loaded;
}

void video_crt_unload(void)
{
    if (crt_api.d3dcompiler != NULL) {
        FreeLibrary(crt_api.d3dcompiler);
    }
    if (crt_api.d3d11 != NULL) {
        FreeLibrary(crt_api.d3d11);
    }
    memset(&crt_api, 0, sizeof(crt_api));
}

void video_crt_compute_layout(uint32_t src_w, uint32_t src_h,
    uint32_t target_w, uint32_t target_h, bool widescreen,
    VideoCrtLayout *out)
{
    memset(out, 0, sizeof(*out));
    if (src_w == 0 || src_h == 0) {
        src_w = 320;
        src_h = 192;
    }
    if (target_w == 0) {
        target_w = 1;
    }
    if (target_h == 0) {
        target_h = 1;
    }

    double src_aspect = (double)src_w / (double)src_h;
    double dst_aspect = (double)target_w / (double)target_h;
    double vp_w = (double)target_w;
    double vp_h = (double)target_h;
    double uv_y_offset = 0.0;
    double uv_y_scale = 1.0;

    if (widescreen && dst_aspect > src_aspect) {
        /* Fit horizontally to the display width and crop top and bottom. */
        uv_y_scale = src_aspect / dst_aspect;
        uv_y_offset = (1.0 - uv_y_scale) * 0.5;
    } else if (dst_aspect > src_aspect) {
        vp_w = (double)target_h * src_aspect;
        if (vp_w > (double)target_w) {
            vp_w = (double)target_w;
        }
    } else if (dst_aspect < src_aspect) {
        vp_h = (double)target_w / src_aspect;
        if (vp_h > (double)target_h) {
            vp_h = (double)target_h;
        }
    }
    if (vp_w < 1.0) {
        vp_w = 1.0;
    }
    if (vp_h < 1.0) {
        vp_h = 1.0;
    }

    uint32_t visible_h = (uint32_t)((double)src_h * uv_y_scale + 0.5);
    if (visible_h == 0) {
        visible_h = 1;
    }
    out->crt_width = src_w > CRT_MAX_SIGNAL_WIDTH ? CRT_MAX_SIGNAL_WIDTH : src_w;
    out->crt_height = visible_h > CRT_HIGH_RES_TRIGGER ? CRT_HIGH_RES_LINES : visible_h;

    out->viewport_w = (float)vp_w;
    out->viewport_h = (float)vp_h;
    out->viewport_x = (float)(((double)target_w - vp_w) * 0.5);
    out->viewport_y = (float)(((double)target_h - vp_h) * 0.5);
    out->uv_y_offset = (float)uv_y_offset;
    out->uv_y_scale = (float)uv_y_scale;
    out->letterboxed = ((uint32_t)(vp_w + 0.5) < target_w) ||
        ((uint32_t)(vp_h + 0.5) < target_h);

    /* Match Retro Crisis 1080p Flat below 4K, 4K Flat at 4K+. */
    if (target_h >= CRT_4K_HEIGHT_THRESHOLD) {
        out->mask_size = 2.0f;
        out->mask_zoom = -5.0f;
        out->slot_width = 12.0f;
        out->double_slot = 2.0f;
    } else {
        out->mask_size = 1.0f;
        out->mask_zoom = -2.0f;
        out->slot_width = 1.0f;
        out->double_slot = 2.0f;
    }
}

static void crt_set_reason(char *reason, size_t size, const char *what,
    HRESULT hr)
{
    if (reason == NULL || size == 0) {
        return;
    }
    int written = snprintf(reason, size, "%s (0x%08lX)", what,
        (unsigned long)hr);
    if (written < 0) {
        reason[0] = '\0';
    }
}

static void crt_target_release(CrtTarget *target)
{
    if (target->srv != NULL) {
        ID3D11ShaderResourceView_Release(target->srv);
        target->srv = NULL;
    }
    if (target->rtv != NULL) {
        ID3D11RenderTargetView_Release(target->rtv);
        target->rtv = NULL;
    }
    if (target->tex != NULL) {
        ID3D11Texture2D_Release(target->tex);
        target->tex = NULL;
    }
    target->width = 0;
    target->height = 0;
}

static bool crt_target_ensure(VideoCrt *crt, CrtTarget *target, uint32_t width,
    uint32_t height)
{
    if (target->tex != NULL && target->width == width &&
        target->height == height) {
        return true;
    }
    crt_target_release(target);
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = crt->rt_format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(crt->device, &desc, NULL,
            &target->tex))) {
        return false;
    }
    if (FAILED(ID3D11Device_CreateRenderTargetView(crt->device,
            (ID3D11Resource *)target->tex, NULL, &target->rtv))) {
        crt_target_release(target);
        return false;
    }
    if (FAILED(ID3D11Device_CreateShaderResourceView(crt->device,
            (ID3D11Resource *)target->tex, NULL, &target->srv))) {
        crt_target_release(target);
        return false;
    }
    target->width = width;
    target->height = height;
    return true;
}

static void crt_release_source_textures(VideoCrt *crt)
{
    if (crt->src_uv_srv != NULL) {
        ID3D11ShaderResourceView_Release(crt->src_uv_srv);
        crt->src_uv_srv = NULL;
    }
    if (crt->src_uv != NULL) {
        ID3D11Texture2D_Release(crt->src_uv);
        crt->src_uv = NULL;
    }
    if (crt->src_main_srv != NULL) {
        ID3D11ShaderResourceView_Release(crt->src_main_srv);
        crt->src_main_srv = NULL;
    }
    if (crt->src_main != NULL) {
        ID3D11Texture2D_Release(crt->src_main);
        crt->src_main = NULL;
    }
    crt->src_w = 0;
    crt->src_h = 0;
}

static bool crt_create_dynamic_tex(ID3D11Device *device, uint32_t width,
    uint32_t height, DXGI_FORMAT format, ID3D11Texture2D **tex,
    ID3D11ShaderResourceView **srv)
{
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateTexture2D(device, &desc, NULL, tex))) {
        return false;
    }
    if (FAILED(ID3D11Device_CreateShaderResourceView(device,
            (ID3D11Resource *)*tex, NULL, srv))) {
        ID3D11Texture2D_Release(*tex);
        *tex = NULL;
        return false;
    }
    return true;
}

static bool crt_ensure_source(VideoCrt *crt, uint32_t width, uint32_t height,
    bool is_nv12)
{
    if (width == 0 || height == 0) {
        return false;
    }
    if (crt->src_main != NULL && crt->src_w == width && crt->src_h == height &&
        crt->src_is_nv12 == is_nv12) {
        return true;
    }
    crt_release_source_textures(crt);
    if (is_nv12) {
        uint32_t uv_w = (width + 1u) / 2u;
        uint32_t uv_h = (height + 1u) / 2u;
        if (!crt_create_dynamic_tex(crt->device, width, height,
                DXGI_FORMAT_R8_UNORM, &crt->src_main, &crt->src_main_srv) ||
            !crt_create_dynamic_tex(crt->device, uv_w, uv_h,
                DXGI_FORMAT_R8G8_UNORM, &crt->src_uv, &crt->src_uv_srv)) {
            crt_release_source_textures(crt);
            return false;
        }
    } else {
        if (!crt_create_dynamic_tex(crt->device, width, height,
                DXGI_FORMAT_B8G8R8X8_UNORM, &crt->src_main,
                &crt->src_main_srv)) {
            crt_release_source_textures(crt);
            return false;
        }
    }
    crt->src_w = width;
    crt->src_h = height;
    crt->src_is_nv12 = is_nv12;
    return true;
}

static bool crt_upload_plane(ID3D11DeviceContext *context, ID3D11Texture2D *tex,
    const BYTE *src, uint32_t src_stride, uint32_t row_bytes, uint32_t rows)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)tex, 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    BYTE *dst = (BYTE *)mapped.pData;
    if (mapped.RowPitch == src_stride && src_stride == row_bytes) {
        memcpy(dst, src, (SIZE_T)row_bytes * rows);
    } else {
        for (uint32_t y = 0; y < rows; ++y) {
            memcpy(dst + (SIZE_T)y * mapped.RowPitch,
                src + (SIZE_T)y * src_stride, row_bytes);
        }
    }
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)tex, 0);
    return true;
}

bool video_crt_upload_bgra(VideoCrt *crt, uint32_t width, uint32_t height,
    const BYTE *data, uint32_t stride)
{
    if (crt == NULL || data == NULL ||
        !crt_ensure_source(crt, width, height, false)) {
        return false;
    }
    if (!crt_upload_plane(crt->context, crt->src_main, data, stride,
            width * 4u, height)) {
        return false;
    }
    crt->has_frame = true;
    ++crt->frame_index;
    return true;
}

bool video_crt_upload_nv12(VideoCrt *crt, uint32_t width, uint32_t height,
    const BYTE *y_plane, uint32_t y_stride, const BYTE *uv_plane,
    uint32_t uv_stride, bool bt709, bool full_range)
{
    if (crt == NULL || y_plane == NULL || uv_plane == NULL ||
        !crt_ensure_source(crt, width, height, true)) {
        return false;
    }
    uint32_t uv_w = (width + 1u) / 2u;
    uint32_t uv_h = (height + 1u) / 2u;
    if (!crt_upload_plane(crt->context, crt->src_main, y_plane, y_stride,
            width, height) ||
        !crt_upload_plane(crt->context, crt->src_uv, uv_plane, uv_stride,
            uv_w * 2u, uv_h)) {
        return false;
    }
    crt->src_bt709 = bt709;
    crt->src_full_range = full_range;
    crt->has_frame = true;
    ++crt->frame_index;
    return true;
}

static bool crt_create_backbuffer_rtv(VideoCrt *crt)
{
    ID3D11Texture2D *back = NULL;
    HRESULT hr = IDXGISwapChain_GetBuffer(crt->swap_chain, 0,
        &crt_iid_texture2d, (void **)&back);
    if (FAILED(hr)) {
        return false;
    }
    hr = ID3D11Device_CreateRenderTargetView(crt->device,
        (ID3D11Resource *)back, NULL, &crt->back_rtv);
    ID3D11Texture2D_Release(back);
    return SUCCEEDED(hr);
}

void video_crt_resize(VideoCrt *crt, const RECT *client)
{
    if (crt == NULL || client == NULL || crt->swap_chain == NULL) {
        return;
    }
    uint32_t w = client->right > client->left ?
        (uint32_t)(client->right - client->left) : 1u;
    uint32_t h = client->bottom > client->top ?
        (uint32_t)(client->bottom - client->top) : 1u;
    if (w == crt->back_w && h == crt->back_h && crt->back_rtv != NULL) {
        return;
    }
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 0, NULL, NULL);
    if (crt->back_rtv != NULL) {
        ID3D11RenderTargetView_Release(crt->back_rtv);
        crt->back_rtv = NULL;
    }
    HRESULT hr = IDXGISwapChain_ResizeBuffers(crt->swap_chain, 0, w, h,
        DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr) && crt_create_backbuffer_rtv(crt)) {
        crt->back_w = w;
        crt->back_h = h;
    }
}

static bool crt_compile_shader(const char *source, size_t source_len,
    const char *entry, const char *target, ID3DBlob **blob)
{
    ID3DBlob *errors = NULL;
    HRESULT hr = crt_api.compile(source, source_len, "wo4k_crt.hlsl", NULL,
        NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
    if (errors != NULL) {
        ID3D10Blob_Release(errors);
    }
    return SUCCEEDED(hr);
}

static bool crt_build_shaders(VideoCrt *crt)
{
    size_t len1 = sizeof(crt_hlsl_part1) - 1;
    size_t len2 = sizeof(crt_hlsl_part2) - 1;
    size_t total = len1 + len2;
    char *source = HeapAlloc(GetProcessHeap(), 0, total + 1);
    if (source == NULL) {
        return false;
    }
    memcpy(source, crt_hlsl_part1, len1);
    memcpy(source + len1, crt_hlsl_part2, len2);
    source[total] = '\0';

    ID3DBlob *vs_blob = NULL;
    ID3DBlob *ps_ntsc_blob = NULL;
    ID3DBlob *ps_bh_blob = NULL;
    ID3DBlob *ps_bv_blob = NULL;
    ID3DBlob *ps_fin_blob = NULL;
    bool ok = crt_compile_shader(source, total, "vs_main", "vs_4_0", &vs_blob) &&
        crt_compile_shader(source, total, "ps_ntsc_linear", "ps_4_0", &ps_ntsc_blob) &&
        crt_compile_shader(source, total, "ps_blur_h", "ps_4_0", &ps_bh_blob) &&
        crt_compile_shader(source, total, "ps_blur_v", "ps_4_0", &ps_bv_blob) &&
        crt_compile_shader(source, total, "ps_crt_final", "ps_4_0", &ps_fin_blob);
    HeapFree(GetProcessHeap(), 0, source);

    if (ok) {
        ok = SUCCEEDED(ID3D11Device_CreateVertexShader(crt->device,
                ID3D10Blob_GetBufferPointer(vs_blob),
                ID3D10Blob_GetBufferSize(vs_blob), NULL, &crt->vs)) &&
            SUCCEEDED(ID3D11Device_CreatePixelShader(crt->device,
                ID3D10Blob_GetBufferPointer(ps_ntsc_blob),
                ID3D10Blob_GetBufferSize(ps_ntsc_blob), NULL, &crt->ps_ntsc)) &&
            SUCCEEDED(ID3D11Device_CreatePixelShader(crt->device,
                ID3D10Blob_GetBufferPointer(ps_bh_blob),
                ID3D10Blob_GetBufferSize(ps_bh_blob), NULL, &crt->ps_blur_h)) &&
            SUCCEEDED(ID3D11Device_CreatePixelShader(crt->device,
                ID3D10Blob_GetBufferPointer(ps_bv_blob),
                ID3D10Blob_GetBufferSize(ps_bv_blob), NULL, &crt->ps_blur_v)) &&
            SUCCEEDED(ID3D11Device_CreatePixelShader(crt->device,
                ID3D10Blob_GetBufferPointer(ps_fin_blob),
                ID3D10Blob_GetBufferSize(ps_fin_blob), NULL, &crt->ps_final));
    }

    if (ps_fin_blob != NULL) {
        ID3D10Blob_Release(ps_fin_blob);
    }
    if (ps_bv_blob != NULL) {
        ID3D10Blob_Release(ps_bv_blob);
    }
    if (ps_bh_blob != NULL) {
        ID3D10Blob_Release(ps_bh_blob);
    }
    if (ps_ntsc_blob != NULL) {
        ID3D10Blob_Release(ps_ntsc_blob);
    }
    if (vs_blob != NULL) {
        ID3D10Blob_Release(vs_blob);
    }
    return ok;
}

static void crt_clear_and_present(VideoCrt *crt, bool sync)
{
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (crt->back_rtv == NULL) {
        return;
    }
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 1, &crt->back_rtv,
        NULL);
    ID3D11DeviceContext_ClearRenderTargetView(crt->context, crt->back_rtv,
        black);
    IDXGISwapChain_Present(crt->swap_chain, (sync && crt->vsync) ? 1u : 0u, 0u);
}

bool video_crt_render(VideoCrt *crt, bool sync)
{
    if (crt == NULL || crt->swap_chain == NULL || crt->back_rtv == NULL) {
        return false;
    }
    if (!crt->has_frame || crt->src_main_srv == NULL) {
        crt_clear_and_present(crt, sync);
        return true;
    }

    VideoCrtLayout layout;
    video_crt_compute_layout(crt->src_w, crt->src_h, crt->back_w, crt->back_h,
        crt->widescreen, &layout);
    if (!crt_target_ensure(crt, &crt->signal_rt, layout.crt_width,
            layout.crt_height)) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ID3D11DeviceContext_Map(crt->context,
            (ID3D11Resource *)crt->cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
            &mapped))) {
        CrtConstants cb;
        cb.src_size[0] = (float)crt->src_w;
        cb.src_size[1] = (float)crt->src_h;
        cb.src_size[2] = 1.0f / (float)crt->src_w;
        cb.src_size[3] = 1.0f / (float)crt->src_h;
        cb.crt_size[0] = (float)layout.crt_width;
        cb.crt_size[1] = (float)layout.crt_height;
        cb.crt_size[2] = 1.0f / (float)layout.crt_width;
        cb.crt_size[3] = 1.0f / (float)layout.crt_height;
        cb.glow_size[0] = (float)CRT_GLOW_WIDTH;
        cb.glow_size[1] = (float)CRT_GLOW_HEIGHT;
        cb.glow_size[2] = 1.0f / (float)CRT_GLOW_WIDTH;
        cb.glow_size[3] = 1.0f / (float)CRT_GLOW_HEIGHT;
        cb.out_size[0] = layout.viewport_w;
        cb.out_size[1] = layout.viewport_h;
        cb.out_size[2] = 1.0f / layout.viewport_w;
        cb.out_size[3] = 1.0f / layout.viewport_h;
        cb.color_params[0] = crt->src_is_nv12 ? 1.0f : 0.0f;
        cb.color_params[1] = crt->src_bt709 ? 1.0f : 0.0f;
        cb.color_params[2] = crt->src_full_range ? 1.0f : 0.0f;
        cb.color_params[3] = (float)(crt->frame_index & 1u);
        cb.mask_params[0] = layout.mask_size;
        cb.mask_params[1] = layout.mask_zoom;
        cb.mask_params[2] = layout.slot_width;
        cb.mask_params[3] = layout.double_slot;
        cb.crop_params[0] = layout.uv_y_offset;
        cb.crop_params[1] = layout.uv_y_scale;
        cb.crop_params[2] = 0.0f;
        cb.crop_params[3] = 0.0f;
        memcpy(mapped.pData, &cb, sizeof(cb));
        ID3D11DeviceContext_Unmap(crt->context,
            (ID3D11Resource *)crt->cbuffer, 0);
    }

    ID3D11ShaderResourceView *null_srvs[3] = { NULL, NULL, NULL };
    ID3D11RenderTargetView *null_rtvs[2] = { NULL, NULL };
    ID3D11DeviceContext_IASetInputLayout(crt->context, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(crt->context,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(crt->context, crt->vs, NULL, 0);
    ID3D11DeviceContext_PSSetSamplers(crt->context, 0, 1, &crt->sampler);
    ID3D11DeviceContext_PSSetConstantBuffers(crt->context, 0, 1, &crt->cbuffer);

    /* Pass 1: Decode + clean 2-phase NTSC + linearize -> signal_rt. */
    D3D11_VIEWPORT vp;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)layout.crt_width;
    vp.Height = (float)layout.crt_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(crt->context, 1, &vp);
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 1, &crt->signal_rt.rtv,
        NULL);
    ID3D11ShaderResourceView *p1_srvs[2] = { crt->src_main_srv, crt->src_uv_srv };
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 2, p1_srvs);
    ID3D11DeviceContext_PSSetShader(crt->context, crt->ps_ntsc, NULL, 0);
    ID3D11DeviceContext_Draw(crt->context, 3, 0);

    /* Pass 2: Horizontal bloom + magic glow (MRT) -> bloom_h, glow_h. */
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 3, null_srvs);
    vp.Width = (float)CRT_GLOW_WIDTH;
    vp.Height = (float)CRT_GLOW_HEIGHT;
    ID3D11DeviceContext_RSSetViewports(crt->context, 1, &vp);
    ID3D11RenderTargetView *p2_rtvs[2] = { crt->bloom_h.rtv, crt->glow_h.rtv };
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 2, p2_rtvs, NULL);
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 1,
        &crt->signal_rt.srv);
    ID3D11DeviceContext_PSSetShader(crt->context, crt->ps_blur_h, NULL, 0);
    ID3D11DeviceContext_Draw(crt->context, 3, 0);

    /* Pass 3: Vertical bloom + magic glow (MRT) -> bloom_rt, glow_rt. */
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 3, null_srvs);
    ID3D11RenderTargetView *p3_rtvs[2] = { crt->bloom_rt.rtv, crt->glow_rt.rtv };
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 2, p3_rtvs, NULL);
    ID3D11ShaderResourceView *p3_srvs[2] = { crt->bloom_h.srv, crt->glow_h.srv };
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 2, p3_srvs);
    ID3D11DeviceContext_PSSetShader(crt->context, crt->ps_blur_v, NULL, 0);
    ID3D11DeviceContext_Draw(crt->context, 3, 0);

    /* Pass 4: Final CRT scanlines + phosphor/slot mask + glow -> backbuffer. */
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 3, null_srvs);
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 2, null_rtvs, NULL);
    ID3D11DeviceContext_OMSetRenderTargets(crt->context, 1, &crt->back_rtv,
        NULL);
    if (layout.letterboxed) {
        static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        ID3D11DeviceContext_ClearRenderTargetView(crt->context, crt->back_rtv,
            black);
    }
    vp.TopLeftX = layout.viewport_x;
    vp.TopLeftY = layout.viewport_y;
    vp.Width = layout.viewport_w;
    vp.Height = layout.viewport_h;
    ID3D11DeviceContext_RSSetViewports(crt->context, 1, &vp);
    ID3D11ShaderResourceView *p4_srvs[3] = {
        crt->signal_rt.srv, crt->bloom_rt.srv, crt->glow_rt.srv
    };
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 3, p4_srvs);
    ID3D11DeviceContext_PSSetShader(crt->context, crt->ps_final, NULL, 0);
    ID3D11DeviceContext_Draw(crt->context, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(crt->context, 0, 3, null_srvs);

    HRESULT hr = IDXGISwapChain_Present(crt->swap_chain,
        (sync && crt->vsync) ? 1u : 0u, 0u);
    return SUCCEEDED(hr);
}

void video_crt_repaint(VideoCrt *crt)
{
    if (crt == NULL) {
        return;
    }
    video_crt_render(crt, false);
}

VideoCrt *video_crt_create(HWND window, bool vsync, bool widescreen,
    char *reason, size_t reason_size)
{
    if (window == NULL || !crt_load_apis()) {
        crt_set_reason(reason, reason_size,
            "D3D11 or D3DCompiler is unavailable",
            HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
        return NULL;
    }
    VideoCrt *crt = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*crt));
    if (crt == NULL) {
        crt_set_reason(reason, reason_size, "out of memory", E_OUTOFMEMORY);
        return NULL;
    }
    crt->window = window;
    crt->vsync = vsync;
    crt->widescreen = widescreen;

    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL actual_level = D3D_FEATURE_LEVEL_10_0;
    HRESULT hr = crt_api.create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
        &crt->device, &actual_level, &crt->context);
    if (FAILED(hr)) {
        crt_set_reason(reason, reason_size,
            "hardware D3D11 device could not be created", hr);
        video_crt_destroy(crt);
        return NULL;
    }

    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *dxgi_adapter = NULL;
    IDXGIFactory *dxgi_factory = NULL;
    hr = ID3D11Device_QueryInterface(crt->device, &crt_iid_dxgi_device,
        (void **)&dxgi_device);
    if (SUCCEEDED(hr)) {
        hr = IDXGIDevice_GetParent(dxgi_device, &crt_iid_dxgi_adapter,
            (void **)&dxgi_adapter);
    }
    if (SUCCEEDED(hr)) {
        hr = IDXGIAdapter_GetParent(dxgi_adapter, &crt_iid_dxgi_factory,
            (void **)&dxgi_factory);
    }

    RECT client = { 0, 0, 1, 1 };
    GetClientRect(window, &client);
    uint32_t w = client.right > client.left ?
        (uint32_t)(client.right - client.left) : 1u;
    uint32_t h = client.bottom > client.top ?
        (uint32_t)(client.bottom - client.top) : 1u;

    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC sc_desc;
        memset(&sc_desc, 0, sizeof(sc_desc));
        sc_desc.BufferCount = 2;
        sc_desc.BufferDesc.Width = w;
        sc_desc.BufferDesc.Height = h;
        sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc_desc.OutputWindow = window;
        sc_desc.SampleDesc.Count = 1;
        sc_desc.Windowed = TRUE;
        sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = IDXGIFactory_CreateSwapChain(dxgi_factory,
            (IUnknown *)crt->device, &sc_desc, &crt->swap_chain);
        if (SUCCEEDED(hr)) {
            /* Never let DXGI alter window style, focus, or Alt+Enter mode. */
            IDXGIFactory_MakeWindowAssociation(dxgi_factory, window,
                DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
        }
    }
    if (dxgi_factory != NULL) {
        IDXGIFactory_Release(dxgi_factory);
    }
    if (dxgi_adapter != NULL) {
        IDXGIAdapter_Release(dxgi_adapter);
    }
    if (dxgi_device != NULL) {
        IDXGIDevice_Release(dxgi_device);
    }
    if (FAILED(hr) || !crt_create_backbuffer_rtv(crt)) {
        crt_set_reason(reason, reason_size,
            "D3D11 swap chain could not be created",
            FAILED(hr) ? hr : E_FAIL);
        video_crt_destroy(crt);
        return NULL;
    }
    crt->back_w = w;
    crt->back_h = h;

    UINT fmt_support = 0;
    const UINT needed = D3D11_FORMAT_SUPPORT_TEXTURE2D |
        D3D11_FORMAT_SUPPORT_RENDER_TARGET |
        D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
    if (SUCCEEDED(ID3D11Device_CheckFormatSupport(crt->device,
            DXGI_FORMAT_R16G16B16A16_FLOAT, &fmt_support)) &&
        (fmt_support & needed) == needed) {
        crt->rt_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    } else {
        crt->rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    D3D11_SAMPLER_DESC samp;
    memset(&samp, 0, sizeof(samp));
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11_BUFFER_DESC cb_desc;
    memset(&cb_desc, 0, sizeof(cb_desc));
    cb_desc.ByteWidth = sizeof(CrtConstants);
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (!crt_build_shaders(crt) ||
        FAILED(ID3D11Device_CreateSamplerState(crt->device, &samp,
            &crt->sampler)) ||
        FAILED(ID3D11Device_CreateBuffer(crt->device, &cb_desc, NULL,
            &crt->cbuffer)) ||
        !crt_target_ensure(crt, &crt->bloom_h, CRT_GLOW_WIDTH,
            CRT_GLOW_HEIGHT) ||
        !crt_target_ensure(crt, &crt->glow_h, CRT_GLOW_WIDTH,
            CRT_GLOW_HEIGHT) ||
        !crt_target_ensure(crt, &crt->bloom_rt, CRT_GLOW_WIDTH,
            CRT_GLOW_HEIGHT) ||
        !crt_target_ensure(crt, &crt->glow_rt, CRT_GLOW_WIDTH,
            CRT_GLOW_HEIGHT)) {
        crt_set_reason(reason, reason_size,
            "CRT shaders or render targets could not be created", E_FAIL);
        video_crt_destroy(crt);
        return NULL;
    }

    crt_clear_and_present(crt, false);
    return crt;
}

void video_crt_destroy(VideoCrt *crt)
{
    if (crt == NULL) {
        return;
    }
    if (crt->context != NULL) {
        ID3D11DeviceContext_ClearState(crt->context);
    }
    crt_release_source_textures(crt);
    crt_target_release(&crt->glow_rt);
    crt_target_release(&crt->bloom_rt);
    crt_target_release(&crt->glow_h);
    crt_target_release(&crt->bloom_h);
    crt_target_release(&crt->signal_rt);
    if (crt->cbuffer != NULL) {
        ID3D11Buffer_Release(crt->cbuffer);
    }
    if (crt->sampler != NULL) {
        ID3D11SamplerState_Release(crt->sampler);
    }
    if (crt->ps_final != NULL) {
        ID3D11PixelShader_Release(crt->ps_final);
    }
    if (crt->ps_blur_v != NULL) {
        ID3D11PixelShader_Release(crt->ps_blur_v);
    }
    if (crt->ps_blur_h != NULL) {
        ID3D11PixelShader_Release(crt->ps_blur_h);
    }
    if (crt->ps_ntsc != NULL) {
        ID3D11PixelShader_Release(crt->ps_ntsc);
    }
    if (crt->vs != NULL) {
        ID3D11VertexShader_Release(crt->vs);
    }
    if (crt->back_rtv != NULL) {
        ID3D11RenderTargetView_Release(crt->back_rtv);
    }
    if (crt->swap_chain != NULL) {
        IDXGISwapChain_Release(crt->swap_chain);
    }
    if (crt->context != NULL) {
        ID3D11DeviceContext_Release(crt->context);
    }
    if (crt->device != NULL) {
        ID3D11Device_Release(crt->device);
    }
    HeapFree(GetProcessHeap(), 0, crt);
}
