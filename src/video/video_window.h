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
    HWND owner;
    VideoBackend *backend;
    RECT bounds;
    bool probe;
    bool layered; /* created hidden: transparent until shown */
    bool staged;  /* on screen but fully transparent and click-through */
    bool shown;
    bool clicked;
    bool close_requested;
    bool destroyed;
    /* Set if the window was ever activated, which must never happen. */
    bool activated;
    /* The other program's window it was last put behind, if any. */
    HWND behind;
} VideoWindow;

typedef enum VideoWindowMove {
    VIDEO_WINDOW_STAYED,
    VIDEO_WINDOW_RAISED, /* back over the game */
    VIDEO_WINDOW_BEHIND  /* behind another program in front */
} VideoWindowMove;

/* probe: a small 640x480 window centred on the primary monitor's work area,
 * not topmost. owner may be NULL. hidden: create it without showing it, so
 * a movie can be opened ahead of time; video_window_show reveals it. */
bool video_window_create(VideoWindow *window, HINSTANCE instance, HWND owner,
    bool probe, bool hidden);
/* Put a hidden window on screen fully transparent and click-through. The
 * compositor then already includes it while the game still presents, so
 * video_window_show makes it visible at once; over a full-screen game,
 * showing a window for the first time otherwise takes the compositor about
 * 300 ms. False when the window cannot be staged. */
bool video_window_stage(VideoWindow *window);
/* Show (or make opaque) without activating, topmost unless probe; repeated
 * calls do nothing. */
void video_window_show(VideoWindow *window);
/* Follows which program is in front, without ever activating the window.
 * While the game (or the player) is, the window stays topmost and above any
 * of the game's windows covering it: DDrawCompat raises its presentation
 * window when the game regains focus, which hid the movie. While another
 * program is, the window leaves the topmost band and stays behind it.
 * Does nothing for a probe window, without an owner, or before staging. */
VideoWindowMove video_window_follow_focus(VideoWindow *window);
/* The same, with the window in front given (NULL: leave it as it is). */
VideoWindowMove video_window_arrange(VideoWindow *window, HWND foreground);
void video_window_set_backend(VideoWindow *window, VideoBackend *backend);
void video_window_destroy(VideoWindow *window);

#endif
