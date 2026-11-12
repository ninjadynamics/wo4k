#include "core/wo_natives.h"

#include <stdio.h>
#include <string.h>

#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_iso.h"
#include "core/wo_log.h"
#include "core/wo_music.h"
#include "core/wo_texfmt.h"
#include "core/wo_texsrc.h"
#include "core/wo_tree.h"

#define SYSTEM_CNF_MAX_BYTES 65536u
#define SHEET_MAX_BYTES (64u * 1024u * 1024u)
#define RECEIPT_MAX_BYTES (16u * 1024u * 1024u)
#define PS1_SOURCE_NOTE "Fixed project-time map; identical PC originals reused"

void wo_ps1_disc_free(WoPs1Disc *disc) {
    if (disc) {
        wo_free(disc->image);
        memset(disc, 0, sizeof(*disc));
    }
}

/* --- Finding the PlayStation disc ----------------------------------------------- */

bool wo_ps1_serial_in(const void *system_cnf, size_t len) {
    /* Europe, USA and Japan/Asia. Every texture and track file is
     * byte-identical on the three discs (285 files compared). */
    static const char *const serials[] = {"SLES" "003.27", "SCUS" "943.51", "SIPS" "600.10"};
    const uint8_t *text = system_cnf;
    for (size_t s = 0; s < WO_COUNT(serials); s++) {
        for (size_t i = 0; i + 11 <= len; i++) {
            bool same = true;
            for (size_t j = 0; j < 4 && same; j++) {
                same = wo_ascii_lower((char)text[i + j]) == wo_ascii_lower(serials[s][j]);
            }
            if (same && (text[i + 4] == '_' || text[i + 4] == '-') &&
                memcmp(text + i + 5, serials[s] + 4, 6) == 0) {
                return true;
            }
        }
    }
    return false;
}

static const WoIsoEntry *find_file(const WoIso *iso, const char *path) {
    for (size_t i = 0; i < iso->count; i++) {
        if (!iso->entries[i].is_dir && wo_str_ieq(iso->entries[i].path, path)) {
            return &iso->entries[i];
        }
    }
    return NULL;
}

/* Errors while reading an image only mean it is not the disc. */
static bool is_ps1_texture_disc(const char *image, uint64_t offset) {
    WoSource source;
    if (!wo_source_open_file(&source, image, NULL)) {
        return false;
    }
    bool match = false;
    WoIso iso;
    if (wo_iso_open(&iso, &source, offset, NULL)) {
        const WoIsoEntry *cnf = find_file(&iso, "SYSTEM.CNF");
        uint8_t *text = NULL;
        if (cnf && wo_iso_read_entry(&iso, cnf, SYSTEM_CNF_MAX_BYTES, &text, NULL)) {
            match = wo_ps1_serial_in(text, cnf->length) &&
                    wo_iso_find(&iso, "WIPEOUT2/TEXTURES/EXPLODE.CMP") != NULL;
            wo_free(text);
        }
        wo_iso_close(&iso);
    }
    wo_source_close(&source);
    return match;
}

static bool is_descriptor_extension(const char *ext) {
    return wo_str_ieq(ext, ".cue") || wo_str_ieq(ext, ".mds");
}

static bool list_disc_candidates(const char *source, WoStrList *out, WoError *error) {
    if (!wo_is_dir(source)) {
        return wo_strlist_push(out, source, error);
    }
    WoTreeList tree;
    if (!wo_tree_list(source, &tree, error)) {
        return false;
    }
    bool ok = true;
    /* Descriptors first, then raw images, each group in folder order. */
    for (int pass = 0; ok && pass < 2; pass++) {
        for (size_t i = 0; ok && i < tree.count; i++) {
            const char *ext = wo_path_extension(tree.items[i].path);
            bool descriptor = is_descriptor_extension(ext);
            bool image = wo_str_ieq(ext, ".iso") || wo_str_ieq(ext, ".bin");
            if (!tree.items[i].is_dir && ((pass == 0 && descriptor) || (pass == 1 && image))) {
                ok = wo_strlist_push(out, tree.items[i].path, error);
            }
        }
    }
    wo_tree_free(&tree);
    return ok;
}

static bool has_cue_sibling(const char *bin) {
    char *cue = wo_path_change_extension(bin, ".cue", NULL);
    bool exists = cue && wo_is_file(cue);
    wo_free(cue);
    return exists;
}

bool wo_ps1_find_disc(const char *source, WoPs1Disc *out, bool *found, WoError *error) {
    memset(out, 0, sizeof(*out));
    *found = false;
    WoStrList candidates = {0};
    if (!list_disc_candidates(source, &candidates, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && !*found && i < candidates.count; i++) {
        const char *candidate = candidates.items[i];
        const char *ext = wo_path_extension(candidate);
        bool described = is_descriptor_extension(ext) || wo_str_ieq(ext, ".mdf") ||
                         (wo_str_ieq(ext, ".bin") && has_cue_sibling(candidate));
        char *image = NULL;
        uint64_t offset = 0;
        if (described) {
            /* An unreadable descriptor (another rip's CUE) is skipped: the
             * disc may still be another candidate. */
            WoTrackList tracks;
            WoError unreadable = {0};
            if (!wo_disc_tracks(candidate, &tracks, &unreadable)) {
                wo_log_text("disc_descriptor_skipped", unreadable.message);
                continue;
            }
            const WoTrack *data = wo_tracks_first_data(&tracks);
            if (data) {
                image = wo_strdup(data->image, error);
                offset = data->offset;
                ok = image != NULL;
            }
            wo_tracks_free(&tracks);
        } else {
            image = wo_strdup(candidate, error);
            ok = image != NULL;
        }
        if (ok && image && is_ps1_texture_disc(image, offset)) {
            out->image = image;
            out->offset = offset;
            *found = true;
            image = NULL;
        }
        wo_free(image);
    }
    wo_strlist_free(&candidates);
    return ok;
}

/* --- Importing its textures ----------------------------------------------------- */

/* "WIPEOUT2/<COMMON|TEXTURES|TRACKnn>/<name>.<CMP|TIM|TTF|TEX>" or
 * "WIPEOUT2/TRACKnn/TRACK.WAD" -> the part after "WIPEOUT2/", or NULL. */
static const char *ps1_texture_relative(const char *path) {
    if (!wo_str_istarts_with(path, "WIPEOUT2/")) {
        return NULL;
    }
    const char *relative = path + 9;
    size_t dir = 0;
    if (wo_str_istarts_with(relative, "COMMON/")) {
        dir = 6;
    } else if (wo_str_istarts_with(relative, "TEXTURES/")) {
        dir = 8;
    } else if (wo_str_istarts_with(relative, "TRACK") && relative[5] >= '0' &&
               relative[5] <= '9' && relative[6] >= '0' && relative[6] <= '9' &&
               relative[7] == '/') {
        dir = 7;
    } else {
        return NULL;
    }
    const char *rest = relative + dir + 1;
    size_t n = strlen(rest);
    if (dir == 7 && wo_str_ieq(rest, "TRACK.WAD")) {
        return relative; /* the track's models and scenery textures */
    }
    static const char *const extensions[] = {".CMP", ".TIM", ".TTF", ".TEX"};
    for (size_t i = 0; n >= 5 && i < WO_COUNT(extensions); i++) {
        if (wo_str_iends_with(rest, extensions[i])) {
            return relative;
        }
    }
    return NULL;
}

static bool reused_from_pc(const WoJson *plan, const char *relative) {
    const WoJson *archives = wo_json_get(plan, "archives");
    for (size_t i = 0; i < wo_json_length(archives); i++) {
        const WoJson *archive = wo_json_at(archives, i);
        if (wo_str_eq(wo_json_str(archive, "selection", ""), "pc_byte_identical") &&
            wo_str_ieq(wo_json_str(archive, "file", ""), relative)) {
            return true;
        }
    }
    return false;
}

static bool extract_ps1_textures(const WoPs1Disc *disc, const char *destination,
                                 const WoJson *plan, size_t *count, WoError *error) {
    *count = 0;
    WoSource source;
    if (!wo_source_open_file(&source, disc->image, error)) {
        return false;
    }
    WoIso iso;
    bool ok = wo_iso_open(&iso, &source, disc->offset, error);
    uint64_t budget = source.size;
    for (size_t i = 0; ok && i < iso.count; i++) {
        const WoIsoEntry *entry = &iso.entries[i];
        const char *relative = entry->is_dir ? NULL : ps1_texture_relative(entry->path);
        if (!relative || reused_from_pc(plan, relative)) {
            continue;
        }
        char *target = wo_disc_budget_take(&budget, entry, error)
                           ? wo_path_child(destination, relative, error)
                           : NULL;
        ok = target && wo_disc_extract_entry(&iso, entry, target, error);
        wo_free(target);
        *count += ok ? 1 : 0;
    }
    wo_iso_close(&iso);
    wo_source_close(&source);
    return ok;
}

static bool write_source_record(const char *destination, size_t count, const char *configured,
                                WoError *error) {
    char *path = wo_path_child(destination, "source.json", error);
    char *full = path ? wo_path_full(configured, error) : NULL;
    WoJson *record = full ? wo_json_new_object() : NULL;
    bool ok = record && wo_json_put_str(record, "edition", WO_PS1_EDITION) &&
              wo_json_put_int(record, "files", (int64_t)count) &&
              wo_json_put_str(record, "configured_source", full) &&
              wo_json_put_str(record, "selection", PS1_SOURCE_NOTE);
    if (full && !ok) {
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    ok = ok && wo_write_json(path, record, error);
    wo_json_free(record);
    wo_free(full);
    wo_free(path);
    return ok;
}

/* No disc, but maybe the PlayStation soundtrack alone. */
static bool music_only(const char *source, bool *only, WoError *error) {
    *only = false;
    char *folder = NULL;
    if (!wo_music_find_folder(source, WO_MUSIC_PSX, &folder, error)) {
        return false;
    }
    if (folder) {
        wo_free(folder);
        *only = true;
        return true;
    }
    const char *ext = wo_path_extension(source);
    if (wo_is_file(source) && (is_descriptor_extension(ext) || wo_str_ieq(ext, ".mdf"))) {
        WoTrackList tracks;
        if (!wo_disc_tracks(source, &tracks, error)) {
            return false;
        }
        *only = wo_tracks_audio_count(&tracks) == wo_music_bank_tracks(WO_MUSIC_PSX) - 1u;
        wo_tracks_free(&tracks);
    }
    return true;
}

bool wo_ps1_import_textures(const char *source, const char *destination, const WoJson *plan,
                            const char *configured_source, bool *imported, WoError *error) {
    *imported = false;
    char *full = wo_path_full(source, error);
    if (!full) {
        return false;
    }
    bool ok = true;
    if (!wo_exists(full)) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND, "The PlayStation source does not exist: %s", full);
    }
    WoPs1Disc disc = {0};
    bool found = false;
    ok = ok && wo_ps1_find_disc(full, &disc, &found, error);
    if (ok && !found) {
        bool only = false;
        ok = music_only(full, &only, error);
        if (ok && !only) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         "No supported PlayStation texture data found. Use an original "
                         "WipEout 2097 / XL PlayStation disc (Europe, USA or Japan); an "
                         "extracted music folder supplies music only.");
        }
    }
    size_t count = 0;
    if (ok && found) {
        ok = extract_ps1_textures(&disc, destination, plan, &count, error) &&
             write_source_record(destination, count,
                                 configured_source ? configured_source : full, error);
        *imported = ok;
    }
    wo_ps1_disc_free(&disc);
    wo_free(full);
    return ok;
}

/* --- Plan helpers --------------------------------------------------------------- */

/* Distinct atlas names (ignoring case) in order of first use. */
static bool plan_atlases(const WoJson *plan, WoStrList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    const WoJson *operations = wo_json_get(plan, "operations");
    if (!wo_json_is(operations, WO_JSON_ARRAY)) {
        return wo_fail(error, WO_ERR_FORMAT, "The texture selection map has no operations.");
    }
    for (size_t i = 0; i < operations->count; i++) {
        const char *atlas = wo_json_str(operations->items[i], "atlas", NULL);
        if (!atlas || !*atlas) {
            wo_strlist_free(out);
            return wo_fail(error, WO_ERR_FORMAT,
                           "Texture selection operation %zu has no atlas.", i + 1);
        }
        if (!wo_strlist_contains_i(out, atlas) && !wo_strlist_push(out, atlas, error)) {
            wo_strlist_free(out);
            return false;
        }
    }
    return true;
}

static bool json_numbers_equal(const WoJson *a, const WoJson *b) {
    if (!wo_json_is(a, WO_JSON_NUMBER) || !wo_json_is(b, WO_JSON_NUMBER)) {
        return false;
    }
    if (a->is_integer && b->is_integer) {
        return a->integer == b->integer;
    }
    return a->number == b->number;
}

static bool patches_pending(const char *game_root, const WoJson *plan);
static bool scenery_pending(const char *game_root, const WoJson *plan);

bool wo_natives_test(const char *game_root, const WoJson *plan, bool require_ps1, bool *healthy,
                     WoError *error) {
    return wo_natives_test_cached(game_root, plan, require_ps1, NULL, healthy, error);
}

bool wo_natives_test_cached(const char *game_root, const WoJson *plan, bool require_ps1,
                            WoHashCache *cache, bool *healthy, WoError *error) {
    *healthy = false;
    char *path = wo_path_join(game_root, "textures/selection.json", error);
    char *sd = path ? wo_path_join(game_root, "textures/sd", error) : NULL;
    if (!sd) {
        wo_free(path);
        return false;
    }
    WoJson *state = NULL;
    WoStrList atlases = {0};
    bool ok = true;
    bool good = wo_is_file(path) && wo_read_json(path, RECEIPT_MAX_BYTES, &state, NULL) &&
                wo_json_has(state, "files") &&
                json_numbers_equal(wo_json_get(state, "revision"), wo_json_get(plan, "revision")) &&
                (!require_ps1 || wo_json_bool(state, "ps1_textures", false));
    if (good) {
        ok = plan_atlases(plan, &atlases, error);
        good = ok;
    }
    const WoJson *files = wo_json_get(state, "files");
    for (size_t i = 0; good && i < atlases.count; i++) {
        const char *expected = NULL;
        size_t matches = 0;
        for (size_t j = 0; j < wo_json_length(files); j++) {
            const WoJson *record = wo_json_at(files, j);
            if (wo_str_ieq(wo_json_str(record, "file", ""), atlases.items[i])) {
                expected = wo_json_str(record, "sha256", "");
                matches++;
            }
        }
        char *sheet = matches == 1 ? wo_path_child(sd, atlases.items[i], error) : NULL;
        if (matches == 1 && !sheet) {
            ok = false;
        }
        char hash[WO_SHA256_HEX_SIZE];
        good = sheet && wo_is_file(sheet) && wo_hash_cache_file(cache, sheet, hash, NULL) &&
               wo_str_ieq(hash, expected);
        wo_free(sheet);
    }
    *healthy = ok && good && !patches_pending(game_root, plan) &&
               !scenery_pending(game_root, plan);
    wo_strlist_free(&atlases);
    wo_json_free(state);
    wo_free(sd);
    wo_free(path);
    return ok;
}

/* --- Building sheets ---------------------------------------------------------------- */

struct WoSheetBuilder {
    char *game_root;
    char *sd_root;
    char *ps1_root;
    WoTexSources *sources;
};

static bool load_texture_file(void *context, WoTexOrigin origin, const char *relative,
                              size_t max_bytes, WoBuf *out, WoError *error) {
    const WoSheetBuilder *builder = context;
    const char *root = origin == WO_TEX_FROM_GAME ? builder->game_root
                       : origin == WO_TEX_FROM_SD ? builder->sd_root
                                                  : builder->ps1_root;
    char *path = wo_path_child(root, relative, error);
    bool ok = path && wo_read_file(path, max_bytes, out, error);
    wo_free(path);
    return ok;
}

WoSheetBuilder *wo_sheet_builder_new(const char *game_root, const char *sd_root,
                                     const char *ps1_root, WoError *error) {
    WoSheetBuilder *builder = wo_alloc(sizeof(*builder), error);
    if (!builder) {
        return NULL;
    }
    builder->game_root = wo_path_full(game_root, error);
    builder->sd_root = builder->game_root ? wo_path_full(sd_root, error) : NULL;
    builder->ps1_root = builder->sd_root ? wo_path_full(ps1_root, error) : NULL;
    builder->sources = builder->ps1_root ? wo_texsrc_new(load_texture_file, builder, error) : NULL;
    if (!builder->sources) {
        wo_sheet_builder_free(builder);
        return NULL;
    }
    return builder;
}

void wo_sheet_builder_free(WoSheetBuilder *builder) {
    if (builder) {
        wo_texsrc_free(builder->sources);
        wo_free(builder->ps1_root);
        wo_free(builder->sd_root);
        wo_free(builder->game_root);
        wo_free(builder);
    }
}

/* A whole-number member; a missing member counts as 0, as before. */
static bool op_int(const WoJson *object, const char *key, int64_t *out, WoError *error) {
    const WoJson *value = wo_json_get(object, key);
    *out = 0;
    if (!value) {
        return true;
    }
    if (value->type != WO_JSON_NUMBER || !value->is_integer) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "The texture selection map has a non-integer \"%s\".", key);
    }
    *out = value->integer;
    return true;
}

/* --- PlayStation scenery -------------------------------------------------------------
 * The PC release's scenery is an earlier build: seven of its eight track
 * models are smaller than the PlayStation's, and its texture layout has
 * mistakes. The track archive (TRACK.WAD) has the same format on both, and
 * the PC game places scenery textures where the archive's own TIM headers
 * say, so with the PlayStation disc imported its archives replace the PC
 * ones and each track's sheet is laid out as PlayStation video memory:
 * sheet page 0 is VRAM x 320, one sheet texel per texel of the frame.
 * The plan's "scenery" lists each track with its sheets. The PC archive is
 * kept under textures/native-source. Its frames are left in the sheet where
 * the PlayStation ones do not cover them: the PC archive repeats textures
 * the game also loads from the shared archives (the start gantry), which
 * the PlayStation models still use at those places. */
#define SCENERY_VRAM_X 320

static bool file_is(const char *path, const char *expected);

static const WoJson *plan_scenery(const WoJson *plan, const char *atlas) {
    const WoJson *scenery = wo_json_get(plan, "scenery");
    for (size_t i = 0; i < wo_json_length(scenery); i++) {
        const WoJson *entry = wo_json_at(scenery, i);
        const WoJson *atlases = wo_json_get(entry, "atlases");
        for (size_t j = 0; j < wo_json_length(atlases); j++) {
            if (wo_str_ieq(wo_json_as_str(wo_json_at(atlases, j), ""), atlas)) {
                return entry;
            }
        }
    }
    return NULL;
}

/* The three archives of a track: the one the game loads, the imported
 * PlayStation one and the retained PC one. */
typedef struct SceneryWads {
    char *game;
    char *ps1;
    char *pc;
} SceneryWads;

static void scenery_wads_free(SceneryWads *wads) {
    wo_free(wads->game);
    wo_free(wads->ps1);
    wo_free(wads->pc);
    memset(wads, 0, sizeof(*wads));
}

static bool scenery_wads(const char *game_root, const WoJson *entry, SceneryWads *wads,
                         WoError *error) {
    memset(wads, 0, sizeof(*wads));
    const char *track = wo_json_str(entry, "track", "");
    char *relative = wo_format(error, "%s/TRACK.WAD", track);
    char *ps1 = relative ? wo_format(error, "textures/ps1-source/%s", relative) : NULL;
    char *pc = ps1 ? wo_format(error, "textures/native-source/%s", relative) : NULL;
    if (pc) {
        wads->game = wo_path_child(game_root, relative, error);
        wads->ps1 = wads->game ? wo_path_child(game_root, ps1, error) : NULL;
        wads->pc = wads->ps1 ? wo_path_child(game_root, pc, error) : NULL;
    }
    wo_free(pc);
    wo_free(ps1);
    wo_free(relative);
    if (!wads->pc) {
        scenery_wads_free(wads);
        return false;
    }
    return true;
}

static bool same_file_bytes(const char *a, const char *b) {
    char hash[WO_SHA256_HEX_SIZE];
    return wo_is_file(a) && wo_sha256_file(a, hash, NULL) && file_is(b, hash);
}

/* Put each imported PlayStation archive in place, keeping the PC one. */
static bool install_scenery(const char *game_root, const WoJson *plan, WoError *error) {
    const WoJson *scenery = wo_json_get(plan, "scenery");
    for (size_t i = 0; i < wo_json_length(scenery); i++) {
        SceneryWads wads;
        if (!scenery_wads(game_root, wo_json_at(scenery, i), &wads, error)) {
            return false;
        }
        bool ok = true;
        if (wo_is_file(wads.ps1) && wo_is_file(wads.game) &&
            !same_file_bytes(wads.ps1, wads.game)) {
            ok = (wo_is_file(wads.pc) || wo_copy_file(wads.game, wads.pc, error)) &&
                 wo_copy_file(wads.ps1, wads.game, error);
        }
        scenery_wads_free(&wads);
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* An imported PlayStation archive is not yet the one the game loads. */
static bool scenery_pending(const char *game_root, const WoJson *plan) {
    const WoJson *scenery = wo_json_get(plan, "scenery");
    bool pending = false;
    for (size_t i = 0; !pending && i < wo_json_length(scenery); i++) {
        SceneryWads wads;
        if (scenery_wads(game_root, wo_json_at(scenery, i), &wads, NULL)) {
            pending = wo_is_file(wads.ps1) && !same_file_bytes(wads.ps1, wads.game);
            scenery_wads_free(&wads);
        }
    }
    return pending;
}

static bool load_scene_frames(const char *wad_path, WoCmp *frames, WoError *error) {
    memset(frames, 0, sizeof(*frames));
    WoBuf bytes = {0};
    WoWad wad = {0};
    bool ok = wo_read_file(wad_path, WO_TEXSRC_MAX_FILE_BYTES, &bytes, error) &&
              wo_wad_parse(bytes.data, bytes.len, &wad, error);
    const WoWadEntry *entry = ok ? wo_wad_find(&wad, "scene.cmp") : NULL;
    if (ok && !entry) {
        ok = wo_fail(error, WO_ERR_FORMAT, "The track archive has no scenery textures: %s",
                     wad_path);
    }
    ok = ok && wo_cmp_decode(bytes.data + entry->offset, entry->size, frames, error);
    wo_wad_free(&wad);
    wo_buf_free(&bytes);
    return ok;
}

/* Lay the sheet's scenery out as the PlayStation archive has it: paste its
 * frames where their headers place them. Art the PC release painted into
 * its sheets for its own models (Sagarmatha's "FEED ME" hoarding) is not
 * carried over: the PlayStation models do not show it. */
static bool apply_scenery(const SceneryWads *wads, uint8_t *sheet, size_t len,
                          WoNativeCounts *counts, WoError *error) {
    WoTgaInfo info;
    if (!wo_tga_info(sheet, len, &info, error)) {
        return false;
    }
    WoCmp frames;
    bool ok = load_scene_frames(wads->game, &frames, error);
    for (size_t i = 0; ok && i < frames.count; i++) {
        if (!frames.sizes[i]) {
            continue;
        }
        const uint8_t *tim = frames.data + frames.offsets[i];
        WoNativeImage image;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t per_word = 1;
        ok = wo_tim_origin(tim, frames.sizes[i], &x, &y, &per_word, error) &&
             wo_tim_decode(tim, frames.sizes[i], &image, error);
        if (!ok) {
            break;
        }
        int64_t left = ((int64_t)x - SCENERY_VRAM_X) * per_word;
        if (left < 0 || left + image.width > info.width || y + image.height > info.height) {
            ok = wo_fail(error, WO_ERR_FORMAT, "Scenery texture %zu lies outside the sheet: %s", i,
                         wads->game);
        } else {
            ok = wo_tga_patch(sheet, len, &image, left, y, image.width, image.height, true,
                              error);
            counts->ps1_regions++;
        }
        wo_native_image_free(&image);
    }
    wo_cmp_free(&frames);
    return ok;
}

/* --- Game data corrections -----------------------------------------------------------
 * The plan's "patches" change single bytes of game files where the PC
 * release's data is wrong and the PlayStation release has it right. Each
 * names the file's SHA-256 before and after, so only the exact retail file
 * is changed. An operation naming a patch belongs to the corrected layout
 * and is applied only where that patch is in effect. */
static const WoJson *plan_patch(const WoJson *plan, const char *id) {
    const WoJson *patches = wo_json_get(plan, "patches");
    for (size_t i = 0; i < wo_json_length(patches); i++) {
        const WoJson *patch = wo_json_at(patches, i);
        if (wo_str_eq(wo_json_str(patch, "id", ""), id)) {
            return patch;
        }
    }
    return NULL;
}

static bool patch_file_is(const char *game_root, const WoJson *patch, const char *key) {
    char *path = patch ? wo_path_child(game_root, wo_json_str(patch, "file", ""), NULL) : NULL;
    bool ok = path && file_is(path, wo_json_str(patch, key, ""));
    wo_free(path);
    return ok;
}

static bool apply_patch(const char *game_root, const WoJson *patch, WoError *error) {
    const char *id = wo_json_str(patch, "id", "");
    if (!patch_file_is(game_root, patch, "sha256")) {
        if (!patch_file_is(game_root, patch, "patched_sha256")) {
            wo_log_text("native_patch_not_applicable", id); /* e.g. a PlayStation archive */
        }
        return true;
    }
    char *path = wo_path_child(game_root, wo_json_str(patch, "file", ""), error);
    WoBuf data = {0};
    int64_t offset = 0;
    int64_t from = 0;
    int64_t to = 0;
    bool ok = path && op_int(patch, "offset", &offset, error) &&
              op_int(patch, "from", &from, error) && op_int(patch, "to", &to, error) &&
              wo_read_file(path, WO_TEXSRC_MAX_FILE_BYTES, &data, error);
    if (ok && (offset < 0 || (uint64_t)offset >= data.len || data.data[offset] != (uint8_t)from)) {
        ok = wo_fail(error, WO_ERR_FORMAT, "Game data correction does not fit: %s", id);
    }
    if (ok) {
        char hash[WO_SHA256_HEX_SIZE];
        data.data[offset] = (uint8_t)to;
        ok = wo_sha256_mem(data.data, data.len, hash, error) &&
             (wo_digest_eq(hash, wo_json_str(patch, "patched_sha256", "")) ||
              wo_fail(error, WO_ERR_FORMAT, "Game data correction does not fit: %s", id)) &&
             wo_write_file_atomic(path, data.data, data.len, error);
    }
    wo_buf_free(&data);
    wo_free(path);
    return ok;
}

static bool apply_patches(const char *game_root, const WoJson *plan, WoError *error) {
    const WoJson *patches = wo_json_get(plan, "patches");
    for (size_t i = 0; i < wo_json_length(patches); i++) {
        if (!apply_patch(game_root, wo_json_at(patches, i), error)) {
            return false;
        }
    }
    return true;
}

/* A correction the plan has for this game data is still to be made. */
static bool patches_pending(const char *game_root, const WoJson *plan) {
    const WoJson *patches = wo_json_get(plan, "patches");
    for (size_t i = 0; i < wo_json_length(patches); i++) {
        if (patch_file_is(game_root, wo_json_at(patches, i), "sha256")) {
            return true;
        }
    }
    return false;
}

static bool decode_operation(WoSheetBuilder *builder, const WoJson *src, bool ps1,
                             WoNativeImage *pixels, WoError *error) {
    int64_t frame = 0;
    if (wo_json_has(src, "material")) {
        int64_t material = 0;
        int64_t level = 0;
        return op_int(src, "material", &material, error) && op_int(src, "level", &level, error) &&
               wo_texsrc_road(builder->sources, wo_json_str(src, "wad", ""),
                              wo_json_str(src, "entry", ""), wo_json_str(src, "table", ""),
                              material, level, pixels, error);
    }
    const char *path = ps1 ? wo_json_str(src, "ps1", "") : wo_json_str(src, "path", "");
    return op_int(src, "frame", &frame, error) &&
           wo_texsrc_frame(builder->sources, path, wo_json_str(src, "wad", ""),
                           wo_json_str(src, "entry", ""), ps1, frame, pixels, error);
}

static bool apply_operation(WoSheetBuilder *builder, const WoJson *op, uint8_t *sheet, size_t len,
                            WoNativeCounts *counts, WoError *error) {
    const WoJson *src = wo_json_get(op, "source");
    if (!wo_json_is(src, WO_JSON_OBJECT)) {
        return wo_fail(error, WO_ERR_FORMAT, "A texture selection operation has no source.");
    }
    bool ps1 = wo_json_has(src, "ps1");
    if (ps1) {
        char *file = wo_path_child(builder->ps1_root, wo_json_str(src, "ps1", ""), error);
        if (!file) {
            return false;
        }
        bool present = wo_is_file(file);
        wo_free(file);
        if (!present) {
            counts->pending_ps1_regions++;
            return true;
        }
    }
    int64_t x = 0;
    int64_t y = 0;
    int64_t width = 0;
    int64_t height = 0;
    if (!op_int(op, "x", &x, error) || !op_int(op, "y", &y, error) ||
        !op_int(op, "width", &width, error) || !op_int(op, "height", &height, error)) {
        return false;
    }
    WoNativeImage pixels;
    if (!decode_operation(builder, src, ps1, &pixels, error)) {
        return false;
    }
    /* Scenery is drawn solid; effects, sprites and shared textures may be
     * blended, where flagged black must stay invisible. */
    const char *name = ps1 ? wo_json_str(src, "ps1", "") : wo_json_str(src, "entry", "");
    bool scenery = wo_str_iends_with(name, "scene.cmp");
    bool ok = wo_tga_patch(sheet, len, &pixels, x, y, width, height, scenery, error);
    wo_native_image_free(&pixels);
    if (!ok) {
        wo_error_context(error, "%s: ", wo_json_str(op, "label", "texture region"));
        return false;
    }
    if (ps1) {
        counts->ps1_regions++;
    } else {
        counts->pc_regions++;
    }
    return true;
}

bool wo_sheet_builder_apply(WoSheetBuilder *builder, const WoJson *plan, const char *atlas,
                            uint8_t *sheet, size_t len, WoNativeCounts *counts, WoError *error) {
    const WoJson *operations = wo_json_get(plan, "operations");
    for (size_t i = 0; i < wo_json_length(operations); i++) {
        const WoJson *op = wo_json_at(operations, i);
        if (!wo_str_ieq(wo_json_str(op, "atlas", ""), atlas)) {
            continue;
        }
        const char *patch = wo_json_str(op, "patch", NULL);
        if (patch &&
            !patch_file_is(builder->game_root, plan_patch(plan, patch), "patched_sha256")) {
            continue; /* made for corrected game data, which this is not */
        }
        if (!apply_operation(builder, op, sheet, len, counts, error)) {
            return false;
        }
    }
    return true;
}

/* --- Installation ------------------------------------------------------------------- */

typedef struct NativePaths {
    char *game;
    char *sd;
    char *ps1;
    char *original;
    char *receipt;
} NativePaths;

static void native_paths_free(NativePaths *p) {
    wo_free(p->game);
    wo_free(p->sd);
    wo_free(p->ps1);
    wo_free(p->original);
    wo_free(p->receipt);
}

static bool native_paths(const char *game_root, NativePaths *p, WoError *error) {
    memset(p, 0, sizeof(*p));
    p->game = wo_path_full(game_root, error);
    p->sd = p->game ? wo_path_child(p->game, "textures/sd", error) : NULL;
    p->ps1 = p->sd ? wo_path_child(p->game, "textures/ps1-source", error) : NULL;
    p->original = p->ps1 ? wo_path_child(p->game, "textures/native-source", error) : NULL;
    p->receipt = p->original ? wo_path_child(p->game, "textures/selection.json", error) : NULL;
    if (!p->receipt) {
        native_paths_free(p);
        return false;
    }
    return true;
}

static bool retain_caches(const char *retain_from, const char *game, WoError *error) {
    static const char *const folders[] = {"textures/ps1-source", "textures/native-source"};
    for (size_t i = 0; i < WO_COUNT(folders); i++) {
        char *old = wo_path_join(retain_from, folders[i], error);
        char *now = old ? wo_path_child(game, folders[i], error) : NULL;
        bool ok = now && (!wo_is_dir(old) || wo_copy_tree(old, now, error));
        wo_free(now);
        wo_free(old);
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* The SHA-256 of the sheet the plan's placements were located in, or NULL
 * when the plan names none for it. The game's models index one layout of a
 * sheet; some game folders carry an earlier sheet, laid out differently. */
static const char *expected_sheet(const WoJson *plan, const char *atlas) {
    const WoJson *sheets = wo_json_get(plan, "sheets");
    for (size_t i = 0; i < wo_json_length(sheets); i++) {
        const WoJson *sheet = wo_json_at(sheets, i);
        if (wo_str_ieq(wo_json_str(sheet, "file", ""), atlas)) {
            return wo_json_str(sheet, "sha256", NULL);
        }
    }
    return NULL;
}

static bool file_is(const char *path, const char *expected) {
    char hash[WO_SHA256_HEX_SIZE];
    return wo_is_file(path) && wo_sha256_file(path, hash, NULL) && wo_digest_eq(hash, expected);
}

/* Rebuild one sheet from its pristine copy; *built is false when the game
 * folder has no such sheet. A freshly extracted sheet becomes the pristine
 * copy when it is the sheet the plan was made for; otherwise a retained copy
 * that is stays. A sheet that is neither is installed as the source has it,
 * without placements made for another layout. */
static bool build_sheet(WoSheetBuilder *builder, const NativePaths *p, const WoJson *plan,
                        const char *atlas, bool sheets_pristine, WoNativeCounts *counts,
                        WoJson *files, bool *built, WoError *error) {
    *built = false;
    char *target = wo_path_child(p->sd, atlas, error);
    char *pristine = target ? wo_path_child(p->original, atlas, error) : NULL;
    if (!pristine) {
        wo_free(target);
        return false;
    }
    bool ok = true;
    if (wo_is_file(target)) {
        WoBuf bytes = {0};
        char hash[WO_SHA256_HEX_SIZE];
        const char *expected = expected_sheet(plan, atlas);
        bool fresh = sheets_pristine && (!expected || file_is(target, expected));
        bool retained = expected && file_is(pristine, expected);
        if (fresh || (!retained && (sheets_pristine || !wo_is_file(pristine)))) {
            ok = wo_copy_file(target, pristine, error);
        }
        ok = ok && wo_read_file(pristine, SHEET_MAX_BYTES, &bytes, error);
        bool known = true;
        if (ok && expected) {
            ok = wo_sha256_mem(bytes.data, bytes.len, hash, error);
            known = ok && wo_digest_eq(hash, expected);
            if (ok && !known) {
                wo_log_text("native_sheet_unrecognised", atlas);
            }
        }
        ok = ok && (!known || wo_sheet_builder_apply(builder, plan, atlas, bytes.data, bytes.len,
                                                     counts, error));
        const WoJson *scenery = ok && known ? plan_scenery(plan, atlas) : NULL;
        if (scenery) {
            SceneryWads wads;
            ok = scenery_wads(p->game, scenery, &wads, error);
            if (ok && same_file_bytes(wads.ps1, wads.game)) {
                ok = apply_scenery(&wads, bytes.data, bytes.len, counts, error);
            }
            scenery_wads_free(&wads);
        }
        ok = ok && wo_write_file_atomic(target, bytes.data, bytes.len, error) &&
             wo_sha256_mem(bytes.data, bytes.len, hash, error);
        if (ok) {
            WoJson *record = wo_json_new_object();
            ok = record && wo_json_put_str(record, "file", atlas) &&
                 wo_json_put_str(record, "sha256", hash) && wo_json_push(files, record);
            if (!ok) {
                wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
            }
        }
        wo_buf_free(&bytes);
        *built = ok;
    }
    wo_free(pristine);
    wo_free(target);
    return ok;
}

static WoJson *make_receipt(const WoJson *plan, const WoNativeCounts *counts, size_t sheets,
                            bool ps1_textures, WoJson *files) {
    WoJson *receipt = wo_json_new_object();
    const WoJson *revision = wo_json_get(plan, "revision");
    bool ok = receipt && wo_json_put_int(receipt, "format", 2) &&
              wo_json_put(receipt, "revision",
                          revision ? wo_json_clone(revision) : wo_json_new_null()) &&
              wo_json_put_str(receipt, "selection", "ps1-preferred") &&
              wo_json_put_int(receipt, "pc_regions", (int64_t)counts->pc_regions) &&
              wo_json_put_int(receipt, "ps1_regions", (int64_t)counts->ps1_regions) &&
              wo_json_put_int(receipt, "pending_ps1_regions",
                              (int64_t)counts->pending_ps1_regions) &&
              wo_json_put_int(receipt, "sheets", (int64_t)sheets) &&
              wo_json_put_bool(receipt, "ps1_textures", ps1_textures);
    if (ok) {
        ok = wo_json_put(receipt, "files", files);
        files = NULL;
    }
    wo_json_free(files);
    if (!ok) {
        wo_json_free(receipt);
        return NULL;
    }
    return receipt;
}

static bool reuse_healthy_build(const WoNativeOptions *options, const NativePaths *p,
                                WoJson **receipt, WoError *error) {
    bool healthy = false;
    if (!wo_natives_test_cached(p->game, options->plan, options->require_ps1, options->cache,
                                &healthy, error)) {
        return false;
    }
    if (healthy && !wo_read_json(p->receipt, RECEIPT_MAX_BYTES, receipt, error)) {
        return false;
    }
    return true;
}

static bool build_all(const WoNativeOptions *options, const NativePaths *p, WoJson **receipt,
                      WoError *error) {
    WoStrList atlases = {0};
    WoSheetBuilder *builder = NULL;
    WoJson *files = wo_json_new_array();
    WoNativeCounts counts = {0, 0, 0};
    size_t sheets = 0;
    bool ok = files != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    ok = ok && plan_atlases(options->plan, &atlases, error) &&
         install_scenery(p->game, options->plan, error) &&
         apply_patches(p->game, options->plan, error);
    if (ok) {
        builder = wo_sheet_builder_new(p->game, p->sd, p->ps1, error);
        ok = builder != NULL;
    }
    for (size_t i = 0; ok && i < atlases.count; i++) {
        bool built = false;
        ok = build_sheet(builder, p, options->plan, atlases.items[i], options->sheets_pristine,
                         &counts, files, &built, error);
        sheets += built ? 1 : 0;
    }
    if (ok && options->require_ps1 && counts.pending_ps1_regions) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND,
                     "The imported PlayStation texture cache is incomplete. Restore the "
                     "configured disc source in Setup.");
    }
    char *source_record = ok ? wo_path_join(p->ps1, "source.json", error) : NULL;
    if (source_record) {
        *receipt = make_receipt(options->plan, &counts, sheets, wo_is_file(source_record), files);
        files = NULL;
        ok = *receipt != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        ok = ok && wo_write_json(p->receipt, *receipt, error);
    } else {
        ok = false;
    }
    wo_free(source_record);
    wo_json_free(files);
    wo_sheet_builder_free(builder);
    wo_strlist_free(&atlases);
    return ok;
}

bool wo_natives_install(const WoNativeOptions *options, WoJson **receipt, WoError *error) {
    *receipt = NULL;
    const WoJson *format = wo_json_get(options->plan, "format");
    if (!wo_json_is(format, WO_JSON_NUMBER) || !format->is_integer || format->integer != 1) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "Unsupported native texture selection map.");
    }
    NativePaths p;
    if (!native_paths(options->game_root, &p, error)) {
        return false;
    }
    bool ok = true;
    bool fresh = options->ps1_source || options->retain_from;
    if (options->incremental && !fresh && wo_is_file(p.receipt)) {
        ok = reuse_healthy_build(options, &p, receipt, error);
        if (!ok || *receipt) {
            native_paths_free(&p);
            return ok;
        }
    }
    if (options->retain_from) {
        ok = retain_caches(options->retain_from, p.game, error);
    }
    if (ok && options->ps1_source) {
        bool imported = false;
        ok = wo_ps1_import_textures(options->ps1_source, p.ps1, options->plan,
                                    options->configured_source, &imported, error);
    }
    if (ok && options->require_ps1) {
        char *record = wo_path_join(p.ps1, "source.json", error);
        ok = record != NULL;
        if (ok && !wo_is_file(record)) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND,
                         "PlayStation artwork needs its configured disc source. Open Setup to "
                         "restore its path.");
        }
        wo_free(record);
    }
    ok = ok && build_all(options, &p, receipt, error);
    if (!ok) {
        wo_json_free(*receipt);
        *receipt = NULL;
    }
    native_paths_free(&p);
    return ok;
}

/* --- Where scenery pictures sit ------------------------------------------------------- */

typedef struct SceneryFrame {
    WoSceneryPlace place;
    char hash[WO_SHA256_HEX_SIZE];
} SceneryFrame;

typedef struct SceneryRect {
    char native[32];
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
} SceneryRect;

struct WoScenery {
    WoSheetBuilder *builder;
    SceneryFrame *frames;
    size_t count;
    size_t cap;
    SceneryRect *rects;
    size_t rect_count;
    size_t rect_cap;
};

static bool picture_hash(const WoNativeImage *image, char hash[WO_SHA256_HEX_SIZE]) {
    return wo_sha256_mem(image->pixels, (size_t)image->width * image->height * sizeof(uint16_t),
                         hash, NULL);
}

static bool scenery_note(WoScenery *scenery, const char *native, const uint8_t *tim, size_t len,
                         bool placed, WoError *error) {
    WoNativeImage image;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t per_word = 1;
    if (!wo_tim_origin(tim, len, &x, &y, &per_word, error) ||
        !wo_tim_decode(tim, len, &image, error)) {
        return false;
    }
    int64_t left = ((int64_t)x - SCENERY_VRAM_X) * per_word;
    bool ok = true;
    if (scenery->rect_count == scenery->rect_cap) {
        size_t cap = scenery->rect_cap ? scenery->rect_cap * 2 : 128;
        SceneryRect *grown = wo_realloc_array(scenery->rects, cap, sizeof(*grown), error);
        ok = grown != NULL;
        if (ok) {
            scenery->rects = grown;
            scenery->rect_cap = cap;
        }
    }
    if (ok) {
        SceneryRect *rect = &scenery->rects[scenery->rect_count++];
        wo_copy_str(rect->native, sizeof(rect->native), native);
        rect->left = left;
        rect->top = y;
        rect->right = left + image.width;
        rect->bottom = (int64_t)y + image.height;
    }
    /* A frame is a place when it lies within one 256-texel page. */
    if (ok && placed && left >= 0 && left / 256 == (left + image.width - 1) / 256 &&
        y + image.height <= 256) {
        if (scenery->count == scenery->cap) {
            size_t cap = scenery->cap ? scenery->cap * 2 : 128;
            SceneryFrame *grown = wo_realloc_array(scenery->frames, cap, sizeof(*grown), error);
            ok = grown != NULL;
            if (ok) {
                scenery->frames = grown;
                scenery->cap = cap;
            }
        }
        if (ok) {
            SceneryFrame *frame = &scenery->frames[scenery->count];
            memset(frame, 0, sizeof(*frame));
            wo_copy_str(frame->place.native, sizeof(frame->place.native), native);
            frame->place.page = (uint32_t)(left / 256);
            frame->place.x = (uint32_t)(left % 256);
            frame->place.y = y;
            frame->place.width = image.width;
            frame->place.height = image.height;
            if (picture_hash(&image, frame->hash)) {
                scenery->count++;
            }
        }
    }
    wo_native_image_free(&image);
    return ok;
}

void wo_scenery_close(WoScenery *scenery) {
    if (scenery) {
        wo_sheet_builder_free(scenery->builder);
        wo_free(scenery->frames);
        wo_free(scenery->rects);
        wo_free(scenery);
    }
}

bool wo_scenery_open(const char *game_root, const WoJson *plan, WoScenery **out, WoError *error) {
    *out = NULL;
    NativePaths p;
    if (!native_paths(game_root, &p, error)) {
        return false;
    }
    WoScenery *scenery = wo_alloc(sizeof(*scenery), error);
    bool ok = scenery != NULL;
    if (ok) {
        memset(scenery, 0, sizeof(*scenery));
    }
    const WoJson *entries = wo_json_get(plan, "scenery");
    for (size_t i = 0; ok && i < wo_json_length(entries); i++) {
        const WoJson *entry = wo_json_at(entries, i);
        const char *atlas = wo_json_as_str(wo_json_at(wo_json_get(entry, "atlases"), 0), "");
        const char *name = atlas;
        for (const char *c = atlas; *c; c++) {
            if (*c == '/' || *c == '\\') {
                name = c + 1;
            }
        }
        char native[32];
        wo_copy_str(native, sizeof(native), name);
        for (char *c = native; *c; c++) {
            *c = wo_ascii_lower(*c);
        }
        SceneryWads wads;
        ok = scenery_wads(p.game, entry, &wads, error);
        if (ok && same_file_bytes(wads.ps1, wads.game)) {
            WoCmp frames;
            ok = load_scene_frames(wads.game, &frames, error);
            for (size_t f = 0; ok && f < frames.count; f++) {
                if (frames.sizes[f]) {
                    ok = scenery_note(scenery, native, frames.data + frames.offsets[f],
                                      frames.sizes[f], true, error);
                }
            }
            wo_cmp_free(&frames);
        }
        scenery_wads_free(&wads);
    }
    if (ok && scenery->count) {
        scenery->builder = wo_sheet_builder_new(p.game, p.sd, p.ps1, error);
        ok = scenery->builder != NULL;
    }
    if (ok && scenery->count) {
        *out = scenery;
    } else {
        wo_scenery_close(scenery);
    }
    native_paths_free(&p);
    return ok;
}

bool wo_scenery_covers(const WoScenery *scenery, const char *native, int64_t page, int64_t x,
                       int64_t y, int64_t width, int64_t height) {
    int64_t left = page * 256 + x;
    for (size_t i = 0; scenery && i < scenery->rect_count; i++) {
        const SceneryRect *rect = &scenery->rects[i];
        if (wo_str_ieq(rect->native, native) && left < rect->right && rect->left < left + width &&
            y < rect->bottom && rect->top < y + height) {
            return true;
        }
    }
    return false;
}

/* "track17-scene-cmp-8.png" -> TRACK17, SCENE, CMP, 8. */
static bool catalogue_source(const char *png, char *dir, size_t dir_size, char *file,
                             size_t file_size, int64_t *frame) {
    char name[128];
    size_t len = strlen(png);
    if (len < 5 || len >= sizeof(name) || !wo_str_iends_with(png, ".png")) {
        return false;
    }
    memcpy(name, png, len - 4);
    name[len - 4] = 0;
    char *parts[8];
    size_t count = 0;
    for (char *c = name, *start = name;; c++) {
        if (*c == '-' || !*c) {
            bool last = !*c;
            *c = 0;
            if (count == WO_COUNT(parts) || !*start) {
                return false;
            }
            parts[count++] = start;
            start = c + 1;
            if (last) {
                break;
            }
        }
    }
    if (count != 4 || !(wo_str_ieq(parts[2], "cmp") || wo_str_ieq(parts[2], "tim"))) {
        return false;
    }
    *frame = 0;
    for (const char *c = parts[3]; *c; c++) {
        if (*c < '0' || *c > '9' || *frame > 100000) {
            return false;
        }
        *frame = *frame * 10 + (*c - '0');
    }
    char joined[96];
    if (snprintf(joined, sizeof(joined), "%s.%s", parts[1], parts[2]) >= (int)sizeof(joined)) {
        return false;
    }
    wo_copy_str(dir, dir_size, parts[0]);
    wo_copy_str(file, file_size, joined);
    for (char *c = dir; *c; c++) {
        *c = (char)((*c >= 'a' && *c <= 'z') ? *c - 32 : *c);
    }
    for (char *c = file; *c; c++) {
        *c = (char)((*c >= 'a' && *c <= 'z') ? *c - 32 : *c);
    }
    return true;
}

bool wo_scenery_places(WoScenery *scenery, const char *png, WoSceneryPlace **places,
                       size_t *count, WoError *error) {
    *places = NULL;
    *count = 0;
    char dir[32];
    char file[96];
    int64_t frame = 0;
    if (!scenery || !catalogue_source(png, dir, sizeof(dir), file, sizeof(file), &frame)) {
        return true;
    }
    WoNativeImage image;
    bool decoded = false;
    if (wo_str_istarts_with(dir, "TRACK") && wo_str_ieq(file, "SCENE.CMP")) {
        char *wad = wo_format(error, "textures/ps1-source/%s/TRACK.WAD", dir);
        if (!wad) {
            return false;
        }
        decoded = wo_texsrc_frame(scenery->builder->sources, "", wad, "scene.cmp", false, frame,
                                  &image, NULL);
        wo_free(wad);
    } else {
        char *relative = wo_format(error, "%s/%s", dir, file);
        char *imported = relative ? wo_path_child(scenery->builder->ps1_root, relative, error) : NULL;
        if (!imported) {
            wo_free(relative);
            return false;
        }
        decoded = wo_texsrc_frame(scenery->builder->sources, relative, "", "", wo_is_file(imported),
                                  frame, &image, NULL);
        wo_free(imported);
        wo_free(relative);
    }
    if (!decoded) {
        return true;
    }
    char hash[WO_SHA256_HEX_SIZE];
    bool ok = true;
    if (picture_hash(&image, hash)) {
        WoSceneryPlace *found = wo_alloc_array(scenery->count, sizeof(*found), error);
        ok = found != NULL;
        for (size_t i = 0; ok && i < scenery->count; i++) {
            const SceneryFrame *candidate = &scenery->frames[i];
            if (candidate->place.width == image.width && candidate->place.height == image.height &&
                wo_digest_eq(candidate->hash, hash)) {
                found[(*count)++] = candidate->place;
            }
        }
        if (ok) {
            *places = found;
        }
    }
    wo_native_image_free(&image);
    return ok;
}

