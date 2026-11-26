#include "core/wo_upscaled.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../tools/deband/filter.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"
#include "core/wo_png.h"
#include "core/wo_upscale.h"

#define RECORD "upscaled.json"
#define RECORD_FORMAT "wo4k.upscaled"
#define RECORD_VERSION 1 /* made again when the way of making them changes */
#define WATERFALL "track13-scene-cmp-16.png"
#define FILTER_REACH_LIMIT 128 /* the filter's own */

static bool deband_again(const float *noise, WoImage *large, unsigned scale, const char *name,
                         WoError *error) {
    DebandOptions options;
    deband_defaults(&options);
    options.band = 0;
    options.radius *= (int)scale;
    if (options.radius > FILTER_REACH_LIMIT) {
        options.radius = FILTER_REACH_LIMIT;
    }
    uint8_t *smooth = deband_smooth(large->pixels, (int)large->width, (int)large->height, &options,
                                    noise, deband_seed(name));
    if (!smooth) {
        return wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    memcpy(large->pixels, smooth, (size_t)large->width * large->height * 4);
    free(smooth);
    return true;
}

/* The record's pictures (name -> source digest) when it was made the same
 * way at the same scale, else NULL. */
static const WoJson *recorded(const WoJson *record, unsigned scale) {
    if (!wo_str_eq(wo_json_str(record, "format", ""), RECORD_FORMAT) ||
        wo_json_int(record, "version", 0) != RECORD_VERSION ||
        wo_json_int(record, "scale", 0) != (int64_t)scale) {
        return NULL;
    }
    return wo_json_get(record, "pictures");
}

static WoJson *load_record(const char *output) {
    char *path = wo_path_join(output, RECORD, NULL);
    WoJson *record = NULL;
    if (path && wo_is_file(path)) {
        wo_read_json(path, 4u << 20, &record, NULL);
    }
    wo_free(path);
    return record;
}

/* Delete `name` of `output` (made by us), when it is there. */
static bool remove_made(const char *output, const char *name, WoError *error) {
    char *path = wo_path_child(output, name, error);
    bool ok = path && (!wo_is_file(path) || wo_delete_file(path, error));
    wo_free(path);
    return ok;
}

static bool save_record(const char *output, unsigned scale, WoJson *pictures, WoError *error) {
    WoJson *record = wo_json_new_object();
    char *path = wo_path_join(output, RECORD, error);
    bool ok = record && path && wo_json_put_str(record, "format", RECORD_FORMAT) &&
              wo_json_put_int(record, "version", RECORD_VERSION) &&
              wo_json_put_int(record, "scale", scale) && wo_json_put(record, "pictures", pictures);
    if (!ok) {
        if (!record || !wo_json_has(record, "pictures")) {
            wo_json_free(pictures);
        }
        if (!wo_error_is_set(error)) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
    }
    ok = ok && wo_write_json(path, record, error);
    wo_json_free(record);
    wo_free(path);
    return ok;
}

static bool catalogued(const WoJson *catalog, const char *name) {
    for (size_t i = 0; i < wo_json_length(catalog); i++) {
        if (wo_str_ieq(wo_json_str(wo_json_at(catalog, i), "file", ""), name)) {
            return true;
        }
    }
    return false;
}

bool wo_upscaled_make(const char *source, const char *output, unsigned scale, const char *authored,
                      const WoJson *catalog, WoHashCache *cache, WoProgress *progress, int from,
                      int to, WoUpscaledResult *result, WoError *error) {
    memset(result, 0, sizeof(*result));
    if (scale != 2 && scale != 4) {
        return wo_fail(error, WO_ERR_FORMAT, "The scale is 2 or 4.");
    }
    WoDirList list = {0};
    if (!wo_is_dir(source)) {
        return true; /* nothing debanded: nothing to make (and nothing kept) */
    }
    if (!wo_list_dir(source, &list, error) || !wo_mkdirs(output, error)) {
        wo_dirlist_free(&list);
        return false;
    }
    WoJson *record = load_record(output);
    const WoJson *before = recorded(record, scale);
    const WoJson *any_before = wo_json_get(record, "pictures");
    WoJson *pictures = wo_json_new_object();
    float *noise = NULL;
    bool ok = pictures != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    size_t total = 0;
    for (size_t i = 0; i < list.count; i++) {
        total += !list.items[i].is_dir && wo_str_iends_with(list.items[i].name, ".png");
    }
    size_t seen = 0;
    for (size_t i = 0; ok && i < list.count; i++) {
        const char *name = list.items[i].name;
        if (list.items[i].is_dir || !wo_str_iends_with(name, ".png")) {
            continue;
        }
        seen++;
        char *theirs = authored ? wo_path_join(authored, name, error) : NULL;
        ok = !authored || theirs;
        bool skip = wo_str_ieq(name, WATERFALL) || (theirs && wo_is_file(theirs)) ||
                    (catalog && !catalogued(catalog, name));
        wo_free(theirs);
        if (!ok || skip) {
            result->left_out += skip;
            continue;
        }
        char *from_path = wo_path_join(source, name, error);
        char *to_path = from_path ? wo_path_join(output, name, error) : NULL;
        char digest[WO_SHA256_HEX_SIZE];
        ok = to_path && (cache ? wo_hash_cache_file(cache, from_path, digest, error)
                               : wo_sha256_file(from_path, digest, error));
        bool same = ok && wo_is_file(to_path) &&
                    wo_digest_eq(wo_json_str(before, name, ""), digest);
        if (ok && !same) {
            if (!noise) {
                noise = wo_alloc_array(DEBAND_NOISE_COUNT, sizeof(float), error);
                ok = noise && (deband_noise(noise) || wo_fail(error, WO_ERR_INTERNAL, "Out of memory."));
            }
            if (progress && ok) {
                char message[96];
                snprintf(message, sizeof(message), "Upscaling textures (%u of %u)", (unsigned)seen,
                         (unsigned)total);
                wo_progress(progress, progress->stage,
                            from + (int)((to - from) * (double)seen / (double)(total ? total : 1)),
                            message);
            }
            WoImage texture = {0};
            WoImage large = {0};
            ok = ok && wo_png_read(from_path, WO_PIXELS_RGBA, &texture, error) &&
                 wo_upscale_texture(texture.pixels, texture.width, texture.height, scale, &large,
                                    error) &&
                 deband_again(noise, &large, scale, name, error) &&
                 wo_png_write(to_path, &large, WO_PIXELS_RGBA, error);
            if (!ok) {
                wo_error_context(error, "%s", name);
            }
            wo_image_free(&large);
            wo_image_free(&texture);
            result->made += ok;
        } else {
            result->kept += ok;
        }
        ok = ok && (wo_json_put_str(pictures, name, digest) ||
                    wo_fail(error, WO_ERR_INTERNAL, "Out of memory."));
        wo_free(to_path);
        wo_free(from_path);
    }
    /* What was made before from pictures no longer upscaled. */
    for (size_t i = 0; ok && any_before && i < any_before->count; i++) {
        const char *name = any_before->keys[i];
        if (!wo_json_has(pictures, name) && wo_str_iends_with(name, ".png") &&
            !strchr(name, '/') && !strchr(name, '\\')) {
            ok = remove_made(output, name, error);
            result->removed += ok;
        }
    }
    /* Kept even after a failure part way: what was made is recorded. */
    bool saved = pictures && save_record(output, scale, pictures, ok ? error : NULL);
    wo_json_free(record);
    wo_free(noise);
    wo_dirlist_free(&list);
    return ok && saved;
}

bool wo_upscaled_remove(const char *output, size_t *removed, WoError *error) {
    *removed = 0;
    WoJson *record = load_record(output);
    const WoJson *pictures = wo_json_get(record, "pictures");
    bool ok = true;
    for (size_t i = 0; ok && pictures && i < pictures->count; i++) {
        const char *name = pictures->keys[i];
        if (wo_str_iends_with(name, ".png") && !strchr(name, '/') && !strchr(name, '\\')) {
            ok = remove_made(output, name, error);
            *removed += ok;
        }
    }
    wo_json_free(record);
    if (ok) {
        char *path = wo_path_join(output, RECORD, error);
        ok = path && (!wo_is_file(path) || wo_delete_file(path, error));
        wo_free(path);
    }
    return ok;
}
