/* Install / repair: build a complete game folder in a scratch area from the
 * configured sources, verify it, then swap it in, keeping the previous one
 * in backups/. Mirrors the previous Install-Mod.ps1 step by step. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core/wo_deband.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hdtex.h"
#include "core/wo_install.h"
#include "core/wo_log.h"
#include "core/wo_music.h"
#include "core/wo_natives.h"
#include "core/wo_ops.h"
#include "core/wo_patch_file.h"
#include "core/wo_sdtex.h"
#include "core/wo_settings.h"
#include "core/wo_sfx.h"
#include "core/wo_sources.h"
#include "wo_version.h"

#define MANIFEST_MAX (16u << 20)

/* Each distinct source is unpacked at most once per installation. */
typedef struct InputCache {
    const char *work;
    WoStrList sources;
    WoStrList folders;
} InputCache;

static const char *cached_input(InputCache *cache, const char *source, WoError *error) {
    for (size_t i = 0; i < cache->sources.count; i++) {
        if (wo_str_ieq(cache->sources.items[i], source)) {
            return cache->folders.items[i];
        }
    }
    char *resolved = wo_resolve_input(source, cache->work, error);
    if (!resolved || !wo_strlist_push(&cache->sources, source, error) ||
        !wo_strlist_push_owned(&cache->folders, resolved, error)) {
        return NULL;
    }
    return cache->folders.items[cache->folders.count - 1];
}

static void cache_free(InputCache *cache) {
    wo_strlist_free(&cache->sources);
    wo_strlist_free(&cache->folders);
}

bool wo_load_manifest(const WoMod *mod, const char *relative, WoJson **out, WoError *error) {
    char *path = wo_mod_path(mod, relative, error);
    bool ok = path && wo_read_json(path, MANIFEST_MAX, out, error);
    wo_free(path);
    return ok;
}

bool wo_pc_file_receipt(const WoMod *mod, const char *game_root, WoJson **out, WoError *error) {
    *out = NULL;
    WoJson *files = NULL;
    WoJson *patches = NULL;
    if (!wo_load_manifest(mod, "manifests/game-files.json", &files, error) ||
        !wo_load_manifest(mod, "manifests/patches.json", &patches, error)) {
        wo_json_free(files);
        return false;
    }
    WoJson *records = wo_json_new_array();
    bool ok = records != NULL;
    const WoJson *required = wo_json_get(files, "required");
    size_t total = wo_json_length(required) + wo_json_length(wo_json_get(patches, "audio"));
    for (size_t i = 0; ok && i < total; i++) {
        char *relative;
        if (i < wo_json_length(required)) {
            relative = wo_strdup(wo_json_as_str(wo_json_at(required, i), ""), error);
        } else {
            const WoJson *entry = wo_json_at(wo_json_get(patches, "audio"), i - wo_json_length(required));
            relative = wo_format(error, "audio/source/%s", wo_json_str(entry, "name", ""));
        }
        char *path = relative ? wo_path_child(game_root, relative, error) : NULL;
        char digest[WO_SHA256_HEX_SIZE];
        if (path && !wo_is_file(path)) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND, "Missing Windows game data: %s", relative);
        } else {
            ok = path && wo_sha256_file(path, digest, error);
        }
        if (ok) {
            WoJson *record = wo_json_new_object();
            ok = record && wo_json_put_str(record, "file", relative) &&
                 wo_json_put_str(record, "sha256", digest) && wo_json_push(records, record);
        }
        wo_free(path);
        wo_free(relative);
    }
    wo_json_free(files);
    wo_json_free(patches);
    if (!ok) {
        wo_json_free(records);
        return false;
    }
    *out = records;
    return true;
}

static bool copy_between(const char *from_root, const char *from, const char *to_root, const char *to,
                         bool required, WoError *error) {
    char *source = wo_path_child(from_root, from, error);
    char *target = source ? wo_path_child(to_root, to, error) : NULL;
    bool ok = target != NULL;
    if (ok && wo_is_file(source)) {
        ok = wo_copy_file(source, target, error);
    } else if (ok && required) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND, "The mod download is incomplete: %s", from);
    }
    wo_free(source);
    wo_free(target);
    return ok;
}

bool wo_install_runtime(const WoMod *mod, const char *game_root, WoError *error) {
    static const char *const required[] = {"wohr.dll", "ddraw.dll", "wo2097-controller.exe",
                                           "wo4k-video.exe", "DDrawCompat-Wipeout2097.ini"};
    char *runtime = wo_mod_path(mod, "runtime", error);
    bool ok = runtime != NULL;
    for (size_t i = 0; ok && i < WO_COUNT(required); i++) {
        ok = copy_between(runtime, required[i], game_root, required[i], true, error);
    }
    WoDirList list = {0};
    ok = ok && wo_list_dir(runtime, &list, error);
    for (size_t i = 0; ok && i < list.count; i++) {
        const char *name = list.items[i].name;
        if (!list.items[i].is_dir && wo_str_istarts_with(name, "lib") && wo_str_iends_with(name, ".dll")) {
            ok = copy_between(runtime, name, game_root, name, true, error);
        }
    }
    wo_dirlist_free(&list);
    static const char *const shaders[] = {"w2097-hd.cg", "w2097-hd.cgp"};
    for (size_t i = 0; ok && i < WO_COUNT(shaders); i++) {
        char *target = wo_format(error, "common-shaders-master/w2097/%s", shaders[i]);
        ok = target && copy_between(runtime, shaders[i], game_root, target, true, error);
        wo_free(target);
    }
    wo_free(runtime);
    return ok;
}

static bool exists_in(const char *root, const char *relative) {
    char *path = wo_path_child(root, relative, NULL);
    bool exists = path && wo_is_file(path);
    wo_free(path);
    return exists;
}

static bool carry_movies(const char *target, const char *stage, WoError *error) {
    static const char *const names[] = {"intro", "WXLINTR", "XTRO1", "XTRO2", "XTRO3", "XTRO4"};
    bool ok = true;
    for (size_t i = 0; ok && i < WO_COUNT(names); i++) {
        char *mp4 = wo_format(error, "movies/%s.mp4", names[i]);
        if (mp4 && exists_in(target, mp4)) {
            /* Drop-in MP4s belong to the user. The original MPEG stays: it
             * plays when Windows cannot decode the replacement. */
            ok = copy_between(target, mp4, stage, mp4, true, error);
        }
        wo_free(mp4);
    }
    return ok;
}

static bool install_bank(const char *input, const char *work_folder, const char *stage,
                         WoMusicBank bank, bool allow_missing, bool *installed, WoError *error) {
    *installed = false;
    bool extracted = false;
    if (!wo_music_extract(input, work_folder, bank, allow_missing, &extracted, error)) {
        return false;
    }
    if (extracted) {
        *installed = wo_music_install(work_folder, stage, bank, NULL, error);
        return *installed;
    }
    return true;
}

static bool install_retained_bank(const char *target, const char *stage, WoMusicBank bank,
                                  bool *installed, WoError *error) {
    *installed = false;
    char *folder = NULL;
    if (!wo_is_dir(target) || !wo_music_find_folder(target, bank, &folder, error)) {
        return !wo_error_is_set(error);
    }
    bool ok = true;
    if (folder) {
        ok = wo_music_install(folder, stage, bank, NULL, error);
        *installed = ok;
    }
    wo_free(folder);
    return ok;
}

static bool has_music_folder(const char *source, WoMusicBank bank) {
    char *folder = NULL;
    bool found = wo_music_find_folder(source, bank, &folder, NULL) && folder;
    wo_free(folder);
    return found;
}

static void set_stage(WoProgress *progress, WoStage stage) {
    if (progress) {
        progress->stage = stage;
    }
}

static char *timestamp_name(const char *prefix) {
    time_t now = time(NULL);
    struct tm local;
    localtime_s(&local, &now);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    return wo_format(NULL, "%s%s", prefix, stamp);
}

static bool apply_settings(const WoMod *mod, const char *stage, WoSoundtrack soundtrack,
                           const WoConfig *config, WoError *error) {
    char *ini = wo_path_join(stage, "Wipeout2097.ini", error);
    char *ddraw = ini ? wo_path_join(stage, "DDrawCompat-Wipeout2097.ini", error) : NULL;
    bool ok = ddraw != NULL;
    if (ok && !wo_is_file(ini)) {
        char *template_ini = wo_mod_path(mod, "runtime/Wipeout2097.ini", error);
        ok = template_ini && wo_copy_file(template_ini, ini, error);
        wo_free(template_ini);
    }
    /* Saves and preferences belong to the user; a repair keeps them. */
    static const char *const kept[] = {"CONFIG.DAT", "SAVEGAME.BIN", "Wipeout2097.ini",
                                       "DDrawCompat-Wipeout2097.ini"};
    for (size_t i = 0; ok && i < WO_COUNT(kept); i++) {
        if (exists_in(mod->game, kept[i])) {
            ok = copy_between(mod->game, kept[i], stage, kept[i], true, error);
        }
    }
    bool was_none = false;
    WoJson *prior = NULL;
    if (ok && wo_receipt_load(mod, &prior, NULL) && prior) {
        was_none = wo_str_eq(wo_json_str(prior, "default_soundtrack", ""), "None");
    }
    wo_json_free(prior);
    ok = ok && wo_settings_apply_soundtrack(ini, soundtrack, was_none, error) &&
         wo_settings_set_sound_mode(ini, config->sound_mode, error) &&
         wo_settings_set_fullscreen(ddraw, config->fullscreen, error);
    wo_free(ddraw);
    wo_free(ini);
    return ok;
}

static bool verify_stage(const char *stage, const WoJson *patches, WoError *error) {
    static const char *const required[] = {"Wipeout2097.exe", "wohr.dll", "ddraw.dll",
                                           "Common/MENU.DAT", "Track01/TRACK.WAD",
                                           "textures/sd/Pics/Hi-res/HUD2.tga", "audio/ENGINES.WAV"};
    for (size_t i = 0; i < WO_COUNT(required); i++) {
        if (!exists_in(stage, required[i])) {
            return wo_fail(error, WO_ERR_NOT_FOUND, "Installation is incomplete: %s", required[i]);
        }
    }
    char *exe = wo_path_join(stage, "Wipeout2097.exe", error);
    char digest[WO_SHA256_HEX_SIZE];
    bool ok = exe && wo_sha256_file(exe, digest, error);
    if (ok && !wo_digest_eq(digest, wo_json_str(patches, "output_sha256", ""))) {
        ok = wo_fail(error, WO_ERR_CHECKSUM, "Final executable checksum failed.");
    }
    wo_free(exe);
    return ok;
}

static WoJson *build_receipt(const WoMod *mod, const char *stage, const WoConfig *saved,
                             WoSoundtrack soundtrack, bool pc_available, bool psx_available,
                             bool ps1_textures, const WoJson *patches, WoError *error) {
    char stamp[40];
    wo_log_timestamp(stamp);
    WoJson *portable = NULL;
    wo_state_load(mod, "setup.json", &portable, NULL);
    WoJson *pc_files = NULL;
    if (!wo_pc_file_receipt(mod, stage, &pc_files, error)) {
        wo_json_free(portable);
        return NULL;
    }
    WoJson *receipt = wo_json_new_object();
    bool ok = receipt && wo_json_put_str(receipt, "version", WO_VERSION_STRING) &&
              wo_json_put_str(receipt, "installed_at", stamp) &&
              wo_json_put_str(receipt, "default_soundtrack", wo_soundtrack_name(soundtrack)) &&
              wo_json_put_bool(receipt, "pc_music_available", pc_available) &&
              wo_json_put_bool(receipt, "playstation_available", psx_available) &&
              wo_json_put_bool(receipt, "playstation_textures", ps1_textures) &&
              /* Made from both games: see wo_is_update. */
              wo_json_put_bool(receipt, "playstation_required", true) &&
              wo_json_put_bool(receipt, "hd_textures", saved->hd_textures) &&
              wo_json_put_bool(receipt, "clean_audio", saved->clean_audio) &&
              wo_json_put_str(receipt, "executable_sha256", wo_json_str(patches, "output_sha256", "")) &&
              wo_json_put_int(receipt, "configuration_revision", saved->revision) &&
              wo_json_put(receipt, "configuration", portable ? portable : wo_json_new_object()) &&
              wo_json_put(receipt, "pc_files", pc_files);
    if (!ok) {
        wo_json_free(receipt);
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        return NULL;
    }
    return receipt;
}

static bool swap_in(const WoMod *mod, const char *stage, WoError *error) {
    char *backup = NULL;
    if (wo_is_dir(mod->game)) {
        char *name = timestamp_name("backups/wobble-");
        backup = name ? wo_mod_path(mod, name, error) : NULL;
        wo_free(name);
        if (backup && wo_exists(backup)) {
            char *unique = wo_format(error, "%s-%lu", backup, (unsigned long)time(NULL) % 1000u);
            wo_free(backup);
            backup = unique;
        }
        bool ok = backup && wo_mkparent(backup, error) && wo_move(mod->game, backup, error);
        if (!ok) {
            wo_free(backup);
            return false;
        }
    }
    bool ok = wo_move(stage, mod->game, error);
    if (!ok && backup && !wo_exists(mod->game)) {
        wo_move(backup, mod->game, NULL);
    }
    wo_free(backup);
    return ok;
}

static bool install_locked(const WoMod *mod, const WoConfig *config, bool preserve_configuration,
                           WoProgress *progress, WoError *error);

void wo_install_debanded(const WoMod *mod, const char *game_root, const WoJson *plan,
                         const WoJson *native_receipt, WoHashCache *cache) {
    WoError error = {0};
    char *selection = wo_mod_path(mod, "manifests/deband-selection.json", &error);
    char *output = selection ? wo_mod_path(mod, "assets/sd", &error) : NULL;
    bool ok = output != NULL;
    if (ok) {
        WoDebandOptions options = {game_root, output, selection, plan, native_receipt, false, cache};
        WoDebandResult result;
        ok = wo_deband_install(&options, &result, &error);
    }
    if (!ok) {
        wo_log_text("textures_not_debanded", error.message);
    }
    wo_free(output);
    wo_free(selection);
}

bool wo_is_update(const WoMod *mod) {
    WoJson *receipt = NULL;
    bool update = wo_receipt_load(mod, &receipt, NULL) && receipt &&
                  !wo_json_bool(receipt, "playstation_required", false);
    wo_json_free(receipt);
    return update;
}

bool wo_install(const WoMod *mod, const WoConfig *config, bool preserve_configuration,
                WoProgress *progress, WoError *error) {
    WoModLock lock;
    if (!wo_mod_lock(mod, 0, &lock, error)) {
        return false;
    }
    bool ok = install_locked(mod, config, preserve_configuration, progress, error);
    wo_mod_unlock(&lock);
    return ok;
}

static bool install_locked(const WoMod *mod, const WoConfig *config, bool preserve_configuration,
                           WoProgress *progress, WoError *error) {
    wo_progress(progress, WO_STAGE_GAME_SOURCE, 3, "Verifying sources");
    if (!config->game_source || !config->game_source[0]) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "Select the Windows game source.");
    }
    if (wo_mod_game_running(mod)) {
        return wo_fail(error, WO_ERR_BUSY, "Close this mod installation before updating it.");
    }
    WoJson *patches = NULL;
    WoJson *plan = NULL;
    WoJson *catalog = NULL;
    if (!wo_load_manifest(mod, "manifests/patches.json", &patches, error) ||
        !wo_load_manifest(mod, "manifests/texture-selection.json", &plan, error) ||
        !wo_load_manifest(mod, "manifests/hd-textures.json", &catalog, error)) {
        wo_json_free(patches);
        wo_json_free(plan);
        return false;
    }
    char *session = wo_mod_new_work_dir(mod, "i-", error);
    char *stage = session ? wo_path_join(session, "wobble", error) : NULL;
    char *inputs = stage ? wo_path_join(session, "inputs", error) : NULL;
    InputCache cache = {inputs, {0}, {0}};
    WoSoundtrack soundtrack = config->soundtrack;
    bool pc_available = false;
    bool psx_available = false;
    bool ps1_textures = false;
    char *native = NULL;
    bool ok = inputs && wo_mkdirs(stage, error) && wo_mkdirs(inputs, error);

    const char *game_input = ok ? cached_input(&cache, config->game_source, error) : NULL;
    ok = game_input != NULL;
    if (ok) {
        native = wo_native_root(game_input, inputs, patches, error);
        ok = native != NULL;
    }
    ok = ok && wo_require_retail_sheets(plan, native, error);
    if (ok) {
        wo_progress(progress, WO_STAGE_GAME_SOURCE, 12,
                    "Copying game data and applying the executable patch");
        ok = wo_copy_game_data(native, stage, error) &&
             wo_patch_install_executable(native, stage, patches, mod->root, error) &&
             carry_movies(mod->game, stage, error);
    }
    char *sd = ok ? wo_path_join(stage, "textures/sd", error) : NULL;
    if (ok) {
        wo_progress(progress, WO_STAGE_TEXTURES, 24, "Extracting SD textures");
        ok = sd && wo_sd_extract(native, sd, error) && wo_sd_add_regional_aliases(sd, error);
    }
    const char *psx_input = NULL;
    if (ok && config->psx_source[0]) {
        set_stage(progress, WO_STAGE_PS1_SOURCE);
        psx_input = cached_input(&cache, config->psx_source, error);
        ok = psx_input != NULL;
    }
    if (ok) {
        wo_progress(progress, WO_STAGE_PS1_SOURCE, 29,
                    "Restoring original texture colours and importing optional PlayStation artwork");
        WoNativeOptions options = {0};
        options.game_root = stage;
        options.plan = plan;
        options.ps1_source = psx_input;
        options.configured_source = psx_input ? config->psx_source : NULL;
        options.retain_from = wo_is_dir(mod->game) ? mod->game : NULL;
        /* A previous WO4K installation as the source brings its patched
         * sheets (wo_sd_extract); only original game data is pristine. */
        char *previous_sd = wo_path_join(native, "textures/sd", error);
        options.sheets_pristine = previous_sd && !wo_is_dir(previous_sd);
        wo_free(previous_sd);
        WoJson *receipt = NULL;
        ok = wo_natives_install(&options, &receipt, error);
        ps1_textures = wo_json_bool(receipt, "ps1_textures", false);
        if (ok && !ps1_textures) {
            /* No disc given, or one with music only, and none imported before. */
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         WO_SOURCES_REQUIRED_MESSAGE
                         " Select a PlayStation disc image of WipEout 2097 / XL in Setup.");
        }
        if (ok) {
            wo_progress(progress, WO_STAGE_TEXTURES, 32, "Smoothing the colour bands of the textures");
            wo_install_debanded(mod, stage, plan, receipt, NULL);
        }
        wo_json_free(receipt);
    }
    if (ok) {
        wo_progress(progress, WO_STAGE_SOUND_EFFECTS, 34, "Processing sound effects locally");
        char *effects = wo_path_join(session, "audio-original", error);
        WoSfxOptions options = {patches, effects, stage, !config->clean_audio, false, NULL};
        WoSfxResult result;
        ok = effects && wo_copy_sound_effects(native, effects, error) &&
             wo_sfx_install(&options, &result, error);
        wo_free(effects);
    }
    if (ok) {
        wo_progress(progress, WO_STAGE_SOUNDTRACK, 46, "Extracting the PC soundtrack");
        const char *pc_source = config->pc_music_source[0] ? config->pc_music_source
                                : has_music_folder(native, WO_MUSIC_PC) ? native
                                                                        : config->game_source;
        const char *pc_input = cached_input(&cache, pc_source, error);
        char *folder = wo_path_join(session, "pc-music", error);
        ok = pc_input && folder &&
             install_bank(pc_input, folder, stage, WO_MUSIC_PC, !config->pc_music_source[0],
                          &pc_available, error);
        if (ok && !pc_available) {
            ok = install_retained_bank(mod->game, stage, WO_MUSIC_PC, &pc_available, error);
            if (ok && !pc_available) {
                wo_progress(progress, WO_STAGE_SOUNDTRACK, 58,
                            "No PC music included; continuing without it");
            }
        }
        wo_free(folder);
    }
    if (ok) {
        const char *psx_music = psx_input;
        if (!psx_music && has_music_folder(native, WO_MUSIC_PSX)) {
            psx_music = native;
        }
        if (!psx_music && wo_is_dir(mod->game) && has_music_folder(mod->game, WO_MUSIC_PSX)) {
            psx_music = mod->game;
        }
        if (psx_music) {
            wo_progress(progress, WO_STAGE_SOUNDTRACK, 64,
                        "Extracting the optional PlayStation soundtrack");
            char *folder = wo_path_join(session, "psx-music", error);
            bool installed = false;
            ok = folder && install_bank(psx_music, folder, stage, WO_MUSIC_PSX, true, &installed, error);
            if (ok && !installed) {
                ok = install_retained_bank(mod->game, stage, WO_MUSIC_PSX, &installed, error);
                if (ok && !installed && soundtrack == WO_SOUNDTRACK_PSX) {
                    ok = wo_fail(error, WO_ERR_NOT_FOUND,
                                 "This PlayStation source supplies textures but no CD audio. Choose "
                                 "No music or supply the full CUE/BIN set.");
                }
            }
            wo_free(folder);
        } else if (soundtrack == WO_SOUNDTRACK_PSX) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         "PlayStation default selected, but no PlayStation soundtrack source was "
                         "provided.");
        }
        psx_available = exists_in(stage, "music/psx/tracks.ini");
        if (soundtrack == WO_SOUNDTRACK_PC && !pc_available) {
            soundtrack = psx_available ? WO_SOUNDTRACK_PSX : WO_SOUNDTRACK_NONE;
        }
    }
    if (ok) {
        wo_progress(progress, WO_STAGE_RUNTIME, 82, "Installing the mod runtime and artwork");
        ok = wo_install_runtime(mod, stage, error);
    }
    if (ok && config->hd_textures) {
        set_stage(progress, WO_STAGE_HD_ART);
        char *canonical = wo_mod_path(mod, "assets/auth", error);
        char *upscaled = canonical ? wo_mod_path(mod, "assets/hd", error) : NULL;
        WoHdOptions options = {catalog, canonical, canonical, stage, NULL, plan, upscaled};
        WoHdResult result = {0};
        ok = upscaled && wo_hd_install(&options, &result, error);
        wo_hd_result_free(&result);
        wo_free(upscaled);
        wo_free(canonical);
    }
    if (ok) {
        set_stage(progress, WO_STAGE_RUNTIME);
        ok = apply_settings(mod, stage, soundtrack, config, error);
    }
    if (ok) {
        wo_progress(progress, WO_STAGE_RUNTIME, 93, "Verifying the completed installation");
        ok = verify_stage(stage, patches, error);
    }
    WoConfig saved = {0};
    if (ok) {
        if (preserve_configuration) {
            ok = wo_config_load(mod, &saved, error);
        } else {
            WoConfig final = *config;
            final.soundtrack = soundtrack;
            ok = wo_config_save(mod, &final, &saved, error);
        }
    }
    WoJson *receipt = ok ? build_receipt(mod, stage, &saved, soundtrack, pc_available, psx_available,
                                         ps1_textures, patches, error)
                         : NULL;
    if (ok) {
        char *receipt_path = wo_path_join(stage, "installation.json", error);
        ok = receipt && receipt_path && wo_write_json(receipt_path, receipt, error) &&
             swap_in(mod, stage, error);
        wo_free(receipt_path);
    }
    wo_json_free(receipt);
    wo_config_free(&saved);
    cache_free(&cache);
    wo_free(sd);
    wo_free(native);
    wo_free(inputs);
    wo_free(stage);
    if (session) {
        /* Scratch can hold whole unpacked discs; the log keeps any error. */
        wo_mod_remove_work_dir(mod, session);
    }
    wo_free(session);
    wo_json_free(patches);
    wo_json_free(plan);
    wo_json_free(catalog);
    if (ok) {
        wo_mod_prune(mod);
        wo_progress(progress, WO_STAGE_GENERAL, 100, "Ready to race");
    }
    return ok;
}
