/* What can end a movie early, besides a click in the player window:
 *   - the skip event, which the game runtime signals when its own message
 *     loop sees Esc, Enter or Space for the game window;
 *   - a newly pressed controller button in the controller host's shared
 *     block Local\WO2097Input-<game pid>;
 *   - the game process exiting.
 * The player reads no keyboard or other input itself. */
#ifndef WO_VIDEO_INPUT_H
#define WO_VIDEO_INPUT_H

#include <stdbool.h>
#include <windows.h>

#include "video/video_core.h"

typedef enum VideoInputEvent {
    VIDEO_INPUT_NONE,
    VIDEO_INPUT_SKIP_KEY,
    VIDEO_INPUT_CONTROLLER,
    VIDEO_INPUT_GAME_EXITED
} VideoInputEvent;

typedef struct VideoInput {
    HANDLE skip_event;
    HANDLE game_process;
    HANDLE mapping;
    void *view;
    const volatile ControllerWire *wire;
    VideoControllerState controller;
    HWND owner;
    bool focus_lost;
} VideoInput;

/* The controller block is read whenever a game process is named, probe
 * mode included, so tests can drive it. */
void video_input_open(VideoInput *input, const VideoOptions *options,
    HWND owner);
/* Handles worth waking for; returns how many were written (at most 2). */
DWORD video_input_handles(const VideoInput *input, HANDLE handles[2]);
VideoInputEvent video_input_poll(VideoInput *input);
void video_input_close(VideoInput *input);

#endif
