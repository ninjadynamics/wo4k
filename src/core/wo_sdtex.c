#include "core/wo_sdtex.h"

#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"
#include "core/wo_png.h"
#include "core/wo_texfmt.h"
#include "core/wo_tree.h"

#define EXPORT_MAX_BYTES (64u * 1024u * 1024u)

static bool is_container(const char *name) {
    return wo_str_iends_with(name, ".cmp") || wo_str_iends_with(name, ".tim") ||
           wo_str_iends_with(name, ".tex");
}

/* TrackNN or Common */
static bool is_data_folder(const char *name) {
    if (wo_str_ieq(name, "Common")) {
        return true;
    }
    return strlen(name) == 7 && wo_str_istarts_with(name, "Track") && name[5] >= '0' &&
           name[5] <= '9' && name[6] >= '0' && name[6] <= '9';
}

static bool copy_containers(const char *source, const char *destination, WoError *error) {
    WoDirList dirs = {0};
    if (!wo_list_dir(source, &dirs, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < dirs.count; i++) {
        const WoDirEntry *dir = &dirs.items[i];
        if (!dir->is_dir || dir->is_link || !is_data_folder(dir->name)) {
            continue;
        }
        char *folder = wo_path_join(source, dir->name, error);
        WoDirList files = {0};
        ok = folder && wo_list_dir(folder, &files, error);
        for (size_t j = 0; ok && j < files.count; j++) {
            const WoDirEntry *file = &files.items[j];
            if (file->is_dir || !is_container(file->name)) {
                continue;
            }
            if (file->is_link) {
                ok = wo_fail(error, WO_ERR_UNSAFE,
                             "Linked files are not supported as game sources: %s", file->name);
                break;
            }
            char *from = wo_path_join(folder, file->name, error);
            char *relative = from ? wo_format(error, "%s/%s", dir->name, file->name) : NULL;
            char *to = relative ? wo_path_child(destination, relative, error) : NULL;
            ok = to && wo_copy_file(from, to, error);
            wo_free(to);
            wo_free(relative);
            wo_free(from);
        }
        wo_dirlist_free(&files);
        wo_free(folder);
    }
    wo_dirlist_free(&dirs);
    return ok;
}

static bool copy_named_folders(const char *source, const char *destination, WoError *error) {
    static const char *const names[] = {"Pics", "Blitsurf", "Textures"};
    for (size_t i = 0; i < WO_COUNT(names); i++) {
        char *input = wo_path_join(source, names[i], error);
        if (input && !wo_is_dir(input)) {
            wo_free(input);
            char *relative = wo_format(error, "textures/sd/%s", names[i]);
            input = relative ? wo_path_join(source, relative, error) : NULL;
            wo_free(relative);
        }
        if (!input) {
            return false;
        }
        bool ok = true;
        if (wo_is_dir(input)) {
            char *output = wo_path_child(destination, names[i], error);
            ok = output && wo_copy_tree(input, output, error);
            wo_free(output);
        }
        wo_free(input);
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* True when any folder in the relative path is named "exports". */
static bool in_exports(const char *relative) {
    for (const char *p = relative; *p; p++) {
        if ((p == relative || p[-1] == '/') && wo_str_istarts_with(p, "exports/")) {
            return true;
        }
    }
    return false;
}

static bool write_inventory(const char *destination, WoError *error) {
    WoTreeList tree;
    if (!wo_tree_list(destination, &tree, error)) {
        return false;
    }
    WoJson *records = wo_json_new_array();
    bool ok = records != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    for (size_t i = 0; ok && i < tree.count; i++) {
        const WoTreeEntry *entry = &tree.items[i];
        if (entry->is_dir || in_exports(entry->relative) ||
            wo_str_ieq(entry->relative, "sources.json")) {
            continue;
        }
        char hash[WO_SHA256_HEX_SIZE];
        ok = wo_sha256_file(entry->path, hash, error);
        /* Backslash-separated, as in existing installations. */
        char *relative = ok ? wo_strdup(entry->relative, error) : NULL;
        for (char *p = relative; p && *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
        WoJson *record = relative ? wo_json_new_object() : NULL;
        ok = record && wo_json_put_str(record, "file", relative) &&
             wo_json_put_str(record, "sha256", hash) && wo_json_push(records, record);
        if (relative && !ok) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
        wo_free(relative);
    }
    char *path = ok ? wo_path_child(destination, "sources.json", error) : NULL;
    ok = path && wo_write_json(path, records, error);
    wo_free(path);
    wo_json_free(records);
    wo_tree_free(&tree);
    return ok;
}

bool wo_sd_extract(const char *native_root, const char *destination, WoError *error) {
    char *source = wo_path_full(native_root, error);
    if (!source) {
        return false;
    }
    bool ok = true;
    if (!wo_is_dir(source)) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND, "The game folder does not exist: %s", source);
    }
    char *previous = ok ? wo_path_join(source, "textures/sd", error) : NULL;
    ok = ok && previous && (!wo_is_dir(previous) || wo_copy_tree(previous, destination, error));
    ok = ok && wo_mkdirs(destination, error) && copy_named_folders(source, destination, error) &&
         copy_containers(source, destination, error) && write_inventory(destination, error);
    wo_free(previous);
    wo_free(source);
    return ok;
}

bool wo_sd_add_regional_aliases(const char *sd_root, WoError *error) {
    static const char *const resolutions[] = {"Hi-res", "Lo-res"};
    static const char *const pairs[][3] = {{"Blitsurf", "RSTART97.PPM", "RSTARTXL.PPM"},
                                           {"Blitsurf", "2097COPY.PPM", "WOXLCOPY.PPM"},
                                           {"Pics", "2097LOGO.TGA", "XLLOGO.TGA"}};
    for (size_t r = 0; r < WO_COUNT(resolutions); r++) {
        for (size_t i = 0; i < WO_COUNT(pairs); i++) {
            char *from_rel = wo_format(error, "%s/%s/%s", pairs[i][0], resolutions[r], pairs[i][1]);
            char *to_rel = from_rel ? wo_format(error, "%s/%s/%s", pairs[i][0], resolutions[r],
                                                pairs[i][2])
                                    : NULL;
            char *from = to_rel ? wo_path_child(sd_root, from_rel, error) : NULL;
            char *to = from ? wo_path_child(sd_root, to_rel, error) : NULL;
            bool ok = to != NULL;
            if (ok && !wo_exists(to) && wo_is_file(from)) {
                ok = wo_copy_file(from, to, error);
            }
            wo_free(to);
            wo_free(from);
            wo_free(to_rel);
            wo_free(from_rel);
            if (!ok) {
                return false;
            }
        }
    }
    return true;
}

/* --- Maintainer PNG export -------------------------------------------------------- */

static bool export_image(const char *sd_root, const char *relative, const char *suffix,
                         const WoImage *image, WoError *error) {
    char *name = wo_format(error, "exports/%s%s", relative, suffix);
    char *path = name ? wo_path_child(sd_root, name, error) : NULL;
    bool ok = path && wo_png_write(path, image, WO_PIXELS_BGRA, error);
    wo_free(path);
    wo_free(name);
    return ok;
}

static bool export_cmp(const char *sd_root, const char *relative, const WoBuf *data,
                       WoError *error) {
    WoCmp cmp;
    if (!wo_cmp_decode(data->data, data->len, &cmp, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < cmp.count; i++) {
        if (cmp.sizes[i] == 0) {
            ok = wo_fail(error, WO_ERR_FORMAT, "Entry %zu of the CMP archive is empty.", i);
            break;
        }
        WoImage image;
        ok = wo_tim_decode_bgra(cmp.data + cmp.offsets[i], cmp.sizes[i], &image, error);
        char suffix[64];
        snprintf(suffix, sizeof(suffix), ".png.parts/%04u.png", (unsigned)i);
        ok = ok && export_image(sd_root, relative, suffix, &image, error);
        wo_image_free(&image);
    }
    wo_cmp_free(&cmp);
    return ok;
}

static bool export_file(const char *sd_root, const WoTreeEntry *entry, WoError *error) {
    const char *ext = wo_path_extension(entry->path);
    bool tga = wo_str_ieq(ext, ".tga");
    bool tim = wo_str_ieq(ext, ".tim");
    bool cmp = wo_str_ieq(ext, ".cmp");
    if (!tga && !tim && !cmp) {
        return true;
    }
    WoBuf data = {0};
    bool ok = wo_read_file(entry->path, EXPORT_MAX_BYTES, &data, error);
    if (ok && cmp) {
        ok = export_cmp(sd_root, entry->relative, &data, error);
    } else if (ok) {
        WoImage image;
        ok = tga ? wo_tga_decode_bgra(data.data, data.len, &image, error)
                 : wo_tim_decode_bgra(data.data, data.len, &image, error);
        ok = ok && export_image(sd_root, entry->relative, ".png", &image, error);
        wo_image_free(&image);
    }
    if (!ok) {
        wo_error_context(error, "Exporting %s: ", entry->relative);
    }
    wo_buf_free(&data);
    return ok;
}

bool wo_sd_export_png(const char *sd_root, WoError *error) {
    WoTreeList tree;
    if (!wo_tree_list(sd_root, &tree, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < tree.count; i++) {
        if (!tree.items[i].is_dir && !in_exports(tree.items[i].relative)) {
            ok = export_file(sd_root, &tree.items[i], error);
        }
    }
    wo_tree_free(&tree);
    return ok;
}
