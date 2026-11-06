#include "video/video_core.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

_Static_assert(sizeof(ControllerWire) == 36, "controller wire layout");

static const wchar_t *const movie_names[VIDEO_MOVIE_COUNT] = {
    L"intro", L"xtro1", L"xtro2", L"xtro3", L"xtro4"
};

/* Explicit names keep drop-in replacements local to the movies folder. */
static const wchar_t *const movie_mp4[VIDEO_MOVIE_COUNT] = {
    L"WXLINTR.mp4", L"XTRO1.mp4", L"XTRO2.mp4", L"XTRO3.mp4", L"XTRO4.mp4"
};

static const wchar_t *const movie_mpg[VIDEO_MOVIE_COUNT] = {
    L"WXLINTR.MPG", L"XTRO1.MPG", L"XTRO2.MPG", L"XTRO3.MPG", L"XTRO4.MPG"
};

bool video_movie_parse(const wchar_t *name, VideoMovie *movie)
{
    if (name == NULL) {
        return false;
    }
    for (int i = 0; i < VIDEO_MOVIE_COUNT; ++i) {
        if (wcscmp(name, movie_names[i]) == 0) {
            *movie = (VideoMovie)i;
            return true;
        }
    }
    return false;
}

const wchar_t *video_movie_name(VideoMovie movie)
{
    if ((int)movie < 0 || movie >= VIDEO_MOVIE_COUNT) {
        return NULL;
    }
    return movie_names[movie];
}

size_t video_movie_candidates(VideoMovie movie, bool original_only,
    const wchar_t *names[VIDEO_MAX_CANDIDATES])
{
    size_t count = 0;
    if ((int)movie < 0 || movie >= VIDEO_MOVIE_COUNT) {
        return 0;
    }
    if (!original_only) {
        if (movie == VIDEO_MOVIE_INTRO) {
            names[count] = L"intro.mp4";
            ++count;
        }
        names[count] = movie_mp4[movie];
        ++count;
    }
    names[count] = movie_mpg[movie];
    ++count;
    return count;
}

bool video_copy(wchar_t *out, size_t capacity, const wchar_t *text)
{
    if (capacity == 0) {
        return false;
    }
    out[0] = L'\0';
    return video_append(out, capacity, text);
}

bool video_append(wchar_t *out, size_t capacity, const wchar_t *text)
{
    size_t length = wcsnlen(out, capacity);
    size_t extra = wcslen(text);
    if (length >= capacity || extra >= capacity - length) {
        return false;
    }
    memcpy(out + length, text, (extra + 1) * sizeof(*text));
    return true;
}

static bool video_is_separator(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

bool video_join(wchar_t *out, size_t capacity, const wchar_t *directory,
    const wchar_t *name)
{
    if (!video_copy(out, capacity, directory)) {
        return false;
    }
    size_t length = wcslen(out);
    if (length > 0 && !video_is_separator(out[length - 1])) {
        if (!video_append(out, capacity, L"\\")) {
            return false;
        }
    }
    return video_append(out, capacity, name);
}

void video_trim_separators(wchar_t *path)
{
    size_t length = wcslen(path);
    while (length > 0 && video_is_separator(path[length - 1])) {
        /* Keep the separator of a drive root such as C:\. */
        if (length == 3 && path[1] == L':') {
            break;
        }
        --length;
        path[length] = L'\0';
    }
}

bool video_parent(wchar_t *path)
{
    video_trim_separators(path);
    size_t length = wcslen(path);
    size_t cut = length;
    while (cut > 0 && !video_is_separator(path[cut - 1])) {
        --cut;
    }
    if (cut == 0 || cut == length) {
        return false;
    }
    path[cut] = L'\0';
    video_trim_separators(path);
    return true;
}

const wchar_t *video_file_name(const wchar_t *path)
{
    const wchar_t *name = path;
    for (const wchar_t *at = path; *at != L'\0'; ++at) {
        if (video_is_separator(*at) || *at == L':') {
            name = at + 1;
        }
    }
    return name;
}

bool video_has_extension(const wchar_t *path, const wchar_t *extension)
{
    size_t length = wcslen(path);
    size_t extension_length = wcslen(extension);
    if (extension_length > length) {
        return false;
    }
    return _wcsicmp(path + length - extension_length, extension) == 0;
}

static bool video_is_file(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool video_find_movie(const wchar_t *game_root, VideoMovie movie,
    bool original_only, wchar_t *out, size_t capacity)
{
    const wchar_t *names[VIDEO_MAX_CANDIDATES];
    wchar_t directory[VIDEO_PATH_CAPACITY];
    size_t count = video_movie_candidates(movie, original_only, names);
    if (capacity > 0) {
        out[0] = L'\0';
    }
    if (!video_join(directory, VIDEO_PATH_CAPACITY, game_root, L"movies")) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!video_join(out, capacity, directory, names[i])) {
            continue;
        }
        if (video_is_file(out)) {
            return true;
        }
    }
    if (capacity > 0) {
        out[0] = L'\0';
    }
    return false;
}

bool video_parse_u32(const wchar_t *text, uint32_t *value)
{
    uint64_t result = 0;
    if (text == NULL || *text == L'\0') {
        return false;
    }
    for (const wchar_t *at = text; *at != L'\0'; ++at) {
        if (*at < L'0' || *at > L'9') {
            return false;
        }
        result = result * 10 + (uint64_t)(*at - L'0');
        if (result > UINT32_MAX) {
            return false;
        }
    }
    *value = (uint32_t)result;
    return true;
}

/* 32-bit processes pass handles as 32-bit values; widen them the way
 * LongToHandle does. */
static HANDLE video_handle_from_u32(uint32_t value)
{
    return (HANDLE)(LONG_PTR)(LONG)value;
}

bool video_parse_arguments(int argc, wchar_t **argv, VideoOptions *options,
    const char **error)
{
    memset(options, 0, sizeof(*options));
    *error = NULL;
    for (int i = 1; i < argc; ++i) {
        const wchar_t *name = argv[i];
        if (wcscmp(name, L"--probe") == 0) {
            options->probe = true;
            continue;
        }
        if (wcscmp(name, L"--original-only") == 0) {
            options->original_only = true;
            continue;
        }
        bool takes_value = wcscmp(name, L"--game-root") == 0 ||
            wcscmp(name, L"--movie") == 0 ||
            wcscmp(name, L"--game-pid") == 0 ||
            wcscmp(name, L"--owner-window") == 0 ||
            wcscmp(name, L"--skip-event") == 0;
        if (!takes_value) {
            *error = "unknown argument";
            return false;
        }
        if (i + 1 >= argc) {
            *error = "missing argument value";
            return false;
        }
        ++i;
        const wchar_t *value = argv[i];
        uint32_t number = 0;
        if (wcscmp(name, L"--game-root") == 0) {
            if (*value == L'\0') {
                *error = "empty --game-root";
                return false;
            }
            options->game_root = value;
        } else if (wcscmp(name, L"--movie") == 0) {
            if (!video_movie_parse(value, &options->movie)) {
                *error = "unknown --movie";
                return false;
            }
            options->has_movie = true;
        } else if (!video_parse_u32(value, &number)) {
            *error = "invalid number";
            return false;
        } else if (wcscmp(name, L"--game-pid") == 0) {
            options->game_pid = number;
        } else if (wcscmp(name, L"--owner-window") == 0) {
            options->owner_window = (HWND)video_handle_from_u32(number);
        } else {
            options->skip_event = video_handle_from_u32(number);
        }
    }
    if (options->game_root == NULL) {
        *error = "missing --game-root";
        return false;
    }
    if (!options->has_movie) {
        *error = "missing --movie";
        return false;
    }
    return true;
}

void json_init(JsonBuffer *json, char *storage, size_t capacity)
{
    json->data = storage;
    json->capacity = capacity;
    json->length = 0;
    json->overflow = capacity == 0;
    json->depth = 0;
    json->has_member = 0;
    if (capacity > 0) {
        storage[0] = '\0';
    }
}

bool json_ok(const JsonBuffer *json)
{
    return !json->overflow;
}

static void json_bytes(JsonBuffer *json, const char *bytes, size_t count)
{
    if (json->overflow) {
        return;
    }
    if (count >= json->capacity - json->length) {
        json->overflow = true;
        return;
    }
    memcpy(json->data + json->length, bytes, count);
    json->length += count;
    json->data[json->length] = '\0';
}

static void json_char(JsonBuffer *json, char c)
{
    json_bytes(json, &c, 1);
}

void json_raw(JsonBuffer *json, const char *text)
{
    json_bytes(json, text, strlen(text));
}

static void json_escape_unit(JsonBuffer *json, uint32_t c)
{
    static const char hex[] = "0123456789abcdef";
    char escape[6] = { '\\', 'u', '0', '0', '0', '0' };
    escape[4] = hex[(c >> 4) & 15u];
    escape[5] = hex[c & 15u];
    json_bytes(json, escape, sizeof(escape));
}

/* Escapes one code point; non-ASCII is written as UTF-8. */
static void json_code_point(JsonBuffer *json, uint32_t c)
{
    char bytes[4];
    if (c == '"' || c == '\\') {
        json_char(json, '\\');
        json_char(json, (char)c);
    } else if (c == '\n') {
        json_raw(json, "\\n");
    } else if (c == '\r') {
        json_raw(json, "\\r");
    } else if (c == '\t') {
        json_raw(json, "\\t");
    } else if (c < 0x20 || c == 0x7f) {
        json_escape_unit(json, c);
    } else if (c < 0x80) {
        json_char(json, (char)c);
    } else if (c < 0x800) {
        bytes[0] = (char)(0xc0 | (c >> 6));
        bytes[1] = (char)(0x80 | (c & 0x3f));
        json_bytes(json, bytes, 2);
    } else if (c < 0x10000) {
        bytes[0] = (char)(0xe0 | (c >> 12));
        bytes[1] = (char)(0x80 | ((c >> 6) & 0x3f));
        bytes[2] = (char)(0x80 | (c & 0x3f));
        json_bytes(json, bytes, 3);
    } else {
        bytes[0] = (char)(0xf0 | (c >> 18));
        bytes[1] = (char)(0x80 | ((c >> 12) & 0x3f));
        bytes[2] = (char)(0x80 | ((c >> 6) & 0x3f));
        bytes[3] = (char)(0x80 | (c & 0x3f));
        json_bytes(json, bytes, 4);
    }
}

void json_string(JsonBuffer *json, const char *utf8)
{
    json_char(json, '"');
    for (const unsigned char *at = (const unsigned char *)utf8; *at != 0; ++at) {
        if (*at >= 0x80) {
            /* Already UTF-8; copy the byte unchanged. */
            json_char(json, (char)*at);
        } else {
            json_code_point(json, *at);
        }
    }
    json_char(json, '"');
}

void json_wide_string(JsonBuffer *json, const wchar_t *text)
{
    json_char(json, '"');
    for (const wchar_t *at = text; *at != L'\0'; ++at) {
        uint32_t c = (uint32_t)*at;
        if (c >= 0xd800 && c <= 0xdbff && at[1] >= 0xdc00 && at[1] <= 0xdfff) {
            c = 0x10000 + ((c - 0xd800) << 10) + ((uint32_t)at[1] - 0xdc00);
            ++at;
        } else if (c >= 0xd800 && c <= 0xdfff) {
            /* An unpaired surrogate has no UTF-8 form. */
            c = 0xfffd;
        }
        json_code_point(json, c);
    }
    json_char(json, '"');
}

void json_object_begin(JsonBuffer *json)
{
    json_char(json, '{');
    if (json->depth >= 31) {
        json->overflow = true;
        return;
    }
    ++json->depth;
    json->has_member &= ~(1u << json->depth);
}

void json_object_end(JsonBuffer *json)
{
    if (json->depth > 0) {
        --json->depth;
    }
    json_char(json, '}');
}

void json_key(JsonBuffer *json, const char *key)
{
    uint32_t bit = 1u << json->depth;
    if ((json->has_member & bit) != 0) {
        json_char(json, ',');
    }
    json->has_member |= bit;
    json_string(json, key);
    json_char(json, ':');
}

void json_member_string(JsonBuffer *json, const char *key, const char *value)
{
    json_key(json, key);
    if (value == NULL) {
        json_raw(json, "null");
    } else {
        json_string(json, value);
    }
}

void json_member_wide(JsonBuffer *json, const char *key, const wchar_t *value)
{
    json_key(json, key);
    if (value == NULL) {
        json_raw(json, "null");
    } else {
        json_wide_string(json, value);
    }
}

void json_member_bool(JsonBuffer *json, const char *key, bool value)
{
    json_key(json, key);
    json_raw(json, value ? "true" : "false");
}

void json_member_uint(JsonBuffer *json, const char *key, uint64_t value)
{
    char digits[24];
    int written = snprintf(digits, sizeof(digits), "%llu",
        (unsigned long long)value);
    json_key(json, key);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        json->overflow = true;
        return;
    }
    json_raw(json, digits);
}

void json_member_fixed(JsonBuffer *json, const char *key, double value,
    int decimals)
{
    char digits[48];
    if (!(value >= -1e15 && value <= 1e15)) {
        /* NaN or a nonsensical duration: JSON has no representation. */
        value = 0;
    }
    int written = snprintf(digits, sizeof(digits), "%.*f", decimals, value);
    json_key(json, key);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        json->overflow = true;
        return;
    }
    json_raw(json, digits);
}

bool video_format_time(uint64_t filetime, char out[VIDEO_TIME_TEXT_SIZE])
{
    FILETIME file;
    SYSTEMTIME system;
    file.dwLowDateTime = (DWORD)(filetime & 0xffffffffu);
    file.dwHighDateTime = (DWORD)(filetime >> 32);
    out[0] = '\0';
    if (!FileTimeToSystemTime(&file, &system)) {
        return false;
    }
    unsigned fraction = (unsigned)(filetime % 10000000u);
    int written = snprintf(out, VIDEO_TIME_TEXT_SIZE,
        "%04u-%02u-%02uT%02u:%02u:%02u.%07uZ",
        (unsigned)system.wYear, (unsigned)system.wMonth,
        (unsigned)system.wDay, (unsigned)system.wHour,
        (unsigned)system.wMinute, (unsigned)system.wSecond, fraction);
    return written > 0 && written < VIDEO_TIME_TEXT_SIZE;
}

bool video_breadcrumb_line(JsonBuffer *json, uint64_t filetime,
    const char *version, const char *event, const wchar_t *movie,
    const char *data_json)
{
    char time_text[VIDEO_TIME_TEXT_SIZE];
    if (!video_format_time(filetime, time_text)) {
        return false;
    }
    json_object_begin(json);
    json_member_string(json, "time", time_text);
    json_member_string(json, "mod_version", version);
    json_member_string(json, "event", event);
    json_member_wide(json, "movie", movie);
    json_key(json, "data");
    json_raw(json, data_json != NULL ? data_json : "null");
    json_object_end(json);
    return json_ok(json);
}

bool video_timed_out(bool opened, double elapsed_seconds,
    double duration_seconds)
{
    if (!opened) {
        return elapsed_seconds > 15.0;
    }
    double limit = duration_seconds + 15.0;
    if (limit < 30.0) {
        limit = 30.0;
    }
    return elapsed_seconds > limit;
}

bool video_controller_pressed(VideoControllerState *state,
    const volatile ControllerWire *wire)
{
    if (wire == NULL) {
        return false;
    }
    LONG before = wire->sequence;
    if ((before & 1) != 0) {
        return false;
    }
    MemoryBarrier();
    DWORD magic = wire->magic;
    DWORD connected = wire->connected;
    DWORD buttons = wire->buttons;
    MemoryBarrier();
    if (wire->sequence != before) {
        return false;
    }
    if (magic != CONTROLLER_MAGIC || connected == 0) {
        return false;
    }
    bool pressed = state->has_baseline && (buttons & ~state->buttons) != 0;
    state->has_baseline = true;
    state->buttons = buttons;
    return pressed;
}
