/* Both backends present through the Enhanced Video Renderer: windowless,
 * drawing straight into the player window, letterboxed with black bars.
 * Unlike DirectShow's default renderer it creates no child window, so
 * nothing ever activates the player and takes focus from the game. */
#ifndef WO_VIDEO_EVR_H
#define WO_VIDEO_EVR_H

#include <windows.h>
#include <evr.h>

/* MinGW's GUID libraries define neither; evr.h only declares the IID. */
extern const GUID video_iid_display_control;
extern const GUID video_clsid_evr;

/* Preserve the picture's aspect ratio inside black borders. */
void video_evr_prepare(IMFVideoDisplayControl *display);
/* Fill the client area; the renderer letterboxes inside it. */
void video_evr_layout(IMFVideoDisplayControl *display, const RECT *client);

#endif
