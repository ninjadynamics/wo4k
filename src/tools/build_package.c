/* Release assembly for the Makefile:
 *
 *   wo4k-build manifests
 *       Refresh manifests/artwork.json and manifests/third-party.json.
 *   wo4k-build sign-input <folder>
 *       Copy the binaries to sign into <folder>, keeping their paths.
 *   wo4k-build package <version> <dist> [<signed folder>]
 *       dist/wo4k-<version>.zip with release-files.json and a .sha256
 *       sidecar. Signed binaries, when given, replace the local builds.
 *
 * Run from the repository root. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"
#include "core/wo_png.h"
#include "core/wo_release.h"
#include "core/wo_tree.h"
#include "core/wo_zip.h"
#include "tools/build_tool.h"

static const char *const signed_binaries[] = {"Setup.exe", "Play.exe", "runtime/wohr.dll",
                                              "runtime/wo2097-controller.exe",
                                              "runtime/wo4k-video.exe"};
static const char *const top_level[] = {"Setup.exe", "Play.exe", "README.md", "LICENSE",
                                        "VERSION",   "release.url", "source.url"};
static const char *const runtime_files[] = {
    "runtime/wohr.dll",        "runtime/wohr.map",        "runtime/ddraw.dll",
    "runtime/libiconv-2.dll",  "runtime/wo2097-controller.exe", "runtime/wo4k-video.exe",
    "runtime/Wipeout2097.ini", "runtime/DDrawCompat-Wipeout2097.ini",
    "runtime/w2097-hd.cg",     "runtime/w2097-hd.cgp"};
static const char *const folders[] = {"assets/hd", "assets/ui", "manifests", "licenses", "docs",
                                      "patches"};

static bool write_if_changed(const char *path, const WoJson *value, WoError *error) {
    WoBuf text = {0};
    WoBuf existing = {0};
    bool ok = wo_json_write(value, true, &text, error);
    bool same = ok && wo_is_file(path) && wo_read_file(path, 64u << 20, &existing, NULL) &&
                existing.len == text.len && memcmp(existing.data, text.data, text.len) == 0;
    ok = ok && (same || wo_write_file_atomic(path, text.data, text.len, error));
    wo_buf_free(&existing);
    wo_buf_free(&text);
    return ok;
}

static bool artwork_manifest(WoError *error) {
    WoDirList list = {0};
    if (!wo_list_dir("assets/hd", &list, error)) {
        return false;
    }
    WoJson *art = wo_json_new_array();
    bool ok = art != NULL;
    for (size_t i = 0; ok && i < list.count; i++) {
        const char *name = list.items[i].name;
        if (list.items[i].is_dir || !wo_str_iends_with(name, ".png")) {
            continue;
        }
        char *path = wo_format(error, "assets/hd/%s", name);
        uint32_t width = 0;
        uint32_t height = 0;
        char digest[WO_SHA256_HEX_SIZE];
        ok = path && wo_png_read_size(path, &width, &height, error) &&
             wo_sha256_file(path, digest, error);
        if (ok) {
            WoJson *entry = wo_json_new_object();
            ok = wo_json_put_str(entry, "file", path) && wo_json_put_int(entry, "width", width) &&
                 wo_json_put_int(entry, "height", height) && wo_json_put_str(entry, "sha256", digest) &&
                 wo_json_put_str(entry, "license", "MIT for mod contributions") &&
                 wo_json_put_str(entry, "provenance",
                                 "Contributor-enhanced artwork based on user-supplied original "
                                 "WipEout artwork; original creators retain attribution.") &&
                 wo_json_push(art, entry);
        }
        wo_free(path);
    }
    wo_dirlist_free(&list);
    ok = ok && write_if_changed("manifests/artwork.json", art, error);
    wo_json_free(art);
    return ok;
}

typedef struct Library {
    const char *name;
    const char *version;
    const char *file;
    const char *license;
    const char *source;
    const char *license_file;
} Library;

static const Library libraries[] = {
    {"DDrawCompat", "0.7.2", "runtime/ddraw.dll", "0BSD", "https://github.com/narzoul/DDrawCompat",
     "licenses/DDrawCompat.txt"},
    {"SDL3 controller host", "3.4.16", "runtime/wo2097-controller.exe", "zlib (SDL3), MIT (host)",
     "https://github.com/libsdl-org/SDL", "licenses/SDL3.txt"},
    {"GNU libiconv", "1.18", "runtime/libiconv-2.dll", "LGPL-2.1-or-later",
     "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz", "licenses/libiconv-LGPL.txt"},
    {"raylib (Setup window)", "5.5", "Setup.exe", "zlib", "https://github.com/raysan5/raylib",
     "licenses/raylib.txt"},
    {"GLFW (inside raylib)", "3.4", "Setup.exe", "zlib", "https://github.com/glfw/glfw",
     "licenses/GLFW.txt"},
    {"miniz", "3.1.2", "Setup.exe", "MIT", "https://github.com/richgel999/miniz",
     "licenses/miniz.txt"}};

static bool third_party_manifest(WoError *error) {
    WoJson *list = wo_json_new_array();
    bool ok = list != NULL;
    for (size_t i = 0; ok && i < WO_COUNT(libraries); i++) {
        const Library *library = &libraries[i];
        char digest[WO_SHA256_HEX_SIZE] = "";
        if (wo_is_file(library->file)) {
            ok = wo_sha256_file(library->file, digest, error);
        }
        WoJson *entry = wo_json_new_object();
        ok = ok && wo_json_put_str(entry, "name", library->name) &&
             wo_json_put_str(entry, "version", library->version) &&
             wo_json_put_str(entry, "file", library->file) &&
             wo_json_put_str(entry, "license", library->license) &&
             wo_json_put_str(entry, "source", library->source) &&
             wo_json_put_str(entry, "license_file", library->license_file) &&
             wo_json_put_str(entry, "sha256", digest) && wo_json_push(list, entry);
    }
    ok = ok && write_if_changed("manifests/third-party.json", list, error);
    wo_json_free(list);
    return ok;
}

bool build_manifests(WoError *error) {
    return artwork_manifest(error) && third_party_manifest(error);
}

bool build_sign_input(const char *folder, WoError *error) {
    bool ok = true;
    for (size_t i = 0; ok && i < WO_COUNT(signed_binaries); i++) {
        char *target = wo_path_child(folder, signed_binaries[i], error);
        ok = target && wo_copy_file(signed_binaries[i], target, error);
        wo_free(target);
    }
    return ok;
}

static bool add_path(WoStrList *paths, const char *relative, WoError *error) {
    /* Maintainer-only helper kept beside the artwork, never shipped. */
    if (wo_str_ieq(relative, "assets/hd/trim.cmd") ||
        (wo_str_istarts_with(relative, "patches/") && !wo_str_iends_with(relative, ".json"))) {
        return true;
    }
    return wo_strlist_contains_i(paths, relative) || wo_strlist_push(paths, relative, error);
}

static bool collect(WoStrList *paths, WoError *error) {
    bool ok = true;
    for (size_t i = 0; ok && i < WO_COUNT(top_level); i++) {
        ok = add_path(paths, top_level[i], error);
    }
    /* Every one ships: a package without one would make the updater's
     * retired-file cleanup delete it from installations. */
    for (size_t i = 0; ok && i < WO_COUNT(runtime_files); i++) {
        ok = wo_is_file(runtime_files[i])
                 ? add_path(paths, runtime_files[i], error)
                 : wo_fail(error, WO_ERR_NOT_FOUND, "%s is missing; build it first.",
                           runtime_files[i]);
    }
    for (size_t f = 0; ok && f < WO_COUNT(folders); f++) {
        WoTreeList files = {0};
        ok = wo_tree_list(folders[f], &files, error);
        for (size_t i = 0; ok && i < files.count; i++) {
            if (files.items[i].is_dir) {
                continue;
            }
            char *relative = wo_format(error, "%s/%s", folders[f], files.items[i].relative);
            ok = relative && add_path(paths, relative, error);
            wo_free(relative);
        }
        wo_tree_free(&files);
    }
    return ok;
}

static bool is_signed_binary(const char *relative) {
    for (size_t i = 0; i < WO_COUNT(signed_binaries); i++) {
        if (wo_str_ieq(relative, signed_binaries[i])) {
            return true;
        }
    }
    return false;
}

bool build_package(const char *version, const char *dist, const char *signed_dir, WoError *error) {
    int order;
    if (!wo_version_compare(version, "0.0.0", &order)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid version \"%s\".", version);
    }
    WoStrList paths = {0};
    WoStrList sources = {0};
    if (!collect(&paths, error)) {
        wo_strlist_free(&paths);
        return false;
    }
    WoJson *inventory = wo_json_new_object();
    WoJson *files = wo_json_new_array();
    bool ok = inventory && files && wo_json_put_str(inventory, "version", version);
    for (size_t i = 0; ok && i < paths.count; i++) {
        const char *relative = paths.items[i];
        char *source = NULL;
        if (signed_dir && is_signed_binary(relative)) {
            source = wo_path_child(signed_dir, relative, error);
            if (source && !wo_is_file(source)) {
                ok = wo_fail(error, WO_ERR_NOT_FOUND, "The signed folder lacks %s.", relative);
            }
        } else {
            source = wo_strdup(relative, error);
        }
        WoFileInfo info;
        char digest[WO_SHA256_HEX_SIZE];
        ok = ok && source && wo_release_path_allowed(relative, error) && wo_stat(source, &info, error);
        if (ok && !info.exists) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND, "Missing release file: %s", relative);
        }
        ok = ok && wo_sha256_file(source, digest, error);
        if (ok) {
            WoJson *entry = wo_json_new_object();
            ok = wo_json_put_str(entry, "path", relative) &&
                 wo_json_put_int(entry, "bytes", (int64_t)info.size) &&
                 wo_json_put_str(entry, "sha256", digest) && wo_json_push(files, entry) &&
                 wo_strlist_push_owned(&sources, source, error);
            source = NULL;
        }
        wo_free(source);
    }
    ok = ok && wo_json_put(inventory, "files", files);
    files = NULL;
    char *inventory_path = ok ? wo_path_join(dist, "release-files.json", error) : NULL;
    char *zip_name = inventory_path ? wo_format(error, "wo4k-%s.zip", version) : NULL;
    char *zip_path = zip_name ? wo_path_join(dist, zip_name, error) : NULL;
    ok = zip_path && wo_mkdirs(dist, error) && wo_write_json(inventory_path, inventory, error);
    /* A fixed timestamp keeps packages reproducible. */
    const char *epoch = getenv("SOURCE_DATE_EPOCH");
    uint64_t when = 1767225600; /* 2026-01-01 00:00:00 UTC */
    if (epoch && *epoch) {
        wo_parse_u64(epoch, 4102444800ull, &when);
    }
    WoZipWriter *writer = NULL;
    ok = ok && wo_zip_writer_begin((time_t)when, &writer, error);
    for (size_t i = 0; ok && i < paths.count; i++) {
        ok = wo_zip_writer_add_file(writer, paths.items[i], sources.items[i], error);
    }
    ok = ok && wo_zip_writer_add_file(writer, "release-files.json", inventory_path, error);
    if (writer && ok) {
        ok = wo_zip_writer_finish(writer, zip_path, error);
    } else if (writer) {
        wo_zip_writer_abort(writer);
    }
    char digest[WO_SHA256_HEX_SIZE];
    if (ok && wo_sha256_file(zip_path, digest, error)) {
        char *sidecar_path = wo_format(error, "%s.sha256", zip_path);
        char *sidecar = wo_format(error, "%s  %s\r\n", digest, zip_name);
        ok = sidecar_path && sidecar && wo_write_file(sidecar_path, sidecar, strlen(sidecar), error);
        if (ok) {
            printf("Packed %zu files: %s\nSHA256 %s\n", paths.count, zip_path, digest);
        }
        wo_free(sidecar);
        wo_free(sidecar_path);
    } else {
        ok = false;
    }
    wo_free(zip_path);
    wo_free(zip_name);
    wo_free(inventory_path);
    wo_json_free(files);
    wo_json_free(inventory);
    wo_strlist_free(&sources);
    wo_strlist_free(&paths);
    return ok;
}
