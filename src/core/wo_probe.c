/* Probing a single source path and searching common folders for sources. */
#include <string.h>
#include <time.h>

#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_iso.h"
#include "core/wo_music.h"
#include "core/wo_natives.h"
#include "core/wo_ops.h"
#include "core/wo_sources.h"
#include "core/wo_zip.h"

#define PROBE_EXE_MAX (3u << 20)
#define PROBE_DESCRIPTOR_MAX (1u << 20)
#define PROBE_SYSTEM_CNF_MAX 4096
#define DISCOVERY_MAX_FOLDERS 1000
#define DISCOVERY_MAX_DEPTH 5

static bool load_manifest(const WoMod *mod, WoJson **manifest) {
    char *path = wo_mod_path(mod, "manifests/patches.json", NULL);
    bool ok = path && wo_read_json(path, 4u << 20, manifest, NULL);
    wo_free(path);
    return ok;
}

static bool load_plan(const WoMod *mod, WoJson **plan) {
    char *path = wo_mod_path(mod, "manifests/texture-selection.json", NULL);
    bool ok = path && wo_read_json(path, 64u << 20, plan, NULL);
    wo_free(path);
    return ok;
}

static bool memory_supported(const WoJson *manifest, const void *data, size_t len) {
    char digest[WO_SHA256_HEX_SIZE];
    return wo_sha256_mem(data, len, digest, NULL) && wo_supported_executable(manifest, digest);
}

static size_t count_audio_tracks(const char *text) {
    size_t count = 0;
    const char *line = text;
    while (*line) {
        const char *s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (wo_str_istarts_with(s, "TRACK")) {
            s += 5;
            bool spaced = *s == ' ' || *s == '\t';
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            bool digits = *s >= '0' && *s <= '9';
            while (*s >= '0' && *s <= '9') {
                s++;
            }
            bool spaced_again = *s == ' ' || *s == '\t';
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            if (spaced && digits && spaced_again && wo_str_istarts_with(s, "AUDIO")) {
                count++;
            }
        }
        line += strcspn(line, "\n");
        if (*line) {
            line++;
        }
    }
    return count;
}

static void set_path(char **slot, const char *path) {
    if (!*slot) {
        *slot = wo_strdup(path, NULL);
    }
}

static void probe_folder(const char *full, const WoJson *manifest, const WoJson *plan,
                         WoSourceInfo *info) {
    static const char *const names[] = {"Wipeout2.exe", "Wipeout2097.exe", "D3D.EXE",
                                        "Wipeout2_HighTest.exe"};
    for (size_t i = 0; i < WO_COUNT(names) && !info->game; i++) {
        char *exe = wo_path_join(full, names[i], NULL);
        char digest[WO_SHA256_HEX_SIZE];
        if (exe && wo_is_file(exe) && wo_sha256_file(exe, digest, NULL) &&
            wo_supported_executable(manifest, digest)) {
            info->game = true;
        }
        wo_free(exe);
    }
    if (info->game && !wo_require_retail_sheets(plan, full, NULL)) {
        info->game = false;
        info->not_retail = true;
    }
    char *folder = NULL;
    if (wo_music_find_folder(full, WO_MUSIC_PC, &folder, NULL) && folder) {
        info->pc_music = true;
        set_path(&info->pc_music_path, folder);
    }
    wo_free(folder);
    folder = NULL;
    if (wo_music_find_folder(full, WO_MUSIC_PSX, &folder, NULL) && folder) {
        info->psx = true;
        info->psx_music = true;
        set_path(&info->psx_path, folder);
    }
    wo_free(folder);
}

static void probe_zip(const char *full, const WoJson *manifest, const WoJson *plan,
                      WoSourceInfo *info) {
    WoZip *zip = NULL;
    if (!wo_zip_open(full, &zip, NULL)) {
        return;
    }
    const char *mds_hash = wo_json_str(manifest, "pc_mds_sha256", "");
    WoBuf data = {0};
    for (size_t i = 0; i < wo_zip_count(zip); i++) {
        const WoZipEntry *entry = wo_zip_entry(zip, i);
        const char *name = wo_path_basename(entry->name);
        if (entry->is_dir) {
            continue;
        }
        if ((wo_str_ieq(name, "Wipeout2.exe") || wo_str_ieq(name, "D3D.exe")) &&
            entry->size < PROBE_EXE_MAX && wo_zip_read(zip, i, PROBE_EXE_MAX, &data, NULL) &&
            memory_supported(manifest, data.data, data.len)) {
            info->game = true;
        }
        /* Loose game files: the retail disc's sheets, or an earlier revision. */
        char sheet_digest[WO_SHA256_HEX_SIZE];
        if (wo_str_iends_with(name, ".tga") && entry->size < PROBE_EXE_MAX &&
            wo_sheet_not_retail(plan, entry->name, "") &&
            wo_zip_read(zip, i, PROBE_EXE_MAX, &data, NULL) &&
            wo_sha256_mem(data.data, data.len, sheet_digest, NULL) &&
            wo_sheet_not_retail(plan, entry->name, sheet_digest)) {
            info->not_retail = true;
        }
        if (wo_str_iends_with(name, ".cue") && entry->size < PROBE_DESCRIPTOR_MAX &&
            wo_zip_read(zip, i, PROBE_DESCRIPTOR_MAX, &data, NULL)) {
            size_t audio = count_audio_tracks((const char *)data.data);
            if (audio == 11) {
                info->psx = true;
                info->psx_music = true;
                set_path(&info->psx_path, full);
            } else if (audio == 9) {
                info->pc_music = true;
                info->game = true;
                set_path(&info->pc_music_path, full);
            }
        }
        if (wo_str_iends_with(name, ".mds") && entry->size < PROBE_DESCRIPTOR_MAX &&
            wo_zip_read(zip, i, PROBE_DESCRIPTOR_MAX, &data, NULL)) {
            char digest[WO_SHA256_HEX_SIZE];
            if (wo_sha256_mem(data.data, data.len, digest, NULL) && wo_digest_eq(digest, mds_hash)) {
                info->game = true;
                info->pc_music = true;
                set_path(&info->pc_music_path, full);
            }
        }
    }
    if (info->not_retail) {
        info->game = false;
    }
    wo_buf_free(&data);
    wo_zip_close(zip);
}

static void probe_image(const char *full, const WoJson *manifest, WoSourceInfo *info) {
    const char *extension = wo_path_extension(full);
    bool bin = wo_str_ieq(extension, ".bin");
    WoTrackList tracks = {0};
    char *cue = bin ? wo_path_change_extension(full, ".cue", NULL) : NULL;
    if (!bin || (cue && wo_is_file(cue))) {
        wo_disc_tracks(full, &tracks, NULL);
    }
    wo_free(cue);
    size_t audio = wo_tracks_audio_count(&tracks);
    if (audio == 9) {
        info->pc_music = true;
        set_path(&info->pc_music_path, full);
    } else if (audio == 11) {
        info->psx = true;
        info->psx_music = true;
        set_path(&info->psx_path, full);
    }
    const WoTrack *data = wo_tracks_first_data(&tracks);
    bool plain = wo_str_ieq(extension, ".iso") || bin;
    if (plain || data) {
        WoSource source;
        WoIso iso;
        memset(&iso, 0, sizeof(iso));
        const char *image = data ? data->image : full;
        uint64_t origin = data ? data->offset : 0;
        if (wo_source_open_file(&source, image, NULL)) {
            if (wo_iso_open(&iso, &source, origin, NULL)) {
                for (size_t i = 0; i < iso.count; i++) {
                    const WoIsoEntry *entry = &iso.entries[i];
                    if (entry->is_dir || strchr(entry->path, '/')) {
                        continue;
                    }
                    uint8_t *bytes = NULL;
                    if ((wo_str_ieq(entry->path, "D3D.exe") || wo_str_ieq(entry->path, "Wipeout2.exe")) &&
                        entry->length < PROBE_EXE_MAX &&
                        wo_iso_read_entry(&iso, entry, PROBE_EXE_MAX, &bytes, NULL) &&
                        memory_supported(manifest, bytes, entry->length)) {
                        info->game = true;
                    }
                    wo_free(bytes);
                    bytes = NULL;
                    if (wo_str_ieq(entry->path, "SYSTEM.CNF") && entry->length < PROBE_SYSTEM_CNF_MAX &&
                        wo_iso_read_entry(&iso, entry, PROBE_SYSTEM_CNF_MAX, &bytes, NULL) &&
                        wo_ps1_serial_in(bytes, entry->length)) {
                        info->psx = true;
                        info->psx_data = true;
                        set_path(&info->psx_path, full);
                    }
                    wo_free(bytes);
                }
                wo_iso_close(&iso);
            }
            wo_source_close(&source);
        }
    }
    wo_tracks_free(&tracks);
}

void wo_source_probe(const WoMod *mod, const char *path, WoSourceInfo *info) {
    memset(info, 0, sizeof(*info));
    if (!path || !*path) {
        return;
    }
    char *full = wo_path_full(path, NULL);
    WoJson *manifest = NULL;
    WoJson *plan = NULL;
    if (!full || !load_manifest(mod, &manifest) || !load_plan(mod, &plan)) {
        wo_json_free(manifest);
        wo_free(full);
        return;
    }
    if (wo_is_dir(full)) {
        probe_folder(full, manifest, plan, info);
    } else if (wo_is_file(full)) {
        const char *extension = wo_path_extension(full);
        if (wo_str_ieq(extension, ".zip")) {
            probe_zip(full, manifest, plan, info);
        } else if (wo_str_ieq(extension, ".mds") || wo_str_ieq(extension, ".mdf") ||
                   wo_str_ieq(extension, ".cue") || wo_str_ieq(extension, ".bin") ||
                   wo_str_ieq(extension, ".iso")) {
            probe_image(full, manifest, info);
        }
    }
    wo_json_free(plan);
    wo_json_free(manifest);
    wo_free(full);
}

/* --- Discovery ------------------------------------------------------------------ */

typedef struct Found {
    char *game;
    char *pc;
    char *psx;
} Found;

static void absorb(Found *found, const char *path, const WoSourceInfo *info) {
    if (!found->game && info->game) {
        found->game = wo_strdup(path, NULL);
    }
    if (!found->pc && info->pc_music) {
        found->pc = wo_strdup(info->pc_music_path ? info->pc_music_path : path, NULL);
    }
    if (!found->psx && info->psx) {
        found->psx = wo_strdup(info->psx_path ? info->psx_path : path, NULL);
    }
}

static bool interesting_name(const char *name, bool include_music) {
    static const char *const words[] = {"wipeout", "wobble", "wo2097", "music", "source"};
    size_t count = include_music ? WO_COUNT(words) : 3;
    for (size_t w = 0; w < count; w++) {
        for (const char *p = name; *p; p++) {
            if (wo_str_istarts_with(p, words[w])) {
                return true;
            }
        }
    }
    return false;
}

static bool candidate_file(const char *name) {
    static const char *const extensions[] = {".zip", ".iso", ".mds", ".cue", ".bin"};
    if (!interesting_name(name, false)) {
        return false;
    }
    for (size_t i = 0; i < WO_COUNT(extensions); i++) {
        if (wo_str_iends_with(name, extensions[i])) {
            return true;
        }
    }
    return false;
}

static bool skipped_folder(const char *path) {
    return strstr(path, "\\.work\\") || strstr(path, "\\backups\\") || strstr(path, "\\.git\\");
}

typedef struct Node {
    char *path;
    int depth;
} Node;

void wo_discover_sources(const WoMod *mod, unsigned max_seconds, volatile long *cancel,
                         WoDiscovery *result) {
    memset(result, 0, sizeof(*result));
    WoStrList roots = {0};
    wo_discovery_roots(mod, &roots, NULL);
    Node *queue = NULL;
    size_t head = 0;
    size_t tail = 0;
    size_t cap = 0;
    WoStrList seen = {0};
    Found found = {0};
    for (size_t i = 0; i < roots.count; i++) {
        if (tail == cap) {
            cap = cap ? cap * 2 : 64;
            Node *grown = wo_realloc_array(queue, cap, sizeof(Node), NULL);
            if (!grown) {
                break;
            }
            queue = grown;
        }
        queue[tail].path = wo_strdup(roots.items[i], NULL);
        queue[tail].depth = 0;
        tail++;
    }
    time_t deadline = time(NULL) + (time_t)max_seconds;
    size_t visited = 0;
    while (head < tail && visited < DISCOVERY_MAX_FOLDERS && time(NULL) < deadline &&
           !(cancel && *cancel) && !(found.game && found.pc && found.psx)) {
        Node node = queue[head++];
        if (!node.path || wo_strlist_contains_i(&seen, node.path)) {
            wo_free(node.path);
            continue;
        }
        wo_strlist_push(&seen, node.path, NULL);
        visited++;
        if (skipped_folder(node.path)) {
            wo_free(node.path);
            continue;
        }
        WoSourceInfo info;
        wo_source_probe(mod, node.path, &info);
        absorb(&found, node.path, &info);
        wo_source_info_free(&info);
        WoDirList list = {0};
        if (wo_list_dir(node.path, &list, NULL)) {
            for (size_t i = 0; i < list.count && !(found.game && found.pc && found.psx); i++) {
                if (list.items[i].is_dir || !candidate_file(list.items[i].name)) {
                    continue;
                }
                char *file = wo_path_join(node.path, list.items[i].name, NULL);
                if (file) {
                    wo_source_probe(mod, file, &info);
                    absorb(&found, file, &info);
                    wo_source_info_free(&info);
                }
                wo_free(file);
            }
            /* Likely folders first, then alphabetical. */
            for (int pass = 0; pass < 2 && node.depth < DISCOVERY_MAX_DEPTH; pass++) {
                for (size_t i = 0; i < list.count; i++) {
                    const WoDirEntry *entry = &list.items[i];
                    if (!entry->is_dir || entry->is_link ||
                        interesting_name(entry->name, true) != (pass == 0)) {
                        continue;
                    }
                    if (tail == cap) {
                        size_t grown_cap = cap ? cap * 2 : 64;
                        Node *grown = wo_realloc_array(queue, grown_cap, sizeof(Node), NULL);
                        if (!grown) {
                            continue;
                        }
                        queue = grown;
                        cap = grown_cap;
                    }
                    queue[tail].path = wo_path_join(node.path, entry->name, NULL);
                    queue[tail].depth = node.depth + 1;
                    tail++;
                }
            }
        }
        wo_dirlist_free(&list);
        wo_free(node.path);
    }
    for (size_t i = head; i < tail; i++) {
        wo_free(queue[i].path);
    }
    wo_free(queue);
    wo_strlist_free(&seen);
    wo_strlist_free(&roots);
    result->game = found.game;
    result->psx = found.psx;
    wo_free(found.pc);
}
