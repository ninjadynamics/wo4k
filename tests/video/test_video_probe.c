/* End-to-end checks of runtime/wo4k-video.exe, run from the repository root.
 *
 * Always: command-line errors and a missing movie (neither opens a window).
 * With movie files: a --probe run of every movie file found, MP4 priority
 * over the MPG, the broken-MP4 fallback to the original, and the three early
 * exits a test can drive: the inherited skip event, a controller button in
 * the shared block and the game process ending. Each probe opens a muted,
 * non-activating 640x480 window for about half a second.
 *
 * Movie files are not part of the repository. They are taken from the
 * folder named by WO4K_TEST_MOVIES, else wobble\movies; without any, the
 * playback cases are skipped. Logs and fixtures go to
 * build\test-work\video-probe. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "video/video_core.h"

#define WORK_DIR L"build\\test-work\\video-probe"
#define PLAYER_PATH L"runtime\\wo4k-video.exe"
#define RUN_TIMEOUT_MS 60000
#define LOG_TEXT_SIZE 16384

static int failures;
static wchar_t player[VIDEO_PATH_CAPACITY];

#define CHECK(expression, message) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL: %s (%s:%d: %s)\n", message, __FILE__, \
                __LINE__, #expression); \
            ++failures; \
        } \
    } while (0)

typedef struct Fixture {
    wchar_t root[VIDEO_PATH_CAPACITY];
    wchar_t game[VIDEO_PATH_CAPACITY];
    wchar_t movies[VIDEO_PATH_CAPACITY];
    wchar_t logs[VIDEO_PATH_CAPACITY];
} Fixture;

static bool make_directory(const wchar_t *path)
{
    return CreateDirectoryW(path, NULL) ||
        GetLastError() == ERROR_ALREADY_EXISTS;
}

/* Deletes the files (not subdirectories) directly inside directory. */
static void clear_files(const wchar_t *directory)
{
    wchar_t pattern[VIDEO_PATH_CAPACITY];
    wchar_t path[VIDEO_PATH_CAPACITY];
    WIN32_FIND_DATAW found;
    if (!video_join(pattern, VIDEO_PATH_CAPACITY, directory, L"*")) {
        return;
    }
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            video_join(path, VIDEO_PATH_CAPACITY, directory, found.cFileName)) {
            DeleteFileW(path);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static bool full_path(const wchar_t *path, wchar_t *out)
{
    DWORD length = GetFullPathNameW(path, VIDEO_PATH_CAPACITY, out, NULL);
    return length > 0 && length < VIDEO_PATH_CAPACITY;
}

/* <work>\<name>\game\movies and <work>\<name>\logs, emptied. */
static bool fixture_create(Fixture *fixture, const wchar_t *name)
{
    wchar_t relative[VIDEO_PATH_CAPACITY];
    if (!video_join(relative, VIDEO_PATH_CAPACITY, WORK_DIR, name) ||
        !full_path(relative, fixture->root) ||
        !video_join(fixture->game, VIDEO_PATH_CAPACITY, fixture->root,
            L"game") ||
        !video_join(fixture->movies, VIDEO_PATH_CAPACITY, fixture->game,
            L"movies") ||
        !video_join(fixture->logs, VIDEO_PATH_CAPACITY, fixture->root,
            L"logs")) {
        return false;
    }
    if (!make_directory(fixture->root) || !make_directory(fixture->game) ||
        !make_directory(fixture->movies) || !make_directory(fixture->logs)) {
        return false;
    }
    clear_files(fixture->movies);
    clear_files(fixture->logs);
    return true;
}

/* A copy, never a link: the source movies stay untouched. */
static bool fixture_add(const Fixture *fixture, const wchar_t *source,
    const wchar_t *name)
{
    wchar_t target[VIDEO_PATH_CAPACITY];
    if (!video_join(target, VIDEO_PATH_CAPACITY, fixture->movies, name)) {
        return false;
    }
    return CopyFileW(source, target, TRUE) != 0;
}

static bool fixture_write(const Fixture *fixture, const wchar_t *name,
    const char *text)
{
    wchar_t target[VIDEO_PATH_CAPACITY];
    if (!video_join(target, VIDEO_PATH_CAPACITY, fixture->movies, name)) {
        return false;
    }
    HANDLE file = CreateFileW(target, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    DWORD length = (DWORD)strlen(text);
    bool ok = WriteFile(file, text, length, &written, NULL) && written == length;
    CloseHandle(file);
    return ok;
}

/* Reads a log into text (always terminated); false when it cannot. */
static bool read_log(const Fixture *fixture, const wchar_t *name, char *text,
    size_t size)
{
    wchar_t path[VIDEO_PATH_CAPACITY];
    text[0] = '\0';
    if (!video_join(path, VIDEO_PATH_CAPACITY, fixture->logs, name)) {
        return false;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD read = 0;
    bool ok = ReadFile(file, text, (DWORD)(size - 1), &read, NULL) != 0;
    CloseHandle(file);
    text[ok ? read : 0] = '\0';
    return ok;
}

static bool movie_log(const Fixture *fixture, const wchar_t *movie, char *text)
{
    wchar_t name[32];
    if (swprintf(name, 32, L"video-%ls.log", movie) < 0) {
        return false;
    }
    return read_log(fixture, name, text, LOG_TEXT_SIZE);
}

typedef struct Launch {
    PROCESS_INFORMATION process;
    bool started;
} Launch;

/* Starts the player; inherit, when not NULL, is the only handle it gets. */
static bool launch(Launch *run, const wchar_t *arguments, HANDLE inherit)
{
    wchar_t command[4 * VIDEO_PATH_CAPACITY];
    STARTUPINFOEXW startup;
    SIZE_T size = 0;
    BOOL inherits = FALSE;
    DWORD flags = 0;
    memset(run, 0, sizeof(*run));
    memset(&startup, 0, sizeof(startup));
    startup.StartupInfo.cb = sizeof(startup.StartupInfo);
    int written = swprintf(command, 4 * VIDEO_PATH_CAPACITY, L"\"%ls\" %ls",
        player, arguments);
    if (written < 0) {
        return false;
    }
    if (inherit != NULL) {
        InitializeProcThreadAttributeList(NULL, 1, 0, &size);
        startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, size);
        if (startup.lpAttributeList == NULL ||
            !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                &size)) {
            HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
            return false;
        }
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherit, sizeof(inherit),
                NULL, NULL)) {
            DeleteProcThreadAttributeList(startup.lpAttributeList);
            HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
            return false;
        }
        startup.StartupInfo.cb = sizeof(startup);
        inherits = TRUE;
        flags = EXTENDED_STARTUPINFO_PRESENT;
    }
    run->started = CreateProcessW(player, command, NULL, NULL, inherits, flags,
        NULL, NULL, &startup.StartupInfo, &run->process) != 0;
    if (startup.lpAttributeList != NULL) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    }
    return run->started;
}

/* Waits for the player; a hung player is terminated and reported. */
static DWORD finish(Launch *run)
{
    DWORD code = 0xffffffffu;
    if (!run->started) {
        return code;
    }
    if (WaitForSingleObject(run->process.hProcess, RUN_TIMEOUT_MS) !=
        WAIT_OBJECT_0) {
        TerminateProcess(run->process.hProcess, 0xdead);
        WaitForSingleObject(run->process.hProcess, 5000);
        fprintf(stderr, "FAIL: the player did not exit within %d s\n",
            RUN_TIMEOUT_MS / 1000);
        ++failures;
    }
    GetExitCodeProcess(run->process.hProcess, &code);
    CloseHandle(run->process.hThread);
    CloseHandle(run->process.hProcess);
    run->started = false;
    return code;
}

static DWORD run_player(const wchar_t *arguments, HANDLE inherit)
{
    Launch run;
    if (!launch(&run, arguments, inherit)) {
        fprintf(stderr, "FAIL: cannot start %ls\n", player);
        ++failures;
        return 0xffffffffu;
    }
    return finish(&run);
}

static bool probe_arguments(wchar_t *out, size_t size, const Fixture *fixture,
    const wchar_t *movie, const wchar_t *extra)
{
    int written = swprintf(out, size, L"--game-root \"%ls\" --movie %ls --probe%ls",
        fixture->game, movie, extra);
    return written > 0 && (size_t)written < size;
}

static void test_usage(void)
{
    Fixture fixture;
    wchar_t arguments[2 * VIDEO_PATH_CAPACITY];
    char text[LOG_TEXT_SIZE];
    CHECK(run_player(L"", NULL) == VIDEO_EXIT_USAGE, "no arguments");
    CHECK(run_player(L"--movie intro", NULL) == VIDEO_EXIT_USAGE,
        "missing --game-root");
    if (!fixture_create(&fixture, L"usage")) {
        CHECK(false, "cannot create the usage fixture");
        return;
    }
    CHECK(swprintf(arguments, 2 * VIDEO_PATH_CAPACITY,
        L"--game-root \"%ls\" --movie outro", fixture.game) > 0, "arguments");
    CHECK(run_player(arguments, NULL) == VIDEO_EXIT_USAGE, "unknown movie");
    CHECK(read_log(&fixture, L"video-error.log", text, sizeof(text)) &&
        strstr(text, "invalid command line: unknown --movie") != NULL &&
        strstr(text, "mod_version=") != NULL,
        "a command-line error is recorded in video-error.log");
    printf("PASS: invalid command lines exit 2 and are recorded.\n");
}

static void test_missing(void)
{
    Fixture fixture;
    wchar_t arguments[2 * VIDEO_PATH_CAPACITY];
    char text[LOG_TEXT_SIZE];
    if (!fixture_create(&fixture, L"missing") ||
        !probe_arguments(arguments, 2 * VIDEO_PATH_CAPACITY, &fixture,
            L"xtro3", L"")) {
        CHECK(false, "cannot create the missing-movie fixture");
        return;
    }
    CHECK(run_player(arguments, NULL) == VIDEO_EXIT_UNAVAILABLE,
        "a missing movie exits 3");
    CHECK(movie_log(&fixture, L"xtro3", text) &&
        strstr(text, "\"event\":\"video_requested\"") != NULL &&
        strstr(text, "\"file\":null") != NULL &&
        strstr(text, "\"event\":\"video_unavailable\"") != NULL &&
        strstr(text, "video_opened") == NULL,
        "a missing movie logs video_unavailable without opening anything");
    printf("PASS: a missing movie exits 3 without playback.\n");
}

typedef struct Source {
    wchar_t directory[VIDEO_PATH_CAPACITY];
    unsigned found;
} Source;

static bool source_file(const Source *source, const wchar_t *name,
    wchar_t *out)
{
    if (!video_join(out, VIDEO_PATH_CAPACITY, source->directory, name)) {
        return false;
    }
    DWORD attributes = GetFileAttributesW(out);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* A probe of fixture/movie: exit 0, and the log shows a passed probe of
 * the expected file. */
static bool probe_passes(const Fixture *fixture, const wchar_t *movie,
    const wchar_t *expected_file, char *text)
{
    wchar_t arguments[2 * VIDEO_PATH_CAPACITY];
    char file_field[96];
    if (!probe_arguments(arguments, 2 * VIDEO_PATH_CAPACITY, fixture, movie,
            L"")) {
        return false;
    }
    DWORD code = run_player(arguments, NULL);
    if (!movie_log(fixture, movie, text)) {
        fprintf(stderr, "  no log for %ls (exit %lu)\n", movie,
            (unsigned long)code);
        return false;
    }
    int written = snprintf(file_field, sizeof(file_field), "\"file\":\"%ls\"",
        expected_file);
    bool ok = code == VIDEO_EXIT_OK && written > 0 &&
        (size_t)written < sizeof(file_field) &&
        strstr(text, file_field) != NULL &&
        strstr(text, "\"event\":\"video_opened\"") != NULL &&
        strstr(text, "\"result\":\"probe passed\"") != NULL &&
        strstr(text, "\"player_activated\":true") == NULL;
    if (!ok) {
        fprintf(stderr, "  exit %lu, log:\n%s\n", (unsigned long)code, text);
    }
    return ok;
}

/* Every movie file the source has, each alone in its fixture. */
static unsigned test_each_file(Source *source)
{
    static const wchar_t *const movies[] = {
        L"intro", L"xtro1", L"xtro2", L"xtro3", L"xtro4"
    };
    char text[LOG_TEXT_SIZE];
    unsigned played = 0;
    for (int m = 0; m < VIDEO_MOVIE_COUNT; ++m) {
        const wchar_t *names[VIDEO_MAX_CANDIDATES];
        size_t count = video_movie_candidates((VideoMovie)m, false, names);
        for (size_t i = 0; i < count; ++i) {
            wchar_t path[VIDEO_PATH_CAPACITY];
            Fixture fixture;
            if (!source_file(source, names[i], path)) {
                continue;
            }
            ++source->found;
            if (!fixture_create(&fixture, L"each") ||
                !fixture_add(&fixture, path, names[i])) {
                CHECK(false, "cannot prepare a playback fixture");
                continue;
            }
            bool passed = probe_passes(&fixture, movies[m], names[i], text);
            CHECK(passed, "probe playback of a movie file");
            if (passed) {
                const char *backend = strstr(text, "\"backend\":\"directshow\"") ?
                    "DirectShow" : "Media Foundation";
                printf("PASS: %ls (%ls) decoded and advanced via %s.\n",
                    movies[m], names[i], backend);
                ++played;
            }
        }
    }
    return played;
}

/* With both the MP4 replacement and the MPG, the MP4 plays. */
static void test_priority(const Source *source)
{
    static const wchar_t *const movies[] = {
        L"intro", L"xtro1", L"xtro2", L"xtro3", L"xtro4"
    };
    char text[LOG_TEXT_SIZE];
    for (int m = 0; m < VIDEO_MOVIE_COUNT; ++m) {
        const wchar_t *names[VIDEO_MAX_CANDIDATES];
        size_t count = video_movie_candidates((VideoMovie)m, false, names);
        wchar_t mp4[VIDEO_PATH_CAPACITY];
        wchar_t mpg[VIDEO_PATH_CAPACITY];
        Fixture fixture;
        /* names[count - 2] is XTRON.mp4 or WXLINTR.mp4. */
        if (!source_file(source, names[count - 2], mp4) ||
            !source_file(source, names[count - 1], mpg)) {
            continue;
        }
        bool ready = fixture_create(&fixture, L"priority") &&
            fixture_add(&fixture, mp4, names[count - 2]) &&
            fixture_add(&fixture, mpg, names[count - 1]);
        CHECK(ready, "cannot prepare the priority fixture");
        if (ready) {
            CHECK(probe_passes(&fixture, movies[m], names[count - 2], text),
                "an MP4 replacement wins over the original MPG");
            printf("PASS: %ls prefers %ls over %ls.\n", movies[m],
                names[count - 2], names[count - 1]);
        }
        return;
    }
    printf("SKIP: MP4 priority needs an MP4 and its MPG in the source.\n");
}

/* An unreadable replacement falls back to the original MPG. */
static void test_broken_replacement(const Source *source)
{
    char text[LOG_TEXT_SIZE];
    wchar_t mpg[VIDEO_PATH_CAPACITY];
    Fixture fixture;
    if (!source_file(source, L"WXLINTR.MPG", mpg)) {
        printf("SKIP: the broken-MP4 fallback needs WXLINTR.MPG.\n");
        return;
    }
    bool ready = fixture_create(&fixture, L"broken") &&
        fixture_add(&fixture, mpg, L"WXLINTR.MPG") &&
        fixture_write(&fixture, L"intro.mp4", "invalid media");
    CHECK(ready, "cannot prepare the broken-MP4 fixture");
    if (!ready) {
        return;
    }
    bool passed = probe_passes(&fixture, L"intro", L"WXLINTR.MPG", text);
    CHECK(passed && strstr(text, "\"file\":\"intro.mp4\"") != NULL &&
        strstr(text, "\"event\":\"video_failed\"") != NULL &&
        strstr(text, "\"event\":\"video_original_fallback\"") != NULL,
        "a broken intro.mp4 falls back to WXLINTR.MPG");
    printf("PASS: a broken intro.mp4 falls back to the original MPG.\n");
}

/* The first movie file the source has, alone in a fixture. */
static bool any_movie(const Source *source, Fixture *fixture,
    const wchar_t *fixture_name, const wchar_t **movie)
{
    static const wchar_t *const movies[] = {
        L"intro", L"xtro1", L"xtro2", L"xtro3", L"xtro4"
    };
    for (int m = 0; m < VIDEO_MOVIE_COUNT; ++m) {
        const wchar_t *names[VIDEO_MAX_CANDIDATES];
        size_t count = video_movie_candidates((VideoMovie)m, false, names);
        for (size_t i = 0; i < count; ++i) {
            wchar_t path[VIDEO_PATH_CAPACITY];
            if (source_file(source, names[i], path)) {
                *movie = movies[m];
                return fixture_create(fixture, fixture_name) &&
                    fixture_add(fixture, path, names[i]);
            }
        }
    }
    return false;
}

static bool skipped_with(const Fixture *fixture, const wchar_t *movie,
    DWORD code, const char *result)
{
    char text[LOG_TEXT_SIZE];
    char field[64];
    text[0] = '\0';
    int written = snprintf(field, sizeof(field), "\"result\":\"%s\"", result);
    bool ok = movie_log(fixture, movie, text) && written > 0 &&
        (size_t)written < sizeof(field) && strstr(text, field) != NULL &&
        code == VIDEO_EXIT_FAILED;
    if (!ok) {
        fprintf(stderr, "  exit %lu, log:\n%s\n", (unsigned long)code, text);
    }
    return ok;
}

/* The runtime's skip event, inherited through a handle list. */
static void test_skip_event(const Source *source)
{
    Fixture fixture;
    const wchar_t *movie = NULL;
    wchar_t arguments[2 * VIDEO_PATH_CAPACITY];
    wchar_t extra[48];
    SECURITY_ATTRIBUTES inherit = { sizeof(inherit), NULL, TRUE };
    if (!any_movie(source, &fixture, L"skip-event", &movie)) {
        CHECK(false, "cannot prepare the skip-event fixture");
        return;
    }
    HANDLE skip = CreateEventW(&inherit, TRUE, TRUE, NULL);
    CHECK(skip != NULL, "CreateEvent");
    if (skip == NULL) {
        return;
    }
    bool ready = swprintf(extra, 48, L" --skip-event %lu",
        (unsigned long)(ULONG_PTR)skip) > 0 &&
        probe_arguments(arguments, 2 * VIDEO_PATH_CAPACITY, &fixture, movie,
            extra);
    DWORD code = ready ? run_player(arguments, skip) : 0xffffffffu;
    CloseHandle(skip);
    /* Skipping ends the probe before it can pass. */
    CHECK(skipped_with(&fixture, movie, code, "skipped"),
        "the inherited skip event ends playback");
    printf("PASS: the inherited skip event ends playback.\n");
}

static bool wait_for_log(const Fixture *fixture, const wchar_t *movie,
    const char *needle, HANDLE process)
{
    char text[LOG_TEXT_SIZE];
    ULONGLONG deadline = GetTickCount64() + 20000;
    while (GetTickCount64() < deadline) {
        if (movie_log(fixture, movie, text) && strstr(text, needle) != NULL) {
            return true;
        }
        if (WaitForSingleObject(process, 2) == WAIT_OBJECT_0) {
            return false;
        }
    }
    return false;
}

/* A button press in Local\WO2097Input-<pid>, after the baseline sample. */
static void test_controller(const Source *source)
{
    Fixture fixture;
    const wchar_t *movie = NULL;
    wchar_t arguments[2 * VIDEO_PATH_CAPACITY];
    wchar_t extra[48];
    wchar_t name[64];
    DWORD pid = GetCurrentProcessId();
    if (!any_movie(source, &fixture, L"controller", &movie) ||
        swprintf(name, 64, L"Local\\WO2097Input-%lu", (unsigned long)pid) < 0 ||
        swprintf(extra, 48, L" --game-pid %lu", (unsigned long)pid) < 0 ||
        !probe_arguments(arguments, 2 * VIDEO_PATH_CAPACITY, &fixture, movie,
            extra)) {
        CHECK(false, "cannot prepare the controller fixture");
        return;
    }
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
        PAGE_READWRITE, 0, sizeof(ControllerWire), name);
    ControllerWire *wire = mapping == NULL ? NULL :
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*wire));
    CHECK(wire != NULL, "cannot create the controller block");
    if (wire == NULL) {
        if (mapping != NULL) {
            CloseHandle(mapping);
        }
        return;
    }
    wire->magic = CONTROLLER_MAGIC;
    wire->connected = 1;
    wire->buttons = 1u << 6;
    InterlockedExchange(&wire->sequence, 2);
    Launch run;
    DWORD code = 0xffffffffu;
    if (launch(&run, arguments, NULL)) {
        /* The baseline (Start held) is sampled before video_opened. */
        bool opened = wait_for_log(&fixture, movie, "\"video_opened\"",
            run.process.hProcess);
        InterlockedIncrement(&wire->sequence);
        wire->buttons = (1u << 6) | 1u;
        InterlockedIncrement(&wire->sequence);
        CHECK(opened, "the player opened the movie");
        code = finish(&run);
    }
    UnmapViewOfFile(wire);
    CloseHandle(mapping);
    CHECK(skipped_with(&fixture, movie, code, "skipped"),
        "a newly pressed controller button ends playback");
    printf("PASS: a new controller button press ends playback; a held one "
        "does not.\n");
}

/* The player stops when the game process is gone. */
static void test_game_exit(const Source *source)
{
    Fixture fixture;
    const wchar_t *movie = NULL;
    wchar_t arguments[2 * VIDEO_PATH_CAPACITY];
    wchar_t extra[48];
    wchar_t shell[MAX_PATH];
    wchar_t command[] = L"cmd.exe /d /c exit 0";
    STARTUPINFOW startup;
    PROCESS_INFORMATION game;
    UINT length = GetSystemDirectoryW(shell, MAX_PATH);
    if (!any_movie(source, &fixture, L"game-exit", &movie) || length == 0 ||
        length >= MAX_PATH || !video_append(shell, MAX_PATH, L"\\cmd.exe")) {
        CHECK(false, "cannot prepare the game-exit fixture");
        return;
    }
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(shell, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
            NULL, NULL, &startup, &game)) {
        CHECK(false, "cannot start a stand-in game process");
        return;
    }
    /* Holding the handle keeps the exited process's id from being reused. */
    WaitForSingleObject(game.hProcess, 10000);
    bool ready = swprintf(extra, 48, L" --game-pid %lu",
        (unsigned long)game.dwProcessId) > 0 &&
        probe_arguments(arguments, 2 * VIDEO_PATH_CAPACITY, &fixture, movie,
            extra);
    DWORD code = ready ? run_player(arguments, NULL) : 0xffffffffu;
    CloseHandle(game.hThread);
    CloseHandle(game.hProcess);
    CHECK(skipped_with(&fixture, movie, code, "game exited"),
        "the player stops when the game exits");
    printf("PASS: the player stops when the game process exits.\n");
}

static bool find_source(Source *source)
{
    wchar_t configured[VIDEO_PATH_CAPACITY];
    DWORD length = GetEnvironmentVariableW(L"WO4K_TEST_MOVIES", configured,
        VIDEO_PATH_CAPACITY);
    const wchar_t *directory = L"wobble\\movies";
    if (length > 0 && length < VIDEO_PATH_CAPACITY) {
        directory = configured;
    }
    source->found = 0;
    return full_path(directory, source->directory);
}

int main(void)
{
    Source source;
    if (!full_path(PLAYER_PATH, player) ||
        GetFileAttributesW(player) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "FAIL: %ls is missing; build it first.\n", PLAYER_PATH);
        return 1;
    }
    if (!make_directory(L"build") || !make_directory(L"build\\test-work") ||
        !make_directory(WORK_DIR)) {
        fprintf(stderr, "FAIL: cannot create %ls\n", WORK_DIR);
        return 1;
    }
    test_usage();
    test_missing();
    if (!find_source(&source)) {
        fprintf(stderr, "FAIL: invalid movie folder\n");
        return 1;
    }
    unsigned played = test_each_file(&source);
    if (source.found == 0) {
        printf("SKIP: no movie files in %ls (set WO4K_TEST_MOVIES); playback "
            "probes skipped.\n", source.directory);
    } else {
        CHECK(played == source.found, "every movie file plays");
        test_priority(&source);
        test_broken_replacement(&source);
        test_skip_event(&source);
        test_controller(&source);
        test_game_exit(&source);
    }
    if (failures != 0) {
        fprintf(stderr, "FAIL: %d video player check(s) failed.\n", failures);
        return 1;
    }
    printf("PASS: wo4k-video.exe command line, missing-movie, playback and "
        "early-exit checks.\n");
    return 0;
}
