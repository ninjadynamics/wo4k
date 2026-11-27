/* Unit tests for src/video/video_core.c: movie lookup order, command line,
 * breadcrumb JSON, timeouts and controller sampling. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "video/video_core.h"
#include "video/video_crt.h"

static int failures;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                #expression); \
            ++failures; \
        } \
    } while (0)

static void test_movie_names(void)
{
    VideoMovie movie = VIDEO_MOVIE_COUNT;
    CHECK(video_movie_parse(L"intro", &movie) && movie == VIDEO_MOVIE_INTRO);
    CHECK(video_movie_parse(L"xtro4", &movie) && movie == VIDEO_MOVIE_XTRO4);
    CHECK(!video_movie_parse(L"xtro5", &movie));
    CHECK(!video_movie_parse(L"INTRO", &movie));
    CHECK(!video_movie_parse(L"", &movie));
    CHECK(wcscmp(video_movie_name(VIDEO_MOVIE_XTRO2), L"xtro2") == 0);
    CHECK(video_movie_name(VIDEO_MOVIE_COUNT) == NULL);
}

static void test_candidates(void)
{
    VideoIntroChoice intro = VIDEO_INTRO_ORIGINAL;
    CHECK(video_intro_parse(L"Original", &intro) && intro == VIDEO_INTRO_ORIGINAL);
    CHECK(video_intro_parse(L"upscaled", &intro) && intro == VIDEO_INTRO_UPSCALED);
    CHECK(video_intro_parse(L"FLUKE", &intro) && intro == VIDEO_INTRO_FLUKE);
    CHECK(!video_intro_parse(L"custom", &intro));

    CHECK(video_override_name(VIDEO_MOVIE_INTRO, VIDEO_INTRO_ORIGINAL) == NULL);
    CHECK(wcscmp(video_override_name(VIDEO_MOVIE_INTRO, VIDEO_INTRO_UPSCALED), L"intro.mp4") == 0);
    CHECK(wcscmp(video_override_name(VIDEO_MOVIE_INTRO, VIDEO_INTRO_FLUKE), L"fluke.mp4") == 0);
    CHECK(wcscmp(video_original_name(VIDEO_MOVIE_INTRO), L"WXLINTR.MPG") == 0);
    CHECK(wcscmp(video_override_name(VIDEO_MOVIE_XTRO3, VIDEO_INTRO_FLUKE), L"XTRO3.mp4") == 0);
    CHECK(wcscmp(video_original_name(VIDEO_MOVIE_XTRO3), L"XTRO3.MPG") == 0);
}

static bool write_text_file(const wchar_t *path, const char *text)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    DWORD len = (DWORD)strlen(text);
    bool ok = len == 0 || (WriteFile(file, text, len, &written, NULL) && written == len);
    CloseHandle(file);
    return ok;
}

static bool write_file(const wchar_t *path)
{
    return write_text_file(path, "");
}

static bool make_directory(const wchar_t *path)
{
    return CreateDirectoryW(path, NULL) ||
        GetLastError() == ERROR_ALREADY_EXISTS;
}

/* build\test-work\video-core\{game\movies, assets\video}, emptied first. */
static bool prepare_game(wchar_t *game, wchar_t *movies, wchar_t *videos,
    size_t capacity)
{
    const wchar_t *const movie_files[] = {
        L"WXLINTR.MPG", L"XTRO1.MPG"
    };
    const wchar_t *const video_files[] = {
        L"fluke.mp4", L"intro.mp4", L"XTRO1.mp4"
    };
    wchar_t path[VIDEO_PATH_CAPACITY];
    wchar_t assets[VIDEO_PATH_CAPACITY];
    if (!make_directory(L"build") || !make_directory(L"build\\test-work") ||
        !make_directory(L"build\\test-work\\video-core") ||
        !video_copy(game, capacity, L"build\\test-work\\video-core\\game") ||
        !make_directory(game) || !video_join(movies, capacity, game, L"movies") ||
        !make_directory(movies) ||
        !video_copy(assets, VIDEO_PATH_CAPACITY, L"build\\test-work\\video-core\\assets") ||
        !make_directory(assets) || !video_join(videos, capacity, assets, L"video") ||
        !make_directory(videos)) {
        return false;
    }
    if (video_join(path, VIDEO_PATH_CAPACITY, game, L"Wipeout2097.ini")) {
        DeleteFileW(path);
    }
    for (size_t i = 0; i < sizeof(movie_files) / sizeof(movie_files[0]); ++i) {
        if (!video_join(path, VIDEO_PATH_CAPACITY, movies, movie_files[i])) {
            return false;
        }
        DeleteFileW(path);
        RemoveDirectoryW(path);
    }
    for (size_t i = 0; i < sizeof(video_files) / sizeof(video_files[0]); ++i) {
        if (!video_join(path, VIDEO_PATH_CAPACITY, videos, video_files[i])) {
            return false;
        }
        DeleteFileW(path);
        RemoveDirectoryW(path);
    }
    return true;
}

static void test_find_movie(void)
{
    wchar_t game[VIDEO_PATH_CAPACITY];
    wchar_t movies[VIDEO_PATH_CAPACITY];
    wchar_t videos[VIDEO_PATH_CAPACITY];
    wchar_t path[VIDEO_PATH_CAPACITY];
    wchar_t found[VIDEO_PATH_CAPACITY];
    if (!prepare_game(game, movies, videos, VIDEO_PATH_CAPACITY)) {
        CHECK(!"cannot prepare build\\test-work\\video-core");
        return;
    }
    CHECK(video_read_intro_choice(game) == VIDEO_INTRO_FLUKE);
    CHECK(video_join(path, VIDEO_PATH_CAPACITY, game, L"Wipeout2097.ini"));
    CHECK(write_text_file(path, "[Video]\r\nIntro=Upscaled\r\n"));
    CHECK(video_read_intro_choice(game) == VIDEO_INTRO_UPSCALED);

    CHECK(!video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_FLUKE, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(found[0] == L'\0');

    CHECK(video_join(path, VIDEO_PATH_CAPACITY, movies, L"WXLINTR.MPG"));
    CHECK(write_file(path));
    CHECK(video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_FLUKE, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"WXLINTR.MPG") == 0);

    CHECK(video_join(path, VIDEO_PATH_CAPACITY, videos, L"fluke.mp4"));
    CHECK(write_file(path));
    CHECK(video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_FLUKE, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"fluke.mp4") == 0);
    /* Original choice bypasses assets/video overrides. */
    CHECK(video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_ORIGINAL, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"WXLINTR.MPG") == 0);

    /* A directory with a candidate's name is not a movie. */
    CHECK(video_join(path, VIDEO_PATH_CAPACITY, videos, L"intro.mp4"));
    CHECK(make_directory(path));
    CHECK(video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_UPSCALED, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"WXLINTR.MPG") == 0);
    CHECK(RemoveDirectoryW(path));
    CHECK(write_file(path));
    CHECK(video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_UPSCALED, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"intro.mp4") == 0);

    CHECK(video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_FLUKE, true,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"WXLINTR.MPG") == 0);

    /* Outro overrides come from assets/video, falling back to wobble/movies. */
    CHECK(!video_find_movie(game, VIDEO_MOVIE_XTRO1, VIDEO_INTRO_FLUKE, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(video_join(path, VIDEO_PATH_CAPACITY, videos, L"XTRO1.mp4"));
    CHECK(write_file(path));
    CHECK(video_find_movie(game, VIDEO_MOVIE_XTRO1, VIDEO_INTRO_FLUKE, false,
        found, VIDEO_PATH_CAPACITY));
    CHECK(wcscmp(video_file_name(found), L"XTRO1.mp4") == 0);

    /* A result that cannot fit is reported, not truncated. */
    CHECK(!video_find_movie(game, VIDEO_MOVIE_INTRO, VIDEO_INTRO_FLUKE, false,
        found, 8));
}

static void test_paths(void)
{
    wchar_t text[16];
    wchar_t path[64];
    CHECK(video_copy(text, 16, L"abc"));
    CHECK(video_append(text, 16, L"defghijklmno"));
    CHECK(wcscmp(text, L"abcdefghijklmno") == 0);
    CHECK(!video_append(text, 16, L"p"));
    CHECK(wcscmp(text, L"abcdefghijklmno") == 0);
    CHECK(!video_copy(text, 4, L"abcd"));
    CHECK(video_join(path, 64, L"C:\\game\\", L"movies"));
    CHECK(wcscmp(path, L"C:\\game\\movies") == 0);
    CHECK(video_join(path, 64, L"C:\\game", L"movies"));
    CHECK(wcscmp(path, L"C:\\game\\movies") == 0);

    CHECK(video_copy(path, 64, L"C:\\mod\\wobble\\"));
    CHECK(video_parent(path));
    CHECK(wcscmp(path, L"C:\\mod") == 0);
    CHECK(video_parent(path));
    CHECK(wcscmp(path, L"C:\\") == 0);
    CHECK(!video_parent(path));
    CHECK(video_copy(path, 64, L"wobble"));
    CHECK(!video_parent(path));

    CHECK(wcscmp(video_file_name(L"C:\\a\\b\\XTRO1.MPG"), L"XTRO1.MPG") == 0);
    CHECK(wcscmp(video_file_name(L"XTRO1.MPG"), L"XTRO1.MPG") == 0);
    CHECK(video_has_extension(L"C:\\a\\intro.MP4", L".mp4"));
    CHECK(!video_has_extension(L"C:\\a\\WXLINTR.MPG", L".mp4"));
    CHECK(!video_has_extension(L"4", L".mp4"));
}

static void test_numbers(void)
{
    uint32_t value = 0;
    CHECK(video_parse_u32(L"0", &value) && value == 0);
    CHECK(video_parse_u32(L"4294967295", &value) && value == UINT32_MAX);
    CHECK(!video_parse_u32(L"4294967296", &value));
    CHECK(!video_parse_u32(L"", &value));
    CHECK(!video_parse_u32(L"12x", &value));
    CHECK(!video_parse_u32(L"-1", &value));
    CHECK(!video_parse_u32(L"0x10", &value));
}

static bool parse(VideoOptions *options, const char **error, int argc, ...)
{
    wchar_t *argv[16];
    va_list arguments;
    va_start(arguments, argc);
    for (int i = 0; i < argc && i < 16; ++i) {
        argv[i] = va_arg(arguments, wchar_t *);
    }
    va_end(arguments);
    return video_parse_arguments(argc, argv, options, error);
}

static void test_arguments(void)
{
    VideoOptions options;
    const char *error = NULL;
    wchar_t program[] = L"wo4k-video.exe";
    wchar_t root_flag[] = L"--game-root";
    wchar_t root[] = L"C:\\mod\\wobble\\.";
    wchar_t movie_flag[] = L"--movie";
    wchar_t movie[] = L"xtro2";
    wchar_t pid_flag[] = L"--game-pid";
    wchar_t pid[] = L"1234";
    wchar_t owner_flag[] = L"--owner-window";
    wchar_t owner[] = L"65802";
    wchar_t skip_flag[] = L"--skip-event";
    wchar_t skip[] = L"4294967295";
    wchar_t probe[] = L"--probe";
    wchar_t original[] = L"--original-only";
    wchar_t bad_movie[] = L"outro";
    wchar_t bad_number[] = L"12a";
    wchar_t unknown[] = L"--fullscreen";

    CHECK(parse(&options, &error, 13, program, root_flag, root, movie_flag,
        movie, pid_flag, pid, owner_flag, owner, skip_flag, skip, probe,
        original));
    CHECK(wcscmp(options.game_root, root) == 0);
    CHECK(options.movie == VIDEO_MOVIE_XTRO2);
    CHECK(options.game_pid == 1234);
    CHECK(options.owner_window == (HWND)(LONG_PTR)65802);
    /* Handles from 32-bit processes widen like LongToHandle. */
    CHECK(options.skip_event == (HANDLE)(LONG_PTR)-1);
    CHECK(options.probe && options.original_only && !options.frames);
    wchar_t frames_flag[] = L"--frames";
    CHECK(parse(&options, &error, 6, program, root_flag, root, movie_flag,
        movie, frames_flag));
    CHECK(options.frames && !options.probe && !options.no_shader);
    wchar_t no_shader_flag[] = L"--no-shader";
    CHECK(parse(&options, &error, 6, program, root_flag, root, movie_flag,
        movie, no_shader_flag));
    CHECK(options.no_shader && !options.frames);
    wchar_t intro_flag[] = L"--intro";
    wchar_t intro_val[] = L"Upscaled";
    CHECK(parse(&options, &error, 7, program, root_flag, root, movie_flag,
        movie, intro_flag, intro_val));
    CHECK(options.has_intro && options.intro == VIDEO_INTRO_UPSCALED);

    CHECK(parse(&options, &error, 5, program, root_flag, root, movie_flag,
        movie));
    CHECK(options.game_pid == 0 && options.owner_window == NULL);
    CHECK(options.skip_event == NULL && !options.probe);
    CHECK(options.start_event == NULL);

    wchar_t start_flag[] = L"--start-event";
    wchar_t start[] = L"812";
    wchar_t stage_flag[] = L"--stage-event";
    wchar_t stage[] = L"816";
    CHECK(parse(&options, &error, 11, program, root_flag, root, movie_flag,
        movie, start_flag, start, skip_flag, skip, stage_flag, stage));
    CHECK(options.start_event == (HANDLE)(LONG_PTR)812);
    CHECK(options.stage_event == (HANDLE)(LONG_PTR)816);
    CHECK(options.skip_event == (HANDLE)(LONG_PTR)-1);

    CHECK(!parse(&options, &error, 3, program, movie_flag, movie));
    CHECK(error != NULL && strcmp(error, "missing --game-root") == 0);
    CHECK(!parse(&options, &error, 3, program, root_flag, root));
    CHECK(error != NULL && strcmp(error, "missing --movie") == 0);
    CHECK(!parse(&options, &error, 5, program, root_flag, root, movie_flag,
        bad_movie));
    CHECK(!parse(&options, &error, 7, program, root_flag, root, movie_flag,
        movie, pid_flag, bad_number));
    CHECK(!parse(&options, &error, 6, program, root_flag, root, movie_flag,
        movie, unknown));
    CHECK(!parse(&options, &error, 6, program, root_flag, root, movie_flag,
        movie, pid_flag));
    CHECK(error != NULL && strcmp(error, "missing argument value") == 0);
}

static void test_json(void)
{
    char storage[256];
    JsonBuffer json;
    json_init(&json, storage, sizeof(storage));
    json_string(&json, "a\"b\\c\n\x01");
    CHECK(json_ok(&json));
    CHECK(strcmp(json.data, "\"a\\\"b\\\\c\\n\\u0001\"") == 0);

    json_init(&json, storage, sizeof(storage));
    /* e-acute, a supplementary character, then an unpaired surrogate. */
    json_wide_string(&json, L"\x00e9\xd83d\xde00\xdc00");
    CHECK(json_ok(&json));
    CHECK(strcmp(json.data, "\"\xc3\xa9\xf0\x9f\x98\x80\xef\xbf\xbd\"") == 0);

    json_init(&json, storage, sizeof(storage));
    json_object_begin(&json);
    json_member_string(&json, "file", "XTRO1.MPG");
    json_member_bool(&json, "probe", true);
    json_member_uint(&json, "width", 320);
    json_member_fixed(&json, "seconds", 1.256, 2);
    json_member_wide(&json, "missing", NULL);
    json_key(&json, "nested");
    json_object_begin(&json);
    json_member_bool(&json, "a", false);
    json_object_end(&json);
    json_member_bool(&json, "after", true);
    json_object_end(&json);
    CHECK(json_ok(&json));
    CHECK(strcmp(json.data, "{\"file\":\"XTRO1.MPG\",\"probe\":true,"
        "\"width\":320,\"seconds\":1.26,\"missing\":null,"
        "\"nested\":{\"a\":false},\"after\":true}") == 0);

    char small[8];
    json_init(&json, small, sizeof(small));
    json_string(&json, "too long for eight");
    CHECK(!json_ok(&json));
    CHECK(strlen(small) < sizeof(small));
}

static void test_breadcrumb(void)
{
    char storage[512];
    char time_text[VIDEO_TIME_TEXT_SIZE];
    JsonBuffer json;
    /* 2026-11-06 09:41:00 UTC plus 1234567 hundred-nanosecond units. */
    SYSTEMTIME system = { 2026, 11, 0, 6, 9, 41, 0, 0 };
    FILETIME file;
    CHECK(SystemTimeToFileTime(&system, &file));
    uint64_t filetime = ((uint64_t)file.dwHighDateTime << 32) |
        file.dwLowDateTime;
    filetime += 1234567;
    CHECK(video_format_time(filetime, time_text));
    CHECK(strcmp(time_text, "2026-11-06T09:41:00.1234567Z") == 0);

    json_init(&json, storage, sizeof(storage));
    CHECK(video_breadcrumb_line(&json, filetime, "0.1.0-beta.24",
        "video_requested", L"intro", "{\"file\":\"WXLINTR.MPG\"}"));
    CHECK(strcmp(json.data, "{\"time\":\"2026-11-06T09:41:00.1234567Z\","
        "\"mod_version\":\"0.1.0-beta.24\",\"event\":\"video_requested\","
        "\"movie\":\"intro\",\"data\":{\"file\":\"WXLINTR.MPG\"}}") == 0);

    json_init(&json, storage, sizeof(storage));
    CHECK(video_breadcrumb_line(&json, filetime, "1.0.0", "video_ended",
        L"xtro1", NULL));
    CHECK(strstr(json.data, "\"data\":null}") != NULL);
}

static void test_timeouts(void)
{
    CHECK(!video_timed_out(false, 15.0, 0));
    CHECK(video_timed_out(false, 15.1, 0));
    CHECK(!video_timed_out(true, 29.9, 5));
    CHECK(video_timed_out(true, 30.1, 5));
    CHECK(!video_timed_out(true, 78.0, 63.8));
    CHECK(video_timed_out(true, 78.9, 63.8));
}

static void test_controller(void)
{
    ControllerWire wire;
    VideoControllerState state = { false, 0 };
    memset(&wire, 0, sizeof(wire));
    CHECK(!video_controller_pressed(&state, NULL));
    /* Not yet written by the host. */
    CHECK(!video_controller_pressed(&state, &wire));
    CHECK(!state.has_baseline);

    wire.magic = CONTROLLER_MAGIC;
    wire.connected = 1;
    wire.buttons = 1u << 6;
    wire.sequence = 2;
    /* A button held when the movie starts is the baseline, not a skip. */
    CHECK(!video_controller_pressed(&state, &wire));
    CHECK(state.has_baseline && state.buttons == (1u << 6));
    CHECK(!video_controller_pressed(&state, &wire));

    wire.buttons = 0;
    wire.sequence = 4;
    CHECK(!video_controller_pressed(&state, &wire));

    /* A write in progress is ignored. */
    wire.buttons = 1;
    wire.sequence = 5;
    CHECK(!video_controller_pressed(&state, &wire));
    wire.sequence = 6;
    CHECK(video_controller_pressed(&state, &wire));

    /* Disconnected or foreign data never skips. */
    wire.buttons = 3;
    wire.connected = 0;
    wire.sequence = 8;
    CHECK(!video_controller_pressed(&state, &wire));
    wire.connected = 1;
    wire.magic = 0;
    CHECK(!video_controller_pressed(&state, &wire));
}

static void test_crt_layout(void)
{
    VideoCrtLayout layout;
    /* 4:3 mode on 1080p: preserves full uncropped video with pillarbox bars. */
    video_crt_compute_layout(320, 192, 1920, 1080, false, &layout);
    CHECK(layout.crt_width == 320 && layout.crt_height == 192);
    CHECK(layout.viewport_w > 1799.0f && layout.viewport_w < 1801.0f);
    CHECK(layout.viewport_h > 1079.0f && layout.viewport_h < 1081.0f);
    CHECK(layout.uv_y_offset == 0.0f && layout.uv_y_scale == 1.0f);
    CHECK(layout.letterboxed);
    CHECK(layout.mask_size == 1.0f && layout.mask_zoom == -2.0f);
    CHECK(layout.slot_width == 1.0f && layout.double_slot == 2.0f);

    /* HD 1440x1080 in 4:3 mode on 1080p: pillarboxed, 640x320 CRT grid. */
    video_crt_compute_layout(1440, 1080, 1920, 1080, false, &layout);
    CHECK(layout.crt_width == 640 && layout.crt_height == 320);
    CHECK(layout.viewport_w > 1439.0f && layout.viewport_w < 1441.0f);
    CHECK(layout.viewport_x > 239.0f && layout.viewport_x < 241.0f);
    CHECK(layout.uv_y_offset == 0.0f && layout.uv_y_scale == 1.0f);
    CHECK(layout.letterboxed);

    /* HD 1440x1080 in Widescreen mode on 1080p: fits horizontally (1920x1080),
     * crops 12.5% top and 12.5% bottom (uv_y_scale = 0.75). */
    video_crt_compute_layout(1440, 1080, 1920, 1080, true, &layout);
    CHECK(layout.crt_width == 640 && layout.crt_height == 320);
    CHECK(layout.viewport_w > 1919.0f && layout.viewport_w < 1921.0f);
    CHECK(layout.viewport_h > 1079.0f && layout.viewport_h < 1081.0f);
    CHECK(!layout.letterboxed);
    CHECK(layout.uv_y_scale > 0.749f && layout.uv_y_scale < 0.751f);
    CHECK(layout.uv_y_offset > 0.124f && layout.uv_y_offset < 0.126f);

    /* 4K display switches automatically to 4K Flat mask parameters. */
    video_crt_compute_layout(1440, 1080, 3840, 2160, true, &layout);
    CHECK(layout.crt_width == 640 && layout.crt_height == 320);
    CHECK(layout.mask_size == 2.0f && layout.mask_zoom == -5.0f);
    CHECK(layout.slot_width == 12.0f && layout.double_slot == 2.0f);

    /* 320x192 inside 640x480 probe window: narrower target letterboxes top/bottom. */
    video_crt_compute_layout(320, 192, 640, 480, true, &layout);
    CHECK(layout.viewport_w > 639.0f && layout.viewport_w < 641.0f);
    CHECK(layout.viewport_h > 383.0f && layout.viewport_h < 385.0f);
    CHECK(layout.uv_y_offset == 0.0f && layout.uv_y_scale == 1.0f);
    CHECK(layout.letterboxed);
}

int main(void)
{
    test_movie_names();
    test_candidates();
    test_find_movie();
    test_paths();
    test_numbers();
    test_arguments();
    test_json();
    test_breadcrumb();
    test_timeouts();
    test_controller();
    test_crt_layout();
    if (failures != 0) {
        fprintf(stderr, "FAIL: %d video core check(s) failed.\n", failures);
        return 1;
    }
    printf("PASS: movie lookup order, command line, breadcrumb JSON, "
        "timeouts and controller sampling.\n");
    return 0;
}
