/* Reconcile: check the configured installation, repair it when needed,
 * bring runtime files and settings up to date, keep optional PlayStation
 * artwork and the soundtracks in step with their sources, then refresh HD
 * artwork and sound effects. Run by Setup when it opens and by Play before
 * every launch. Mirrors the previous Reconcile-Installation.ps1,
 * Music-State.ps1 and Refresh-Assets.ps1. */
#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hashcache.h"
#include "core/wo_hdtex.h"
#include "core/wo_install.h"
#include "core/wo_log.h"
#include "core/wo_music.h"
#include "core/wo_natives.h"
#include "core/wo_ops.h"
#include "core/wo_settings.h"
#include "core/wo_sfx.h"
#include "core/wo_sources.h"
#include "core/wo_update.h"
#include "wo_version.h"

static void set_stage(WoProgress *progress, WoStage stage) {
    if (progress) {
        progress->stage = stage;
    }
}

static bool exists_in(const char *root, const char *relative) {
    char *path = wo_path_child(root, relative, NULL);
    bool exists = path && wo_is_file(path);
    wo_free(path);
    return exists;
}

/* False for a missing file or a folder: neither can be opened as a file. */
static bool file_digest(WoHashCache *cache, const char *root, const char *relative,
                        char digest[WO_SHA256_HEX_SIZE]) {
    char *path = wo_path_child(root, relative, NULL);
    bool ok = path && wo_hash_cache_file(cache, path, digest, NULL);
    wo_free(path);
    return ok;
}

static const char *receipt_digest(const WoJson *receipt, const char *relative) {
    const WoJson *files = wo_json_get(receipt, "pc_files");
    for (size_t i = 0; i < wo_json_length(files); i++) {
        if (wo_str_eq(wo_json_str(wo_json_at(files, i), "file", ""), relative)) {
            return wo_json_str(wo_json_at(files, i), "sha256", NULL);
        }
    }
    return NULL;
}

static bool listed_digest(const WoJson *list, const char *digest) {
    for (size_t i = 0; i < wo_json_length(list); i++) {
        if (wo_digest_eq(wo_json_as_str(wo_json_at(list, i), ""), digest)) {
            return true;
        }
    }
    return false;
}

/* The sheets the plan patches, each once: the plan's 1,000+ operations
 * name only a few dozen sheets. */
static bool plan_atlases(const WoJson *plan, WoStrList *atlases) {
    const WoJson *operations = wo_json_get(plan, "operations");
    for (size_t i = 0; i < wo_json_length(operations); i++) {
        const char *atlas = wo_json_str(wo_json_at(operations, i), "atlas", "");
        if (!wo_strlist_contains_i(atlases, atlas) && !wo_strlist_push(atlases, atlas, NULL)) {
            return false;
        }
    }
    return true;
}

/* Test-ModPcInstallation: game data, executable, texture sheets and the
 * original sound effects are present and unchanged. */
static bool pc_installation_healthy(const WoMod *mod, const WoJson *receipt, const WoJson *files,
                                    const WoJson *patches, const WoJson *plan,
                                    WoHashCache *cache) {
    const char *game = mod->game;
    char digest[WO_SHA256_HEX_SIZE];
    const WoJson *required = wo_json_get(files, "required");
    for (size_t i = 0; i < wo_json_length(required); i++) {
        const char *relative = wo_json_as_str(wo_json_at(required, i), "");
        const char *expected = receipt_digest(receipt, relative);
        if (!expected && !exists_in(game, relative)) {
            return false;
        }
        if (expected &&
            (!file_digest(cache, game, relative, digest) || !wo_digest_eq(digest, expected))) {
            return false;
        }
    }
    if (!file_digest(cache, game, "Wipeout2097.exe", digest) ||
        !wo_digest_eq(digest, wo_json_str(patches, "output_sha256", ""))) {
        return false;
    }
    WoStrList atlases = {0};
    bool healthy = plan_atlases(plan, &atlases);
    for (size_t i = 0; healthy && i < atlases.count; i++) {
        char *relative = wo_format(NULL, "textures/sd/%s", atlases.items[i]);
        healthy = relative && exists_in(game, relative);
        wo_free(relative);
    }
    char *sources_path = healthy ? wo_path_join(game, "textures/sd/sources.json", NULL) : NULL;
    WoJson *sources = NULL;
    if (sources_path && wo_is_file(sources_path) &&
        !wo_read_json(sources_path, 16u << 20, &sources, NULL)) {
        healthy = false;
    }
    wo_free(sources_path);
    char *sd = healthy ? wo_path_join(game, "textures/sd", NULL) : NULL;
    for (size_t i = 0; healthy && sd && i < wo_json_length(sources); i++) {
        const WoJson *record = wo_json_at(sources, i);
        char relative[1024];
        wo_copy_str(relative, sizeof(relative), wo_json_str(record, "file", ""));
        for (char *p = relative; *p; p++) {
            if (*p == '\\') {
                *p = '/';
            }
        }
        const char *extension = wo_path_extension(relative);
        if (wo_strlist_contains_i(&atlases, relative) ||
            !(wo_str_ieq(extension, ".tga") || wo_str_ieq(extension, ".cmp") ||
              wo_str_ieq(extension, ".tim") || wo_str_ieq(extension, ".tex"))) {
            continue;
        }
        healthy = file_digest(cache, sd, relative, digest) &&
                  wo_digest_eq(digest, wo_json_str(record, "sha256", ""));
    }
    wo_free(sd);
    wo_json_free(sources);
    wo_strlist_free(&atlases);
    const WoJson *audio = wo_json_get(patches, "audio");
    for (size_t i = 0; healthy && i < wo_json_length(audio); i++) {
        const WoJson *entry = wo_json_at(audio, i);
        char *relative = wo_format(NULL, "audio/source/%s", wo_json_str(entry, "name", ""));
        if (!relative || !file_digest(cache, game, relative, digest)) {
            healthy = false;
        } else {
            const char *expected = receipt_digest(receipt, relative);
            if (expected) {
                healthy = wo_digest_eq(digest, expected);
            } else if (wo_json_bool(receipt, "clean_audio", true)) {
                healthy = listed_digest(wo_json_get(entry, "source_hashes"), digest) ||
                          wo_digest_eq(digest, wo_json_str(entry, "output_sha256", ""));
            }
        }
        wo_free(relative);
    }
    return healthy;
}

static bool sync_file(WoHashCache *cache, const char *source, const char *target, int *updated,
                      WoError *error) {
    char a[WO_SHA256_HEX_SIZE];
    char b[WO_SHA256_HEX_SIZE];
    if (wo_is_file(target) && wo_hash_cache_file(cache, source, a, NULL) &&
        wo_hash_cache_file(cache, target, b, NULL) && wo_digest_eq(a, b)) {
        return true;
    }
    (*updated)++;
    return wo_copy_file(source, target, error);
}

static bool merge_ini(const char *template_path, const char *path, bool sectioned, int *updated,
                      WoError *error) {
    WoIni defaults;
    WoIni current;
    if (!wo_ini_load(template_path, &defaults, error)) {
        return false;
    }
    if (!wo_ini_load(path, &current, error)) {
        wo_ini_free(&defaults);
        return false;
    }
    int added = 0;
    bool ok = wo_ini_merge_defaults(&current, &defaults, sectioned, &added, error);
    if (ok && added > 0) {
        ok = wo_ini_save(path, &current, error);
        (*updated)++;
    }
    wo_ini_free(&current);
    wo_ini_free(&defaults);
    return ok;
}

/* Sync-ModRuntime: replace changed runtime files and add newly introduced
 * settings without touching existing preferences. */
static bool sync_runtime(const WoMod *mod, WoHashCache *cache, int *updated, WoError *error) {
    *updated = 0;
    char *runtime = wo_mod_path(mod, "runtime", error);
    WoDirList list = {0};
    bool ok = runtime && wo_list_dir(runtime, &list, error);
    for (size_t i = 0; ok && i < list.count; i++) {
        const char *name = list.items[i].name;
        bool wanted = wo_str_ieq(name, "wohr.dll") || wo_str_ieq(name, "ddraw.dll") ||
                      wo_str_ieq(name, "wo2097-controller.exe") || wo_str_ieq(name, "wo4k-video.exe") ||
                      (wo_str_istarts_with(name, "lib") && wo_str_iends_with(name, ".dll"));
        if (list.items[i].is_dir || !wanted) {
            continue;
        }
        char *source = wo_path_join(runtime, name, error);
        char *target = source ? wo_path_join(mod->game, name, error) : NULL;
        ok = target && sync_file(cache, source, target, updated, error);
        wo_free(target);
        wo_free(source);
    }
    wo_dirlist_free(&list);
    static const char *const required[] = {"wohr.dll", "ddraw.dll", "wo2097-controller.exe"};
    for (size_t i = 0; ok && i < WO_COUNT(required); i++) {
        char *relative = wo_format(error, "runtime/%s", required[i]);
        char *path = relative ? wo_mod_path(mod, relative, error) : NULL;
        if (path && !wo_is_file(path)) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND, "The mod download is incomplete: %s", relative);
        }
        wo_free(path);
        wo_free(relative);
    }
    static const char *const shaders[] = {"w2097-hd.cg", "w2097-hd.cgp"};
    for (size_t i = 0; ok && i < WO_COUNT(shaders); i++) {
        char *source = wo_path_join(runtime, shaders[i], error);
        char *relative = wo_format(error, "common-shaders-master/w2097/%s", shaders[i]);
        char *target = relative ? wo_path_child(mod->game, relative, error) : NULL;
        if (source && target && wo_is_file(source)) {
            ok = sync_file(cache, source, target, updated, error);
        }
        wo_free(target);
        wo_free(relative);
        wo_free(source);
    }
    char *ini = ok ? wo_path_join(mod->game, "Wipeout2097.ini", error) : NULL;
    char *ini_template = ini ? wo_path_join(runtime, "Wipeout2097.ini", error) : NULL;
    char *ddraw = ini_template ? wo_path_join(mod->game, "DDrawCompat-Wipeout2097.ini", error) : NULL;
    char *ddraw_template = ddraw ? wo_path_join(runtime, "DDrawCompat-Wipeout2097.ini", error) : NULL;
    ok = ok && ddraw_template;
    if (ok && !wo_is_file(ini)) {
        ok = wo_copy_file(ini_template, ini, error);
        (*updated)++;
    }
    ok = ok && merge_ini(ini_template, ini, true, updated, error) &&
         merge_ini(ddraw_template, ddraw, false, updated, error);
    wo_free(ddraw_template);
    wo_free(ddraw);
    wo_free(ini_template);
    wo_free(ini);
    wo_free(runtime);
    return ok;
}

/* --- Soundtracks (Music-State.ps1) ------------------------------------------------ */

static WoJson *music_state(WoHashCache *cache, const char *game, WoMusicBank bank,
                           const char *source, const char *stamp) {
    WoStrList files = {0};
    wo_music_files(game, bank, &files, NULL);
    WoJson *records = wo_json_new_array();
    for (size_t i = 0; i < files.count; i++) {
        char digest[WO_SHA256_HEX_SIZE];
        char *relative = wo_path_relative(game, files.items[i], NULL);
        if (relative && wo_hash_cache_file(cache, files.items[i], digest, NULL)) {
            WoJson *record = wo_json_new_object();
            wo_json_put_str(record, "file", relative);
            wo_json_put_str(record, "sha256", digest);
            wo_json_push(records, record);
        }
        wo_free(relative);
    }
    WoJson *state = wo_json_new_object();
    wo_json_put_str(state, "source", source ? source : "");
    wo_json_put_str(state, "source_stamp", stamp ? stamp : "");
    wo_json_put_bool(state, "available", files.count > 0);
    wo_json_put(state, "files", records);
    wo_strlist_free(&files);
    return state;
}

static bool music_state_healthy(WoHashCache *cache, const char *game, WoMusicBank bank,
                                const WoJson *state) {
    if (!state || !wo_json_bool(state, "available", false) || !wo_json_has(state, "files")) {
        return false;
    }
    WoStrList files = {0};
    wo_music_files(game, bank, &files, NULL);
    bool healthy = files.count > 0;
    const WoJson *records = wo_json_get(state, "files");
    for (size_t i = 0; healthy && i < files.count; i++) {
        char *relative = wo_path_relative(game, files.items[i], NULL);
        const char *expected = NULL;
        size_t matches = 0;
        for (size_t r = 0; relative && r < wo_json_length(records); r++) {
            if (wo_str_eq(wo_json_str(wo_json_at(records, r), "file", ""), relative)) {
                expected = wo_json_str(wo_json_at(records, r), "sha256", "");
                matches++;
            }
        }
        char digest[WO_SHA256_HEX_SIZE];
        healthy = matches == 1 && wo_hash_cache_file(cache, files.items[i], digest, NULL) &&
                  wo_digest_eq(digest, expected);
        wo_free(relative);
    }
    wo_strlist_free(&files);
    return healthy;
}

typedef struct MusicResult {
    bool pc_available;
    bool psx_available;
    int updated;
} MusicResult;

static bool ensure_music(const WoMod *mod, const WoConfig *config, const WoJson *receipt,
                         WoHashCache *cache, WoProgress *progress, MusicResult *result,
                         WoError *error) {
    memset(result, 0, sizeof(*result));
    set_stage(progress, WO_STAGE_SOUNDTRACK);
    WoJson *previous = NULL;
    if (!wo_state_load(mod, "music-sources.json", &previous, error)) {
        return false;
    }
    WoJson *states = wo_json_new_object();
    bool ok = states != NULL;
    for (int b = 0; ok && b < 2; b++) {
        WoMusicBank bank = b == 0 ? WO_MUSIC_PC : WO_MUSIC_PSX;
        const char *key = b == 0 ? "Pc" : "Psx";
        WoSoundtrack soundtrack = b == 0 ? WO_SOUNDTRACK_PC : WO_SOUNDTRACK_PSX;
        const char *source = b == 1 ? config->psx_source
                             : config->pc_music_source[0] ? config->pc_music_source
                                                          : config->game_source;
        const WoJson *old = wo_json_get(previous, key);
        char *folder = NULL;
        wo_music_find_folder(source, bank, &folder, NULL);
        char *stamp = wo_source_stamp(folder ? folder : source, error);
        wo_free(folder);
        WoStrList files = {0};
        wo_music_files(mod->game, bank, &files, NULL);
        bool complete = files.count > 0;
        wo_strlist_free(&files);
        bool same = old && wo_str_eq(wo_json_str(old, "source", ""), source);
        const char *old_stamp = wo_json_str(old, "source_stamp", "");
        WoJson *state = NULL;
        if (!stamp) {
            ok = false;
        } else if (!old && complete &&
                   wo_json_int(receipt, "configuration_revision", -1) == config->revision) {
            state = music_state(cache, mod->game, bank, source, stamp);
        } else if (same && ((music_state_healthy(cache, mod->game, bank, old) &&
                             (wo_str_eq(old_stamp, stamp) || wo_stamp_unavailable(stamp))) ||
                            (!wo_json_bool(old, "available", false) && !complete &&
                             wo_str_eq(old_stamp, stamp)))) {
            state = wo_json_clone(old);
        } else if (!source[0] || !wo_exists(source)) {
            if (complete && !old) {
                state = music_state(cache, mod->game, bank, source, stamp);
            } else if (old && !wo_json_bool(old, "available", false) && same) {
                state = wo_json_clone(old);
            } else if ((old && wo_json_bool(old, "available", false)) ||
                       config->soundtrack == soundtrack) {
                ok = wo_fail(error, WO_ERR_NOT_FOUND,
                             "The configured %s soundtrack needs its source. Restore its path in Setup.",
                             key);
            } else {
                state = music_state(cache, mod->game, bank, source, stamp);
            }
        } else {
            wo_progress(progress, WO_STAGE_SOUNDTRACK, 70,
                        b == 0 ? "Updating the PC soundtrack..." : "Updating the PlayStation soundtrack...");
            char *session = wo_mod_new_work_dir(mod, "ensure-music-", error);
            char *bank_folder = session ? wo_path_join(session, "bank", error) : NULL;
            char *input = bank_folder ? wo_resolve_input(source, session, error) : NULL;
            bool extracted = false;
            ok = input && wo_music_extract(input, bank_folder, bank, true, &extracted, error);
            if (ok && extracted) {
                ok = wo_music_install(bank_folder, mod->game, bank, NULL, error);
                result->updated++;
            } else if (ok && !(complete && !old) && config->soundtrack == soundtrack) {
                ok = wo_fail(error, WO_ERR_NOT_FOUND,
                             "The selected %s source has no CD audio. Choose No music or a complete "
                             "soundtrack source in Setup.",
                             key);
            }
            if (ok) {
                state = music_state(cache, mod->game, bank, source, stamp);
            }
            wo_mod_remove_work_dir(mod, session);
            wo_free(input);
            wo_free(bank_folder);
            wo_free(session);
        }
        wo_free(stamp);
        if (ok) {
            bool available = wo_json_bool(state, "available", false);
            if (b == 0) {
                result->pc_available = available;
            } else {
                result->psx_available = available;
            }
            ok = wo_json_put(states, key, state);
        }
    }
    ok = ok && wo_state_save(mod, "music-sources.json", states, error);
    wo_json_free(states);
    wo_json_free(previous);
    return ok;
}

/* --- PlayStation artwork ---------------------------------------------------------- */

static bool ensure_native_textures(const WoMod *mod, const WoConfig *config, const WoJson *receipt,
                                   const WoJson *plan, WoHashCache *cache, WoProgress *progress,
                                   bool *ps1_textures, WoError *error) {
    set_stage(progress, WO_STAGE_PS1_SOURCE);
    char *origin = wo_mod_game_path(mod, "textures/ps1-source/source.json", error);
    if (!origin) {
        return false;
    }
    bool origin_exists = wo_is_file(origin);
    wo_free(origin);
    bool require_ps1 = wo_json_bool(receipt, "playstation_textures", false) || origin_exists;
    bool healthy = false;
    if (!wo_natives_test_cached(mod->game, plan, require_ps1, cache, &healthy, error)) {
        return false;
    }
    WoJson *import_state = NULL;
    if (!wo_state_load(mod, "ps1-import.json", &import_state, error)) {
        return false;
    }
    const char *source = config->psx_source;
    char *stamp = wo_source_stamp(source, error);
    bool ok = stamp != NULL;
    bool same_source = import_state && wo_str_eq(wo_json_str(import_state, "source", ""), source);
    bool changed = source[0] && ok &&
                   (!same_source || (!wo_str_eq(wo_json_str(import_state, "source_stamp", ""), stamp) &&
                                     !wo_stamp_unavailable(stamp)));
    bool needs_source = source[0] && (changed || !healthy);
    if (ok && needs_source && !wo_exists(source)) {
        if ((!healthy && !origin_exists) || (import_state && !same_source)) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         "Configured PlayStation artwork needs its disc source. Restore the path in "
                         "Setup.");
        }
        needs_source = false;
    }
    char *session = NULL;
    char *input = NULL;
    if (ok && needs_source) {
        wo_progress(progress, WO_STAGE_PS1_SOURCE, 40, "Importing PlayStation artwork...");
        session = wo_mod_new_work_dir(mod, "ensure-textures-", error);
        input = session ? wo_resolve_input(source, session, error) : NULL;
        ok = input != NULL;
    }
    WoJson *native_receipt = NULL;
    if (ok) {
        WoNativeOptions options = {0};
        options.game_root = mod->game;
        options.plan = plan;
        options.ps1_source = input;
        options.configured_source = input ? source : NULL;
        options.incremental = true;
        options.require_ps1 = require_ps1;
        options.cache = cache;
        set_stage(progress, WO_STAGE_TEXTURES);
        ok = wo_natives_install(&options, &native_receipt, error);
    }
    *ps1_textures = wo_json_bool(native_receipt, "ps1_textures", false);
    if (ok && source[0]) {
        WoJson *state = wo_json_new_object();
        wo_json_put_str(state, "source", source);
        wo_json_put_str(state, "source_stamp", stamp);
        wo_json_put_bool(state, "ps1_textures", *ps1_textures);
        ok = wo_state_save(mod, "ps1-import.json", state, error);
        wo_json_free(state);
    }
    if (session) {
        wo_mod_remove_work_dir(mod, session);
    }
    wo_json_free(native_receipt);
    wo_free(input);
    wo_free(session);
    wo_free(stamp);
    wo_json_free(import_state);
    return ok;
}

/* --- Repair decision ------------------------------------------------------------- */

static bool repair(const WoMod *mod, const WoConfig *config, const WoJson *receipt, bool healthy,
                   WoProgress *progress, WoError *error) {
    const WoJson *previous = wo_json_get(receipt, "configuration");
    char *old_game = wo_config_resolve_path(mod, wo_json_str(previous, "GameSource", ""), NULL);
    WoConfig args;
    if (!wo_config_copy(config, &args, error)) {
        wo_free(old_game);
        return false;
    }
    bool ok = true;
    set_stage(progress, WO_STAGE_GAME_SOURCE);
    if (!args.game_source[0] || !wo_exists(args.game_source)) {
        if (healthy && (!old_game || !old_game[0] || wo_str_ieq(old_game, args.game_source))) {
            /* Repair from the installed copy itself. */
            wo_free(args.game_source);
            args.game_source = wo_strdup(mod->game, error);
            ok = args.game_source != NULL;
        } else {
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         "The Windows game needs repair and its source is unavailable. Restore its "
                         "path in Setup.");
        }
    }
    if (ok && wo_str_ieq(args.game_source, mod->game)) {
        char *cache = wo_mod_game_path(mod, "audio/source", error);
        if (cache && !wo_is_dir(cache)) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         "The original sound-effect cache is missing. Select your original PC game "
                         "source in Setup to repair it.");
        }
        wo_free(cache);
    }
    if (ok && args.psx_source[0] && !wo_exists(args.psx_source)) {
        char *prior = wo_config_resolve_path(mod, wo_json_str(previous, "PsxMusicSource", ""), NULL);
        WoJson *import_state = NULL;
        if (wo_state_load(mod, "ps1-import.json", &import_state, NULL) && import_state) {
            wo_free(prior);
            prior = wo_strdup(wo_json_str(import_state, "source", ""), NULL);
        }
        WoStrList psx_music = {0};
        wo_music_files(mod->game, WO_MUSIC_PSX, &psx_music, NULL);
        char *origin = wo_mod_game_path(mod, "textures/ps1-source/source.json", NULL);
        bool retained = (origin && wo_is_file(origin)) || psx_music.count > 0;
        if (prior && wo_str_eq(prior, args.psx_source) && retained) {
            /* Use the retained PS1 artwork and music instead of the missing disc. */
            args.psx_source[0] = 0;
        }
        wo_free(origin);
        wo_strlist_free(&psx_music);
        wo_json_free(import_state);
        wo_free(prior);
    }
    if (ok && args.pc_music_source[0] && !wo_exists(args.pc_music_source)) {
        char *prior = wo_config_resolve_path(mod, wo_json_str(previous, "PcMusicSource", ""), NULL);
        char *retained = NULL;
        if ((!previous || (prior && wo_str_eq(prior, args.pc_music_source))) &&
            wo_music_find_folder(mod->game, WO_MUSIC_PC, &retained, NULL) && retained) {
            wo_free(args.pc_music_source);
            args.pc_music_source = retained;
            retained = NULL;
        }
        wo_free(retained);
        wo_free(prior);
    }
    ok = ok && wo_install(mod, &args, true, progress, error);
    wo_config_free(&args);
    wo_free(old_game);
    return ok;
}

/* --- Reconcile -------------------------------------------------------------------- */

static bool load_manifests(const WoMod *mod, WoJson **files, WoJson **patches, WoJson **plan,
                           WoError *error) {
    return wo_load_manifest(mod, "manifests/game-files.json", files, error) &&
           wo_load_manifest(mod, "manifests/patches.json", patches, error) &&
           wo_load_manifest(mod, "manifests/texture-selection.json", plan, error);
}

static bool reconcile_inner(const WoMod *mod, bool force_repair, WoProgress *progress,
                            WoHashCache *cache, WoLaps *laps, WoReconcileResult *result,
                            WoError *error) {
    memset(result, 0, sizeof(*result));
    if (wo_mod_game_running(mod)) {
        return wo_fail(error, WO_ERR_BUSY, "Close the game before reconciling its installation.");
    }
    WoConfig config;
    WoJson *receipt = NULL;
    if (!wo_config_load(mod, &config, error)) {
        return false;
    }
    bool ok = wo_receipt_load(mod, &receipt, error);
    if (ok && !config.game_source[0] && !receipt) {
        wo_config_free(&config);
        return true; /* nothing installed or configured yet */
    }
    WoConfig saved;
    ok = ok && wo_config_save(mod, &config, &saved, error);
    if (ok) {
        wo_config_free(&config);
        config = saved;
    }
    WoJson *files = NULL;
    WoJson *patches = NULL;
    WoJson *plan = NULL;
    ok = ok && load_manifests(mod, &files, &patches, &plan, error);
    wo_laps_mark(laps, "config_ms");
    wo_progress(progress, WO_STAGE_GENERAL, 5, "Checking the configured installation...");
    bool healthy = ok && pc_installation_healthy(mod, receipt, files, patches, plan, cache);
    wo_laps_mark(laps, "check_ms");
    const WoJson *previous = wo_json_get(receipt, "configuration");
    char *old_game = wo_config_resolve_path(mod, wo_json_str(previous, "GameSource", ""), NULL);
    bool source_moved = old_game && old_game[0] && !wo_str_ieq(old_game, config.game_source);
    bool needs_repair = force_repair || !healthy ||
                        !wo_str_eq(wo_json_str(receipt, "version", ""), WO_VERSION_STRING) ||
                        source_moved;
    wo_free(old_game);
    if (ok && needs_repair) {
        result->repaired = true;
        ok = repair(mod, &config, receipt, healthy, progress, error);
        if (ok) {
            wo_json_free(receipt);
            receipt = NULL;
            wo_config_free(&config);
            ok = wo_receipt_load(mod, &receipt, error) && wo_config_load(mod, &config, error);
        }
        wo_laps_mark(laps, "repair_ms");
    }
    char *ini = NULL;
    char *ddraw = NULL;
    bool settings_missing = false;
    if (ok) {
        ini = wo_path_join(mod->game, "Wipeout2097.ini", error);
        ddraw = ini ? wo_path_join(mod->game, "DDrawCompat-Wipeout2097.ini", error) : NULL;
        ok = ddraw != NULL;
        settings_missing = ok && (!wo_is_file(ini) || !wo_is_file(ddraw));
        wo_progress(progress, WO_STAGE_RUNTIME, 30, "Updating the mod runtime...");
        ok = ok && sync_runtime(mod, cache, &result->runtime_updated, error);
        wo_laps_mark(laps, "runtime_ms");
    }
    bool ps1_textures = false;
    ok = ok && ensure_native_textures(mod, &config, receipt, plan, cache, progress, &ps1_textures,
                                     error);
    wo_laps_mark(laps, "textures_ms");
    MusicResult music = {0};
    ok = ok && ensure_music(mod, &config, receipt, cache, progress, &music, error);
    wo_laps_mark(laps, "music_ms");
    if (ok && config.soundtrack == WO_SOUNDTRACK_PC && !music.pc_available) {
        config.soundtrack = music.psx_available ? WO_SOUNDTRACK_PSX : WO_SOUNDTRACK_NONE;
        WoConfig updated;
        ok = wo_config_save(mod, &config, &updated, error);
        if (ok) {
            wo_config_free(&config);
            config = updated;
        }
    }
    if (ok) {
        set_stage(progress, WO_STAGE_RUNTIME);
        bool changed = settings_missing ||
                       wo_json_int(receipt, "configuration_revision", -1) != config.revision;
        if (changed) {
            bool was_none = wo_str_eq(wo_json_str(receipt, "default_soundtrack", ""), "None");
            ok = wo_settings_apply_soundtrack(ini, config.soundtrack, was_none, error) &&
                 wo_settings_set_sound_mode(ini, config.sound_mode, error) &&
                 wo_settings_set_fullscreen(ddraw, config.fullscreen, error);
        }
    }
    if (ok && receipt) {
        wo_json_put_int(receipt, "configuration_revision", config.revision);
        wo_json_put_bool(receipt, "hd_textures", config.hd_textures);
        wo_json_put_bool(receipt, "clean_audio", config.clean_audio);
        wo_json_put_bool(receipt, "pc_music_available", music.pc_available);
        wo_json_put_bool(receipt, "playstation_available", music.psx_available);
        wo_json_put_bool(receipt, "playstation_textures", ps1_textures);
        wo_json_put_str(receipt, "default_soundtrack", wo_soundtrack_name(config.soundtrack));
        if (!wo_json_has(receipt, "pc_files")) {
            WoJson *pc_files = NULL;
            ok = wo_pc_file_receipt(mod, mod->game, &pc_files, error) &&
                 wo_json_put(receipt, "pc_files", pc_files);
        }
        ok = ok && wo_receipt_save(mod, receipt, error);
    }
    wo_laps_mark(laps, "receipt_ms");
    if (ok) {
        int kept = 0;
        ok = wo_cleanup_retired(mod, &result->retired_removed, &kept, error);
        wo_laps_mark(laps, "cleanup_ms");
    }
    result->installed = ok;
    wo_free(ini);
    wo_free(ddraw);
    wo_json_free(files);
    wo_json_free(patches);
    wo_json_free(plan);
    wo_json_free(receipt);
    wo_config_free(&config);
    return ok;
}

/* --- Asset refresh ----------------------------------------------------------------- */

/* Repair-Layout: retire the generated files of the old textures/hd folder,
 * saving any user-edited copy under backups/artwork-migration. */
static bool migrate_layout(const WoMod *mod, int *removed, WoError *error) {
    *removed = 0;
    char *legacy = wo_mod_game_path(mod, "textures/hd", error);
    if (!legacy) {
        return false;
    }
    if (wo_chain_has_link(mod->root, legacy)) {
        wo_free(legacy);
        return wo_fail(error, WO_ERR_UNSAFE, "Layout cleanup refuses linked directories.");
    }
    static const char *const names[] = {"font-and-numbers", "hud-gauges", "notice", "title"};
    bool ok = true;
    for (size_t i = 0; ok && i < WO_COUNT(names) && wo_is_dir(legacy); i++) {
        char *canonical_rel = wo_format(error, "assets/hd/%s.png", names[i]);
        char *canonical = canonical_rel ? wo_mod_path(mod, canonical_rel, error) : NULL;
        wo_free(canonical_rel);
        if (!canonical || !wo_is_file(canonical)) {
            wo_free(canonical);
            continue;
        }
        char *png_name = wo_format(error, "%s.png", names[i]);
        char *png = png_name ? wo_path_child(legacy, png_name, error) : NULL;
        wo_free(png_name);
        char a[WO_SHA256_HEX_SIZE];
        char b[WO_SHA256_HEX_SIZE];
        if (png && wo_is_file(png)) {
            if (wo_sha256_file(png, a, error) && wo_sha256_file(canonical, b, error) &&
                !wo_digest_eq(a, b)) {
                char *saved_rel = wo_format(error, "backups/artwork-migration/%s-%s.png", a, names[i]);
                char *saved = saved_rel ? wo_mod_path(mod, saved_rel, error) : NULL;
                ok = saved && (wo_is_file(saved) || wo_copy_file(png, saved, error));
                wo_free(saved);
                wo_free(saved_rel);
            }
            ok = ok && wo_delete_file(png, error);
            (*removed)++;
        }
        static const char *const generated[] = {".bmp", ".rgba"};
        for (size_t g = 0; ok && g < WO_COUNT(generated); g++) {
            char *name = wo_format(error, "%s%s", names[i], generated[g]);
            char *path = name ? wo_path_child(legacy, name, error) : NULL;
            if (path && wo_is_file(path)) {
                ok = wo_delete_file(path, error);
                (*removed)++;
            }
            wo_free(path);
            wo_free(name);
        }
        wo_free(png);
        wo_free(canonical);
    }
    char *state = ok ? wo_path_child(legacy, "install-state.json", error) : NULL;
    if (state && wo_is_file(state)) {
        ok = wo_delete_file(state, error);
        (*removed)++;
    }
    wo_free(state);
    if (ok && wo_is_dir(legacy) && wo_dir_is_empty(legacy)) {
        wo_delete_empty_dir(legacy, NULL);
    }
    wo_free(legacy);
    return ok;
}

static bool refresh_assets(const WoMod *mod, WoProgress *progress, WoHashCache *cache,
                           WoLaps *laps, WoError *error) {
    if (wo_mod_game_running(mod)) {
        return wo_fail(error, WO_ERR_BUSY,
                       "Close the game before refreshing its artwork or sound effects.");
    }
    wo_progress(progress, WO_STAGE_HD_ART, 85, "Refreshing artwork and audio...");
    int removed = 0;
    WoJson *receipt = NULL;
    WoJson *patches = NULL;
    WoJson *catalog = NULL;
    bool ok = migrate_layout(mod, &removed, error) && wo_receipt_load(mod, &receipt, error);
    if (ok && !receipt) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND, "Run Setup / Install / repair before playing.");
    }
    char *effects = ok ? wo_mod_game_path(mod, "audio/source", error) : NULL;
    if (ok && (!effects || !wo_is_dir(effects))) {
        set_stage(progress, WO_STAGE_SOUND_EFFECTS);
        ok = wo_fail(error, WO_ERR_NOT_FOUND,
                     "Run Setup / Install / repair once to retain the original effects for automatic "
                     "audio refresh.");
    }
    ok = ok && wo_load_manifest(mod, "manifests/patches.json", &patches, error) &&
         wo_load_manifest(mod, "manifests/hd-textures.json", &catalog, error);
    wo_laps_mark(laps, "layout_ms");
    WoHdResult art = {0};
    if (ok && wo_json_bool(receipt, "hd_textures", false)) {
        set_stage(progress, WO_STAGE_HD_ART);
        char *canonical = wo_mod_path(mod, "assets/hd", error);
        WoJson *plan = NULL;
        ok = wo_load_manifest(mod, "manifests/texture-selection.json", &plan, error);
        WoHdOptions options = {catalog, canonical, canonical, mod->game, cache, plan};
        ok = ok && canonical && wo_hd_install(&options, &art, error);
        wo_json_free(plan);
        wo_free(canonical);
        wo_laps_mark(laps, "art_ms");
    }
    WoSfxResult audio = {0};
    if (ok) {
        set_stage(progress, WO_STAGE_SOUND_EFFECTS);
        WoSfxOptions options = {patches, effects, mod->game,
                                !wo_json_bool(receipt, "clean_audio", true), true, cache};
        ok = wo_sfx_install(&options, &audio, error);
        wo_laps_mark(laps, "sfx_ms");
    }
    if (ok) {
        char stamp[40];
        wo_log_timestamp(stamp);
        WoJson *summary = wo_json_new_object();
        wo_json_put_str(summary, "at", stamp);
        wo_json_put_str(summary, "state", "complete");
        wo_json_put_int(summary, "layout", 2);
        wo_json_put_int(summary, "obsolete_files_removed", removed);
        wo_json_put_int(summary, "art_checked", (int64_t)art.checked);
        wo_json_put_int(summary, "art_updated", (int64_t)art.updated);
        wo_json_put_int(summary, "audio_checked", (int64_t)audio.checked);
        wo_json_put_int(summary, "audio_updated", (int64_t)audio.updated);
        if (art.files) {
            wo_json_put(summary, "art_files", wo_json_clone(art.files));
        }
        char *path = wo_mod_path(mod, "logs/asset-refresh.json", NULL);
        if (path) {
            wo_write_json(path, summary, NULL);
        }
        wo_free(path);
        wo_log_event("asset_refresh", summary);
        wo_laps_mark(laps, "summary_ms");
    }
    wo_hd_result_free(&art);
    wo_free(effects);
    wo_json_free(catalog);
    wo_json_free(patches);
    wo_json_free(receipt);
    return ok;
}

bool wo_refresh_assets(const WoMod *mod, WoProgress *progress, WoError *error) {
    return refresh_assets(mod, progress, NULL, NULL, error);
}

/* The integrity cache (.state/integrity.json, see wo_hashcache.h) lets an
 * unchanged installation be checked without reading every file. Install /
 * repair (force_repair) ignores it and verifies everything again. It is
 * only ever an optimisation: when it cannot be opened or saved, the check
 * reads every file as before. */
static WoHashCache *open_integrity_cache(const WoMod *mod, bool fresh) {
    char *path = wo_path_child(mod->state, "integrity.json", NULL);
    if (path && wo_chain_has_link(mod->root, path)) {
        /* Like the other state files: never written through a link. */
        wo_free(path);
        path = NULL;
    }
    const char *note = NULL;
    WoError error = {0};
    WoHashCache *cache = wo_hash_cache_open(path, mod->root, fresh, &note, &error);
    if (note) {
        wo_log_text("integrity_cache_reset", note);
    }
    if (!cache) {
        wo_log_text("integrity_cache_unavailable", error.message);
    }
    wo_free(path);
    return cache;
}

static void close_integrity_cache(WoHashCache *cache, bool complete, bool fresh) {
    if (!cache) {
        return;
    }
    WoError error = {0};
    /* Only a complete check knows which files are still checked. */
    if (!wo_hash_cache_save(cache, complete, &error)) {
        wo_log_text("integrity_cache_not_saved", error.message);
    }
    WoHashCacheStats stats;
    wo_hash_cache_stats(cache, &stats);
    WoJson *data = wo_json_new_object();
    wo_json_put_bool(data, "loaded", stats.loaded);
    wo_json_put_bool(data, "full_check", fresh || !stats.loaded);
    wo_json_put_int(data, "entries", (int64_t)stats.entries);
    wo_json_put_int(data, "reused", (int64_t)stats.hits);
    wo_json_put_int(data, "hashed", (int64_t)stats.hashed);
    wo_json_put_int(data, "hashed_bytes", (int64_t)stats.hashed_bytes);
    wo_json_put_int(data, "not_remembered", (int64_t)stats.racy);
    wo_log_event("integrity_cache", data);
    wo_hash_cache_free(cache);
}

bool wo_reconcile(const WoMod *mod, bool force_repair, WoProgress *progress,
                  WoReconcileResult *result, WoError *error) {
    WoModLock lock;
    if (!wo_mod_lock(mod, 0, &lock, error)) {
        memset(result, 0, sizeof(*result));
        return false;
    }
    WoLaps laps;
    wo_laps_begin(&laps);
    WoHashCache *cache = open_integrity_cache(mod, force_repair);
    wo_laps_mark(&laps, "cache_open_ms");
    bool ok = reconcile_inner(mod, force_repair, progress, cache, &laps, result, error);
    if (ok && result->installed) {
        ok = refresh_assets(mod, progress, cache, &laps, error);
    }
    close_integrity_cache(cache, ok && result->installed, force_repair);
    wo_laps_mark(&laps, "cache_save_ms");
    if (ok) {
        wo_mod_prune(mod);
        wo_laps_mark(&laps, "prune_ms");
        wo_progress(progress, WO_STAGE_GENERAL, 100, "Installation checked and ready.");
    }
    wo_laps_log(&laps, "reconcile_timing");
    wo_mod_unlock(&lock);
    return ok;
}
