/* DirectShow backend. Windows' own MPEG-I Stream Splitter and MPEG Video
 * Decoder play the game's original MPEG-1 video elementary streams, which
 * Media Foundation's MPEG source rejects. The renderer's window becomes a
 * letterboxed child of the player window. */
#include "video/video_backend.h"

#include <stdio.h>

#include <dshow.h>

#include "video/video_core.h"

typedef struct DshowPlayer {
    VideoBackend base;
    HWND window;
    IGraphBuilder *graph;
    IMediaControl *control;
    IMediaEventEx *events;
    IMediaSeeking *seeking;
    IVideoWindow *video_window;
    bool owns_video_window;
} DshowPlayer;

static void dshow_reason(char *reason, size_t size, const char *what,
    HRESULT hr)
{
    int written = snprintf(reason, size, "%s (0x%08lX)", what, (unsigned long)hr);
    if (written < 0 && size > 0) {
        reason[0] = '\0';
    }
}

static void dshow_release(DshowPlayer *player)
{
    if (player->control != NULL) {
        IMediaControl_Stop(player->control);
    }
    if (player->video_window != NULL) {
        if (player->owns_video_window) {
            /* Detach before the player window can be destroyed. */
            IVideoWindow_put_Visible(player->video_window, OAFALSE);
            IVideoWindow_put_MessageDrain(player->video_window, (OAHWND)NULL);
            IVideoWindow_put_Owner(player->video_window, (OAHWND)NULL);
        }
        IVideoWindow_Release(player->video_window);
    }
    if (player->seeking != NULL) {
        IMediaSeeking_Release(player->seeking);
    }
    if (player->events != NULL) {
        IMediaEventEx_Release(player->events);
    }
    if (player->control != NULL) {
        IMediaControl_Release(player->control);
    }
    if (player->graph != NULL) {
        IGraphBuilder_Release(player->graph);
    }
}

static void dshow_poll(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    long code = 0;
    LONG_PTR first = 0;
    LONG_PTR second = 0;
    while (IMediaEventEx_GetEvent(player->events, &code, &first, &second, 0) ==
        S_OK) {
        if (code == EC_COMPLETE && backend->state != VIDEO_STATE_FAILED) {
            backend->state = VIDEO_STATE_ENDED;
        } else if (code == EC_ERRORABORT || code == EC_ERRORABORTEX) {
            video_backend_fail(backend, (HRESULT)first,
                "DirectShow playback failed");
        } else if (code == EC_USERABORT) {
            video_backend_fail(backend, E_ABORT,
                "the DirectShow video window closed");
        }
        IMediaEventEx_FreeEventParams(player->events, code, first, second);
    }
}

static void dshow_start(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    HRESULT hr = IMediaControl_Run(player->control);
    if (FAILED(hr)) {
        video_backend_fail(backend, hr, "DirectShow could not start playback");
        return;
    }
    backend->state = VIDEO_STATE_PLAYING;
}

static bool dshow_position(VideoBackend *backend, LONGLONG *position_hns)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    LONGLONG position = 0;
    if (player->seeking == NULL ||
        FAILED(IMediaSeeking_GetCurrentPosition(player->seeking, &position))) {
        return false;
    }
    *position_hns = position;
    return true;
}

static void dshow_layout(VideoBackend *backend, const RECT *client)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    RECT fitted = video_letterbox(client, backend->info.aspect_x,
        backend->info.aspect_y);
    IVideoWindow_SetWindowPosition(player->video_window, fitted.left,
        fitted.top, fitted.right - fitted.left, fitted.bottom - fitted.top);
}

static void dshow_repaint(VideoBackend *backend)
{
    /* The renderer's child window paints itself. */
    (void)backend;
}

static void dshow_destroy(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    dshow_release(player);
    HeapFree(GetProcessHeap(), 0, player);
}

static const VideoBackendOps dshow_ops = {
    dshow_poll, dshow_start, dshow_position, dshow_layout, dshow_repaint,
    dshow_destroy
};

static HRESULT dshow_query(IGraphBuilder *graph, REFIID iid, void **out)
{
    return IGraphBuilder_QueryInterface(graph, iid, out);
}

/* The renderer's native size and preferred display aspect. */
static HRESULT dshow_read_video_info(IGraphBuilder *graph, VideoInfo *info)
{
    IBasicVideo *basic = NULL;
    IBasicVideo2 *basic2 = NULL;
    long width = 0;
    long height = 0;
    HRESULT hr = dshow_query(graph, &IID_IBasicVideo, (void **)&basic);
    if (SUCCEEDED(hr)) {
        hr = IBasicVideo_GetVideoSize(basic, &width, &height);
        IBasicVideo_Release(basic);
    }
    if (SUCCEEDED(hr) && (width <= 0 || height <= 0)) {
        hr = E_UNEXPECTED;
    }
    if (FAILED(hr)) {
        return hr;
    }
    info->width = (uint32_t)width;
    info->height = (uint32_t)height;
    info->aspect_x = (uint32_t)width;
    info->aspect_y = (uint32_t)height;
    if (SUCCEEDED(dshow_query(graph, &IID_IBasicVideo2, (void **)&basic2))) {
        long aspect_x = 0;
        long aspect_y = 0;
        if (SUCCEEDED(IBasicVideo2_GetPreferredAspectRatio(basic2, &aspect_x,
                &aspect_y)) && aspect_x > 0 && aspect_y > 0) {
            info->aspect_x = (uint32_t)aspect_x;
            info->aspect_y = (uint32_t)aspect_y;
        }
        IBasicVideo2_Release(basic2);
    }
    return S_OK;
}

static void dshow_mute(IGraphBuilder *graph)
{
    IBasicAudio *audio = NULL;
    if (SUCCEEDED(dshow_query(graph, &IID_IBasicAudio, (void **)&audio))) {
        IBasicAudio_put_Volume(audio, -10000);
        IBasicAudio_Release(audio);
    }
}

static HRESULT dshow_attach_window(DshowPlayer *player)
{
    IVideoWindow *video = player->video_window;
    HRESULT hr = IVideoWindow_put_Owner(video, (OAHWND)player->window);
    if (FAILED(hr)) {
        return hr;
    }
    player->owns_video_window = true;
    hr = IVideoWindow_put_WindowStyle(video,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    if (SUCCEEDED(hr)) {
        /* Clicks on the video reach the player window, which skips. */
        hr = IVideoWindow_put_MessageDrain(video, (OAHWND)player->window);
    }
    if (SUCCEEDED(hr)) {
        /* MinGW's IVideoWindow_HideCursor macro names its parameter after
         * the method, which breaks the expansion; call the vtable. */
        video->lpVtbl->HideCursor(video, OATRUE);
    }
    return hr;
}

HRESULT video_dshow_create(HWND window, const wchar_t *path, bool mute,
    VideoBackend **out, char *reason, size_t reason_size)
{
    *out = NULL;
    DshowPlayer *player = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*player));
    if (player == NULL) {
        dshow_reason(reason, reason_size, "out of memory", E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    player->base.ops = &dshow_ops;
    player->base.name = "directshow";
    player->base.state = VIDEO_STATE_OPENING;
    player->window = window;
    const char *step = "DirectShow is unavailable";
    HRESULT hr = CoCreateInstance(&CLSID_FilterGraph, NULL,
        CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&player->graph);
    if (SUCCEEDED(hr)) {
        step = "DirectShow could not render the file";
        hr = IGraphBuilder_RenderFile(player->graph, path, NULL);
    }
    if (SUCCEEDED(hr)) {
        step = "DirectShow has no playback control";
        hr = dshow_query(player->graph, &IID_IMediaControl,
            (void **)&player->control);
    }
    if (SUCCEEDED(hr)) {
        hr = dshow_query(player->graph, &IID_IMediaEventEx,
            (void **)&player->events);
    }
    if (SUCCEEDED(hr)) {
        hr = dshow_query(player->graph, &IID_IVideoWindow,
            (void **)&player->video_window);
    }
    if (SUCCEEDED(hr)) {
        step = "no video stream";
        hr = dshow_read_video_info(player->graph, &player->base.info);
    }
    if (SUCCEEDED(hr)) {
        step = "DirectShow could not attach the video window";
        hr = dshow_attach_window(player);
    }
    if (FAILED(hr)) {
        dshow_reason(reason, reason_size, step, hr);
        dshow_release(player);
        HeapFree(GetProcessHeap(), 0, player);
        return hr;
    }
    LONGLONG duration = 0;
    if (SUCCEEDED(dshow_query(player->graph, &IID_IMediaSeeking,
            (void **)&player->seeking)) &&
        SUCCEEDED(IMediaSeeking_GetDuration(player->seeking, &duration))) {
        player->base.info.duration_seconds = (double)duration / 1e7;
    }
    if (mute) {
        dshow_mute(player->graph);
    }
    player->base.state = VIDEO_STATE_READY;
    *out = &player->base;
    return S_OK;
}
