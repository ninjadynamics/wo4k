/* The player window: black, borderless, topmost over the game window's
 * monitor, owned by the game window and never activated, so the DirectDraw
 * game keeps focus and its surfaces. The cursor is hidden over it. */
#ifndef WO_VIDEO_WINDOW_H
#define WO_VIDEO_WINDOW_H

#include <stdbool.h>
#include <windows.h>

#include "video/video_backend.h"

typedef struct VideoWindow {
    HWND hwnd;
    VideoBackend *backend;
    bool clicked;
    bool close_requested;
    bool destroyed;
    /* Set if the window was ever activated, which must never happen. */
    bool activated;
} VideoWindow;

/* probe: a small 640x480 window centred on the primary monitor's work area,
 * not topmost. owner may be NULL. */
bool video_window_create(VideoWindow *window, HINSTANCE instance, HWND owner,
    bool probe);
void video_window_set_backend(VideoWindow *window, VideoBackend *backend);
void video_window_destroy(VideoWindow *window);

#endif
