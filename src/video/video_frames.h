/* Decoded frame transport for wo4k-video.exe:
 *
 * 1. Shared memory (--frames): 32-bit RGB frames go to
 *    Local\WO4KVideoFrames-<pid> (common/wo_video_frames.h) so the game
 *    runtime can present them on its own DirectDraw surfaces.
 * 2. In-process (CRT shader window): decoded frames stay in a local
 *    double buffer (VideoFrameBuffer) and are uploaded directly to the
 *    D3D11 CRT renderer. Media Foundation prefers native NV12 here to avoid
 *    CPU color conversion on low-end PCs, falling back to RGB32 if needed.
 *
 * The shared-memory mapping is created with the first frame, never before:
 * a file that fails earlier leaves the name free for the fallback to the
 * original. */
#ifndef WO_VIDEO_FRAMES_SINK_H
#define WO_VIDEO_FRAMES_SINK_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "common/wo_video_frames.h"
#include "video/video_backend.h"
#include "video/video_crt.h"

typedef enum VideoPixelFormat {
    VIDEO_PIXEL_BGRA,
    VIDEO_PIXEL_NV12
} VideoPixelFormat;

/* The two frame slots in shared memory (--frames). */
typedef struct VideoFrameSlots {
    HANDLE mapping;
    WoVideoFrames *view;
    BYTE *slots;
    volatile LONG published; /* frames published, readable without the view */
} VideoFrameSlots;

/* Creates the mapping named after this process for frames of this size. */
bool video_frame_slots_create(VideoFrameSlots *slots, uint32_t width,
    uint32_t height);
/* Copies a frame (rows pitch bytes apart from its top row, negative going up
 * in memory) into the slot not holding the newest one; returns that slot. */
LONG video_frame_slots_write(VideoFrameSlots *slots, const BYTE *top,
    LONG pitch, LONGLONG time_hns);
/* Makes a written slot the newest. */
void video_frame_slots_publish(VideoFrameSlots *slots, LONG slot);
void video_frame_slots_close(VideoFrameSlots *slots);

/* In-process double buffer for the D3D11 CRT renderer (no shared section). */
typedef struct VideoFrameBuffer {
    BYTE *data;
    uint32_t width;
    uint32_t height;
    uint32_t y_stride;
    uint32_t uv_stride;
    SIZE_T uv_offset;
    SIZE_T slot_size;
    VideoPixelFormat format;
    bool bt709;
    bool full_range;
    volatile LONG latest;
    volatile LONG published;
    volatile LONG sequence[WO_VIDEO_FRAMES_SLOTS];
    HWND notify_window;
    volatile LONG notified;
} VideoFrameBuffer;

bool video_frame_buffer_create(VideoFrameBuffer *buffer, uint32_t width,
    uint32_t height, VideoPixelFormat format, bool bt709, bool full_range,
    HWND notify_window);
LONG video_frame_buffer_write_bgra(VideoFrameBuffer *buffer, const BYTE *top,
    LONG pitch);
LONG video_frame_buffer_write_nv12(VideoFrameBuffer *buffer,
    const BYTE *y_top, LONG y_pitch, const BYTE *uv_top, LONG uv_pitch);
void video_frame_buffer_publish(VideoFrameBuffer *buffer, LONG slot);
/* If a newer frame has been published than *last_shown, uploads it to crt
 * and renders it. Returns true when a new frame was rendered. */
bool video_frame_buffer_present(VideoFrameBuffer *buffer, VideoCrt *crt,
    LONG *last_shown, bool sync);
void video_frame_buffer_close(VideoFrameBuffer *buffer);

typedef struct VideoFrames VideoFrames;

/* The presentation time; false while playback has not started. Called on
 * the worker thread. */
typedef bool (*VideoFramesClock)(void *context, LONGLONG *time_hns);

/* Opens the file's first video stream. When local is false (--frames),
 * outputs 32-bit RGB to shared memory. When local is true (CRT window),
 * prefers native NV12 (falling back to RGB32) into an in-process buffer and
 * wakes notify_window with WM_NULL as each frame is published. */
VideoFrames *video_frames_open(const wchar_t *path, bool local,
    HWND notify_window, VideoInfo *info, HRESULT *error, const char **what);
/* The worker publishes the first frame as soon as it is decoded, so the
 * game or CRT window has a picture at once; this lets it publish the rest
 * as clock reaches them. */
bool video_frames_run(VideoFrames *frames, VideoFramesClock clock, void *context);
bool video_frames_cued(const VideoFrames *frames);
/* The worker stopped: the stream ended, failed, or changed beyond use. */
bool video_frames_ended(const VideoFrames *frames);
/* Frames published so far (any thread). */
const volatile LONG *video_frames_counter(const VideoFrames *frames);
/* Frames dropped because decoding fell behind the clock. */
LONG video_frames_dropped(const VideoFrames *frames);
/* Presents the newest local-mode frame to crt if one arrived since *last_shown. */
bool video_frames_present(VideoFrames *frames, VideoCrt *crt, LONG *last_shown,
    bool sync);
/* Stops the worker and releases everything. */
void video_frames_close(VideoFrames *frames);

#endif
