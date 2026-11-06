#include "core/wo_natives.h"

#include <string.h>

#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_iso.h"
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

/* --- Finding the European disc ------------------------------------------------- */

static bool contains_serial(const uint8_t *text, size_t len) {
    static const char prefix[] = "SLES";
    static const char suffix[] = "003.27";
    for (size_t i = 0; i + 11 <= len; i++) {
        bool same = true;
        for (size_t j = 0; j < 4 && same; j++) {
            same = wo_ascii_lower((char)text[i + j]) == wo_ascii_lower(prefix[j]);
        }
        if (same && (text[i + 4] == '_' || text[i + 4] == '-') &&
            memcmp(text + i + 5, suffix, 6) == 0) {
            return true;
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
            match = contains_serial(text, cnf->length) &&
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
            WoTrackList tracks;
            ok = wo_disc_tracks(candidate, &tracks, error);
            const WoTrack *data = ok ? wo_tracks_first_data(&tracks) : NULL;
            if (data) {
                image = wo_strdup(data->image, error);
                offset = data->offset;
                ok = image != NULL;
            }
            if (ok) {
                wo_tracks_free(&tracks);
            }
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

/* "WIPEOUT2/<COMMON|TEXTURES|TRACKnn>/<name>.<CMP|TIM|TTF|TEX>" -> the part
 * after "WIPEOUT2/", or NULL. */
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
    for (size_t i = 0; ok && i < iso.count; i++) {
        const WoIsoEntry *entry = &iso.entries[i];
        const char *relative = entry->is_dir ? NULL : ps1_texture_relative(entry->path);
        if (!relative || reused_from_pc(plan, relative)) {
            continue;
        }
        char *target = wo_path_child(destination, relative, error);
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
                         "No supported PlayStation texture data found. Use the European WipEout "
                         "2097 disc (SLES-00327); an extracted music folder supplies music only.");
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
    *healthy = ok && good;
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
    bool ok = wo_tga_patch(sheet, len, &pixels, x, y, width, height, error);
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

/* Rebuild one sheet from its pristine copy; *built is false when the game
 * folder has no such sheet. */
static bool build_sheet(WoSheetBuilder *builder, const NativePaths *p, const WoJson *plan,
                        const char *atlas, WoNativeCounts *counts, WoJson *files, bool *built,
                        WoError *error) {
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
        if (!wo_is_file(pristine)) {
            ok = wo_copy_file(target, pristine, error);
        }
        ok = ok && wo_read_file(pristine, SHEET_MAX_BYTES, &bytes, error) &&
             wo_sheet_builder_apply(builder, plan, atlas, bytes.data, bytes.len, counts, error) &&
             wo_write_file_atomic(target, bytes.data, bytes.len, error) &&
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
    ok = ok && plan_atlases(options->plan, &atlases, error);
    if (ok) {
        builder = wo_sheet_builder_new(p->game, p->sd, p->ps1, error);
        ok = builder != NULL;
    }
    for (size_t i = 0; ok && i < atlases.count; i++) {
        bool built = false;
        ok = build_sheet(builder, p, options->plan, atlases.items[i], &counts, files, &built,
                         error);
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
