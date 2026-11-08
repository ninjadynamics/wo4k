/* In-game presentation (--frames): decoded frames go, as 32-bit RGB, to
 * shared memory the game runtime draws from (common/wo_video_frames.h).
 *
 * Media Foundation: a source reader decodes the video on a worker thread
 * and publishes each frame when the presentation clock reaches it. Sound
 * stays with the media session. DirectShow (the original MPEG-1 files) fills
 * the same slots from a sample grabber; see video_dshow.c.
 *
 * The mapping is created with the first frame, never before: a file that
 * fails earlier leaves the name free for the fallback to the original. */
#ifndef WO_VIDEO_FRAMES_SINK_H
#define WO_VIDEO_FRAMES_SINK_H

#include <stdbool.h>
#include <windows.h>

#include "common/wo_video_frames.h"
#include "video/video_backend.h"

/* The two frame slots, as either decoder writes them. */
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

typedef struct VideoFrames VideoFrames;

/* The presentation time; false while playback has not started. Called on
 * the worker thread. */
typedef bool (*VideoFramesClock)(void *context, LONGLONG *time_hns);

/* Opens the file's first video stream as 32-bit RGB; fills info's frame
 * size. Media Foundation must be started. NULL with *error and *what set on
 * failure. */
VideoFrames *video_frames_open(const wchar_t *path, VideoInfo *info,
    HRESULT *error, const char **what);
/* The worker publishes the first frame as soon as it is decoded, so the
 * game has a picture at once; this lets it publish the rest as clock
 * reaches them. */
bool video_frames_run(VideoFrames *frames, VideoFramesClock clock, void *context);
bool video_frames_cued(const VideoFrames *frames);
/* The worker stopped: the stream ended, failed, or changed beyond use. */
bool video_frames_ended(const VideoFrames *frames);
/* Frames published so far (any thread). */
const volatile LONG *video_frames_counter(const VideoFrames *frames);
/* Stops the worker and releases everything. */
void video_frames_close(VideoFrames *frames);

#endif
