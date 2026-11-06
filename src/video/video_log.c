#include "video/video_log.h"

#include <stdio.h>
#include <string.h>

#include "wo_version.h"

#define VIDEO_LOG_LINE_SIZE 4096

static bool video_logs_directory(const wchar_t *mod_root, wchar_t *out,
    size_t capacity)
{
    if (!video_join(out, capacity, mod_root, L"logs")) {
        return false;
    }
    if (CreateDirectoryW(out, NULL)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool video_write_file(const wchar_t *path, const char *bytes,
    size_t count, bool truncate)
{
    DWORD access = truncate ? GENERIC_WRITE : FILE_APPEND_DATA;
    DWORD disposition = truncate ? CREATE_ALWAYS : OPEN_ALWAYS;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE file = CreateFileW(path, access, share, NULL, disposition,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool ok = count <= MAXDWORD &&
        WriteFile(file, bytes, (DWORD)count, &written, NULL) &&
        written == count;
    CloseHandle(file);
    return ok;
}

void video_log_open(VideoLog *log, const wchar_t *mod_root, VideoMovie movie)
{
    wchar_t directory[VIDEO_PATH_CAPACITY];
    wchar_t name[32];
    memset(log, 0, sizeof(*log));
    log->movie = video_movie_name(movie);
    if (log->movie == NULL || !video_logs_directory(mod_root, directory,
            VIDEO_PATH_CAPACITY)) {
        return;
    }
    if (!video_copy(name, 32, L"video-") || !video_append(name, 32, log->movie) ||
        !video_append(name, 32, L".log")) {
        return;
    }
    log->enabled = video_join(log->path, VIDEO_PATH_CAPACITY, directory, name);
}

void video_log_event(VideoLog *log, const char *event, const char *data_json,
    bool truncate)
{
    char storage[VIDEO_LOG_LINE_SIZE];
    JsonBuffer json;
    FILETIME now;
    if (!log->enabled) {
        return;
    }
    GetSystemTimePreciseAsFileTime(&now);
    uint64_t filetime = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    json_init(&json, storage, sizeof(storage));
    if (!video_breadcrumb_line(&json, filetime, WO_VERSION_STRING, event,
            log->movie, data_json)) {
        /* Too long: keep the event, drop its data. */
        json_init(&json, storage, sizeof(storage));
        if (!video_breadcrumb_line(&json, filetime, WO_VERSION_STRING, event,
                log->movie, NULL)) {
            return;
        }
    }
    json_raw(&json, "\r\n");
    if (!json_ok(&json)) {
        return;
    }
    video_write_file(log->path, json.data, json.length, truncate);
}

void video_log_text(VideoLog *log, const char *event, const char *text)
{
    char storage[1024];
    JsonBuffer json;
    json_init(&json, storage, sizeof(storage));
    if (text == NULL) {
        json_raw(&json, "null");
    } else {
        json_string(&json, text);
    }
    video_log_event(log, event, json_ok(&json) ? json.data : NULL, false);
}

void video_log_startup_error(const wchar_t *mod_root, const char *message)
{
    wchar_t directory[VIDEO_PATH_CAPACITY];
    wchar_t path[VIDEO_PATH_CAPACITY];
    char text[512];
    if (!video_logs_directory(mod_root, directory, VIDEO_PATH_CAPACITY) ||
        !video_join(path, VIDEO_PATH_CAPACITY, directory, L"video-error.log")) {
        return;
    }
    int written = snprintf(text, sizeof(text), "mod_version=%s\r\n%s\r\n",
        WO_VERSION_STRING, message);
    if (written < 0 || (size_t)written >= sizeof(text)) {
        return;
    }
    video_write_file(path, text, (size_t)written, true);
}
