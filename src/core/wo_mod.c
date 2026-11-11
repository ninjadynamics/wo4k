#include "core/wo_mod.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_log.h"

#define STATE_MAX_BYTES (16u << 20)

bool wo_mod_init(WoMod *mod, const char *root, WoError *error) {
    memset(mod, 0, sizeof(*mod));
    mod->root = wo_path_full(root, error);
    if (!mod->root) {
        return false;
    }
    mod->game = wo_path_join(mod->root, "wobble", error);
    mod->state = mod->game ? wo_path_join(mod->root, ".state", error) : NULL;
    mod->work = mod->state ? wo_path_join(mod->root, ".work", error) : NULL;
    if (!mod->work) {
        wo_mod_free(mod);
        return false;
    }
    return true;
}

void wo_mod_free(WoMod *mod) {
    wo_free(mod->root);
    wo_free(mod->game);
    wo_free(mod->state);
    wo_free(mod->work);
    memset(mod, 0, sizeof(*mod));
}

char *wo_mod_path(const WoMod *mod, const char *relative, WoError *error) {
    return wo_path_child(mod->root, relative, error);
}

char *wo_mod_game_path(const WoMod *mod, const char *relative, WoError *error) {
    return wo_path_child(mod->game, relative, error);
}

char *wo_mod_new_work_dir(const WoMod *mod, const char *prefix, WoError *error) {
    uint8_t random[8];
    if (!wo_random_bytes(random, sizeof(random), error)) {
        return NULL;
    }
    char name[64];
    snprintf(name, sizeof(name), "%s%02x%02x%02x%02x%02x%02x%02x%02x", prefix, random[0],
             random[1], random[2], random[3], random[4], random[5], random[6], random[7]);
    char *path = wo_path_child(mod->work, name, error);
    if (path && (wo_chain_has_link(mod->root, mod->work) || !wo_mkdirs(path, error))) {
        if (!wo_error_is_set(error)) {
            wo_fail(error, WO_ERR_UNSAFE, "The .work folder is a link; refusing to use it.");
        }
        wo_free(path);
        return NULL;
    }
    return path;
}

void wo_mod_remove_work_dir(const WoMod *mod, const char *path) {
    if (!path) {
        return;
    }
    WoError error = {0};
    if (!wo_path_is_within(mod->work, path, false) || wo_chain_has_link(mod->root, path)) {
        wo_log_text("work_cleanup_refused", path);
        return;
    }
    if (!wo_delete_tree(mod->work, path, &error)) {
        wo_log_text("work_cleanup_failed", error.message);
    }
}

/* --- Disk use ----------------------------------------------------------------------- */

static void delete_entry(const WoMod *mod, const char *folder, const WoDirEntry *entry) {
    char *path = wo_path_join(folder, entry->name, NULL);
    WoError error = {0};
    bool ok = path && !wo_chain_has_link(mod->root, path) &&
              (entry->is_dir ? wo_delete_tree(mod->root, path, &error)
                             : wo_delete_file(path, &error));
    if (!ok && error.message[0]) {
        wo_log_text("prune_failed", error.message);
    }
    wo_free(path);
}

/* Delete all but the newest `keep` directories whose names start with
 * prefix; the names carry a sortable timestamp. */
static void keep_newest(const WoMod *mod, const char *folder, const WoDirList *list,
                        const char *prefix, size_t keep) {
    size_t matching = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].is_dir && !list->items[i].is_link &&
            wo_str_istarts_with(list->items[i].name, prefix)) {
            matching++;
        }
    }
    size_t excess = matching > keep ? matching - keep : 0;
    for (size_t i = 0; i < list->count && excess > 0; i++) {
        const WoDirEntry *entry = &list->items[i];
        if (entry->is_dir && !entry->is_link && wo_str_istarts_with(entry->name, prefix)) {
            delete_entry(mod, folder, entry);
            excess--;
        }
    }
}

/* Scratch names written by this and earlier releases' installers. */
static bool scratch_name(const char *name) {
    static const char *const prefixes[] = {
        "i-", "update-", "ensure-music-", "ensure-textures-", "ensure-request-", "native-",
        "texture-source-", "audio-source-", "native-import-", "optional-music-", "native-textures-",
        "request-", "status-", "check-status-", "discovery-", "update-status-", "unused.json"};
    for (size_t i = 0; i < WO_COUNT(prefixes); i++) {
        if (wo_str_istarts_with(name, prefixes[i])) {
            return true;
        }
    }
    return false;
}

static uint64_t now_ticks(void) {
    /* FILETIME ticks: 100 ns since 1601. */
    return ((uint64_t)time(NULL) + 11644473600ull) * 10000000ull;
}

static uint64_t days(unsigned count) {
    return (uint64_t)count * 24 * 3600 * 10000000ull;
}

/* The soundtracks of the kept game backup are dropped: they are most of its
 * size and the current installation has its own copy. */
static void slim_backups(const WoMod *mod, const char *folder, const WoDirList *list) {
    for (size_t i = 0; i < list->count; i++) {
        const WoDirEntry *entry = &list->items[i];
        if (!entry->is_dir || entry->is_link || !wo_str_istarts_with(entry->name, "wobble-")) {
            continue;
        }
        char *backup = wo_path_join(folder, entry->name, NULL);
        char *music = backup ? wo_path_join(backup, "music", NULL) : NULL;
        WoError error = {0};
        if (music && wo_is_dir(music) && !wo_chain_has_link(mod->root, music) &&
            !wo_delete_tree(mod->root, music, &error)) {
            wo_log_text("prune_failed", error.message);
        }
        wo_free(music);
        wo_free(backup);
    }
}

static void prune_old_files(const WoMod *mod, const char *folder, uint64_t max_age,
                            bool (*wanted)(const char *name)) {
    WoDirList list = {0};
    if (!wo_is_dir(folder) || wo_chain_has_link(mod->root, folder) ||
        !wo_list_dir(folder, &list, NULL)) {
        return;
    }
    uint64_t now = now_ticks();
    for (size_t i = 0; i < list.count; i++) {
        const WoDirEntry *entry = &list.items[i];
        if (entry->is_link || !wanted(entry->name)) {
            continue;
        }
        bool install = entry->is_dir && wo_str_istarts_with(entry->name, "i-");
        if (install || entry->mtime + max_age < now) {
            delete_entry(mod, folder, entry);
        }
    }
    wo_dirlist_free(&list);
}

static bool log_name(const char *name) {
    /* Session logs and their attachments: <operation>-<date>-...log[.*].
     * Logs of real crashes are kept for good (only --purge removes them). */
    if (wo_str_iends_with(name, WO_LOG_CRASH_SUFFIX)) {
        return false;
    }
    return strstr(name, ".log") != NULL || wo_str_iends_with(name, ".txt");
}

void wo_mod_prune(const WoMod *mod) {
    char *backups = wo_path_join(mod->root, "backups", NULL);
    WoDirList list = {0};
    if (backups && wo_is_dir(backups) && !wo_chain_has_link(mod->root, backups) &&
        wo_list_dir(backups, &list, NULL)) {
        keep_newest(mod, backups, &list, "wobble-", WO_KEEP_GAME_BACKUPS);
        keep_newest(mod, backups, &list, "update-", WO_KEEP_UPDATE_BACKUPS);
        slim_backups(mod, backups, &list);
    }
    wo_dirlist_free(&list);
    wo_free(backups);
    prune_old_files(mod, mod->work, days(WO_WORK_MAX_AGE_DAYS), scratch_name);
    char *logs = wo_path_join(mod->root, "logs", NULL);
    if (logs) {
        prune_old_files(mod, logs, days(WO_LOG_MAX_AGE_DAYS), log_name);
    }
    wo_free(logs);
}

/* --- Configuration ---------------------------------------------------------------- */

char *wo_config_resolve_path(const WoMod *mod, const char *stored, WoError *error) {
    if (!stored || !*stored) {
        return wo_strdup("", error);
    }
    if (wo_path_is_absolute(stored)) {
        return wo_path_full(stored, error);
    }
    return wo_mod_path(mod, stored, error);
}

char *wo_config_portable_path(const WoMod *mod, const char *path, WoError *error) {
    if (!path || !*path) {
        return wo_strdup("", error);
    }
    char *full = wo_config_resolve_path(mod, path, error);
    if (!full) {
        return NULL;
    }
    char *relative = wo_path_relative(mod->root, full, error);
    if (relative) {
        wo_free(full);
        return relative;
    }
    return full;
}

void wo_config_free(WoConfig *config) {
    wo_free(config->game_source);
    wo_free(config->pc_music_source);
    wo_free(config->psx_source);
    memset(config, 0, sizeof(*config));
}

bool wo_config_copy(const WoConfig *from, WoConfig *to, WoError *error) {
    *to = *from;
    to->game_source = wo_strdup(from->game_source ? from->game_source : "", error);
    to->pc_music_source = wo_strdup(from->pc_music_source ? from->pc_music_source : "", error);
    to->psx_source = wo_strdup(from->psx_source ? from->psx_source : "", error);
    if (!to->game_source || !to->pc_music_source || !to->psx_source) {
        wo_config_free(to);
        return false;
    }
    return true;
}

bool wo_receipt_load(const WoMod *mod, WoJson **out, WoError *error) {
    *out = NULL;
    char *path = wo_path_join(mod->game, "installation.json", error);
    bool ok = path != NULL;
    if (ok && wo_is_file(path)) {
        ok = wo_read_json(path, STATE_MAX_BYTES, out, error);
    }
    wo_free(path);
    return ok;
}

bool wo_receipt_save(const WoMod *mod, const WoJson *receipt, WoError *error) {
    char *path = wo_path_join(mod->game, "installation.json", error);
    bool ok = path && wo_write_json_if_changed(path, receipt, NULL, error);
    wo_free(path);
    return ok;
}

bool wo_state_load(const WoMod *mod, const char *name, WoJson **out, WoError *error) {
    *out = NULL;
    char *path = wo_path_child(mod->state, name, error);
    bool ok = path != NULL;
    if (ok && wo_is_file(path)) {
        ok = wo_read_json(path, STATE_MAX_BYTES, out, error);
    }
    wo_free(path);
    return ok;
}

bool wo_state_save(const WoMod *mod, const char *name, const WoJson *value, WoError *error) {
    char *path = wo_path_child(mod->state, name, error);
    bool ok = path != NULL;
    if (ok && wo_chain_has_link(mod->root, path)) {
        ok = wo_fail(error, WO_ERR_UNSAFE, "Saved settings refuse filesystem links (%s).", name);
    }
    ok = ok && wo_write_json_if_changed(path, value, NULL, error);
    wo_free(path);
    return ok;
}

/* A member of the first object that has it; the saved file wins over the
 * receipt, which wins over the fallback. */
static const WoJson *pick(const WoJson *saved, const char *saved_key, const WoJson *receipt,
                          const char *receipt_key) {
    const WoJson *value = wo_json_get(saved, saved_key);
    if (!value && receipt_key) {
        value = wo_json_get(receipt, receipt_key);
    }
    return value;
}

bool wo_config_load(const WoMod *mod, WoConfig *config, WoError *error) {
    memset(config, 0, sizeof(*config));
    WoJson *receipt = NULL;
    WoJson *saved_file = NULL;
    if (!wo_receipt_load(mod, &receipt, error) || !wo_state_load(mod, "setup.json", &saved_file,
                                                                 error)) {
        wo_json_free(receipt);
        return false;
    }
    bool ok = true;
    if (saved_file && wo_json_int(saved_file, "format", 0) != 1) {
        ok = wo_fail(error, WO_ERR_UNSUPPORTED, "Unsupported saved Setup configuration.");
    }
    const WoJson *saved = saved_file ? saved_file : wo_json_get(receipt, "configuration");

    const char *game = wo_json_str(saved, "GameSource", NULL);
    if (!game) {
        game = wo_is_dir(mod->game) ? "wobble" : "";
    }
    const char *psx = wo_json_str(saved, "PsxMusicSource", "");
    WoJson *origin = NULL;
    if (ok && !saved) {
        /* Very early installations recorded the PS1 disc only here. */
        char *origin_path = wo_mod_game_path(mod, "textures/ps1-source/source.json", NULL);
        if (origin_path && wo_is_file(origin_path) &&
            wo_read_json(origin_path, STATE_MAX_BYTES, &origin, NULL)) {
            psx = wo_json_str(origin, "configured_source", "");
        }
        wo_free(origin_path);
    }
    if (ok) {
        config->revision = wo_json_int(saved, "revision", 0);
        config->game_source = wo_config_resolve_path(mod, game, error);
        config->pc_music_source =
            config->game_source
                ? wo_config_resolve_path(mod, wo_json_str(saved, "PcMusicSource", ""), error)
                : NULL;
        config->psx_source = config->pc_music_source ? wo_config_resolve_path(mod, psx, error)
                                                     : NULL;
        ok = config->psx_source != NULL;
    }
    if (ok) {
        const char *soundtrack =
            wo_json_as_str(pick(saved, "DefaultSoundtrack", receipt, "default_soundtrack"), "Pc");
        config->hd_textures =
            wo_json_as_bool(pick(saved, "HdTextures", receipt, "hd_textures"), true);
        config->clean_audio =
            wo_json_as_bool(pick(saved, "CleanAudio", receipt, "clean_audio"), true);
        char *game_ini = wo_path_join(mod->game, "Wipeout2097.ini", error);
        char *ddraw_ini = game_ini ? wo_path_join(mod->game, "DDrawCompat-Wipeout2097.ini", error)
                                   : NULL;
        const WoJson *mode = wo_json_get(saved, "SoundMode");
        config->sound_mode = mode ? (int)wo_json_as_int(mode, 0)
                                  : (game_ini ? wo_settings_get_sound_mode(game_ini) : 2);
        const char *fullscreen = wo_json_str(saved, "FullscreenMode", NULL);
        bool fullscreen_ok = true;
        if (fullscreen) {
            fullscreen_ok = wo_fullscreen_parse(fullscreen, &config->fullscreen);
        } else if (ddraw_ini) {
            config->fullscreen = wo_settings_get_fullscreen(ddraw_ini);
        }
        if (!WO_EXCLUSIVE_FULLSCREEN_OFFERED) {
            config->fullscreen = WO_FULLSCREEN_BORDERLESS;
        }
        if (!wo_soundtrack_parse(soundtrack, &config->soundtrack) ||
            !wo_sound_mode_valid(config->sound_mode) || !fullscreen_ok) {
            ok = wo_fail(error, WO_ERR_FORMAT, "Invalid saved Setup preferences.");
        }
        wo_free(game_ini);
        wo_free(ddraw_ini);
    }
    wo_json_free(origin);
    wo_json_free(saved_file);
    wo_json_free(receipt);
    if (!ok) {
        wo_config_free(config);
    }
    return ok;
}

static WoJson *config_to_json(const WoMod *mod, const WoConfig *config, WoError *error) {
    WoJson *stored = wo_json_new_object();
    char *game = wo_config_portable_path(mod, config->game_source, error);
    char *pc = game ? wo_config_portable_path(mod, config->pc_music_source, error) : NULL;
    char *psx = pc ? wo_config_portable_path(mod, config->psx_source, error) : NULL;
    bool ok = stored && psx && wo_json_put_int(stored, "format", 1) &&
              wo_json_put_int(stored, "revision", 1) &&
              wo_json_put_str(stored, "GameSource", game) &&
              wo_json_put_str(stored, "PcMusicSource", pc) &&
              wo_json_put_str(stored, "PsxMusicSource", psx) &&
              wo_json_put_str(stored, "DefaultSoundtrack", wo_soundtrack_name(config->soundtrack)) &&
              wo_json_put_bool(stored, "HdTextures", config->hd_textures) &&
              wo_json_put_bool(stored, "CleanAudio", config->clean_audio) &&
              wo_json_put_int(stored, "SoundMode", config->sound_mode) &&
              wo_json_put_str(stored, "FullscreenMode",
                              wo_fullscreen_name(WO_EXCLUSIVE_FULLSCREEN_OFFERED
                                                     ? config->fullscreen
                                                     : WO_FULLSCREEN_BORDERLESS));
    wo_free(game);
    wo_free(pc);
    wo_free(psx);
    if (!ok) {
        if (!wo_error_is_set(error)) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
        wo_json_free(stored);
        return NULL;
    }
    return stored;
}

static bool json_scalar_equal(const WoJson *a, const WoJson *b) {
    if (!a || !b || a->type != b->type) {
        return false;
    }
    switch (a->type) {
    case WO_JSON_STRING:
        return strcmp(a->string, b->string) == 0;
    case WO_JSON_BOOL:
        return a->boolean == b->boolean;
    case WO_JSON_NUMBER:
        return a->number == b->number;
    case WO_JSON_NULL:
        return true;
    default:
        return false;
    }
}

bool wo_config_save(const WoMod *mod, const WoConfig *config, WoConfig *saved, WoError *error) {
    if (!wo_sound_mode_valid(config->sound_mode)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid Setup preferences.");
    }
    WoJson *stored = config_to_json(mod, config, error);
    WoJson *previous = NULL;
    if (!stored || !wo_state_load(mod, "setup.json", &previous, error)) {
        wo_json_free(stored);
        return false;
    }
    bool write = true;
    if (previous) {
        bool same = true;
        for (size_t i = 0; i < stored->count && same; i++) {
            if (!wo_str_eq(stored->keys[i], "revision")) {
                same = json_scalar_equal(stored->items[i], wo_json_get(previous, stored->keys[i]));
            }
        }
        write = !same;
        wo_json_put_int(stored, "revision", wo_json_int(previous, "revision", 0) + 1);
    }
    bool ok = !write || wo_state_save(mod, "setup.json", stored, error);
    wo_json_free(previous);
    wo_json_free(stored);
    if (ok && saved) {
        ok = wo_config_load(mod, saved, error);
    }
    return ok;
}
