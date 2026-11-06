/* A playback backend renders one file into the player window. Media
 * Foundation is tried first; DirectShow handles what it cannot open, such as
 * the game's original MPEG-1 video elementary streams. Backends are driven
 * by polling from the window thread, so they need no callback objects. */
#ifndef WO_VIDEO_BACKEND_H
#define WO_VIDEO_BACKEND_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef enum VideoState {
    VIDEO_STATE_OPENING,
    VIDEO_STATE_READY,
    VIDEO_STATE_PLAYING,
    VIDEO_STATE_ENDED,
    VIDEO_STATE_FAILED
} VideoState;

typedef struct VideoInfo {
    uint32_t width;
    uint32_t height;
    double duration_seconds;
} VideoInfo;

typedef struct VideoBackend VideoBackend;

typedef struct VideoBackendOps {
    /* Drains pending media events and updates state. */
    void (*poll)(VideoBackend *backend);
    /* Starts playback once the state is VIDEO_STATE_READY. */
    void (*start)(VideoBackend *backend);
    bool (*position)(VideoBackend *backend, LONGLONG *position_hns);
    void (*layout)(VideoBackend *backend, const RECT *client);
    void (*repaint)(VideoBackend *backend);
    void (*destroy)(VideoBackend *backend);
} VideoBackendOps;

struct VideoBackend {
    const VideoBackendOps *ops;
    const char *name;
    VideoState state;
    VideoInfo info;
    HRESULT error;
    char reason[192];
};

/* Records a failure; the first reason wins. */
void video_backend_fail(VideoBackend *backend, HRESULT error,
    const char *what);

/* Each returns a backend in VIDEO_STATE_OPENING or VIDEO_STATE_READY, or a
 * failure HRESULT with reason (at least reason_size bytes) filled in. */
HRESULT video_mf_create(HWND window, const wchar_t *path, bool mute,
    VideoBackend **out, char *reason, size_t reason_size);
HRESULT video_dshow_create(HWND window, const wchar_t *path, bool mute,
    VideoBackend **out, char *reason, size_t reason_size);

/* Media Foundation is loaded on first use; this releases it at exit. */
void video_mf_unload(void);

#endif
