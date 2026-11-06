/* Pure helpers for wo4k-video.exe: movie lookup, command line, breadcrumb
 * JSON, timeouts and controller sampling. Nothing here creates
 * windows or touches COM, so tests/video/test_video_core.c covers it. */
#ifndef WO_VIDEO_CORE_H
#define WO_VIDEO_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#include "runtime/controller_wire.h"

/* Paths are bounded. The game runtime passes roots shorter than MAX_PATH. */
#define VIDEO_PATH_CAPACITY 1024
#define VIDEO_MAX_CANDIDATES 3

/* Probe mode passes once playback has advanced this far (100 ns units). */
#define VIDEO_PROBE_POSITION_HNS 2500000LL

/* Exit codes, documented in src/video/main.c. */
enum {
    VIDEO_EXIT_OK = 0,
    VIDEO_EXIT_FAILED = 1,
    VIDEO_EXIT_USAGE = 2,
    VIDEO_EXIT_UNAVAILABLE = 3
};

typedef enum VideoMovie {
    VIDEO_MOVIE_INTRO,
    VIDEO_MOVIE_XTRO1,
    VIDEO_MOVIE_XTRO2,
    VIDEO_MOVIE_XTRO3,
    VIDEO_MOVIE_XTRO4,
    VIDEO_MOVIE_COUNT
} VideoMovie;

bool video_movie_parse(const wchar_t *name, VideoMovie *movie);
const wchar_t *video_movie_name(VideoMovie movie);

/* File names under <game root>\movies, in priority order. Replacements come
 * first; original_only leaves just the original MPEG-1 file. */
size_t video_movie_candidates(VideoMovie movie, bool original_only,
    const wchar_t *names[VIDEO_MAX_CANDIDATES]);

/* Writes the first existing candidate's full path into out. */
bool video_find_movie(const wchar_t *game_root, VideoMovie movie,
    bool original_only, wchar_t *out, size_t capacity);

bool video_has_extension(const wchar_t *path, const wchar_t *extension);
const wchar_t *video_file_name(const wchar_t *path);

/* Bounded wide-string helpers. Each returns false, leaving out terminated,
 * when the result would not fit. */
bool video_copy(wchar_t *out, size_t capacity, const wchar_t *text);
bool video_append(wchar_t *out, size_t capacity, const wchar_t *text);
bool video_join(wchar_t *out, size_t capacity, const wchar_t *directory,
    const wchar_t *name);

/* Removes trailing separators (keeping a drive root such as C:\). */
void video_trim_separators(wchar_t *path);
/* Replaces path with its parent directory; false when it has none. */
bool video_parent(wchar_t *path);

typedef struct VideoOptions {
    const wchar_t *game_root;
    VideoMovie movie;
    bool has_movie;
    DWORD game_pid;
    HWND owner_window;
    HANDLE skip_event;
    HANDLE start_event; /* prepared mode: open hidden, play when signalled */
    HANDLE stage_event; /* prepared mode: put the window on screen, transparent */
    bool probe;
    bool original_only;
} VideoOptions;

/* Parses argv (argv[0] is the program). On failure *error names the problem
 * in plain ASCII. */
bool video_parse_arguments(int argc, wchar_t **argv, VideoOptions *options,
    const char **error);
bool video_parse_u32(const wchar_t *text, uint32_t *value);

/* JSON text built into a caller-owned buffer. Overflow is sticky: once the
 * buffer is full, nothing more is written and json_ok() returns false. */
typedef struct JsonBuffer {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
    unsigned depth;
    uint32_t has_member;
} JsonBuffer;

void json_init(JsonBuffer *json, char *storage, size_t capacity);
bool json_ok(const JsonBuffer *json);
void json_raw(JsonBuffer *json, const char *text);
void json_string(JsonBuffer *json, const char *utf8);
void json_wide_string(JsonBuffer *json, const wchar_t *text);
void json_object_begin(JsonBuffer *json);
void json_object_end(JsonBuffer *json);
void json_key(JsonBuffer *json, const char *key);
void json_member_string(JsonBuffer *json, const char *key, const char *value);
void json_member_wide(JsonBuffer *json, const char *key, const wchar_t *value);
void json_member_bool(JsonBuffer *json, const char *key, bool value);
void json_member_uint(JsonBuffer *json, const char *key, uint64_t value);
void json_member_fixed(JsonBuffer *json, const char *key, double value,
    int decimals);

/* ISO 8601 UTC with seven fractional digits, as .NET's "o" format writes:
 * 2026-11-06T09:41:00.1234567Z. Needs 29 bytes including the terminator. */
#define VIDEO_TIME_TEXT_SIZE 32
bool video_format_time(uint64_t filetime, char out[VIDEO_TIME_TEXT_SIZE]);

/* One breadcrumb line without its line ending:
 * {"time":...,"mod_version":...,"event":...,"movie":...,"data":<data_json>}
 * data_json must be a complete JSON value; NULL writes null. */
bool video_breadcrumb_line(JsonBuffer *json, uint64_t filetime,
    const char *version, const char *event, const wchar_t *movie,
    const char *data_json);


/* The script's limits: 15 s to open, then max(30, duration + 15) s. */
bool video_timed_out(bool opened, double elapsed_seconds,
    double duration_seconds);

/* Skip detection over the controller host's seqlock-protected block. The
 * first valid sample is the baseline; later, any newly pressed button skips.
 * Torn, disconnected or foreign samples are ignored. */
typedef struct VideoControllerState {
    bool has_baseline;
    DWORD buttons;
} VideoControllerState;

bool video_controller_pressed(VideoControllerState *state,
    const volatile ControllerWire *wire);

#endif
