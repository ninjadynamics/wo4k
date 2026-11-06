#include "core/wo_sources.h"

#include <stdio.h>
#include <string.h>

#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_tree.h"
#include "core/wo_zip.h"

#define SOURCE_ZIP_LIMIT (12ull << 30)
#define NATIVE_SEARCH_DEPTH 3
/* .NET DateTime ticks start in year 1; FILETIME starts in 1601. */
#define DOTNET_TICKS_AT_1601 504911232000000000ull

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* "TrackNN" (exactly two digits), any case. */
static bool track_folder(const char *name, size_t len) {
    return len == 7 && wo_str_istarts_with(name, "track") && is_digit(name[5]) && is_digit(name[6]);
}

static bool name_is(const char *name, size_t len, const char *expected) {
    return strlen(expected) == len && wo_str_istarts_with(name, expected);
}

static bool movie_name(const char *name) {
    static const char *const stems[] = {"WXLINTR", "XTRO1", "XTRO2", "XTRO3", "XTRO4"};
    for (size_t i = 0; i < WO_COUNT(stems); i++) {
        size_t len = strlen(stems[i]);
        if (wo_str_istarts_with(name, stems[i]) &&
            (wo_str_ieq(name + len, ".MPG") || wo_str_ieq(name + len, ".mp4"))) {
            return true;
        }
    }
    return wo_str_ieq(name, "intro.mp4");
}

static bool executable_name(const char *name) {
    return wo_str_ieq(name, "Wipeout2.exe") || wo_str_ieq(name, "Wipeout2_HighTest.exe") ||
           wo_str_ieq(name, "Wipeout2097.exe") || wo_str_ieq(name, "D3D.exe");
}

bool wo_game_file(const char *relative) {
    const char *slash = strchr(relative, '/');
    if (!slash) {
        return executable_name(relative) || wo_str_ieq(relative, "CONFIG.DAT") ||
               wo_str_ieq(relative, "SAVEGAME.BIN") || wo_str_ieq(relative, "Wipeout2097.ini");
    }
    size_t top = (size_t)(slash - relative);
    const char *rest = slash + 1;
    if ((name_is(relative, top, "MPEG") || name_is(relative, top, "movies")) && !strchr(rest, '/') &&
        movie_name(rest)) {
        return !wo_str_ieq(rest, "intro.mp4") || name_is(relative, top, "movies");
    }
    return name_is(relative, top, "Common") || name_is(relative, top, "Blitsurf") ||
           name_is(relative, top, "Pics") || name_is(relative, top, "Sound") ||
           name_is(relative, top, "Textures") || track_folder(relative, top);
}

static char *random_child(const char *work, const char *prefix, WoError *error) {
    uint8_t random[8];
    if (!wo_random_bytes(random, sizeof(random), error)) {
        return NULL;
    }
    char name[48];
    snprintf(name, sizeof(name), "%s%02x%02x%02x%02x%02x%02x%02x%02x", prefix, random[0], random[1],
             random[2], random[3], random[4], random[5], random[6], random[7]);
    return wo_path_child(work, name, error);
}

char *wo_resolve_input(const char *source, const char *work, WoError *error) {
    char *full = wo_path_full(source, error);
    if (!full) {
        return NULL;
    }
    if (wo_str_iends_with(full, ".zip") && wo_is_file(full)) {
        char *destination = random_child(work, "", error);
        WoZip *zip = NULL;
        WoZipLimits limits = {SOURCE_ZIP_LIMIT, 0, false};
        bool ok = destination && wo_zip_open(full, &zip, error) &&
                  wo_zip_extract_all(zip, destination, &limits, error);
        wo_zip_close(zip);
        wo_free(full);
        if (!ok) {
            wo_free(destination);
            return NULL;
        }
        return destination;
    }
    if (!wo_exists(full)) {
        wo_fail(error, WO_ERR_NOT_FOUND, "Source does not exist: %s", full);
        wo_free(full);
        return NULL;
    }
    return full;
}

bool wo_supported_executable(const WoJson *manifest, const char *sha256) {
    if (wo_digest_eq(wo_json_str(manifest, "output_sha256", ""), sha256)) {
        return true;
    }
    const WoJson *list = wo_json_get(manifest, "executables");
    for (size_t i = 0; i < wo_json_length(list); i++) {
        if (wo_digest_eq(wo_json_str(wo_json_at(list, i), "source_sha256", ""), sha256)) {
            return true;
        }
    }
    return false;
}

static bool filter_game_file(void *context, const char *relative) {
    (void)context;
    return wo_game_file(relative);
}

static char *native_root_inner(const char *source, const char *work, const WoJson *manifest,
                               int depth, WoError *error) {
    char *input = wo_resolve_input(source, work, error);
    if (!input) {
        return NULL;
    }
    if (!wo_is_dir(input)) {
        char *image = NULL;
        uint64_t offset = 0;
        char *destination = random_child(work, "native-", error);
        bool ok = destination && wo_disc_data_track(input, &image, &offset, error) &&
                  wo_disc_import(image, offset, destination, filter_game_file, NULL, NULL, error);
        wo_free(image);
        wo_free(input);
        if (!ok) {
            wo_free(destination);
            return NULL;
        }
        input = destination;
    }
    WoTreeList files = {0};
    if (!wo_tree_list(input, &files, error)) {
        wo_free(input);
        return NULL;
    }
    char *found = NULL;
    for (size_t i = 0; !found && i < files.count; i++) {
        const WoTreeEntry *entry = &files.items[i];
        if (entry->is_dir || !executable_name(wo_path_basename(entry->path))) {
            continue;
        }
        char digest[WO_SHA256_HEX_SIZE];
        char *folder = wo_path_parent(entry->path, NULL);
        char *common = folder ? wo_path_join(folder, "Common", NULL) : NULL;
        char *track = folder ? wo_path_join(folder, "Track01", NULL) : NULL;
        if (folder && common && track && wo_is_dir(common) && wo_is_dir(track) &&
            wo_sha256_file(entry->path, digest, NULL) && wo_supported_executable(manifest, digest)) {
            found = folder;
            folder = NULL;
        }
        wo_free(folder);
        wo_free(common);
        wo_free(track);
    }
    /* Disc images inside a folder or ZIP (an ISO set inside an archive). */
    for (size_t i = 0; !found && depth < NATIVE_SEARCH_DEPTH && i < files.count; i++) {
        const WoTreeEntry *entry = &files.items[i];
        const char *extension = wo_path_extension(entry->path);
        if (entry->is_dir || !(wo_str_ieq(extension, ".mds") || wo_str_ieq(extension, ".cue") ||
                               wo_str_ieq(extension, ".iso"))) {
            continue;
        }
        char *nested_work = random_child(work, "nested-", NULL);
        if (nested_work && wo_mkdirs(nested_work, NULL)) {
            found = native_root_inner(entry->path, nested_work, manifest, depth + 1, NULL);
        }
        wo_free(nested_work);
    }
    wo_tree_free(&files);
    wo_free(input);
    if (!found) {
        wo_fail(error, WO_ERR_NOT_FOUND,
                "No supported PC executable found. Supply the original Windows game disc or a "
                "supported Wobble folder; a PlayStation disc supplies optional artwork and music, "
                "not the Windows executable.");
    }
    return found;
}

char *wo_native_root(const char *source, const char *work, const WoJson *manifest,
                     WoError *error) {
    return native_root_inner(source, work, manifest, 0, error);
}

static bool copy_file_into(const char *from, const char *destination, const char *relative,
                           WoError *error) {
    char *target = wo_path_child(destination, relative, error);
    bool ok = target && wo_copy_file(from, target, error);
    wo_free(target);
    return ok;
}

bool wo_copy_game_data(const char *native_root, const char *destination, WoError *error) {
    WoDirList top = {0};
    if (!wo_list_dir(native_root, &top, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < top.count; i++) {
        const WoDirEntry *entry = &top.items[i];
        size_t len = strlen(entry->name);
        char *path = wo_path_join(native_root, entry->name, error);
        if (!path) {
            ok = false;
            break;
        }
        if (entry->is_dir && !entry->is_link &&
            (name_is(entry->name, len, "Common") || track_folder(entry->name, len))) {
            /* Texture containers are installed separately with the SD textures. */
            WoTreeList files = {0};
            ok = wo_tree_list(path, &files, error);
            for (size_t f = 0; ok && f < files.count; f++) {
                const WoTreeEntry *file = &files.items[f];
                const char *extension = wo_path_extension(file->path);
                if (file->is_dir || wo_str_ieq(extension, ".cmp") ||
                    wo_str_ieq(extension, ".tim") || wo_str_ieq(extension, ".tex")) {
                    continue;
                }
                char *relative = wo_format(error, "%s/%s", entry->name, file->relative);
                ok = relative && copy_file_into(file->path, destination, relative, error);
                wo_free(relative);
            }
            wo_tree_free(&files);
        } else if (entry->is_dir && !entry->is_link &&
                   (name_is(entry->name, len, "MPEG") || name_is(entry->name, len, "movies"))) {
            WoDirList movies = {0};
            ok = wo_list_dir(path, &movies, error);
            for (size_t m = 0; ok && m < movies.count; m++) {
                if (movies.items[m].is_dir || !movie_name(movies.items[m].name)) {
                    continue;
                }
                char *from = wo_path_join(path, movies.items[m].name, error);
                char *relative = wo_format(error, "movies/%s", movies.items[m].name);
                ok = from && relative && copy_file_into(from, destination, relative, error);
                wo_free(relative);
                wo_free(from);
            }
            wo_dirlist_free(&movies);
        } else if (!entry->is_dir &&
                   (wo_str_ieq(entry->name, "CONFIG.DAT") || wo_str_ieq(entry->name, "SAVEGAME.BIN") ||
                    wo_str_ieq(entry->name, "Wipeout2097.ini"))) {
            ok = copy_file_into(path, destination, entry->name, error);
        }
        wo_free(path);
    }
    wo_dirlist_free(&top);
    return ok;
}

bool wo_copy_sound_effects(const char *native_root, const char *destination, WoError *error) {
    static const char *const candidates[] = {"audio/source", "source", "Sound", "audio"};
    char *folder = NULL;
    for (size_t i = 0; !folder && i < WO_COUNT(candidates); i++) {
        char *path = wo_path_join(native_root, candidates[i], error);
        if (path && wo_is_dir(path)) {
            folder = path;
        } else {
            wo_free(path);
        }
    }
    if (!folder) {
        folder = wo_strdup(native_root, error);
    }
    WoDirList list = {0};
    bool ok = folder && wo_list_dir(folder, &list, error);
    size_t copied = 0;
    for (size_t i = 0; ok && i < list.count; i++) {
        if (list.items[i].is_dir || !wo_str_iends_with(list.items[i].name, ".wav")) {
            continue;
        }
        char *from = wo_path_join(folder, list.items[i].name, error);
        ok = from && copy_file_into(from, destination, list.items[i].name, error);
        wo_free(from);
        copied++;
    }
    if (ok && copied == 0) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND,
                     "No sound effects found in the selected Sound/audio folder.");
    }
    wo_dirlist_free(&list);
    wo_free(folder);
    return ok;
}

/* --- Stamps --------------------------------------------------------------------- */

static bool stamp_extension(const char *name) {
    static const char *const extensions[] = {".iso", ".cue", ".bin", ".mds",
                                             ".mdf", ".zip", ".wav", ".mp3"};
    for (size_t i = 0; i < WO_COUNT(extensions); i++) {
        if (wo_str_iends_with(name, extensions[i])) {
            return true;
        }
    }
    return wo_str_ieq(name, "tracks.ini");
}

static bool append_file_stamp(WoBuf *out, const char *path, uint64_t size, uint64_t mtime,
                              WoError *error) {
    return wo_buf_printf(out, error, "%s|%llu|%llu", path, (unsigned long long)size,
                         (unsigned long long)(mtime + DOTNET_TICKS_AT_1601));
}

typedef struct StampFiles {
    WoStrList entries;
} StampFiles;

static bool collect_stamp_file(void *context, const char *path, const char *relative,
                               const WoDirEntry *entry, bool *descend, WoError *error) {
    (void)relative;
    (void)descend;
    StampFiles *files = context;
    if (entry->is_dir || !stamp_extension(entry->name)) {
        return true;
    }
    WoBuf line = {0};
    bool ok = append_file_stamp(&line, path, entry->size, entry->mtime, error) &&
              wo_buf_terminate(&line, error);
    ok = ok && wo_strlist_push_owned(&files->entries, wo_buf_detach(&line), error);
    wo_buf_free(&line);
    return ok;
}

bool wo_stamp_unavailable(const char *stamp) {
    return stamp && wo_str_iends_with(stamp, "|unavailable");
}

char *wo_source_stamp(const char *path, WoError *error) {
    if (!path || !*path) {
        return wo_strdup("", error);
    }
    WoFileInfo info;
    if (!wo_stat(path, &info, NULL) || !info.exists) {
        return wo_format(error, "%s|unavailable", path);
    }
    char *full = wo_path_full(path, error);
    if (!full) {
        return NULL;
    }
    WoBuf out = {0};
    bool ok = true;
    if (info.is_dir) {
        StampFiles files = {0};
        ok = wo_walk(full, 2, collect_stamp_file, &files, error);
        wo_strlist_sort_i(&files.entries);
        ok = ok && wo_buf_printf(&out, error, "%s|folder|", full);
        for (size_t i = 0; ok && i < files.entries.count; i++) {
            ok = wo_buf_printf(&out, error, "%s%s", i ? "|" : "", files.entries.items[i]);
        }
        wo_strlist_free(&files.entries);
    } else {
        ok = append_file_stamp(&out, full, info.size, info.mtime, error);
        const char *extension = wo_path_extension(full);
        if (ok && (wo_str_ieq(extension, ".cue") || wo_str_ieq(extension, ".mds"))) {
            WoTrackList tracks = {0};
            if (wo_disc_tracks(full, &tracks, NULL)) {
                WoStrList seen = {0};
                for (size_t i = 0; ok && i < tracks.count; i++) {
                    const char *image = tracks.items[i].image;
                    if (!image || wo_strlist_contains_i(&seen, image)) {
                        continue;
                    }
                    wo_strlist_push(&seen, image, NULL);
                    WoFileInfo image_info;
                    if (wo_stat(image, &image_info, NULL) && image_info.exists) {
                        ok = wo_buf_append_byte(&out, '|', error) &&
                             append_file_stamp(&out, image, image_info.size, image_info.mtime, error);
                    } else {
                        ok = wo_buf_printf(&out, error, "|%s|unavailable", image);
                    }
                }
                wo_strlist_free(&seen);
            }
            wo_tracks_free(&tracks);
        }
    }
    wo_free(full);
    if (!ok || !wo_buf_terminate(&out, error)) {
        wo_buf_free(&out);
        return NULL;
    }
    return wo_buf_detach(&out);
}
