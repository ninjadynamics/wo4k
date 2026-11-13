#include "core/wo_settings.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"

#define INI_MAX_BYTES (1u << 20)

static const char *skip_space(const char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* A value ends at the line's end or a ";" comment, as the game's Windows
 * profile reader stops there. */
static bool value_ends(const char *s) {
    s = skip_space(s);
    return *s == 0 || *s == ';';
}

/* Section name of a "[name]" line, which may end in a comment, copied into
 * out, or false. */
static bool line_section(const char *line, char *out, size_t cap) {
    const char *s = skip_space(line);
    if (*s != '[') {
        return false;
    }
    const char *close = strchr(s + 1, ']');
    if (!close || close == s + 1 || !value_ends(close + 1)) {
        return false;
    }
    size_t len = (size_t)(close - (s + 1));
    if (len >= cap) {
        return false;
    }
    memcpy(out, s + 1, len);
    out[len] = 0;
    return true;
}

/* Pointer just past "key =" when the line sets key, else NULL. */
static const char *line_value(const char *line, const char *key) {
    const char *s = skip_space(line);
    size_t len = strlen(key);
    if (!wo_str_istarts_with(s, key)) {
        return NULL;
    }
    s = skip_space(s + len);
    if (*s != '=') {
        return NULL;
    }
    return skip_space(s + 1);
}

static char *trimmed_copy(const char *start, size_t len, WoError *error) {
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }
    return wo_strndup(start, len, error);
}

void wo_ini_free(WoIni *ini) {
    wo_strlist_free(&ini->lines);
}

bool wo_ini_parse(const char *text, WoIni *ini, WoError *error) {
    memset(ini, 0, sizeof(*ini));
    const char *line = text;
    while (*line) {
        size_t len = strcspn(line, "\r\n");
        if (!wo_strlist_push_owned(&ini->lines, wo_strndup(line, len, error), error)) {
            wo_ini_free(ini);
            return false;
        }
        line += len;
        if (*line == '\r') {
            line++;
        }
        if (*line == '\n') {
            line++;
        }
    }
    return true;
}

bool wo_ini_load(const char *path, WoIni *ini, WoError *error) {
    memset(ini, 0, sizeof(*ini));
    if (!wo_exists(path)) {
        return true;
    }
    char *text = NULL;
    if (!wo_read_text(path, INI_MAX_BYTES, &text, error)) {
        return false;
    }
    bool ok = wo_ini_parse(text, ini, error);
    wo_free(text);
    return ok;
}

bool wo_ini_save(const char *path, const WoIni *ini, WoError *error) {
    WoBuf out = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < ini->lines.count; i++) {
        ok = wo_buf_append_str(&out, ini->lines.items[i], error) &&
             wo_buf_append_str(&out, "\r\n", error);
    }
    ok = ok && wo_write_file_atomic(path, out.data ? (const char *)out.data : "", out.len, error);
    wo_buf_free(&out);
    return ok;
}

/* [start, end) of the lines belonging to section; section "" is the part
 * before the first header. False when the section does not exist. */
static bool section_range(const WoIni *ini, const char *section, size_t *start, size_t *end) {
    if (!section) {
        *start = 0;
        *end = ini->lines.count;
        return true;
    }
    bool inside = section[0] == 0;
    bool found = inside;
    *start = 0;
    *end = ini->lines.count;
    for (size_t i = 0; i < ini->lines.count; i++) {
        char name[128];
        if (!line_section(ini->lines.items[i], name, sizeof(name))) {
            continue;
        }
        if (inside) {
            *end = i;
            return true;
        }
        if (wo_str_ieq(name, section)) {
            inside = true;
            found = true;
            *start = i + 1;
        }
    }
    return found;
}

static bool insert_line(WoIni *ini, size_t at, char *line, WoError *error) {
    if (!wo_strlist_push_owned(&ini->lines, line, error)) {
        return false;
    }
    memmove(&ini->lines.items[at + 1], &ini->lines.items[at],
            (ini->lines.count - 1 - at) * sizeof(char *));
    ini->lines.items[at] = line;
    return true;
}

static void remove_line(WoIni *ini, size_t at) {
    wo_free(ini->lines.items[at]);
    memmove(&ini->lines.items[at], &ini->lines.items[at + 1],
            (ini->lines.count - at - 1) * sizeof(char *));
    ini->lines.count--;
}

static bool append_section(WoIni *ini, const char *section, char *line, WoError *error) {
    size_t count = ini->lines.count;
    bool ok = true;
    if (count > 0 && ini->lines.items[count - 1][0] != 0) {
        ok = wo_strlist_push(&ini->lines, "", error);
    }
    if (ok && section && section[0]) {
        ok = wo_strlist_push_owned(&ini->lines, wo_format(error, "[%s]", section), error);
    }
    if (!ok) {
        wo_free(line);
        return false;
    }
    return wo_strlist_push_owned(&ini->lines, line, error);
}

/* Insert after the last non-blank line of the range. */
static bool insert_in_range(WoIni *ini, size_t start, size_t end, char *line, WoError *error) {
    size_t at = end;
    while (at > start && ini->lines.items[at - 1][0] == 0) {
        at--;
    }
    return insert_line(ini, at, line, error);
}

char *wo_ini_get(const WoIni *ini, const char *section, const char *key, WoError *error) {
    size_t start;
    size_t end;
    if (!section_range(ini, section, &start, &end)) {
        return NULL;
    }
    for (size_t i = start; i < end; i++) {
        const char *value = line_value(ini->lines.items[i], key);
        if (value) {
            return trimmed_copy(value, strlen(value), error);
        }
    }
    return NULL;
}

bool wo_ini_set(WoIni *ini, const char *section, const char *key, const char *value,
                const char *separator, WoError *error) {
    char *line = wo_format(error, "%s%s%s", key, separator, value);
    if (!line) {
        return false;
    }
    size_t start;
    size_t end;
    if (!section_range(ini, section, &start, &end)) {
        return append_section(ini, section, line, error);
    }
    bool replaced = false;
    for (size_t i = end; i > start; i--) {
        if (!line_value(ini->lines.items[i - 1], key)) {
            continue;
        }
        if (replaced) {
            remove_line(ini, i - 1);
        } else {
            wo_free(ini->lines.items[i - 1]);
            ini->lines.items[i - 1] = line;
            replaced = true;
        }
    }
    return replaced || insert_in_range(ini, start, end, line, error);
}

bool wo_ini_replace_existing(WoIni *ini, const char *key, const char *value,
                             const char *separator, WoError *error) {
    bool found = false;
    for (size_t i = 0; i < ini->lines.count; i++) {
        if (line_value(ini->lines.items[i], key)) {
            char *line = wo_format(error, "%s%s%s", key, separator, value);
            if (!line) {
                return false;
            }
            wo_free(ini->lines.items[i]);
            ini->lines.items[i] = line;
            found = true;
        }
    }
    return found;
}

bool wo_ini_set_everywhere(WoIni *ini, const char *key, const char *value,
                           const char *separator, const char *fallback_section, WoError *error) {
    wo_error_clear(error);
    if (wo_ini_replace_existing(ini, key, value, separator, error)) {
        return true;
    }
    if (wo_error_is_set(error)) {
        return false;
    }
    return wo_ini_set(ini, fallback_section, key, value, separator, error);
}

static bool is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_alnum(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

/* Key of a "key=value" line (trimmed, newly allocated) or NULL.
 * identifier_only: the key must be a letter followed by letters/digits
 * (DDrawCompat style); otherwise any text without ';', '#' or '['. */
static char *line_key(const char *line, bool identifier_only) {
    const char *start = skip_space(line);
    const char *equals = strchr(start, '=');
    if (!equals || equals == start) {
        return NULL;
    }
    const char *end = equals;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    if (end == start) {
        return NULL;
    }
    for (const char *p = start; p < end; p++) {
        bool ok = identifier_only ? (p == start ? is_alpha(*p) : is_alnum(*p))
                                  : (*p != ';' && *p != '#' && *p != '[');
        if (!ok) {
            return NULL;
        }
    }
    return wo_strndup(start, (size_t)(end - start), NULL);
}

bool wo_ini_merge_defaults(WoIni *ini, const WoIni *defaults, bool sectioned, int *added,
                           WoError *error) {
    *added = 0;
    char section[128] = "";
    for (size_t i = 0; i < defaults->lines.count; i++) {
        const char *line = defaults->lines.items[i];
        char name[128];
        if (line_section(line, name, sizeof(name))) {
            wo_copy_str(section, sizeof(section), name);
            continue;
        }
        char *key = line_key(line, !sectioned);
        if (!key) {
            continue;
        }
        size_t start;
        size_t end;
        bool has_section = section_range(ini, sectioned ? section : NULL, &start, &end);
        bool present = false;
        for (size_t j = start; has_section && j < end && !present; j++) {
            present = line_value(ini->lines.items[j], key) != NULL;
        }
        wo_free(key);
        if (present) {
            continue;
        }
        char *copy = wo_strdup(line, error);
        bool ok = copy && (has_section ? insert_in_range(ini, start, end, copy, error)
                                       : append_section(ini, sectioned ? section : NULL, copy,
                                                        error));
        if (!ok) {
            return false;
        }
        (*added)++;
    }
    return true;
}

/* --- Game settings ------------------------------------------------------------- */

const char *wo_fullscreen_name(WoFullscreen mode) {
    return mode == WO_FULLSCREEN_EXCLUSIVE ? "exclusive" : "borderless";
}

bool wo_fullscreen_parse(const char *name, WoFullscreen *mode) {
    if (wo_str_ieq(name, "borderless")) {
        *mode = WO_FULLSCREEN_BORDERLESS;
        return true;
    }
    if (wo_str_ieq(name, "exclusive")) {
        *mode = WO_FULLSCREEN_EXCLUSIVE;
        return true;
    }
    return false;
}

const char *wo_soundtrack_name(WoSoundtrack soundtrack) {
    return soundtrack == WO_SOUNDTRACK_PSX ? "Psx" : soundtrack == WO_SOUNDTRACK_NONE ? "None" : "Pc";
}

bool wo_soundtrack_parse(const char *name, WoSoundtrack *soundtrack) {
    if (wo_str_eq(name, "Pc")) {
        *soundtrack = WO_SOUNDTRACK_PC;
    } else if (wo_str_eq(name, "Psx")) {
        *soundtrack = WO_SOUNDTRACK_PSX;
    } else if (wo_str_eq(name, "None")) {
        *soundtrack = WO_SOUNDTRACK_NONE;
    } else {
        return false;
    }
    return true;
}

bool wo_sound_mode_valid(int mode) {
    return mode == 1 || mode == 2 || mode == 5;
}

/* "exclusive", "exclusive(0)" or "exclusive( 1 )", optionally followed by a
 * '#' comment. */
static bool fullscreen_value(const char *value, WoFullscreen *mode) {
    char text[64];
    size_t len = strcspn(value, "#");
    if (len >= sizeof(text)) {
        return false;
    }
    memcpy(text, value, len);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        len--;
    }
    text[len] = 0;
    if (wo_str_ieq(text, "borderless")) {
        *mode = WO_FULLSCREEN_BORDERLESS;
        return true;
    }
    if (!wo_str_istarts_with(text, "exclusive")) {
        return false;
    }
    const char *rest = skip_space(text + 9);
    if (*rest == 0) {
        *mode = WO_FULLSCREEN_EXCLUSIVE;
        return true;
    }
    if (*rest != '(') {
        return false;
    }
    rest = skip_space(rest + 1);
    if (*rest != '0' && *rest != '1') {
        return false;
    }
    rest = skip_space(rest + 1);
    if (*rest != ')' || *skip_space(rest + 1) != 0) {
        return false;
    }
    *mode = WO_FULLSCREEN_EXCLUSIVE;
    return true;
}

WoFullscreen wo_settings_get_fullscreen(const char *ddraw_ini) {
    WoIni ini;
    WoFullscreen mode = WO_FULLSCREEN_BORDERLESS;
    if (!wo_ini_load(ddraw_ini, &ini, NULL)) {
        return mode;
    }
    for (size_t i = 0; i < ini.lines.count; i++) {
        const char *value = line_value(ini.lines.items[i], "FullscreenMode");
        if (value && fullscreen_value(value, &mode)) {
            break;
        }
    }
    wo_ini_free(&ini);
    return mode;
}

bool wo_settings_set_fullscreen(const char *ddraw_ini, WoFullscreen mode, WoError *error) {
    WoIni ini;
    if (!wo_ini_load(ddraw_ini, &ini, error)) {
        return false;
    }
    bool ok = wo_ini_set(&ini, NULL, "FullscreenMode", wo_fullscreen_name(mode), " = ", error) &&
              wo_ini_save(ddraw_ini, &ini, error);
    wo_ini_free(&ini);
    return ok;
}

int wo_settings_get_sound_mode(const char *game_ini) {
    WoIni ini;
    int mode = 2;
    if (!wo_ini_load(game_ini, &ini, NULL)) {
        return mode;
    }
    size_t start;
    size_t end;
    if (section_range(&ini, "Audio", &start, &end)) {
        for (size_t i = start; i < end; i++) {
            const char *value = line_value(ini.lines.items[i], "SoundMode");
            if (!value) {
                continue;
            }
            char digit = value[0];
            if ((digit == '1' || digit == '2' || digit == '5') && value_ends(value + 1)) {
                mode = digit - '0';
                break;
            }
        }
    }
    wo_ini_free(&ini);
    return mode;
}

bool wo_settings_set_sound_mode(const char *game_ini, int mode, WoError *error) {
    if (!wo_sound_mode_valid(mode)) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "Invalid audio output mode %d.", mode);
    }
    WoIni ini;
    if (!wo_ini_load(game_ini, &ini, error)) {
        return false;
    }
    char value[2] = {(char)('0' + mode), 0};
    bool ok = wo_ini_set(&ini, "Audio", "SoundMode", value, "=", error) &&
              wo_ini_save(game_ini, &ini, error);
    wo_ini_free(&ini);
    return ok;
}

int wo_settings_get_render(const char *game_ini, const char *key, int limit, int fallback) {
    WoIni ini;
    if (!wo_ini_load(game_ini, &ini, NULL)) {
        return fallback;
    }
    char *text = wo_ini_get(&ini, "Render", key, NULL);
    int value = 0;
    bool number = text && text[0];
    for (const char *c = text; number && *c; c++) {
        number = *c >= '0' && *c <= '9' && value <= limit;
        value = value * 10 + (*c - '0');
    }
    wo_free(text);
    wo_ini_free(&ini);
    return number && value <= limit ? value : fallback;
}

bool wo_settings_set_render(const char *game_ini, const char *key, int limit, int value,
                            WoError *error) {
    if (value < 0 || value > limit) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "Invalid %s setting %d.", key, value);
    }
    WoIni ini;
    if (!wo_ini_load(game_ini, &ini, error)) {
        return false;
    }
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    bool ok = wo_ini_set(&ini, "Render", key, text, "=", error) &&
              wo_ini_save(game_ini, &ini, error);
    wo_ini_free(&ini);
    return ok;
}

bool wo_settings_apply_soundtrack(const char *game_ini, WoSoundtrack soundtrack, bool was_none,
                                  WoError *error) {
    WoIni ini;
    if (!wo_ini_load(game_ini, &ini, error)) {
        return false;
    }
    bool ok = wo_ini_set_everywhere(&ini, "Soundtrack",
                                    soundtrack == WO_SOUNDTRACK_PSX ? "1" : "0", "=", "Audio",
                                    error);
    if (ok && soundtrack == WO_SOUNDTRACK_NONE) {
        wo_ini_replace_existing(&ini, "MusicVolume", "0", "=", error);
    } else if (ok && was_none) {
        wo_ini_replace_existing(&ini, "MusicVolume", "100", "=", error);
    }
    ok = ok && !wo_error_is_set(error) && wo_ini_save(game_ini, &ini, error);
    wo_ini_free(&ini);
    return ok;
}

bool wo_settings_set_log_level(const char *ddraw_ini, const char *level, WoError *error) {
    WoIni ini;
    if (!wo_ini_load(ddraw_ini, &ini, error)) {
        return false;
    }
    bool ok = wo_ini_set_everywhere(&ini, "LogLevel", level, " = ", NULL, error) &&
              wo_ini_save(ddraw_ini, &ini, error);
    wo_ini_free(&ini);
    return ok;
}

static bool allowed_key(const char *key) {
    static const char *allowed[] = {
        "EngineTrails",  "GeometryWidthPercent", "FloatRotations",   "SoftShadows",
        "ShadowOpacityPercent", "ShadowSoftnessPercent", "GlowAftOffset", "GlowUpOffset",
        "ShowFPS",       "ShadowLiftPercent",    "HighlightsPercent", "Soundtrack",
        "MusicVolume",   "EffectsVolume",        "Track",            "SoundMode",
        "EnableDDrawCompat", "LogLevel",         "DisplayResolution", "DisplayAspectRatio",
        "FullscreenMode", "AltTabFix",           "ResolutionScale",  "RenderColorDepth",
        "Antialiasing",  "TextureFilter",        "ColorKeyMethod",   "FpsLimiter",
        "VertexFixup",   "VertexBufferMemoryType", "SpriteDetection", "SpriteFilter",
        "VSync",         "PresentDelay",         "CpuAffinity",      "CpuAffinityRotation",
        "BltFilter"};
    for (size_t i = 0; i < WO_COUNT(allowed); i++) {
        if (wo_str_eq(key, allowed[i])) {
            return true;
        }
    }
    return false;
}

static bool allowed_value(const char *value) {
    size_t len = 0;
    for (const char *p = value; *p; p++, len++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  strchr("_().,% +:-", c) != NULL;
        if (!ok || len >= 100) {
            return false;
        }
    }
    return true;
}

WoJson *wo_settings_snapshot(const char *game_root) {
    static const char *files[] = {"Wipeout2097.ini", "DDrawCompat.ini",
                                  "DDrawCompat-Wipeout2097.ini"};
    WoJson *list = wo_json_new_array();
    for (size_t f = 0; f < WO_COUNT(files); f++) {
        char *path = wo_path_join(game_root, files[f], NULL);
        WoIni ini;
        if (!path || !wo_exists(path) || !wo_ini_load(path, &ini, NULL)) {
            wo_free(path);
            continue;
        }
        WoJson *settings = wo_json_new_object();
        char section[128] = "";
        for (size_t i = 0; i < ini.lines.count; i++) {
            const char *line = ini.lines.items[i];
            char name[128];
            if (line_section(line, name, sizeof(name))) {
                wo_copy_str(section, sizeof(section), name);
                continue;
            }
            char *key = line_key(line, true);
            if (!key) {
                continue;
            }
            const char *raw = line_value(line, key);
            size_t len = raw ? strcspn(raw, ";#") : 0;
            char *value = raw ? trimmed_copy(raw, len, NULL) : NULL;
            if (value && allowed_key(key) && allowed_value(value)) {
                char *qualified = wo_format(NULL, "%s/%s", section, key);
                if (qualified) {
                    wo_json_put_str(settings, qualified, value);
                }
                wo_free(qualified);
            }
            wo_free(value);
            wo_free(key);
        }
        WoJson *entry = wo_json_new_object();
        wo_json_put_str(entry, "file", files[f]);
        wo_json_put(entry, "settings", settings);
        wo_json_push(list, entry);
        wo_ini_free(&ini);
        wo_free(path);
    }
    return list;
}
