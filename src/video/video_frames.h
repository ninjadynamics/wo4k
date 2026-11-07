/* In-game presentation (--frames): a Media Foundation source reader decodes
 * the video to 32-bit RGB on a worker thread and publishes each frame, when
 * the presentation clock reaches it, to shared memory the game runtime draws
 * from (common/wo_video_frames.h). Sound stays with the media session. */
#ifndef WO_VIDEO_FRAMES_SINK_H
#define WO_VIDEO_FRAMES_SINK_H

#include <stdbool.h>
#include <windows.h>

#include "video/video_backend.h"

typedef struct VideoFrames VideoFrames;

/* The presentation time; false while playback has not started. Called on
 * the worker thread. */
typedef bool (*VideoFramesClock)(void *context, LONGLONG *time_hns);

/* Opens the file's first video stream as 32-bit RGB and creates the mapping,
 * named after this process; fills info's frame size. Media Foundation must
 * be started. NULL with *error and *what set on failure. */
VideoFrames *video_frames_open(const wchar_t *path, VideoInfo *info,
    HRESULT *error, const char **what);
/* The worker publishes the first frame as soon as it is decoded, so the
 * game has a picture at once; this lets it publish the rest as clock
 * reaches them. */
bool video_frames_run(VideoFrames *frames, VideoFramesClock clock, void *context);
bool video_frames_cued(const VideoFrames *frames);
bool video_frames_ended(const VideoFrames *frames);
/* Frames published so far (any thread). */
const volatile LONG *video_frames_counter(const VideoFrames *frames);
/* Stops the worker and releases everything. */
void video_frames_close(VideoFrames *frames);

#endif
