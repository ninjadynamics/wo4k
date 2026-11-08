/* DirectShow backend. Windows' own MPEG-I Stream Splitter and MPEG Video
 * Decoder play the game's original MPEG-1 video elementary streams, which
 * Media Foundation's MPEG source rejects.
 *
 * The graph renders only into renderers placed in it beforehand: the
 * Enhanced Video Renderer, windowless in the player window, and (unless
 * muted) a DirectSound renderer. DirectShow's default video renderer is
 * never used: it shows a child window with ShowWindow, which activates the
 * player and would take focus from the game.
 *
 * --frames: a sample grabber taking 32-bit RGB, then a null renderer, take
 * the video renderer's place. The null renderer holds each frame until its
 * time, so the grabber sees a frame when the one before it is due: that one
 * is published then (or by poll, when its time comes first), and the new
 * one is written to the free slot, waiting its turn. */
#include "video/video_backend.h"

#include <stddef.h>
#include <stdio.h>

#include <dshow.h>
#include <qedit.h>

#include "video/video_evr.h"
#include "video/video_frames.h"

/* qedit.dll: deprecated, still in every Windows since XP. */
static const GUID dshow_clsid_sample_grabber = {
    0xc1f400a0, 0x3f08, 0x11d3, { 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37 }
};
static const GUID dshow_clsid_null_renderer = {
    0xc1f400a4, 0x3f08, 0x11d3, { 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37 }
};
static const GUID dshow_iid_sample_grabber = {
    0x6b652fff, 0x11fe, 0x4fce, { 0x92, 0xad, 0x02, 0x66, 0xb5, 0xd7, 0xc7, 0x8f }
};
static const GUID dshow_iid_sample_grabber_cb = {
    0x0579154a, 0x2b53, 0x4994, { 0xb0, 0xd0, 0xe7, 0x73, 0x14, 0x8e, 0xff, 0x85 }
};

typedef struct DshowPlayer {
    VideoBackend base;
    HWND window;
    IGraphBuilder *graph;
    IMediaControl *control;
    IMediaEventEx *events;
    IMediaSeeking *seeking;
    IBaseFilter *video_renderer;
    IMFVideoDisplayControl *display;
    /* --frames */
    bool frames_mode;
    IBaseFilter *grabber_filter;
    ISampleGrabber *grabber;
    ISampleGrabberCB callback; /* lives as long as the player */
    CRITICAL_SECTION lock;     /* pending, and the slot written */
    bool lock_ready;
    VideoFrameSlots slots;
    uint32_t frame_width;
    uint32_t frame_height;
    LONG frame_pitch; /* negative: bottom-up rows */
    LONG pending;     /* a written slot not yet published, or -1 */
    LONGLONG pending_time;
    volatile LONG stopping;
    volatile LONG slots_failed;
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
    InterlockedExchange(&player->stopping, 1);
    if (player->control != NULL) {
        IMediaControl_Stop(player->control);
        IMediaControl_Release(player->control);
    }
    if (player->grabber != NULL) {
        /* Stopped: no streaming thread is in the callback. */
        ISampleGrabber_SetCallback(player->grabber, NULL, 0);
        ISampleGrabber_Release(player->grabber);
    }
    if (player->grabber_filter != NULL) {
        IBaseFilter_Release(player->grabber_filter);
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
    video_frame_slots_close(&player->slots);
    if (player->lock_ready) {
        DeleteCriticalSection(&player->lock);
    }
}

/* Publishes the frame waiting its turn, if any. Under the lock. */
static void dshow_publish_pending(DshowPlayer *player)
{
    if (player->pending >= 0) {
        video_frame_slots_publish(&player->slots, player->pending);
        player->pending = -1;
    }
}

static HRESULT STDMETHODCALLTYPE dshow_callback_query(ISampleGrabberCB *callback,
    REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &dshow_iid_sample_grabber_cb)) {
        *out = callback;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

/* The player owns the callback; the grabber lets go of it before the
 * player is freed (dshow_release). */
static ULONG STDMETHODCALLTYPE dshow_callback_add_ref(ISampleGrabberCB *callback)
{
    (void)callback;
    return 2;
}

static ULONG STDMETHODCALLTYPE dshow_callback_release(ISampleGrabberCB *callback)
{
    (void)callback;
    return 1;
}

/* On a streaming thread, as the null renderer lets the frame before go. */
static HRESULT STDMETHODCALLTYPE dshow_callback_sample(ISampleGrabberCB *callback,
    double time, IMediaSample *sample)
{
    DshowPlayer *player = (DshowPlayer *)((BYTE *)callback - offsetof(DshowPlayer, callback));
    BYTE *data = NULL;
    DWORD span = player->frame_pitch < 0 ? -(DWORD)player->frame_pitch :
        (DWORD)player->frame_pitch;
    if (player->stopping || player->slots_failed ||
        FAILED(IMediaSample_GetPointer(sample, &data)) ||
        (ULONGLONG)IMediaSample_GetActualDataLength(sample) <
            (ULONGLONG)span * player->frame_height) {
        return S_OK;
    }
    EnterCriticalSection(&player->lock);
    dshow_publish_pending(player);
    if (player->slots.view == NULL &&
        !video_frame_slots_create(&player->slots, player->frame_width, player->frame_height)) {
        InterlockedExchange(&player->slots_failed, 1);
    } else {
        const BYTE *top = player->frame_pitch < 0 ?
            data + (SIZE_T)span * (player->frame_height - 1) : data;
        player->pending_time = (LONGLONG)(time * 1e7 + 0.5);
        player->pending = video_frame_slots_write(&player->slots, top,
            player->frame_pitch, player->pending_time);
    }
    LeaveCriticalSection(&player->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dshow_callback_buffer(ISampleGrabberCB *callback,
    double time, BYTE *buffer, LONG length)
{
    (void)callback;
    (void)time;
    (void)buffer;
    (void)length;
    return E_NOTIMPL;
}

static ISampleGrabberCBVtbl dshow_callback_vtable = {
    dshow_callback_query, dshow_callback_add_ref, dshow_callback_release,
    dshow_callback_sample, dshow_callback_buffer
};

static bool dshow_position(VideoBackend *backend, LONGLONG *position_hns);

/* Before playing, the first frame goes at once; then each when due. */
static void dshow_poll_frames(DshowPlayer *player)
{
    VideoBackend *backend = &player->base;
    LONGLONG now = 0;
    if (player->slots_failed) {
        video_backend_fail(backend, E_OUTOFMEMORY,
            "the frames cannot be shared with the game");
        return;
    }
    EnterCriticalSection(&player->lock);
    if (player->pending >= 0 && (backend->state != VIDEO_STATE_PLAYING ||
            (dshow_position(backend, &now) && now >= player->pending_time))) {
        dshow_publish_pending(player);
    }
    LeaveCriticalSection(&player->lock);
    if (player->slots.published > 0) {
        backend->cued = true;
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
    if (player->frames_mode && backend->state != VIDEO_STATE_FAILED) {
        dshow_poll_frames(player);
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
    if (player->display != NULL) {
        video_evr_layout(player->display, client);
    }
}

static void dshow_repaint(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    if (player->display != NULL) {
        IMFVideoDisplayControl_RepaintVideo(player->display);
    }
}

static void dshow_destroy(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    dshow_release(player);
    HeapFree(GetProcessHeap(), 0, player);
}

/* A paused graph is cued: the first frame is presented, nothing plays.
 * --frames is cued once that frame is published (dshow_poll_frames). */
static void dshow_preroll(VideoBackend *backend)
{
    DshowPlayer *player = (DshowPlayer *)backend;
    if (backend->state != VIDEO_STATE_READY || backend->cued) {
        return;
    }
    if (SUCCEEDED(IMediaControl_Pause(player->control)) && !player->frames_mode) {
        backend->cued = true;
    }
}

static const VideoBackendOps dshow_ops = {
    dshow_poll, dshow_start, dshow_preroll, dshow_position, dshow_layout,
    dshow_repaint, dshow_destroy
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

/* --frames: the sample grabber, taking only 32-bit RGB, and the null
 * renderer that paces it. */
static HRESULT dshow_add_frames(DshowPlayer *player)
{
    AM_MEDIA_TYPE type;
    HRESULT hr = CoCreateInstance(&dshow_clsid_sample_grabber, NULL,
        CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&player->grabber_filter);
    if (SUCCEEDED(hr)) {
        hr = IGraphBuilder_AddFilter(player->graph, player->grabber_filter, L"Frames");
    }
    if (SUCCEEDED(hr)) {
        hr = IBaseFilter_QueryInterface(player->grabber_filter,
            &dshow_iid_sample_grabber, (void **)&player->grabber);
    }
    if (SUCCEEDED(hr)) {
        memset(&type, 0, sizeof(type));
        type.majortype = MEDIATYPE_Video;
        type.subtype = MEDIASUBTYPE_RGB32;
        type.formattype = FORMAT_VideoInfo;
        hr = ISampleGrabber_SetMediaType(player->grabber, &type);
    }
    if (SUCCEEDED(hr)) {
        hr = ISampleGrabber_SetBufferSamples(player->grabber, FALSE);
    }
    if (SUCCEEDED(hr)) {
        hr = ISampleGrabber_SetOneShot(player->grabber, FALSE);
    }
    if (SUCCEEDED(hr)) {
        hr = ISampleGrabber_SetCallback(player->grabber, &player->callback, 0);
    }
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(&dshow_clsid_null_renderer, NULL, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&player->video_renderer);
    }
    if (SUCCEEDED(hr)) {
        hr = IGraphBuilder_AddFilter(player->graph, player->video_renderer, L"Frame Clock");
    }
    return hr;
}

/* Connects output to the first input pin of filter, through whatever
 * splitter and decoders DirectShow finds. */
static HRESULT dshow_connect(IGraphBuilder *graph, IPin *output, IBaseFilter *filter)
{
    IPin *input = dshow_find_pin(filter, PINDIR_INPUT);
    if (input == NULL) {
        return VFW_E_CANNOT_CONNECT;
    }
    HRESULT hr = IGraphBuilder_Connect(graph, output, input);
    IPin_Release(input);
    return hr;
}

/* Every output still unconnected (the sound) goes to the renderers in the
 * graph; without an audio renderer it stays silent. */
static void dshow_render_rest(IGraphBuilder *graph)
{
    IEnumFilters *filters = NULL;
    IBaseFilter *filter = NULL;
    IPin *outputs[16];
    ULONG count = 0;
    if (FAILED(IGraphBuilder_EnumFilters(graph, &filters))) {
        return;
    }
    while (count < 16 && IEnumFilters_Next(filters, 1, &filter, NULL) == S_OK) {
        IEnumPins *pins = NULL;
        IPin *pin = NULL;
        if (SUCCEEDED(IBaseFilter_EnumPins(filter, &pins))) {
            while (count < 16 && IEnumPins_Next(pins, 1, &pin, NULL) == S_OK) {
                PIN_DIRECTION direction = PINDIR_INPUT;
                IPin *peer = NULL;
                if (SUCCEEDED(IPin_QueryDirection(pin, &direction)) &&
                    direction == PINDIR_OUTPUT && IPin_ConnectedTo(pin, &peer) != S_OK) {
                    outputs[count++] = pin;
                } else {
                    IPin_Release(pin);
                }
                if (peer != NULL) {
                    IPin_Release(peer);
                }
            }
            IEnumPins_Release(pins);
        }
        IBaseFilter_Release(filter);
    }
    IEnumFilters_Release(filters);
    IFilterGraph2 *graph2 = NULL;
    bool render = SUCCEEDED(dshow_query(graph, &IID_IFilterGraph2, (void **)&graph2));
    for (ULONG i = 0; i < count; ++i) {
        if (render) {
            IFilterGraph2_RenderEx(graph2, outputs[i],
                AM_RENDEREX_RENDERTOEXISTINGRENDERERS, NULL);
        }
        IPin_Release(outputs[i]);
    }
    if (graph2 != NULL) {
        IFilterGraph2_Release(graph2);
    }
}

/* source -> splitter -> decoder -> grabber -> null renderer, then sound. */
static HRESULT dshow_render_frames(DshowPlayer *player, const wchar_t *path)
{
    IBaseFilter *source = NULL;
    IPin *output = NULL;
    HRESULT hr = IGraphBuilder_AddSourceFilter(player->graph, path, L"Source", &source);
    if (SUCCEEDED(hr)) {
        output = dshow_find_pin(source, PINDIR_OUTPUT);
        hr = output != NULL ? S_OK : VFW_E_CANNOT_RENDER;
    }
    if (SUCCEEDED(hr)) {
        hr = dshow_connect(player->graph, output, player->grabber_filter);
    }
    if (output != NULL) {
        IPin_Release(output);
        output = NULL;
    }
    if (SUCCEEDED(hr)) {
        output = dshow_find_pin(player->grabber_filter, PINDIR_OUTPUT);
        hr = output != NULL ? dshow_connect(player->graph, output, player->video_renderer) :
            VFW_E_CANNOT_CONNECT;
    }
    if (output != NULL) {
        IPin_Release(output);
    }
    if (source != NULL) {
        IBaseFilter_Release(source);
    }
    if (SUCCEEDED(hr)) {
        dshow_render_rest(player->graph);
    }
    return hr;
}

/* The grabber's agreed frame: top-down when its height is negative. */
static HRESULT dshow_read_frame_format(DshowPlayer *player)
{
    AM_MEDIA_TYPE type;
    memset(&type, 0, sizeof(type));
    HRESULT hr = ISampleGrabber_GetConnectedMediaType(player->grabber, &type);
    if (FAILED(hr)) {
        return hr;
    }
    hr = VFW_E_INVALIDMEDIATYPE;
    if (IsEqualGUID(&type.formattype, &FORMAT_VideoInfo) && type.pbFormat != NULL &&
        type.cbFormat >= sizeof(VIDEOINFOHEADER)) {
        const BITMAPINFOHEADER *bitmap = &((const VIDEOINFOHEADER *)type.pbFormat)->bmiHeader;
        LONG height = bitmap->biHeight < 0 ? -bitmap->biHeight : bitmap->biHeight;
        if (bitmap->biBitCount == 32 && bitmap->biWidth > 0 && height > 0 &&
            bitmap->biWidth <= WO_VIDEO_FRAMES_MAX_WIDTH && height <= WO_VIDEO_FRAMES_MAX_HEIGHT) {
            player->frame_width = (uint32_t)bitmap->biWidth;
            player->frame_height = (uint32_t)height;
            LONG stride = bitmap->biWidth * 4;
            player->frame_pitch = bitmap->biHeight < 0 ? stride : -stride;
            player->base.info.width = player->frame_width;
            player->base.info.height = player->frame_height;
            hr = S_OK;
        }
    }
    if (type.cbFormat != 0) {
        CoTaskMemFree(type.pbFormat);
    }
    if (type.pUnk != NULL) {
        IUnknown_Release(type.pUnk);
    }
    return hr;
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
    bool frames, VideoBackend **out, char *reason, size_t reason_size)
{
    *out = NULL;
    DshowPlayer *player = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*player));
    if (player == NULL) {
        dshow_reason(reason, reason_size, "out of memory", E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    player->base.ops = &dshow_ops;
    player->base.name = frames ? "directshow_frames" : "directshow";
    player->base.state = VIDEO_STATE_OPENING;
    player->window = window;
    player->frames_mode = frames;
    player->pending = -1;
    player->callback.lpVtbl = &dshow_callback_vtable;
    InitializeCriticalSection(&player->lock);
    player->lock_ready = true;
    const char *step = "DirectShow is unavailable";
    HRESULT hr = CoCreateInstance(&CLSID_FilterGraph, NULL,
        CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&player->graph);
    if (SUCCEEDED(hr) && frames) {
        step = "the DirectShow sample grabber is unavailable";
        hr = dshow_add_frames(player);
    } else if (SUCCEEDED(hr)) {
        step = "the Enhanced Video Renderer is unavailable";
        hr = dshow_add_video_renderer(player);
    }
    if (SUCCEEDED(hr)) {
        if (!mute) {
            dshow_add_audio_renderer(player->graph);
        }
        step = "DirectShow could not render the file";
        hr = frames ? dshow_render_frames(player, path) : dshow_render(player->graph, path);
    }
    if (SUCCEEDED(hr) && frames) {
        step = "DirectShow could not decode the video to RGB";
        hr = dshow_read_frame_format(player);
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
    if (frames) {
        player->base.frames_published = &player->slots.published;
    } else {
        dshow_read_video_info(player);
    }
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
