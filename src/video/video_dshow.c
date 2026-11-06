/* DirectShow backend. Windows' own MPEG-I Stream Splitter and MPEG Video
 * Decoder play the game's original MPEG-1 video elementary streams, which
 * Media Foundation's MPEG source rejects.
 *
 * The graph renders only into renderers placed in it beforehand: the
 * Enhanced Video Renderer, windowless in the player window, and (unless
 * muted) a DirectSound renderer. DirectShow's default video renderer is
 * never used: it shows a child window with ShowWindow, which activates the
 * player and would take focus from the game. */
#include "video/video_backend.h"

#include <stdio.h>

#include <dshow.h>

#include "video/video_evr.h"

typedef struct DshowPlayer {
    VideoBackend base;
    HWND window;
    IGraphBuilder *graph;
    IMediaControl *control;
    IMediaEventEx *events;
    IMediaSeeking *seeking;
    IBaseFilter *video_renderer;
    IMFVideoDisplayControl *display;
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
        IMediaControl_Release(player->control);
    }
    if (player->display != NULL) {
        IMFVideoDisplayControl_Release(player->display);
    }
    if (player->video_renderer != NULL) {
        IBaseFilter_Release(player->video_renderer);
    }
    if (player->seeking != NULL) {
        IMediaSeeking_Release(player->seeking);
    }
    if (player->events != NULL) {
        IMediaEventEx_Release(player->events);
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
            video_backend_fail(backend, E_ABORT, "DirectShow playback stopped");
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
    video_evr_layout(player->display, client);
}

static void dshow_repaint(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    IMFVideoDisplayControl_RepaintVideo(player->display);
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

/* The first pin of the given direction, with a reference, or NULL. */
static IPin *dshow_find_pin(IBaseFilter *filter, PIN_DIRECTION direction)
{
    IEnumPins *pins = NULL;
    IPin *pin = NULL;
    IPin *found = NULL;
    if (FAILED(IBaseFilter_EnumPins(filter, &pins))) {
        return NULL;
    }
    while (found == NULL && IEnumPins_Next(pins, 1, &pin, NULL) == S_OK) {
        PIN_DIRECTION actual = PINDIR_INPUT;
        if (SUCCEEDED(IPin_QueryDirection(pin, &actual)) &&
            actual == direction) {
            found = pin;
        } else {
            IPin_Release(pin);
        }
    }
    IEnumPins_Release(pins);
    return found;
}

static bool dshow_input_connected(IBaseFilter *filter)
{
    IPin *peer = NULL;
    IPin *input = dshow_find_pin(filter, PINDIR_INPUT);
    if (input == NULL) {
        return false;
    }
    bool connected = IPin_ConnectedTo(input, &peer) == S_OK;
    if (peer != NULL) {
        IPin_Release(peer);
    }
    IPin_Release(input);
    return connected;
}

/* The Enhanced Video Renderer, windowless in the player window. */
static HRESULT dshow_add_video_renderer(DshowPlayer *player)
{
    IMFGetService *service = NULL;
    HRESULT hr = CoCreateInstance(&video_clsid_evr, NULL, CLSCTX_INPROC_SERVER,
        &IID_IBaseFilter, (void **)&player->video_renderer);
    if (SUCCEEDED(hr)) {
        hr = IGraphBuilder_AddFilter(player->graph, player->video_renderer,
            L"Enhanced Video Renderer");
    }
    if (SUCCEEDED(hr)) {
        hr = IBaseFilter_QueryInterface(player->video_renderer,
            &IID_IMFGetService, (void **)&service);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFGetService_GetService(service, &MR_VIDEO_RENDER_SERVICE,
            &video_iid_display_control, (void **)&player->display);
        IMFGetService_Release(service);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFVideoDisplayControl_SetVideoWindow(player->display,
            player->window);
    }
    if (SUCCEEDED(hr)) {
        video_evr_prepare(player->display);
    }
    return hr;
}

/* Audio is optional: without a sound device the movie plays silently. */
static void dshow_add_audio_renderer(IGraphBuilder *graph)
{
    IBaseFilter *audio = NULL;
    if (FAILED(CoCreateInstance(&CLSID_DSoundRender, NULL,
            CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&audio))) {
        return;
    }
    IGraphBuilder_AddFilter(graph, audio, L"Audio Renderer");
    IBaseFilter_Release(audio);
}

/* source -> splitter -> decoders -> the renderers already in the graph. */
static HRESULT dshow_render(IGraphBuilder *graph, const wchar_t *path)
{
    IBaseFilter *source = NULL;
    IFilterGraph2 *graph2 = NULL;
    IPin *output = NULL;
    HRESULT hr = IGraphBuilder_AddSourceFilter(graph, path, L"Source", &source);
    if (SUCCEEDED(hr)) {
        output = dshow_find_pin(source, PINDIR_OUTPUT);
        hr = output != NULL ? S_OK : VFW_E_CANNOT_RENDER;
    }
    if (SUCCEEDED(hr)) {
        hr = dshow_query(graph, &IID_IFilterGraph2, (void **)&graph2);
    }
    if (SUCCEEDED(hr)) {
        hr = IFilterGraph2_RenderEx(graph2, output,
            AM_RENDEREX_RENDERTOEXISTINGRENDERERS, NULL);
    }
    if (graph2 != NULL) {
        IFilterGraph2_Release(graph2);
    }
    if (output != NULL) {
        IPin_Release(output);
    }
    if (source != NULL) {
        IBaseFilter_Release(source);
    }
    return hr;
}

static void dshow_read_video_info(DshowPlayer *player)
{
    SIZE size = { 0, 0 };
    SIZE aspect = { 0, 0 };
    VideoInfo *info = &player->base.info;
    if (FAILED(IMFVideoDisplayControl_GetNativeVideoSize(player->display,
            &size, &aspect))) {
        return;
    }
    if (size.cx > 0 && size.cy > 0) {
        info->width = (uint32_t)size.cx;
        info->height = (uint32_t)size.cy;
    }
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
        step = "the Enhanced Video Renderer is unavailable";
        hr = dshow_add_video_renderer(player);
    }
    if (SUCCEEDED(hr)) {
        if (!mute) {
            dshow_add_audio_renderer(player->graph);
        }
        step = "DirectShow could not render the file";
        hr = dshow_render(player->graph, path);
    }
    if (SUCCEEDED(hr) && !dshow_input_connected(player->video_renderer)) {
        step = "no video stream";
        hr = VFW_E_CANNOT_RENDER;
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
    if (FAILED(hr)) {
        dshow_reason(reason, reason_size, step, hr);
        dshow_release(player);
        HeapFree(GetProcessHeap(), 0, player);
        return hr;
    }
    dshow_read_video_info(player);
    LONGLONG duration = 0;
    if (SUCCEEDED(dshow_query(player->graph, &IID_IMediaSeeking,
            (void **)&player->seeking)) &&
        SUCCEEDED(IMediaSeeking_GetDuration(player->seeking, &duration))) {
        player->base.info.duration_seconds = (double)duration / 1e7;
    }
    player->base.state = VIDEO_STATE_READY;
    *out = &player->base;
    return S_OK;
}
