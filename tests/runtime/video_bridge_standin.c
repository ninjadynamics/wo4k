/* A windowless stand-in for wo4k-video.exe, used by run_render_init to test
 * the runtime's movie launch (src/runtime/movies_patch.c) without opening a
 * player. It checks the command line with the player's own parser and the
 * inherited skip event, then posts key-downs to the "game" (the renderer
 * fixture): an auto-repeat Esc must not signal the event, a fresh Esc must.
 * Results go to bridge-result.txt beside this executable. */
#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include "video/video_core.h"

static char report[1024];
static size_t report_length;

static void record(const char *name, bool value)
{
    int written = snprintf(report + report_length,
        sizeof(report) - report_length, "%s %d\r\n", name, value ? 1 : 0);
    if (written > 0 && (size_t)written < sizeof(report) - report_length) {
        report_length += (size_t)written;
    }
}

static bool own_directory(wchar_t *out, DWORD capacity)
{
    DWORD length = GetModuleFileNameW(NULL, out, capacity);
    if (length == 0 || length >= capacity) {
        return false;
    }
    while (length > 0 && out[length - 1] != L'\\') {
        --length;
    }
    out[length] = L'\0';
    video_trim_separators(out);
    return true;
}

static void save_report(const wchar_t *directory)
{
    wchar_t path[VIDEO_PATH_CAPACITY];
    if (!video_join(path, VIDEO_PATH_CAPACITY, directory, L"bridge-result.txt")) {
        return;
    }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, report, (DWORD)report_length, &written, NULL);
    CloseHandle(file);
}

/* Posts a key-down to every thread of the game process that has a message
 * queue. */
static bool post_key_once(DWORD game_pid, WPARAM key, LPARAM flags)
{
    bool posted = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 thread;
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    memset(&thread, 0, sizeof(thread));
    thread.dwSize = sizeof(thread);
    for (BOOL more = Thread32First(snapshot, &thread); more;
        more = Thread32Next(snapshot, &thread)) {
        if (thread.th32OwnerProcessID == game_pid &&
            PostThreadMessageW(thread.th32ThreadID, WM_KEYDOWN, key, flags)) {
            posted = true;
        }
    }
    CloseHandle(snapshot);
    return posted;
}

static bool post_key(DWORD game_pid, WPARAM key, LPARAM flags)
{
    ULONGLONG deadline = GetTickCount64() + 1000;
    while (!post_key_once(game_pid, key, flags)) {
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(10);
    }
    return true;
}

static bool is_game(DWORD pid)
{
    wchar_t image[VIDEO_PATH_CAPACITY];
    DWORD size = VIDEO_PATH_CAPACITY;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == NULL) {
        return false;
    }
    bool ok = QueryFullProcessImageNameW(process, 0, image, &size) &&
        _wcsicmp(video_file_name(image), L"render_init_fixture.exe") == 0;
    CloseHandle(process);
    return ok;
}

static void check(int argc, wchar_t **argv, const wchar_t *directory)
{
    VideoOptions options;
    const char *error = NULL;
    wchar_t root[VIDEO_PATH_CAPACITY];
    bool parsed = video_parse_arguments(argc, argv, &options, &error);
    record("arguments_parsed", parsed);
    if (!parsed) {
        return;
    }
    DWORD length = GetFullPathNameW(options.game_root, VIDEO_PATH_CAPACITY,
        root, NULL);
    bool root_ok = length > 0 && length < VIDEO_PATH_CAPACITY;
    if (root_ok) {
        video_trim_separators(root);
        root_ok = _wcsicmp(root, directory) == 0;
    }
    record("game_root_matches", root_ok);
    record("movie_is_intro", options.movie == VIDEO_MOVIE_INTRO);
    record("game_pid_is_game", is_game(options.game_pid));
    record("not_probe", !options.probe && !options.original_only);
    HANDLE skip = options.skip_event;
    DWORD flags = 0;
    bool valid = skip != NULL && GetHandleInformation(skip, &flags) &&
        WaitForSingleObject(skip, 0) == WAIT_TIMEOUT;
    record("skip_event_inherited", valid);
    if (!valid) {
        return;
    }
    /* Bit 30 set: the key was already down, an auto-repeat. */
    bool repeat_posted = post_key(options.game_pid, VK_ESCAPE, 0x40000001);
    bool repeat_ignored = repeat_posted &&
        WaitForSingleObject(skip, 300) == WAIT_TIMEOUT;
    record("repeat_ignored", repeat_ignored);
    bool fresh_posted = post_key(options.game_pid, VK_ESCAPE, 1);
    record("skip_signaled", fresh_posted &&
        WaitForSingleObject(skip, 2000) == WAIT_OBJECT_0);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
    LPWSTR command_line, int show)
{
    wchar_t directory[VIDEO_PATH_CAPACITY];
    int argc = 0;
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    ULONGLONG started = GetTickCount64();
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL || !own_directory(directory, VIDEO_PATH_CAPACITY)) {
        return 1;
    }
    check(argc, argv, directory);
    LocalFree(argv);
    save_report(directory);
    /* The fixture expects a movie call to take at least a second. */
    ULONGLONG elapsed = GetTickCount64() - started;
    if (elapsed < 1200) {
        Sleep((DWORD)(1200 - elapsed));
    }
    return 0;
}
