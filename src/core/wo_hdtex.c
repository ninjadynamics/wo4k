#include "core/wo_hdtex.h"

#include <stdlib.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_png.h"
#include "core/wo_tree.h"

#define INDEX_MAX_BYTES (16u * 1024u * 1024u)
#define PAGE_SIDE 256

/* --- Discovered artwork -------------------------------------------------------- */

typedef struct Input {
    char *relative; /* '/'-separated */
    char *path;
} Input;

typedef struct Inputs {
    Input *items;
    size_t count;
    size_t cap;
} Inputs;

static void inputs_free(Inputs *inputs) {
    for (size_t i = 0; i < inputs->count; i++) {
        wo_free(inputs->items[i].relative);
        wo_free(inputs->items[i].path);
    }
    wo_free(inputs->items);
    memset(inputs, 0, sizeof(*inputs));
}

static Input *find_input(const Inputs *inputs, const char *relative) {
    for (size_t i = 0; i < inputs->count; i++) {
        if (wo_str_ieq(inputs->items[i].relative, relative)) {
            return &inputs->items[i];
        }
    }
    return NULL;
}

/* A later folder's file replaces an earlier one with the same name. */
static bool add_input(Inputs *inputs, const char *relative, const char *path, WoError *error) {
    char *path_copy = wo_strdup(path, error);
    if (!path_copy) {
        return false;
    }
    Input *existing = find_input(inputs, relative);
    if (existing) {
        wo_free(existing->path);
        existing->path = path_copy;
        return true;
    }
    if (inputs->count == inputs->cap) {
        size_t cap = inputs->cap ? inputs->cap * 2 : 64;
        Input *grown = wo_realloc_array(inputs->items, cap, sizeof(*grown), error);
        if (!grown) {
            wo_free(path_copy);
            return false;
        }
        inputs->items = grown;
        inputs->cap = cap;
    }
    char *relative_copy = wo_strdup(relative, error);
    if (!relative_copy) {
        wo_free(path_copy);
        return false;
    }
    inputs->items[inputs->count].relative = relative_copy;
    inputs->items[inputs->count].path = path_copy;
    inputs->count++;
    return true;
}

static bool collect_folder(const char *folder, Inputs *inputs, WoError *error) {
    if (!folder || !wo_is_dir(folder)) {
        return true;
    }
    WoTreeList tree;
    if (!wo_tree_list(folder, &tree, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < tree.count; i++) {
        const WoTreeEntry *entry = &tree.items[i];
        if (entry->is_dir || !wo_str_iends_with(entry->relative, ".png")) {
            continue;
        }
        if (entry->is_link) {
            ok = wo_fail(error, WO_ERR_UNSAFE, "Linked artwork is not supported: %s",
                         entry->relative);
            break;
        }
        ok = add_input(inputs, entry->relative, entry->path, error);
    }
    wo_tree_free(&tree);
    return ok;
}

static int compare_inputs(const void *a, const void *b) {
    const Input *left = a;
    const Input *right = b;
    int order = wo_str_icmp(left->relative, right->relative);
    return order ? order : strcmp(left->relative, right->relative);
}

/* --- Native sheets ------------------------------------------------------------- */

typedef struct Native {
    char *stem;
    uint32_t pages;
} Native;

typedef struct Natives {
    Native *items;
    size_t count;
    size_t cap;
} Natives;

static void natives_free(Natives *natives) {
    for (size_t i = 0; i < natives->count; i++) {
        wo_free(natives->items[i].stem);
    }
    wo_free(natives->items);
    memset(natives, 0, sizeof(*natives));
}

static const Native *find_native(const Natives *natives, const char *stem) {
    for (size_t i = 0; i < natives->count; i++) {
        if (wo_str_ieq(natives->items[i].stem, stem)) {
            return &natives->items[i];
        }
    }
    return NULL;
}

static bool read_tga_header(const char *path, uint8_t header[18], bool *complete, WoError *error) {
    *complete = false;
    WoFile *file = NULL;
    if (!wo_file_open_read(path, &file, error)) {
        return false;
    }
    bool ok = true;
    if (wo_file_size(file) >= 18) {
        ok = wo_file_read_at(file, 0, header, 18, error);
        *complete = ok;
    }
    wo_file_close(file, NULL);
    return ok;
}

static bool set_native(Natives *natives, const char *stem, uint32_t pages, WoError *error) {
    for (size_t i = 0; i < natives->count; i++) {
        if (wo_str_ieq(natives->items[i].stem, stem)) {
            natives->items[i].pages = pages;
            return true;
        }
    }
    if (natives->count == natives->cap) {
        size_t cap = natives->cap ? natives->cap * 2 : 64;
        Native *grown = wo_realloc_array(natives->items, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        natives->items = grown;
        natives->cap = cap;
    }
    char *copy = wo_strdup(stem, error);
    if (!copy) {
        return false;
    }
    natives->items[natives->count].stem = copy;
    natives->items[natives->count].pages = pages;
    natives->count++;
    return true;
}

static bool has_hi_res_folder(const char *relative) {
    for (const char *p = relative; *p; p++) {
        if (wo_str_istarts_with(p, "hi-res")) {
            return true;
        }
    }
    return false;
}

/* Page counts of the game's TGA sheets: a W x H sheet with W a multiple of
 * H holds W/H square pages. Hi-res sheets win over others of the same name. */
static bool collect_natives(const char *game_root, Natives *natives, WoError *error) {
    char *pics = wo_path_join(game_root, "textures/sd/Pics", error);
    if (!pics) {
        return false;
    }
    WoTreeList tree = {0};
    bool ok = !wo_is_dir(pics) || wo_tree_list(pics, &tree, error);
    for (size_t i = 0; ok && i < tree.count; i++) {
        const WoTreeEntry *entry = &tree.items[i];
        if (entry->is_dir || !wo_str_iends_with(entry->relative, ".tga")) {
            continue;
        }
        const char *name = wo_path_basename(entry->path);
        char *stem = wo_strndup(name, strlen(name) - 4, error);
        ok = stem != NULL;
        if (ok && (!find_native(natives, stem) || has_hi_res_folder(entry->relative))) {
            uint8_t header[18];
            bool complete = false;
            ok = read_tga_header(entry->path, header, &complete, error);
            uint32_t w = complete ? wo_le16(header + 12) : 0;
            uint32_t h = complete ? wo_le16(header + 14) : 0;
            if (ok && h > 0 && w >= h && w % h == 0) {
                ok = set_native(natives, stem, w / h, error);
            }
        }
        wo_free(stem);
    }
    wo_tree_free(&tree);
    wo_free(pics);
    return ok;
}

/* --- Catalog helpers ------------------------------------------------------------ */

static const WoJson *catalog_entry(const WoJson *catalog, const char *relative) {
    for (size_t i = 0; i < wo_json_length(catalog); i++) {
        const WoJson *entry = wo_json_at(catalog, i);
        if (wo_str_ieq(wo_json_str(entry, "file", ""), relative)) {
            return entry;
        }
    }
    return NULL;
}

/* A whole number; missing counts as 0, as in the previous installer. */
static bool get_int(const WoJson *object, const char *key, int64_t *out) {
    const WoJson *value = wo_json_get(object, key);
    *out = 0;
    if (!value) {
        return true;
    }
    if (value->type != WO_JSON_NUMBER || !value->is_integer) {
        return false;
    }
    *out = value->integer;
    return true;
}

static bool is_region_png_name(const char *name) {
    size_t len = strlen(name);
    if (len <= 4 || !wo_str_iends_with(name, ".png")) {
        return false;
    }
    for (size_t i = 0; i < len - 4; i++) {
        char c = wo_ascii_lower(name[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

/* File name without its last extension. */
static char *stem_of(const char *path, WoError *error) {
    const char *name = wo_path_basename(path);
    const char *ext = wo_path_extension(name);
    return wo_strndup(name, (size_t)(ext - name), error);
}

/* "<stem>.page<N>" -> stem and N (else page -1); a ".tga" left on the stem
 * is dropped. */
static bool split_page(char *stem, int64_t *page) {
    *page = -1;
    size_t len = strlen(stem);
    size_t digits = 0;
    while (digits < len && stem[len - 1 - digits] >= '0' && stem[len - 1 - digits] <= '9') {
        digits++;
    }
    if (digits > 0 && digits <= 9 && len >= digits + 5 &&
        wo_str_istarts_with(stem + len - digits - 5, ".page")) {
        uint64_t value = 0;
        if (!wo_parse_u64(stem + len - digits, 999999999u, &value)) {
            return false;
        }
        *page = (int64_t)value;
        stem[len - digits - 5] = 0;
    } else if (digits > 9 && len >= digits + 5 &&
               wo_str_istarts_with(stem + len - digits - 5, ".page")) {
        return false;
    }
    if (wo_str_iends_with(stem, ".tga")) {
        stem[strlen(stem) - 4] = 0;
    }
    return true;
}

static bool parent_is_pics_folder(const char *relative) {
    const char *slash = strrchr(relative, '/');
    if (!slash) {
        return true;
    }
    size_t len = (size_t)(slash - relative);
    return (len == 11 && (wo_str_istarts_with(relative, "Pics/Hi-res/") ||
                          wo_str_istarts_with(relative, "Pics/Lo-res/")));
}

/* Kind of an uncatalogued PNG named after a native sheet. */
static bool infer_kind(const char *relative, const Natives *natives, const char **kind,
                       WoError *error) {
    char *stem = stem_of(relative, error);
    if (!stem) {
        return false;
    }
    int64_t page = -1;
    bool named = split_page(stem, &page);
    const Native *native = named ? find_native(natives, stem) : NULL;
    wo_free(stem);
    if (!native || !parent_is_pics_folder(relative)) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "Cannot map HD PNG '%s' to a game texture. Use an exported artwork name, "
                       "or the original TGA name (for example SKY01.png or HUD2.page0.png).",
                       relative);
    }
    if (page >= (int64_t)native->pages) {
        return wo_fail(error, WO_ERR_FORMAT, "HD page number exceeds the original texture: %s",
                       relative);
    }
    *kind = page < 0 ? "strip" : "atlas";
    return true;
}

static bool add_regions(const WoJson *entry, const char *relative, const WoImage *image,
                        const Natives *natives, WoHdRegionIndex *index, WoError *error) {
    const WoJson *regions = wo_json_get(entry, "regions");
    if (!is_region_png_name(wo_json_str(entry, "file", "")) ||
        !wo_json_is(regions, WO_JSON_ARRAY) || regions->count == 0) {
        return wo_fail(error, WO_ERR_FORMAT, "HD texture has no valid region mapping: %s",
                       relative);
    }
    int64_t sw = 0;
    int64_t sh = 0;
    if (!get_int(entry, "source_width", &sw) || !get_int(entry, "source_height", &sh) || sw < 1 ||
        sh < 1) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "Invalid source rectangle in the HD texture manifest: %s", relative);
    }
    /* "png": use the PNG's alpha; "black-key": black is transparent, as for
     * the native textures; "auto" (the default): "png" when the image has
     * any transparency. */
    const WoJson *mode_value = wo_json_get(entry, "alpha_mode");
    const char *mode = mode_value ? wo_json_as_str(mode_value, NULL) : "auto";
    if (!mode || !(wo_str_ieq(mode, "auto") || wo_str_ieq(mode, "png") ||
                   wo_str_ieq(mode, "black-key"))) {
        return wo_fail(error, WO_ERR_FORMAT, "Unknown HD transparency mode for: %s", relative);
    }
    bool alpha;
    if (wo_str_ieq(mode, "png")) {
        alpha = true;
    } else if (wo_str_ieq(mode, "black-key")) {
        alpha = false;
    } else {
        alpha = wo_image_has_transparency(image);
    }
    for (size_t i = 0; i < regions->count; i++) {
        const WoJson *region = regions->items[i];
        const char *native_name = wo_json_str(region, "native", "");
        char *stem = stem_of(native_name, error);
        if (!stem) {
            return false;
        }
        const Native *native = find_native(natives, stem);
        wo_free(stem);
        int64_t page = 0;
        int64_t x = 0;
        int64_t y = 0;
        int64_t width = 0;
        int64_t height = 0;
        bool valid = native && get_int(region, "page", &page) && get_int(region, "x", &x) &&
                     get_int(region, "y", &y) && get_int(region, "width", &width) &&
                     get_int(region, "height", &height) && page < (int64_t)native->pages &&
                     width == sw && height == sh && x >= 0 && y >= 0 && x + sw <= PAGE_SIDE &&
                     y + sh <= PAGE_SIDE;
        if (!valid) {
            return wo_fail(error, WO_ERR_FORMAT, "HD region placement is invalid: %s", relative);
        }
        if (!wo_hd_regions_add(index, relative, native_name, page, x, y, sw, sh, alpha, error)) {
            return false;
        }
    }
    return true;
}

static WoJson *file_record(const char *relative, const char *kind, uint32_t width,
                           uint32_t height) {
    WoJson *record = wo_json_new_object();
    bool ok = record && wo_json_put_str(record, "file", relative) &&
              wo_json_put(record, "kind", kind ? wo_json_new_string(kind) : wo_json_new_null()) &&
              wo_json_put_int(record, "width", width) && wo_json_put_int(record, "height", height);
    if (!ok) {
        wo_json_free(record);
        return NULL;
    }
    return record;
}

static bool check_input(const WoJson *catalog, const Input *input, const Natives *natives,
                        WoHdRegionIndex *index, WoJson *files, WoError *error) {
    const WoJson *entry = catalog_entry(catalog, input->relative);
    const char *kind = NULL;
    if (entry) {
        kind = wo_json_str(entry, "kind", NULL);
    } else if (!infer_kind(input->relative, natives, &kind, error)) {
        return false;
    }
    if (kind && wo_str_eq(kind, "reference")) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "HD image '%s' is a reference screenshot, not a game texture. %s",
                       input->relative, wo_json_str(entry, "reason", ""));
    }
    WoImage image;
    if (!wo_png_read(input->path, WO_PIXELS_BGRA, &image, error)) {
        return false;
    }
    bool ok = true;
    if (kind && wo_str_eq(kind, "region")) {
        ok = add_regions(entry, input->relative, &image, natives, index, error);
    }
    WoJson *record = ok ? file_record(input->relative, kind, image.width, image.height) : NULL;
    if (ok && !(record && wo_json_push(files, record))) {
        ok = wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    wo_image_free(&image);
    return ok;
}

/* --- Public API ------------------------------------------------------------------ */

void wo_hd_result_free(WoHdResult *result) {
    if (result) {
        wo_json_free(result->files);
        memset(result, 0, sizeof(*result));
    }
}

static bool require_artwork(const WoJson *catalog, const Inputs *inputs, WoError *error) {
    for (size_t i = 0; i < wo_json_length(catalog); i++) {
        const WoJson *entry = wo_json_at(catalog, i);
        if (wo_json_bool(entry, "required", false)) {
            const char *file = wo_json_str(entry, "file", "");
            if (!find_input(inputs, file)) {
                return wo_fail(error, WO_ERR_NOT_FOUND, "Missing HD artwork: %s", file);
            }
        }
    }
    return true;
}

static bool prepare(const WoHdOptions *options, Inputs *inputs, WoHdRegionIndex *index,
                    WoHdResult *result, WoError *error) {
    memset(inputs, 0, sizeof(*inputs));
    memset(index, 0, sizeof(*index));
    memset(result, 0, sizeof(*result));
    if (!wo_json_is(options->catalog, WO_JSON_ARRAY)) {
        return wo_fail(error, WO_ERR_FORMAT, "The HD texture catalog must be a list.");
    }
    Natives natives = {0};
    result->files = wo_json_new_array();
    bool ok = result->files != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    ok = ok && collect_folder(options->canonical_dir, inputs, error) &&
         collect_folder(options->source_dir, inputs, error) &&
         require_artwork(options->catalog, inputs, error) &&
         collect_natives(options->game_root, &natives, error);
    if (ok && inputs->count > 1) {
        qsort(inputs->items, inputs->count, sizeof(Input), compare_inputs);
    }
    for (size_t i = 0; ok && i < inputs->count; i++) {
        ok = check_input(options->catalog, &inputs->items[i], &natives, index, result->files,
                         error);
    }
    natives_free(&natives);
    if (ok) {
        result->checked = inputs->count;
        result->region_placements = index->count;
    } else {
        inputs_free(inputs);
        wo_hd_regions_free(index);
        wo_hd_result_free(result);
    }
    return ok;
}

bool wo_hd_build_index(const WoHdOptions *options, WoHdRegionIndex *index, WoHdResult *result,
                       WoError *error) {
    Inputs inputs;
    bool ok = prepare(options, &inputs, index, result, error);
    inputs_free(&inputs);
    return ok;
}

bool wo_hd_regions_save(const WoHdRegionIndex *index, const char *path, bool *changed,
                        WoError *error) {
    *changed = false;
    WoBuf data = {0};
    if (!wo_hd_regions_serialize(index, &data, error)) {
        wo_buf_free(&data);
        return false;
    }
    bool ok = true;
    bool same = false;
    if (wo_is_file(path)) {
        WoBuf old = {0};
        WoError ignored = {0};
        /* An unreadable or oversized old index is simply replaced. */
        if (wo_read_file(path, INDEX_MAX_BYTES, &old, &ignored)) {
            same = old.len == data.len && memcmp(old.data, data.data, data.len) == 0;
        }
        wo_buf_free(&old);
    }
    if (!same) {
        ok = wo_write_file_atomic(path, data.data, data.len, error);
        *changed = ok;
    }
    wo_buf_free(&data);
    return ok;
}

/* Keep a supplied PNG in the shipped artwork folder. */
static bool keep_artwork(const char *canonical, const Input *input, bool *copied,
                         WoError *error) {
    *copied = false;
    char *destination = wo_path_child(canonical, input->relative, error);
    char *source = destination ? wo_path_full(input->path, error) : NULL;
    if (!source) {
        wo_free(destination);
        return false;
    }
    bool ok = true;
    if (!wo_str_ieq(source, destination)) {
        bool same = false;
        if (wo_is_file(destination)) {
            char a[WO_SHA256_HEX_SIZE];
            char b[WO_SHA256_HEX_SIZE];
            ok = wo_sha256_file(source, a, error) && wo_sha256_file(destination, b, error);
            same = ok && wo_digest_eq(a, b);
        }
        if (ok && !same) {
            ok = wo_copy_file(source, destination, error);
            *copied = ok;
        }
    }
    wo_free(source);
    wo_free(destination);
    return ok;
}

bool wo_hd_install(const WoHdOptions *options, WoHdResult *result, WoError *error) {
    Inputs inputs;
    WoHdRegionIndex index;
    if (!prepare(options, &inputs, &index, result, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < inputs.count; i++) {
        bool copied = false;
        ok = keep_artwork(options->canonical_dir, &inputs.items[i], &copied, error);
        result->updated += copied ? 1 : 0;
    }
    char *path = ok ? wo_path_child(options->game_root, "textures/hd-regions.bin", error) : NULL;
    bool changed = false;
    ok = path && wo_hd_regions_save(&index, path, &changed, error);
    result->region_index_updated = changed;
    result->updated += changed ? 1 : 0;
    wo_free(path);
    wo_hd_regions_free(&index);
    inputs_free(&inputs);
    if (!ok) {
        wo_hd_result_free(result);
    }
    return ok;
}
