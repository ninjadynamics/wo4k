/* Shared memory through which wo4k-video.exe (64-bit) hands decoded movie
 * frames to the game runtime (32-bit), which presents them through the
 * game's own DirectDraw surfaces: exclusive fullscreen shows no other
 * window. The player creates WO_VIDEO_FRAMES_NAME followed by its decimal
 * process ID once it knows the frame size. The mapping holds this header,
 * then WO_VIDEO_FRAMES_SLOTS frames of slot_size bytes each: 32-bit
 * X8R8G8B8 pixels, top row first, stride bytes per row. */
#ifndef WO_VIDEO_FRAMES_H
#define WO_VIDEO_FRAMES_H

#include <windows.h>

#define WO_VIDEO_FRAMES_NAME L"Local\\WO4KVideoFrames-"
#define WO_VIDEO_FRAMES_MAGIC 0x4d524657u /* "WFRM" */
#define WO_VIDEO_FRAMES_SLOTS 2
#define WO_VIDEO_FRAMES_HEADER 64 /* offset of slot 0 */
#define WO_VIDEO_FRAMES_MAX_WIDTH 4096
#define WO_VIDEO_FRAMES_MAX_HEIGHT 2304

typedef struct WoVideoFrames {
    DWORD magic;
    DWORD header_size;
    DWORD width;
    DWORD height;
    DWORD stride;
    DWORD slot_size;
    volatile LONG latest; /* newest complete slot, -1 before the first */
    volatile LONG frames; /* frames published so far */
    /* Odd while the player writes the slot. A reader keeps a copy only if
     * the slot's sequence was even, and unchanged after copying. */
    volatile LONG sequence[WO_VIDEO_FRAMES_SLOTS];
    LONGLONG time_hns[WO_VIDEO_FRAMES_SLOTS];
} WoVideoFrames;

_Static_assert(sizeof(WoVideoFrames) <= WO_VIDEO_FRAMES_HEADER,
    "the frame header fits before slot 0");

#endif
