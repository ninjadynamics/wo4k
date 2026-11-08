/* wo4k-video.exe: plays one of the game's movies over the game window.
 *
 *   wo4k-video.exe --game-root <dir> --movie <intro|xtro1|xtro2|xtro3|xtro4>
 *       [--game-pid N] [--owner-window HWND] [--skip-event HANDLE]
 *       [--start-event HANDLE [--stage-event HANDLE]] [--probe]
 *       [--original-only] [--frames]
 *
 *   --game-root      the game folder. Movies come from its movies folder;
 *                    breadcrumbs go to <parent>\logs\video-<movie>.log.
 *   --movie          which movie. intro tries intro.mp4, WXLINTR.mp4, then
 *                    WXLINTR.MPG; xtroN tries XTRON.mp4, then XTRON.MPG.
 *   --game-pid       the game process. Its controller block skips on a new
 *                    button press, and the player stops if the game exits.
 *   --owner-window   the game window (decimal). The player covers that
 *                    window's monitor and is owned by it.
 *   --skip-event     an inherited event handle (decimal). The game signals
 *                    it on Esc, Enter or Space.
 *   --start-event    an inherited event handle (decimal). The player opens
 *                    the movie in a hidden window and waits; when the game
 *                    signals the event it plays, showing the window once
 *                    playback runs. A skip event signalled while waiting
 *                    ends the player without showing anything.
 *   --stage-event    an inherited event handle (decimal), with
 *                    --start-event. The game signals it a few frames before
 *                    the start; the waiting player then puts its window on
 *                    screen fully transparent, so the compositor takes over
 *                    from the full-screen game while the game still presents
 *                    and the movie appears the moment it starts.
 *   --probe          automated tests: a muted 640x480 window that exits 0
 *                    once playback has advanced 250 ms.
 *   --original-only  only the original MPEG-1 file.
 *   --frames         no window: each frame goes, at its presentation time,
 *                    to the game through shared memory
 *                    (common/wo_video_frames.h), and the game presents it
 *                    itself. Exclusive fullscreen shows no other window.
 *
 * Exit codes: 0 finished, skipped or probe passed; 1 playback failed (or the
 * probe did not pass); 2 invalid command line; 3 no movie file found.
 *
 * Media Foundation plays MP4 replacements. The original MPEG-1 video
 * elementary streams are rejected by its MPEG source, so they fall back to
 * DirectShow, with --frames too. A failing MP4 falls back to the original
 * MPG when present; with --frames only if it gave the game no frame, as the
 * frame mapping is created with the first. */
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#include "video/video_backend.h"
#include "video/video_core.h"
#include "video/video_input.h"
#include "video/video_log.h"
#include "video/video_window.h"

typedef struct VideoApp {
    HINSTANCE instance;
    VideoOptions options;
    wchar_t game_root[VIDEO_PATH_CAPACITY];
    wchar_t mod_root[VIDEO_PATH_CAPACITY];
    HWND owner;
    VideoLog log;
    VideoInput input;
    VideoWindow window;
    bool window_ready;
    /* Prepared mode (--start-event): the movie is opened in a hidden window
     * ahead of time; playback starts, and the window shows, once the game
     * signals this event. */
    HANDLE start_event;
    bool start_signalled;
    /* Optional: signalled shortly before the start, while the game still
     * presents frames, to stage the window (see video_window_stage). */
    HANDLE stage_event;
    /* Times the window followed the game in or out of the front (logged up
     * to VIDEO_FOCUS_MOVES_LOGGED). */
    unsigned focus_moves;
} VideoApp;

#define VIDEO_FOCUS_MOVES_LOGGED 32

typedef struct VideoAttempt {
    bool opened;
    bool ready;        /* prepared mode: the file is open, waiting to start */
    bool cued;         /* prepared mode: the first frame is held */
    bool playing;      /* the backend reported that playback started */
    bool clock_moving; /* the presentation clock passed zero: frames show */
    ULONGLONG start_requested; /* GetTickCount64() when playback was asked for */
    bool failed;
    bool window_lost;
    LONG frames; /* --frames: frames handed to the game */
    char result[256];
    double elapsed_seconds;
} VideoAttempt;

/* Opening a file and closing a graph are synchronous calls. If one never
 * returns, the game would wait for this process forever, so a watchdog ends
 * it once the playback loop has made no progress for this long. */
#define VIDEO_HANG_MS 30000
/* A decoder normally shows the first frame well within a second of the
 * start; one that hangs (some 10-bit H.264) fails here, so the original
 * MPEG can play instead. */
#define VIDEO_START_MS 15000

static volatile LONG64 video_heartbeat;

static void video_beat(void)
{
    InterlockedExchange64(&video_heartbeat, (LONG64)GetTickCount64());
}

static ULONGLONG video_since_beat(void)
{
    LONG64 last = InterlockedCompareExchange64(&video_heartbeat, 0, 0);
    return GetTickCount64() - (ULONGLONG)last;
}

static DWORD WINAPI video_watchdog(void *parameter)
{
    VideoLog *log = parameter;
    while (video_since_beat() <= VIDEO_HANG_MS) {
        Sleep(500);
    }
    video_log_text(log, "video_failed", "The player stopped responding.");
    TerminateProcess(GetCurrentProcess(), VIDEO_EXIT_FAILED);
    return VIDEO_EXIT_FAILED;
}

static void video_watchdog_start(VideoLog *log)
{
    video_beat();
    HANDLE thread = CreateThread(NULL, 0, video_watchdog, log, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    }
}

static void video_set_text(char *out, size_t size, const char *text)
{
    int written = snprintf(out, size, "%s", text);
    if (written < 0 && size > 0) {
        out[0] = '\0';
    }
}

static bool video_resolve_roots(VideoApp *app)
{
    DWORD length = GetFullPathNameW(app->options.game_root,
        VIDEO_PATH_CAPACITY, app->game_root, NULL);
    if (length == 0 || length >= VIDEO_PATH_CAPACITY) {
        return false;
    }
    video_trim_separators(app->game_root);
    if (!video_copy(app->mod_root, VIDEO_PATH_CAPACITY, app->game_root)) {
        return false;
    }
    return video_parent(app->mod_root);
}

/* Only a live window of the game process may own the player. */
static HWND video_valid_owner(const VideoOptions *options)
{
    HWND owner = options->owner_window;
    if (owner == NULL || !IsWindow(owner)) {
        return NULL;
    }
    if (options->game_pid != 0) {
        DWORD pid = 0;
        GetWindowThreadProcessId(owner, &pid);
        if (pid != options->game_pid) {
            return NULL;
        }
    }
    return owner;
}

static void video_log_requested(VideoApp *app, const wchar_t *path,
    bool original_only)
{
    char storage[1024];
    JsonBuffer json;
    json_init(&json, storage, sizeof(storage));
    json_object_begin(&json);
    json_member_wide(&json, "file", path[0] != L'\0' ? video_file_name(path) :
        NULL);
    json_member_bool(&json, "original_only", original_only);
    json_member_bool(&json, "probe", app->options.probe);
    json_member_bool(&json, "frames", app->options.frames);
    json_member_bool(&json, "owner_window", app->owner != NULL);
    json_object_end(&json);
    /* A fresh request starts a fresh log; the original-file retry appends. */
    video_log_event(&app->log, "video_requested",
        json_ok(&json) ? json.data : NULL, !original_only);
}

static void video_log_opened(VideoApp *app, const VideoBackend *backend)
{
    char storage[512];
    JsonBuffer json;
    json_init(&json, storage, sizeof(storage));
    json_object_begin(&json);
    json_member_uint(&json, "width", backend->info.width);
    json_member_uint(&json, "height", backend->info.height);
    json_member_fixed(&json, "duration_seconds",
        backend->info.duration_seconds, 3);
    json_member_string(&json, "backend", backend->name);
    json_object_end(&json);
    video_log_event(&app->log, "video_opened",
        json_ok(&json) ? json.data : NULL, false);
}

static void video_log_closed(VideoApp *app, const VideoAttempt *attempt)
{
    char storage[1024];
    JsonBuffer json;
    json_init(&json, storage, sizeof(storage));
    json_object_begin(&json);
    json_member_bool(&json, "opened", attempt->opened);
    json_member_bool(&json, "failed", attempt->failed);
    json_member_string(&json, "result", attempt->result);
    json_member_bool(&json, "game_focus_lost", app->input.focus_lost);
    json_member_bool(&json, "player_activated", app->window.activated);
    json_member_fixed(&json, "elapsed_seconds", attempt->elapsed_seconds, 2);
    if (app->options.frames) {
        json_member_uint(&json, "frames", (uint64_t)attempt->frames);
    }
    json_object_end(&json);
    video_log_event(&app->log, "video_closed",
        json_ok(&json) ? json.data : NULL, false);
}

static void video_log_fallback(VideoApp *app, const char *reason)
{
    char storage[512];
    JsonBuffer json;
    json_init(&json, storage, sizeof(storage));
    json_object_begin(&json);
    json_member_string(&json, "reason", reason);
    json_object_end(&json);
    video_log_event(&app->log, "video_directshow_fallback",
        json_ok(&json) ? json.data : NULL, false);
}

/* Keeps the window over the game while it is in front and behind any other
 * program that is (see video_window_follow_focus). */
static void video_follow_focus(VideoApp *app)
{
    VideoWindowMove move = video_window_follow_focus(&app->window);
    if (move == VIDEO_WINDOW_STAYED ||
        ++app->focus_moves > VIDEO_FOCUS_MOVES_LOGGED) {
        return;
    }
    video_log_event(&app->log, move == VIDEO_WINDOW_RAISED ?
        "video_window_raised" : "video_window_behind", NULL, false);
}

static bool video_signalled(HANDLE handle);

/* Sleeps until input, a window message, the skip or start event or the
 * game's exit, at most 15 ms, then dispatches the window's messages and
 * follows the game in and out of the front. */
static void video_pump(VideoApp *app)
{
    HANDLE handles[4];
    DWORD count = video_input_handles(&app->input, handles);
    if (app->start_event != NULL && !app->start_signalled) {
        handles[count] = app->start_event;
        ++count;
        /* Manual-reset: once signalled it would end every wait at once
         * while staging waits for the first frame. */
        if (app->stage_event != NULL && !app->window.staged &&
            !video_signalled(app->stage_event)) {
            handles[count] = app->stage_event;
            ++count;
        }
    }
    video_beat();
    MsgWaitForMultipleObjectsEx(count, handles, 15, QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    video_follow_focus(app);
}

static VideoBackend *video_open_directshow(VideoApp *app, const wchar_t *path,
    const char *media_foundation_reason, char *reason, size_t reason_size)
{
    VideoBackend *backend = NULL;
    char directshow_reason[192];
    video_log_fallback(app, media_foundation_reason);
    HRESULT hr = video_dshow_create(app->window.hwnd, path,
        app->options.probe, app->options.frames, &backend, directshow_reason,
        sizeof(directshow_reason));
    if (FAILED(hr)) {
        int written = snprintf(reason, reason_size, "%s; %s",
            media_foundation_reason, directshow_reason);
        if (written < 0 && reason_size > 0) {
            reason[0] = '\0';
        }
        return NULL;
    }
    return backend;
}

static void video_fail(VideoApp *app, VideoAttempt *attempt, const char *reason,
    bool log_failure)
{
    attempt->failed = true;
    video_set_text(attempt->result, sizeof(attempt->result), reason);
    if (log_failure) {
        video_log_text(&app->log, "video_failed", reason);
    }
}

/* Returns true when the attempt has finished, one way or another. */
static bool video_step(VideoApp *app, VideoBackend *backend,
    VideoAttempt *attempt, double elapsed)
{
    if (backend->state == VIDEO_STATE_READY && !attempt->opened) {
        attempt->opened = true;
        if (!attempt->ready) {
            video_log_opened(app, backend);
        }
        video_window_set_backend(&app->window, backend);
        video_log_event(&app->log, "video_play_requested", NULL, false);
        attempt->start_requested = GetTickCount64();
        backend->ops->start(backend);
        return false;
    }
    if (backend->state == VIDEO_STATE_ENDED) {
        video_set_text(attempt->result, sizeof(attempt->result), "finished");
        video_log_event(&app->log, "video_ended", NULL, false);
        return true;
    }
    /* Startup breadcrumbs: how long the screen stays black after the
     * request, until playback runs and its clock (the first frame) moves. */
    if (backend->state == VIDEO_STATE_PLAYING && !attempt->playing) {
        attempt->playing = true;
        video_log_event(&app->log, "video_playing", NULL, false);
    }
    LONGLONG position = 0;
    if (attempt->playing && !attempt->clock_moving &&
        backend->ops->position(backend, &position) && position > 0) {
        attempt->clock_moving = true;
        video_log_event(&app->log, "video_clock_moving", NULL, false);
    }
    position = 0;
    if (app->options.probe && backend->state == VIDEO_STATE_PLAYING &&
        backend->ops->position(backend, &position) &&
        position >= VIDEO_PROBE_POSITION_HNS) {
        video_set_text(attempt->result, sizeof(attempt->result),
            "probe passed");
        return true;
    }
    if (attempt->start_requested && !attempt->clock_moving &&
        GetTickCount64() - attempt->start_requested > VIDEO_START_MS) {
        video_fail(app, attempt, "playback did not start", false);
        return true;
    }
    if (video_timed_out(attempt->opened, elapsed,
            backend->info.duration_seconds)) {
        video_fail(app, attempt, "playback timed out", false);
        return true;
    }
    return false;
}

static bool video_check_inputs(VideoApp *app, VideoAttempt *attempt)
{
    if (app->window.destroyed) {
        attempt->window_lost = true;
        video_set_text(attempt->result, sizeof(attempt->result),
            "window closed");
        return true;
    }
    if (app->window.clicked || app->window.close_requested) {
        video_set_text(attempt->result, sizeof(attempt->result), "skipped");
        return true;
    }
    VideoInputEvent event = video_input_poll(&app->input);
    if (event == VIDEO_INPUT_SKIP_KEY || event == VIDEO_INPUT_CONTROLLER) {
        video_set_text(attempt->result, sizeof(attempt->result), "skipped");
        return true;
    }
    if (event == VIDEO_INPUT_GAME_EXITED) {
        video_set_text(attempt->result, sizeof(attempt->result),
            "game exited");
        return true;
    }
    return false;
}

static bool video_signalled(HANDLE handle)
{
    return handle != NULL && WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
}

/* Prepared mode, before the start event: the window is hidden and input
 * is not watched yet (a controller press during the splash is not a skip).
 * Only the game can end the wait, by exiting or by signalling the skip
 * event when it no longer wants this player. */
static bool video_wait_ended(VideoApp *app, VideoAttempt *attempt)
{
    if (app->window.destroyed) {
        attempt->window_lost = true;
        video_set_text(attempt->result, sizeof(attempt->result),
            "window closed");
        return true;
    }
    if (video_signalled(app->input.skip_event)) {
        video_set_text(attempt->result, sizeof(attempt->result),
            "not needed");
        return true;
    }
    if (video_signalled(app->input.game_process)) {
        video_set_text(attempt->result, sizeof(attempt->result),
            "game exited");
        return true;
    }
    return false;
}

/* Shows a hidden (prepared) window once it holds a frame: at once when the
 * movie was cued, else when playback runs, so the game's own last frame
 * stays on screen instead of a black window. After 500 ms it shows
 * regardless, as an unprepared window would have been from the start. */
static void video_reveal(VideoApp *app, const VideoBackend *backend,
    const VideoAttempt *attempt)
{
    if (app->window.shown || !attempt->opened) {
        return;
    }
    if (backend->cued || attempt->playing || attempt->clock_moving ||
        GetTickCount64() - attempt->start_requested >= 500) {
        LARGE_INTEGER begin;
        LARGE_INTEGER end;
        LARGE_INTEGER frequency;
        QueryPerformanceCounter(&begin);
        video_window_show(&app->window);
        QueryPerformanceCounter(&end);
        char storage[128];
        JsonBuffer json;
        json_init(&json, storage, sizeof(storage));
        json_object_begin(&json);
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
            json_member_fixed(&json, "show_ms", (double)(end.QuadPart -
                begin.QuadPart) * 1000.0 / (double)frequency.QuadPart, 1);
        }
        json_member_bool(&json, "cued", backend->cued);
        json_object_end(&json);
        video_log_event(&app->log, "video_window_shown",
            json_ok(&json) ? json.data : NULL, false);
    }
}

static void video_play(VideoApp *app, const wchar_t *path,
    VideoAttempt *attempt)
{
    char reason[400];
    char media_foundation_reason[192];
    VideoBackend *backend = NULL;
    bool directshow_tried = false;
    ULONGLONG started = GetTickCount64();
    memset(attempt, 0, sizeof(*attempt));
    reason[0] = '\0';
    HRESULT hr = video_mf_create(app->window.hwnd, path, app->options.probe,
        app->options.frames, &backend, media_foundation_reason,
        sizeof(media_foundation_reason));
    if (FAILED(hr)) {
        directshow_tried = true;
        backend = video_open_directshow(app, path, media_foundation_reason,
            reason, sizeof(reason));
    }
    for (;;) {
        double elapsed = (double)(GetTickCount64() - started) / 1000.0;
        if (backend == NULL) {
            video_fail(app, attempt, reason, true);
            break;
        }
        if (!app->start_signalled) {
            if (video_wait_ended(app, attempt)) {
                break;
            }
        } else if (video_check_inputs(app, attempt)) {
            break;
        }
        backend->ops->poll(backend);
        if (backend->state == VIDEO_STATE_FAILED) {
            if (!attempt->opened && !directshow_tried &&
                (backend->frames_published == NULL || *backend->frames_published == 0)) {
                /* Media Foundation gave up before playing anything. */
                directshow_tried = true;
                video_set_text(media_foundation_reason,
                    sizeof(media_foundation_reason), backend->reason);
                backend->ops->destroy(backend);
                backend = video_open_directshow(app, path,
                    media_foundation_reason, reason, sizeof(reason));
                continue;
            }
            video_fail(app, attempt, backend->reason, true);
            break;
        }
        if (!app->start_signalled) {
            if (backend->state == VIDEO_STATE_READY && !attempt->ready) {
                attempt->ready = true;
                video_log_opened(app, backend);
                video_log_event(&app->log, "video_ready", NULL, false);
                /* Hold the first frame in the hidden window. */
                backend->ops->preroll(backend);
            }
            if (backend->cued && !attempt->cued) {
                attempt->cued = true;
                video_log_event(&app->log, "video_cued", NULL, false);
            }
            /* Staging slows the game's own frames a little (the compositor
             * then draws them), so it waits for the game's stage event. */
            if (attempt->cued && video_signalled(app->stage_event) &&
                video_window_stage(&app->window)) {
                video_log_event(&app->log, "video_window_staged", NULL, false);
            }
            if (video_signalled(app->start_event)) {
                app->start_signalled = true;
                video_log_event(&app->log, "video_start_received", NULL, false);
                /* Playback limits count from the game's request. */
                started = GetTickCount64();
                continue;
            }
            if (!attempt->ready && video_timed_out(false, elapsed, 0.0)) {
                video_fail(app, attempt, "playback timed out", false);
                break;
            }
            video_pump(app);
            continue;
        }
        if (video_step(app, backend, attempt, elapsed)) {
            break;
        }
        if (!app->options.frames) {
            video_reveal(app, backend, attempt);
        }
        video_pump(app);
    }
    attempt->elapsed_seconds = (double)(GetTickCount64() - started) / 1000.0;
    if (backend != NULL && backend->frames_published != NULL) {
        attempt->frames = *backend->frames_published;
    }
    video_window_set_backend(&app->window, NULL);
    if (backend != NULL) {
        backend->ops->destroy(backend);
    }
}

static int video_run(VideoApp *app)
{
    bool original_only = app->options.original_only;
    wchar_t path[VIDEO_PATH_CAPACITY];
    for (;;) {
        bool found = video_find_movie(app->game_root, app->options.movie,
            original_only, path, VIDEO_PATH_CAPACITY);
        video_log_requested(app, path, original_only);
        if (!found) {
            video_log_text(&app->log, "video_unavailable",
                "No movie file was found.");
            return VIDEO_EXIT_UNAVAILABLE;
        }
        if (!app->window_ready && !app->options.frames) {
            bool hidden = !app->start_signalled;
            if (!video_window_create(&app->window, app->instance, app->owner,
                    app->options.probe, hidden)) {
                video_log_text(&app->log, "video_failed",
                    "The player window could not be created.");
                return VIDEO_EXIT_FAILED;
            }
            app->window_ready = true;
            if (!hidden) {
                video_log_event(&app->log, "video_window_shown", NULL, false);
            }
        }
        VideoAttempt attempt;
        video_play(app, path, &attempt);
        video_log_closed(app, &attempt);
        wchar_t original[VIDEO_PATH_CAPACITY];
        /* A broken replacement can still fall back to the supplied MPG. */
        if (attempt.failed && !original_only && !attempt.window_lost &&
            (!app->options.frames || attempt.frames == 0) &&
            video_has_extension(path, L".mp4") &&
            video_find_movie(app->game_root, app->options.movie, true,
                original, VIDEO_PATH_CAPACITY)) {
            video_log_event(&app->log, "video_original_fallback", NULL, false);
            original_only = true;
            continue;
        }
        if (app->options.probe) {
            bool passed = !attempt.failed &&
                strcmp(attempt.result, "probe passed") == 0;
            return passed ? VIDEO_EXIT_OK : VIDEO_EXIT_FAILED;
        }
        return attempt.failed ? VIDEO_EXIT_FAILED : VIDEO_EXIT_OK;
    }
}

static int video_main(VideoApp *app, int argc, wchar_t **argv)
{
    const char *error = NULL;
    if (!video_parse_arguments(argc, argv, &app->options, &error)) {
        if (app->options.game_root != NULL && video_resolve_roots(app)) {
            char message[128];
            int written = snprintf(message, sizeof(message),
                "invalid command line: %s", error);
            if (written > 0 && (size_t)written < sizeof(message)) {
                video_log_startup_error(app->mod_root, message);
            }
        }
        return VIDEO_EXIT_USAGE;
    }
    if (!video_resolve_roots(app)) {
        return VIDEO_EXIT_USAGE;
    }
    app->owner = video_valid_owner(&app->options);
    video_log_open(&app->log, app->mod_root, app->options.movie);
    if (app->options.start_event != NULL) {
        app->start_event = video_input_check_event(app->options.start_event);
        if (app->start_event == NULL) {
            /* Never play over the splash: the game launches a player
             * normally when this one is gone. */
            video_log_text(&app->log, "video_failed",
                "The start event is not usable.");
            return VIDEO_EXIT_USAGE;
        }
    }
    app->start_signalled = app->start_event == NULL;
    /* Without a usable stage event the window simply shows at the start. */
    if (app->start_event != NULL && app->options.stage_event != NULL) {
        app->stage_event = video_input_check_event(app->options.stage_event);
    }
    video_watchdog_start(&app->log);
    HRESULT hr = CoInitializeEx(NULL,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        video_log_text(&app->log, "video_failed",
            "COM could not be initialized.");
        return VIDEO_EXIT_FAILED;
    }
    video_input_open(&app->input, &app->options, app->owner);
    int code = video_run(app);
    if (app->window_ready) {
        video_window_destroy(&app->window);
    }
    video_input_close(&app->input);
    if (app->start_event != NULL) {
        CloseHandle(app->start_event);
    }
    if (app->stage_event != NULL) {
        CloseHandle(app->stage_event);
    }
    video_mf_unload();
    CoUninitialize();
    return code;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
    LPWSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    (void)show;
    /* The player lives in the game folder beside third-party DLLs such as
     * a 32-bit ddraw.dll. Load system components from System32 only. */
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        return VIDEO_EXIT_USAGE;
    }
    static VideoApp app;
    app.instance = instance;
    int code = video_main(&app, argc, argv);
    LocalFree(argv);
    return code;
}
