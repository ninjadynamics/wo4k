/* D3D11 CRT + clean 2-phase NTSC shader renderer for wo4k-video.exe.
 *
 * Models the look of Retro Crisis's "RC GDV-NTSC - Dreamcast - Clean"
 * (1080p Flat / 4K Flat) preset in a clean-room, four-pass HLSL pipeline
 * designed to run effortlessly on low-end integrated GPUs:
 *   Pass 1 (<= 640x320): NV12/BGRA decode, clean 2-phase NTSC YIQ filtering,
 *                        input gamma linearization.
 *   Pass 2 (320x240 MRT): Horizontal Gaussian bloom + magic-glow highlight blur.
 *   Pass 3 (320x240 MRT): Vertical Gaussian bloom + magic-glow highlight blur.
 *   Pass 4 (fitted rect): Horizontal CRT beam shaping, scanline synthesis,
 *                         6-subpixel phosphor mask with zoom, staggered slot
 *                         mask, mask bloom, brightboost, glow and gamma out.
 *
 * d3d11.dll and d3dcompiler_47.dll are loaded dynamically from System32. If
 * hardware D3D11 is unavailable, video_crt_create returns NULL and the player
 * falls back to EVR unchanged. */
#ifndef WO_VIDEO_CRT_H
#define WO_VIDEO_CRT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

typedef struct VideoCrt VideoCrt;

typedef struct VideoCrtLayout {
    uint32_t crt_width;
    uint32_t crt_height;
    float viewport_x;
    float viewport_y;
    float viewport_w;
    float viewport_h;
    float uv_y_offset;
    float uv_y_scale;
    float mask_size;
    float mask_zoom;
    float slot_width;
    float double_slot;
    bool letterboxed;
} VideoCrtLayout;

/* Pure layout math: computes internal CRT signal grid, destination viewport
 * inside (target_w, target_h), vertical source crop (in widescreen mode, fits
 * horizontally and crops top/bottom), and 1080p vs 4K Flat mask parameters. */
void video_crt_compute_layout(uint32_t src_w, uint32_t src_h,
    uint32_t target_w, uint32_t target_h, bool widescreen,
    VideoCrtLayout *out);

/* Creates a hardware D3D11 CRT renderer bound to window. On failure returns
 * NULL and fills reason (if non-NULL). */
VideoCrt *video_crt_create(HWND window, bool vsync, bool widescreen,
    char *reason, size_t reason_size);

/* Uploads a decoded 32-bit BGRA/XRGB frame (top-down, stride bytes/row). */
bool video_crt_upload_bgra(VideoCrt *crt, uint32_t width, uint32_t height,
    const BYTE *data, uint32_t stride);

/* Uploads a decoded NV12 frame (Y plane width x height, interleaved UV plane
 * width x (height/2), top-down). */
bool video_crt_upload_nv12(VideoCrt *crt, uint32_t width, uint32_t height,
    const BYTE *y_plane, uint32_t y_stride, const BYTE *uv_plane,
    uint32_t uv_stride, bool bt709, bool full_range);

/* Updates swap chain size to match the window client rectangle. */
void video_crt_resize(VideoCrt *crt, const RECT *client);

/* Runs the CRT shader passes on the last uploaded frame and presents it.
 * If sync is false, presents immediately without waiting for VSync (used
 * while cueing hidden windows or in --probe). */
bool video_crt_render(VideoCrt *crt, bool sync);

/* Re-presents the current frame (or black before the first frame). */
void video_crt_repaint(VideoCrt *crt);

/* Releases all GPU resources and frees crt. */
void video_crt_destroy(VideoCrt *crt);

/* Releases dynamically loaded D3D11/D3DCompiler DLLs at process exit. */
void video_crt_unload(void);

#endif
