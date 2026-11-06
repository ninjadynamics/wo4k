/* wo4k-build: build-time helper used by the Makefile (never shipped).
 *
 *   wo4k-build icon <input.png> <output.ico>
 *       Multi-size icon (16-256 px PNG frames) from a square PNG.
 *   wo4k-build hd-catalog <manifests/hd-textures.json> <output.h>
 *       C table of the HD atlas aliases used by the game runtime.
 *   wo4k-build fetch <manifests/dependencies.json> <deps folder>
 *   wo4k-build manifests
 *   wo4k-build sign-input <folder>
 *   wo4k-build package <version> <dist folder> [<signed folder>]
 *       See build_fetch.c and build_package.c.
 *
 * Maintainer commands (they read game files you own and never modify them):
 *
 *   wo4k-build export-textures <PC game folder or wobble> <output folder>
 *       Copy the SD texture containers, then decode every TGA, TIM and CMP
 *       frame to PNG under <output folder>/exports.
 *   wo4k-build extract-music <folder or .mds/.cue/.bin> <output folder> pc|psx
 *       Write the bank's trackNN.wav files and tracks.ini.
 *
 * Outputs are written only when their content changes, so dependent targets
 * are not rebuilt needlessly. */
#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_json.h"
#include "core/wo_music.h"
#include "core/wo_sdtex.h"
#include "core/wo_win.h"
#include "tools/build_tool.h"

static bool write_if_changed(const char *path, const void *data, size_t len, WoError *error) {
    WoBuf existing = {0};
    bool same = wo_is_file(path) && wo_read_file(path, len + 1, &existing, NULL) &&
                existing.len == len && memcmp(existing.data, data, len) == 0;
    wo_buf_free(&existing);
    return same || wo_write_file_atomic(path, data, len, error);
}

static bool name_ok(const char *text, const char *suffix) {
    size_t len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (len <= suffix_len || strcmp(text + len - suffix_len, suffix) != 0) {
        return false;
    }
    for (size_t i = 0; i < len - suffix_len; i++) {
        char c = text[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool hd_catalog(const char *manifest_path, const char *output, WoError *error) {
    WoJson *catalog = NULL;
    if (!wo_read_json(manifest_path, 16u << 20, &catalog, error)) {
        return false;
    }
    WoBuf text = {0};
    bool ok = wo_buf_append_str(&text, "/* Generated from manifests/hd-textures.json. */\n"
                                       "typedef struct HdAlias {const WCHAR *png;const char "
                                       "*native;UINT page;} HdAlias;\n"
                                       "static const HdAlias hd_aliases[]={\n",
                                error);
    for (size_t i = 0; ok && i < wo_json_length(catalog); i++) {
        const WoJson *entry = wo_json_at(catalog, i);
        if (!wo_str_eq(wo_json_str(entry, "kind", ""), "atlas")) {
            continue;
        }
        const char *file = wo_json_str(entry, "file", "");
        const char *native = wo_json_str(entry, "native", "");
        int64_t page = wo_json_int(entry, "page", -1);
        if (!name_ok(file, ".png") || !name_ok(native, ".tga") || page < 0 || page > 255) {
            ok = wo_fail(error, WO_ERR_FORMAT, "Invalid HD texture catalog entry %zu.", i);
            break;
        }
        ok = wo_buf_printf(&text, error, "    {L\"%s\",\"%s\",%d},\n", file, native, (int)page);
    }
    ok = ok && wo_buf_append_str(&text, "};", error);
    ok = ok && write_if_changed(output, text.data, text.len, error);
    wo_buf_free(&text);
    wo_json_free(catalog);
    return ok;
}

int wmain(int argc, wchar_t **argv);

int wmain(int argc, wchar_t **argv) {
    char *args[8] = {0};
    for (int i = 1; i < argc && i < 8; i++) {
        args[i] = wo_utf8(argv[i], NULL);
    }
    WoError error = {0};
    bool ok;
    if (argc == 4 && wo_str_eq(args[1], "icon")) {
        WoBuf ico = {0};
        ok = build_icon(args[2], &ico, &error) && write_if_changed(args[3], ico.data, ico.len, &error);
        wo_buf_free(&ico);
    } else if (argc == 4 && wo_str_eq(args[1], "hd-catalog")) {
        ok = hd_catalog(args[2], args[3], &error);
    } else if (argc == 4 && wo_str_eq(args[1], "fetch")) {
        ok = build_fetch(args[2], args[3], &error);
    } else if (argc == 2 && wo_str_eq(args[1], "manifests")) {
        ok = build_manifests(&error);
    } else if (argc == 3 && wo_str_eq(args[1], "sign-input")) {
        ok = build_sign_input(args[2], &error);
    } else if ((argc == 4 || argc == 5) && wo_str_eq(args[1], "package")) {
        ok = build_package(args[2], args[3], argc == 5 ? args[4] : NULL, &error);
    } else if (argc == 4 && wo_str_eq(args[1], "export-textures")) {
        ok = wo_sd_extract(args[2], args[3], &error) && wo_sd_export_png(args[3], &error);
    } else if (argc == 5 && wo_str_eq(args[1], "extract-music") &&
               (wo_str_eq(args[4], "pc") || wo_str_eq(args[4], "psx"))) {
        bool extracted = false;
        WoMusicBank bank = wo_str_eq(args[4], "pc") ? WO_MUSIC_PC : WO_MUSIC_PSX;
        ok = wo_music_extract(args[2], args[3], bank, false, &extracted, &error);
    } else {
        fprintf(stderr, "usage: wo4k-build icon <in.png> <out.ico>\n"
                        "       wo4k-build hd-catalog <hd-textures.json> <out.h>\n"
                        "       wo4k-build fetch <dependencies.json> <deps folder>\n"
                        "       wo4k-build manifests\n"
                        "       wo4k-build sign-input <folder>\n"
                        "       wo4k-build package <version> <dist> [<signed folder>]\n"
                        "       wo4k-build export-textures <game folder> <output folder>\n"
                        "       wo4k-build extract-music <source> <output folder> pc|psx\n");
        ok = false;
        wo_fail(&error, WO_ERR_FORMAT, "Invalid arguments.");
    }
    if (!ok) {
        fprintf(stderr, "wo4k-build: %s\n", error.message);
    }
    for (int i = 0; i < 8; i++) {
        wo_free(args[i]);
    }
    return ok ? 0 : 1;
}
