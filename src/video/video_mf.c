/* Media Foundation backend: a media session with the Enhanced Video Renderer
 * drawing into the player window. The DLLs are loaded from System32 at run
 * time, so a Windows edition without Media Foundation still reaches the
 * DirectShow fallback instead of failing to start. */
#include "video/video_backend.h"

#include <stdio.h>
#include <string.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>

#include "video/video_evr.h"
#include "video/video_frames.h"

typedef HRESULT (WINAPI *MfStartupFn)(ULONG version, DWORD flags);
typedef HRESULT (WINAPI *MfShutdownFn)(void);
typedef HRESULT (WINAPI *MfCreateSourceResolverFn)(IMFSourceResolver **resolver);
typedef HRESULT (WINAPI *MfCreateMediaSessionFn)(IMFAttributes *configuration,
    IMFMediaSession **session);
typedef HRESULT (WINAPI *MfCreateTopologyFn)(IMFTopology **topology);
typedef HRESULT (WINAPI *MfCreateTopologyNodeFn)(MF_TOPOLOGY_TYPE type,
    IMFTopologyNode **node);
typedef HRESULT (WINAPI *MfCreateVideoRendererFn)(HWND window,
    IMFActivate **activate);
typedef HRESULT (WINAPI *MfCreateAudioRendererFn)(IMFActivate **activate);
typedef HRESULT (WINAPI *MfGetServiceFn)(IUnknown *object, REFGUID service,
    REFIID riid, LPVOID *result);

static struct {
    bool attempted;
    bool started;
    HMODULE mfplat;
    HMODULE mf;
    MfStartupFn startup;
    MfShutdownFn shutdown;
    MfCreateSourceResolverFn create_source_resolver;
    MfCreateMediaSessionFn create_media_session;
    MfCreateTopologyFn create_topology;
    MfCreateTopologyNodeFn create_topology_node;
    MfCreateVideoRendererFn create_video_renderer;
    MfCreateAudioRendererFn create_audio_renderer;
    MfGetServiceFn get_service;
} mf_api;

typedef struct MfPlayer {
    VideoBackend base;
    HWND window;
    IMFMediaSource *source;
    IMFMediaSession *session;
    IMFVideoDisplayControl *display;
    IMFPresentationClock *clock;
    IMFSimpleAudioVolume *volume; /* muted while prerolling */
    /* --frames: a source reader hands frames to the game; the session, if
     * any, plays only the sound and is the clock. Without sound there is
     * no session and the clock counts from start(). */
    bool frames_mode;
    VideoFrames *frames;
    LONGLONG started_qpc;
    LONGLONG qpc_frequency;
    bool start_pending; /* start() came before the first frame */
    bool has_audio;
    /* Preroll: a muted Start, then Pause as soon as it has started. */
    bool prerolling;
    bool play_requested; /* start() arrived while prerolling */
    bool closed;
    bool destroying;
} MfPlayer;

/* GetProcAddress returns FARPROC; void (*)(void) converts to any function
 * pointer type without a cast-function-type warning. */
typedef void (*MfAnyFunction)(void);

static MfAnyFunction mf_symbol(HMODULE module, const char *name)
{
    if (module == NULL) {
        return NULL;
    }
    return (MfAnyFunction)GetProcAddress(module, name);
}

static bool mf_load(void)
{
    if (mf_api.attempted) {
        return mf_api.started;
    }
    mf_api.attempted = true;
    mf_api.mfplat = LoadLibraryExW(L"mfplat.dll", NULL,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    mf_api.mf = LoadLibraryExW(L"mf.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    mf_api.startup = (MfStartupFn)mf_symbol(mf_api.mfplat, "MFStartup");
    mf_api.shutdown = (MfShutdownFn)mf_symbol(mf_api.mfplat, "MFShutdown");
    mf_api.create_source_resolver = (MfCreateSourceResolverFn)mf_symbol(
        mf_api.mfplat, "MFCreateSourceResolver");
    mf_api.create_media_session = (MfCreateMediaSessionFn)mf_symbol(
        mf_api.mf, "MFCreateMediaSession");
    mf_api.create_topology = (MfCreateTopologyFn)mf_symbol(
        mf_api.mf, "MFCreateTopology");
    mf_api.create_topology_node = (MfCreateTopologyNodeFn)mf_symbol(
        mf_api.mf, "MFCreateTopologyNode");
    mf_api.create_video_renderer = (MfCreateVideoRendererFn)mf_symbol(
        mf_api.mf, "MFCreateVideoRendererActivate");
    mf_api.create_audio_renderer = (MfCreateAudioRendererFn)mf_symbol(
        mf_api.mf, "MFCreateAudioRendererActivate");
    mf_api.get_service = (MfGetServiceFn)mf_symbol(mf_api.mf, "MFGetService");
    bool complete = mf_api.startup != NULL && mf_api.shutdown != NULL &&
        mf_api.create_source_resolver != NULL &&
        mf_api.create_media_session != NULL &&
        mf_api.create_topology != NULL &&
        mf_api.create_topology_node != NULL &&
        mf_api.create_video_renderer != NULL &&
        mf_api.create_audio_renderer != NULL && mf_api.get_service != NULL;
    if (complete && SUCCEEDED(mf_api.startup(MF_VERSION, MFSTARTUP_LITE))) {
        mf_api.started = true;
    }
    return mf_api.started;
}

void video_mf_unload(void)
{
    if (mf_api.started) {
        mf_api.shutdown();
    }
    if (mf_api.mf != NULL) {
        FreeLibrary(mf_api.mf);
    }
    if (mf_api.mfplat != NULL) {
        FreeLibrary(mf_api.mfplat);
    }
    memset(&mf_api, 0, sizeof(mf_api));
}

static void mf_reason(char *reason, size_t size, const char *what, HRESULT hr)
{
    int written = snprintf(reason, size, "%s (0x%08lX)", what, (unsigned long)hr);
    if (written < 0 && size > 0) {
        reason[0] = '\0';
    }
}

/* Frame size from the stream's native media type. */
static void mf_read_video_info(IMFMediaTypeHandler *handler, VideoInfo *info)
{
    IMFMediaType *type = NULL;
    UINT64 packed = 0;
    if (FAILED(IMFMediaTypeHandler_GetCurrentMediaType(handler, &type))) {
        return;
    }
    if (SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &packed))) {
        info->width = (uint32_t)(packed >> 32);
        info->height = (uint32_t)(packed & 0xffffffffu);
    }
    IMFMediaType_Release(type);
}

/* source stream -> renderer, one topology branch per selected stream. */
static HRESULT mf_add_branch(IMFTopology *topology, IMFMediaSource *source,
    IMFPresentationDescriptor *presentation, IMFStreamDescriptor *stream,
    IMFActivate *renderer)
{
    IMFTopologyNode *input = NULL;
    IMFTopologyNode *output = NULL;
    HRESULT hr = mf_api.create_topology_node(MF_TOPOLOGY_SOURCESTREAM_NODE,
        &input);
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_SetUnknown(input, &MF_TOPONODE_SOURCE,
            (IUnknown *)source);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_SetUnknown(input,
            &MF_TOPONODE_PRESENTATION_DESCRIPTOR, (IUnknown *)presentation);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_SetUnknown(input, &MF_TOPONODE_STREAM_DESCRIPTOR,
            (IUnknown *)stream);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopology_AddNode(topology, input);
    }
    if (SUCCEEDED(hr)) {
        hr = mf_api.create_topology_node(MF_TOPOLOGY_OUTPUT_NODE, &output);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_SetObject(output, (IUnknown *)renderer);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_SetUINT32(output, &MF_TOPONODE_STREAMID, 0);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_SetUINT32(output,
            &MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopology_AddNode(topology, output);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFTopologyNode_ConnectOutput(input, 0, output, 0);
    }
    if (output != NULL) {
        IMFTopologyNode_Release(output);
    }
    if (input != NULL) {
        IMFTopologyNode_Release(input);
    }
    return hr;
}

typedef struct MfStreams {
    bool video;
    bool audio;
} MfStreams;

/* Selects the first video stream (and, unless muted, the first audio
 * stream) and deselects everything else. */
static HRESULT mf_build_topology(MfPlayer *player,
    IMFPresentationDescriptor *presentation, bool mute,
    IMFTopology *topology, MfStreams *streams)
{
    DWORD count = 0;
    HRESULT hr = IMFPresentationDescriptor_GetStreamDescriptorCount(
        presentation, &count);
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; ++i) {
        BOOL selected = FALSE;
        IMFStreamDescriptor *stream = NULL;
        IMFMediaTypeHandler *handler = NULL;
        IMFActivate *renderer = NULL;
        GUID major = GUID_NULL;
        hr = IMFPresentationDescriptor_GetStreamDescriptorByIndex(presentation,
            i, &selected, &stream);
        if (SUCCEEDED(hr)) {
            hr = IMFStreamDescriptor_GetMediaTypeHandler(stream, &handler);
        }
        if (SUCCEEDED(hr)) {
            hr = IMFMediaTypeHandler_GetMajorType(handler, &major);
        }
        if (SUCCEEDED(hr)) {
            if (!streams->video && IsEqualGUID(&major, &MFMediaType_Video)) {
                mf_read_video_info(handler, &player->base.info);
                /* --frames leaves the video stream to the reader. */
                hr = player->frames_mode ? S_OK :
                    mf_api.create_video_renderer(player->window, &renderer);
                streams->video = SUCCEEDED(hr);
            } else if (!mute && !streams->audio &&
                IsEqualGUID(&major, &MFMediaType_Audio)) {
                hr = mf_api.create_audio_renderer(&renderer);
                streams->audio = SUCCEEDED(hr);
            }
        }
        if (SUCCEEDED(hr) && renderer != NULL) {
            hr = IMFPresentationDescriptor_SelectStream(presentation, i);
            if (SUCCEEDED(hr)) {
                hr = mf_add_branch(topology, player->source, presentation,
                    stream, renderer);
            }
        } else if (SUCCEEDED(hr)) {
            hr = IMFPresentationDescriptor_DeselectStream(presentation, i);
        }
        if (renderer != NULL) {
            IMFActivate_Release(renderer);
        }
        if (handler != NULL) {
            IMFMediaTypeHandler_Release(handler);
        }
        if (stream != NULL) {
            IMFStreamDescriptor_Release(stream);
        }
    }
    return hr;
}

static HRESULT mf_open_source(const wchar_t *path, IMFMediaSource **source)
{
    IMFSourceResolver *resolver = NULL;
    IUnknown *object = NULL;
    MF_OBJECT_TYPE type = MF_OBJECT_INVALID;
    HRESULT hr = mf_api.create_source_resolver(&resolver);
    if (SUCCEEDED(hr)) {
        hr = IMFSourceResolver_CreateObjectFromURL(resolver, path,
            MF_RESOLUTION_MEDIASOURCE, NULL, &type, &object);
    }
    if (SUCCEEDED(hr)) {
        hr = IUnknown_QueryInterface(object, &IID_IMFMediaSource,
            (void **)source);
    }
    if (object != NULL) {
        IUnknown_Release(object);
    }
    if (resolver != NULL) {
        IMFSourceResolver_Release(resolver);
    }
    return hr;
}

static void mf_release(MfPlayer *player)
{
    /* First: its worker reads the session clock. */
    if (player->frames != NULL) {
        video_frames_close(player->frames);
        player->frames = NULL;
    }
    if (player->volume != NULL) {
        IMFSimpleAudioVolume_Release(player->volume);
        player->volume = NULL;
    }
    if (player->clock != NULL) {
        IMFPresentationClock_Release(player->clock);
        player->clock = NULL;
    }
    if (player->display != NULL) {
        IMFVideoDisplayControl_Release(player->display);
        player->display = NULL;
    }
    if (player->source != NULL) {
        IMFMediaSource_Shutdown(player->source);
        IMFMediaSource_Release(player->source);
        player->source = NULL;
    }
    if (player->session != NULL) {
        IMFMediaSession_Shutdown(player->session);
        IMFMediaSession_Release(player->session);
        player->session = NULL;
    }
}

static void mf_topology_ready(MfPlayer *player)
{
    if (player->frames_mode) {
        if (player->base.state == VIDEO_STATE_OPENING) {
            player->base.state = VIDEO_STATE_READY;
        }
        return;
    }
    HRESULT hr = mf_api.get_service((IUnknown *)player->session,
        &MR_VIDEO_RENDER_SERVICE, &video_iid_display_control,
        (void **)&player->display);
    if (FAILED(hr)) {
        video_backend_fail(&player->base, hr,
            "the video renderer has no display control");
        return;
    }
    video_evr_prepare(player->display);
    if (player->base.state == VIDEO_STATE_OPENING) {
        player->base.state = VIDEO_STATE_READY;
    }
}

static void mf_unmute(MfPlayer *player)
{
    if (player->volume != NULL) {
        IMFSimpleAudioVolume_SetMute(player->volume, FALSE);
    }
}

static void mf_take_clock(MfPlayer *player)
{
    IMFClock *clock = NULL;
    if (player->clock == NULL &&
        SUCCEEDED(IMFMediaSession_GetClock(player->session, &clock))) {
        IMFClock_QueryInterface(clock, &IID_IMFPresentationClock,
            (void **)&player->clock);
        IMFClock_Release(clock);
    }
}

/* Start, or resume a cued presentation from the frame it holds. */
static void mf_resume(MfPlayer *player)
{
    mf_unmute(player);
    PROPVARIANT start;
    PropVariantInit(&start);
    HRESULT hr = IMFMediaSession_Start(player->session, &GUID_NULL, &start);
    if (FAILED(hr)) {
        video_backend_fail(&player->base, hr,
            "Media Foundation could not start playback");
        return;
    }
    mf_take_clock(player);
}

static void mf_handle_event(MfPlayer *player, IMFMediaEvent *event)
{
    MediaEventType type = MEUnknown;
    HRESULT status = S_OK;
    UINT32 topology_status = 0;
    IMFMediaEvent_GetType(event, &type);
    IMFMediaEvent_GetStatus(event, &status);
    if (type == MESessionClosed) {
        player->closed = true;
        return;
    }
    if (player->destroying) {
        return;
    }
    if (FAILED(status)) {
        video_backend_fail(&player->base, status,
            type == MESessionTopologySet || type == MESessionTopologyStatus ?
            "Media Foundation could not build a playback topology" :
            "Media Foundation playback failed");
        return;
    }
    switch (type) {
    case MESessionTopologyStatus:
        if (SUCCEEDED(IMFMediaEvent_GetUINT32(event, &MF_EVENT_TOPOLOGY_STATUS,
                &topology_status)) && topology_status == MF_TOPOSTATUS_READY) {
            mf_topology_ready(player);
        }
        break;
    case MESessionStarted:
        if (player->prerolling && !player->play_requested) {
            /* The first frame is on its way; hold the presentation there. */
            HRESULT hr = IMFMediaSession_Pause(player->session);
            if (FAILED(hr)) {
                video_backend_fail(&player->base, hr,
                    "Media Foundation could not hold the first frame");
            }
            break;
        }
        if (player->prerolling) {
            player->prerolling = false;
            mf_unmute(player);
        }
        if (player->base.state == VIDEO_STATE_READY) {
            player->base.state = VIDEO_STATE_PLAYING;
        }
        break;
    case MESessionPaused:
        if (player->prerolling) {
            player->prerolling = false;
            player->base.cued = true;
            if (player->play_requested) {
                mf_resume(player);
            }
        }
        break;
    case MESessionEnded:
        if (player->base.state != VIDEO_STATE_FAILED) {
            player->base.state = VIDEO_STATE_ENDED;
        }
        break;
    case MEError:
        video_backend_fail(&player->base, E_FAIL,
            "Media Foundation playback failed");
        break;
    default:
        break;
    }
}

static void mf_start(VideoBackend *backend);

static void mf_poll(VideoBackend *backend)
{
    MfPlayer *player = (MfPlayer *)backend;
    if (player->frames != NULL) {
        if (video_frames_cued(player->frames)) {
            backend->cued = true;
            if (player->start_pending) {
                player->start_pending = false;
                mf_start(backend);
            }
        }
        if (player->session == NULL && backend->state == VIDEO_STATE_PLAYING &&
            video_frames_ended(player->frames)) {
            backend->state = VIDEO_STATE_ENDED;
        }
    }
    if (player->session == NULL) {
        return;
    }
    for (;;) {
        IMFMediaEvent *event = NULL;
        HRESULT hr = IMFMediaSession_GetEvent(player->session,
            MF_EVENT_FLAG_NO_WAIT, &event);
        if (hr == MF_E_NO_EVENTS_AVAILABLE) {
            break;
        }
        if (FAILED(hr)) {
            if (!player->destroying) {
                video_backend_fail(backend, hr,
                    "the Media Foundation event queue failed");
            }
            player->closed = true;
            break;
        }
        mf_handle_event(player, event);
        IMFMediaEvent_Release(event);
    }
}

static void mf_start(VideoBackend *backend)
{
    MfPlayer *player = (MfPlayer *)backend;
    /* --frames: the clock starts with the first frame, not before it, as the
     * decoder can take a while to start where the movie was not prepared. */
    if (player->frames != NULL && !video_frames_cued(player->frames)) {
        player->start_pending = true;
        return;
    }
    if (player->session == NULL) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        player->started_qpc = now.QuadPart;
        if (backend->state == VIDEO_STATE_READY) {
            backend->state = VIDEO_STATE_PLAYING;
        }
        return;
    }
    if (player->prerolling) {
        /* Played as soon as the preroll's Start or Pause completes. */
        player->play_requested = true;
        return;
    }
    mf_resume(player);
}

/* A muted Start, paused again on MESessionStarted: decoders, renderers and
 * the audio device are then running and the first frame is presented, so
 * the real start is immediate. Never prerolls where it could be heard. */
static void mf_preroll(VideoBackend *backend)
{
    MfPlayer *player = (MfPlayer *)backend;
    if (player->session == NULL) {
        return; /* --frames without sound: the reader cues itself */
    }
    if (backend->state != VIDEO_STATE_READY || player->prerolling ||
        backend->cued || player->clock != NULL) {
        return;
    }
    if (player->has_audio) {
        if (player->volume == NULL &&
            FAILED(mf_api.get_service((IUnknown *)player->session,
                &MR_POLICY_VOLUME_SERVICE, &IID_IMFSimpleAudioVolume,
                (void **)&player->volume))) {
            player->volume = NULL;
            return;
        }
        if (FAILED(IMFSimpleAudioVolume_SetMute(player->volume, TRUE))) {
            return;
        }
    }
    PROPVARIANT start;
    PropVariantInit(&start);
    if (FAILED(IMFMediaSession_Start(player->session, &GUID_NULL, &start))) {
        mf_unmute(player);
        return;
    }
    player->prerolling = true;
    mf_take_clock(player);
}

static bool mf_position(VideoBackend *backend, LONGLONG *position_hns)
{
    MfPlayer *player = (MfPlayer *)backend;
    MFTIME time = 0;
    if (player->session == NULL && player->frames != NULL) {
        LARGE_INTEGER now;
        if (backend->state != VIDEO_STATE_PLAYING || !QueryPerformanceCounter(&now)) {
            return false;
        }
        *position_hns = (now.QuadPart - player->started_qpc) * 10000000 /
            player->qpc_frequency;
        return true;
    }
    if (player->clock == NULL ||
        FAILED(IMFPresentationClock_GetTime(player->clock, &time))) {
        return false;
    }
    *position_hns = time;
    return true;
}

static void mf_layout(VideoBackend *backend, const RECT *client)
{
    MfPlayer *player = (MfPlayer *)backend;
    if (player->display != NULL) {
        video_evr_layout(player->display, client);
    }
}

static void mf_repaint(VideoBackend *backend)
{
    MfPlayer *player = (MfPlayer *)backend;
    if (player->display != NULL && !player->destroying) {
        IMFVideoDisplayControl_RepaintVideo(player->display);
    }
}

/* Close is asynchronous. Keep the window responsive while the session
 * finishes, but never wait more than two seconds. */
static void mf_destroy(VideoBackend *backend)
{
    MfPlayer *player = (MfPlayer *)backend;
    player->destroying = true;
    if (player->session != NULL &&
        SUCCEEDED(IMFMediaSession_Close(player->session))) {
        ULONGLONG deadline = GetTickCount64() + 2000;
        while (!player->closed && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
            MSG message;
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            mf_poll(backend);
        }
    }
    mf_release(player);
    HeapFree(GetProcessHeap(), 0, player);
}

/* The worker thread's clock for --frames: false until playing. */
static bool mf_frames_clock(void *context, LONGLONG *time_hns)
{
    MfPlayer *player = context;
    return player->base.state == VIDEO_STATE_PLAYING &&
        mf_position(&player->base, time_hns);
}

static const VideoBackendOps mf_ops = {
    mf_poll, mf_start, mf_preroll, mf_position, mf_layout, mf_repaint,
    mf_destroy
};

HRESULT video_mf_create(HWND window, const wchar_t *path, bool mute,
    bool frames, VideoBackend **out, char *reason, size_t reason_size)
{
    IMFPresentationDescriptor *presentation = NULL;
    IMFTopology *topology = NULL;
    MfStreams streams = { false, false };
    UINT64 duration = 0;
    *out = NULL;
    if (!mf_load()) {
        mf_reason(reason, reason_size, "Media Foundation is unavailable",
            HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }
    MfPlayer *player = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*player));
    if (player == NULL) {
        mf_reason(reason, reason_size, "out of memory", E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    player->base.ops = &mf_ops;
    player->base.name = frames ? "media_foundation_frames" : "media_foundation";
    player->frames_mode = frames;
    player->base.state = VIDEO_STATE_OPENING;
    player->window = window;
    const char *step = "Media Foundation could not open the file";
    HRESULT hr = S_OK;
    if (frames) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        player->qpc_frequency = frequency.QuadPart;
        player->frames = video_frames_open(path, &player->base.info, &hr, &step);
    }
    if (SUCCEEDED(hr)) {
        step = "Media Foundation could not open the file";
        hr = mf_open_source(path, &player->source);
    }
    if (SUCCEEDED(hr)) {
        step = "Media Foundation could not read the presentation";
        hr = IMFMediaSource_CreatePresentationDescriptor(player->source,
            &presentation);
    }
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(IMFPresentationDescriptor_GetUINT64(presentation,
                &MF_PD_DURATION, &duration))) {
            player->base.info.duration_seconds = (double)duration / 1e7;
        }
        step = "Media Foundation could not create a topology";
        hr = mf_api.create_topology(&topology);
    }
    if (SUCCEEDED(hr)) {
        hr = mf_build_topology(player, presentation, mute, topology, &streams);
    }
    if (SUCCEEDED(hr) && !streams.video) {
        step = "no video stream";
        hr = MF_E_INVALIDMEDIATYPE;
    }
    player->has_audio = streams.audio;
    /* --frames without sound needs no session at all. */
    bool session = !frames || streams.audio;
    if (SUCCEEDED(hr) && session) {
        step = "Media Foundation could not create a media session";
        hr = mf_api.create_media_session(NULL, &player->session);
    }
    if (SUCCEEDED(hr) && session) {
        step = "Media Foundation could not set the topology";
        hr = IMFMediaSession_SetTopology(player->session, 0, topology);
    }
    if (SUCCEEDED(hr) && frames) {
        if (!session) {
            player->base.state = VIDEO_STATE_READY;
        }
        step = "the frame worker could not start";
        hr = video_frames_run(player->frames, mf_frames_clock, player) ? S_OK :
            HRESULT_FROM_WIN32(GetLastError());
    }
    if (topology != NULL) {
        IMFTopology_Release(topology);
    }
    if (presentation != NULL) {
        IMFPresentationDescriptor_Release(presentation);
    }
    if (FAILED(hr)) {
        mf_reason(reason, reason_size, step, hr);
        mf_release(player);
        HeapFree(GetProcessHeap(), 0, player);
        return hr;
    }
    if (player->frames != NULL) {
        player->base.frames_published = video_frames_counter(player->frames);
    }
    *out = &player->base;
    return S_OK;
}
