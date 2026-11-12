/* Golden comparisons against a real installation and the user's original
 * discs. Every test skips (and says why) when its data is not available,
 * and nothing outside build/test-work is ever written.
 *
 * Data is looked up in the main checkout, found from WO_GOLDEN_ROOT, the
 * repository root or (for a git worktree) three folders up:
 *   wobble/                 an installation made by the previous installer
 *   .state/setup.json       its configured sources (GameSource, PcMusicSource,
 *                           PsxMusicSource; ZIPs are unpacked here with miniz)
 * WO_GOLDEN_PC_DISC and WO_GOLDEN_PS1_DISC may name a disc descriptor, image,
 * folder or ZIP to use instead of the configured music sources. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_audioclean.h"
#include "core/wo_cdaudio.h"
#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hdtex.h"
#include "core/wo_iso.h"
#include "core/wo_miniz.h"
#include "core/wo_music.h"
#include "core/wo_natives.h"
#include "core/wo_patch.h"
#include "core/wo_sdtex.h"
#include "wo_test.h"

static char *golden_root;
static WoJson *setup;

static void skip(const char *why) {
    printf("  skip (%s)\n", why);
}

static char *under(const char *root, const char *relative) {
    return wo_path_join(root, relative, NULL);
}

static void find_golden_root(void) {
    const char *candidates[] = {getenv("WO_GOLDEN_ROOT"), ".", "../../.."};
    for (size_t i = 0; i < WO_COUNT(candidates) && !golden_root; i++) {
        if (!candidates[i] || !*candidates[i]) {
            continue;
        }
        char *full = wo_path_full(candidates[i], NULL);
        char *probe = full ? under(full, "wobble/textures/selection.json") : NULL;
        if (probe && wo_is_file(probe)) {
            golden_root = full;
            full = NULL;
        }
        wo_free(probe);
        wo_free(full);
    }
    if (golden_root) {
        char *path = under(golden_root, ".state/setup.json");
        if (path && wo_is_file(path)) {
            wo_read_json(path, 1 << 20, &setup, NULL);
        }
        wo_free(path);
    }
}

static bool same_file_hash(const char *path, const char *expected) {
    char hash[WO_SHA256_HEX_SIZE];
    return wo_sha256_file(path, hash, NULL) && wo_digest_eq(hash, expected);
}

/* --- ZIP sources (unpacked once into build/test-work and reused) ----------------- */

typedef struct ZipWriter {
    WoFile *file;
} ZipWriter;

static size_t zip_read(void *opaque, mz_uint64 offset, void *buffer, size_t n) {
    return wo_source_read(opaque, offset, buffer, n, NULL) ? n : 0;
}

static size_t zip_write(void *opaque, mz_uint64 offset, const void *buffer, size_t n) {
    (void)offset;
    ZipWriter *writer = opaque;
    return wo_file_write(writer->file, buffer, n, NULL) ? n : 0;
}

static bool unzip_all(const char *zip_path, const char *folder, WoError *error) {
    WoSource source;
    if (!wo_source_open_file(&source, zip_path, error)) {
        return false;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zip_read;
    zip.m_pIO_opaque = &source;
    bool ok = mz_zip_reader_init(&zip, source.size, 0);
    if (!ok) {
        wo_fail(error, WO_ERR_FORMAT, "Cannot read the ZIP %s.", zip_path);
    }
    mz_uint count = ok ? mz_zip_reader_get_num_files(&zip) : 0;
    for (mz_uint i = 0; ok && i < count; i++) {
        mz_zip_archive_file_stat stat;
        ok = mz_zip_reader_file_stat(&zip, i, &stat);
        if (!ok || mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }
        char *target = wo_path_child(folder, stat.m_filename, error);
        ZipWriter writer = {NULL};
        ok = target && wo_file_create(target, &writer.file, error);
        if (ok) {
            ok = mz_zip_reader_extract_to_callback(&zip, i, zip_write, &writer, 0);
            if (!ok) {
                wo_fail(error, WO_ERR_FORMAT, "Cannot unpack %s.", stat.m_filename);
            }
            ok = wo_file_close(writer.file, error) && ok;
        }
        wo_free(target);
    }
    mz_zip_reader_end(&zip);
    wo_source_close(&source);
    return ok;
}

/* A configured source, unpacked when it is a ZIP. NULL when unavailable. */
static char *resolve_source(const char *env, const char *setting, const char *cache_name) {
    const char *configured = getenv(env);
    if (!configured || !*configured) {
        configured = wo_json_str(setup, setting, NULL);
    }
    if (!configured || !wo_exists(configured)) {
        return NULL;
    }
    if (!wo_str_iends_with(configured, ".zip")) {
        return wo_path_full(configured, NULL);
    }
    WoFileInfo info;
    char *base = wo_path_full("build/test-work", NULL);
    char *folder = base ? under(base, cache_name) : NULL;
    char *marker = folder ? under(folder, ".unpacked") : NULL;
    char *stamp = NULL;
    if (marker && wo_stat(configured, &info, NULL)) {
        stamp = wo_format(NULL, "%s|%llu|%llu", configured, (unsigned long long)info.size,
                          (unsigned long long)info.mtime);
    }
    char *current = NULL;
    bool fresh = stamp && wo_read_text(marker, 4096, &current, NULL) && strcmp(current, stamp) == 0;
    WoError error = {0};
    if (stamp && !fresh) {
        printf("  (unpacking %s)\n", wo_path_basename(configured));
        fflush(stdout);
        fresh = wo_mkdirs(base, &error) && wo_delete_tree(base, folder, &error) &&
                unzip_all(configured, folder, &error) &&
                wo_write_file(marker, stamp, strlen(stamp), &error);
        if (!fresh) {
            wo_test_fail(__FILE__, __LINE__, "unpack", error.message);
        }
    }
    wo_free(current);
    wo_free(stamp);
    wo_free(marker);
    wo_free(base);
    if (!fresh) {
        wo_free(folder);
        return NULL;
    }
    return folder;
}

/* First .mds/.cue below a folder (searched 8 levels deep), or the path itself. */
static char *find_descriptor(const char *source, int depth) {
    if (!wo_is_dir(source)) {
        return wo_strdup(source, NULL);
    }
    if (depth > 8) {
        return NULL;
    }
    char *found = NULL;
    WoDirList list = {0};
    if (wo_list_dir(source, &list, NULL)) {
        for (size_t i = 0; i < list.count && !found; i++) {
            const char *name = list.items[i].name;
            if (!list.items[i].is_dir &&
                (wo_str_iends_with(name, ".mds") || wo_str_iends_with(name, ".cue"))) {
                found = under(source, name);
            }
        }
        for (size_t i = 0; i < list.count && !found; i++) {
            if (list.items[i].is_dir && !list.items[i].is_link) {
                char *child = under(source, list.items[i].name);
                found = child ? find_descriptor(child, depth + 1) : NULL;
                if (found && !wo_is_file(found)) {
                    wo_free(found);
                    found = NULL;
                }
                wo_free(child);
            }
        }
    }
    wo_dirlist_free(&list);
    return found;
}

/* --- Sound effects ---------------------------------------------------------------- */

static void test_audio_clean_matches_exe(void) {
    if (!golden_root) {
        skip("no golden installation");
        return;
    }
    char *source = under(golden_root, "wobble/audio/source");
    char *expected_text = NULL;
    WoError error = {0};
    if (!wo_is_dir(source) ||
        !wo_read_text(wo_test_fixture("golden/audio-clean-exe.txt"), 1 << 20, &expected_text,
                      &error)) {
        skip("no wobble/audio/source");
        wo_free(source);
        return;
    }
    size_t compared = 0;
    for (char *line = strtok(expected_text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *space = strchr(line, ' ');
        if (line[0] == '#' || !space) {
            continue;
        }
        *space = 0;
        char *path = under(source, line);
        WoBuf input = {0};
        uint8_t *output = NULL;
        char hash[WO_SHA256_HEX_SIZE];
        CHECK_OK(wo_read_file(path, WO_AUDIO_MAX_BYTES, &input, &error) &&
                     wo_audio_clean(input.data, input.len, path, &output, &error) &&
                     wo_sha256_mem(output, input.len, hash, &error),
                 &error);
        if (output && !wo_digest_eq(hash, space + 1)) {
            wo_test_fail(__FILE__, __LINE__, line, "differs from audio-clean.exe");
        }
        compared++;
        wo_free(output);
        wo_buf_free(&input);
        wo_free(path);
    }
    printf("  (%zu effects bit-identical to audio-clean.exe)\n", compared);
    CHECK(compared == 39);
    wo_free(expected_text);
    wo_free(source);
}

/* --- HD region index ----------------------------------------------------------------- */

static void test_hd_regions_match(void) {
    if (!golden_root) {
        skip("no golden installation");
        return;
    }
    char *game = under(golden_root, "wobble");
    char *installed = under(game, "textures/hd-regions.bin");
    WoError error = {0};
    WoJson *catalog = NULL;
    if (!wo_is_file(installed)) {
        skip("no wobble/textures/hd-regions.bin");
    } else {
        CHECK_OK(wo_read_json("manifests/hd-textures.json", 16 << 20, &catalog, &error), &error);
        WoHdOptions options = {catalog, "assets/hd", "assets/hd", game, NULL, NULL};
        WoHdRegionIndex index;
        WoHdResult result;
        CHECK_OK(wo_hd_build_index(&options, &index, &result, &error), &error);
        WoBuf built = {0};
        WoBuf expected = {0};
        CHECK_OK(wo_hd_regions_serialize(&index, &built, &error), &error);
        CHECK_OK(wo_read_file(installed, 16 << 20, &expected, &error), &error);
        CHECK(built.len == expected.len);
        CHECK(built.len == expected.len && memcmp(built.data, expected.data, built.len) == 0);
        printf("  (%zu PNGs checked, %zu placements, %zu bytes)\n", result.checked,
               result.region_placements, built.len);
        wo_buf_free(&expected);
        wo_buf_free(&built);
        wo_hd_regions_free(&index);
        wo_hd_result_free(&result);
    }
    wo_json_free(catalog);
    wo_free(installed);
    wo_free(game);
}

/* --- Native texture sheets ----------------------------------------------------------- */

static void test_native_sheets_match(void) {
    if (!golden_root) {
        skip("no golden installation");
        return;
    }
    char *game = under(golden_root, "wobble");
    char *receipt_path = under(game, "textures/selection.json");
    char *sd = under(game, "textures/sd");
    char *ps1 = under(game, "textures/ps1-source");
    char *original = under(game, "textures/native-source");
    WoError error = {0};
    WoJson *plan = NULL;
    WoJson *receipt = NULL;
    CHECK_OK(wo_read_json("manifests/texture-selection.json", 32 << 20, &plan, &error), &error);
    CHECK_OK(wo_read_json(receipt_path, 16 << 20, &receipt, &error), &error);
    if (wo_json_int(receipt, "revision", -1) != wo_json_int(plan, "revision", -2)) {
        /* Built by an older selection; the next Play rebuilds the sheets. */
        skip("the golden installation's sheets predate this texture selection");
        wo_json_free(receipt);
        wo_json_free(plan);
        wo_free(original);
        wo_free(ps1);
        wo_free(sd);
        wo_free(receipt_path);
        wo_free(game);
        return;
    }
    WoSheetBuilder *builder = wo_sheet_builder_new(game, sd, ps1, &error);
    CHECK(builder != NULL);
    WoNativeCounts counts = {0, 0, 0};
    const WoJson *files = wo_json_get(receipt, "files");
    size_t matched = 0;
    for (size_t i = 0; builder && i < wo_json_length(files); i++) {
        const char *atlas = wo_json_str(wo_json_at(files, i), "file", "");
        const char *hash = wo_json_str(wo_json_at(files, i), "sha256", "");
        char *pristine = wo_path_child(original, atlas, &error);
        WoBuf sheet = {0};
        char built[WO_SHA256_HEX_SIZE];
        CHECK_OK(pristine && wo_read_file(pristine, 64 << 20, &sheet, &error) &&
                     wo_sheet_builder_apply(builder, plan, atlas, sheet.data, sheet.len, &counts,
                                            &error) &&
                     wo_sha256_mem(sheet.data, sheet.len, built, &error),
                 &error);
        if (sheet.len && wo_digest_eq(built, hash)) {
            matched++;
        } else {
            wo_test_fail(__FILE__, __LINE__, atlas, "rebuilt sheet differs from selection.json");
        }
        wo_buf_free(&sheet);
        wo_free(pristine);
    }
    printf("  (%zu of %zu sheets identical; %zu PC, %zu PS1, %zu pending regions)\n", matched,
           wo_json_length(files), counts.pc_regions, counts.ps1_regions,
           counts.pending_ps1_regions);
    CHECK(matched == wo_json_length(files) && matched > 0);
    CHECK(counts.pc_regions == (size_t)wo_json_int(receipt, "pc_regions", -1));
    CHECK(counts.ps1_regions == (size_t)wo_json_int(receipt, "ps1_regions", -1));
    CHECK(counts.pending_ps1_regions == (size_t)wo_json_int(receipt, "pending_ps1_regions", -1));
    bool healthy = false;
    CHECK_OK(wo_natives_test(game, plan, true, &healthy, &error), &error);
    CHECK(healthy);
    wo_sheet_builder_free(builder);
    wo_json_free(receipt);
    wo_json_free(plan);
    wo_free(original);
    wo_free(ps1);
    wo_free(sd);
    wo_free(receipt_path);
    wo_free(game);
}

/* --- SD extraction from the configured game folder ------------------------------------- */

static void test_sd_inventory_matches(void) {
    const char *game_source = wo_json_str(setup, "GameSource", NULL);
    if (!golden_root || !game_source || !wo_is_dir(game_source)) {
        skip("no configured game folder");
        return;
    }
    const char *dir = wo_test_dir("golden-sd");
    char *expected_path = under(golden_root, "wobble/textures/sd/sources.json");
    char *built_path = dir ? under(dir, "sources.json") : NULL;
    WoError error = {0};
    WoJson *expected = NULL;
    WoJson *built = NULL;
    CHECK_OK(dir && wo_sd_extract(game_source, dir, &error), &error);
    CHECK_OK(wo_read_json(expected_path, 16 << 20, &expected, &error), &error);
    CHECK_OK(wo_read_json(built_path, 16 << 20, &built, &error), &error);
    size_t same = 0;
    for (size_t i = 0; i < wo_json_length(expected); i++) {
        const WoJson *want = wo_json_at(expected, i);
        for (size_t j = 0; j < wo_json_length(built); j++) {
            const WoJson *have = wo_json_at(built, j);
            if (wo_str_ieq(wo_json_str(want, "file", "?"), wo_json_str(have, "file", "")) &&
                wo_digest_eq(wo_json_str(want, "sha256", ""), wo_json_str(have, "sha256", ""))) {
                same++;
                break;
            }
        }
    }
    printf("  (%zu of %zu inventory records identical; %zu written)\n", same,
           wo_json_length(expected), wo_json_length(built));
    CHECK(same == wo_json_length(expected) && same == wo_json_length(built));
    wo_json_free(built);
    wo_json_free(expected);
    wo_free(built_path);
    wo_free(expected_path);
}

/* --- Disc music, data tracks and executables -------------------------------------------- */

typedef struct HashSink {
    WoSha256 *hash;
} HashSink;

static bool hash_sink(void *context, const void *data, size_t len, WoError *error) {
    HashSink *sink = context;
    return wo_sha256_update(sink->hash, data, len, error);
}

static void check_music(const WoTrackList *tracks, const char *bank_folder) {
    WoError error = {0};
    WoBuf ini = {0};
    char *toc_path = under(bank_folder, "tracks.ini");
    WoBuf expected = {0};
    CHECK_OK(wo_tracks_write_ini(tracks, &ini, &error), &error);
    CHECK_OK(wo_read_file(toc_path, 1 << 20, &expected, &error), &error);
    CHECK(ini.len == expected.len && memcmp(ini.data, expected.data, ini.len) == 0);
    size_t identical = 0;
    size_t songs = 0;
    for (size_t i = 0; i < tracks->count; i++) {
        const WoTrack *t = &tracks->items[i];
        if (!t->audio) {
            continue;
        }
        songs++;
        char name[32];
        snprintf(name, sizeof(name), "track%02d.wav", t->number);
        char *installed = under(bank_folder, name);
        char want[WO_SHA256_HEX_SIZE];
        char got[WO_SHA256_HEX_SIZE];
        WoSource source = {0};
        HashSink sink = {wo_sha256_begin(&error)};
        CHECK_OK(wo_sha256_file(installed, want, &error) &&
                     wo_source_open_file(&source, t->image, &error),
                 &error);
        if (sink.hash && source.read_at) {
            CHECK_OK(wo_cdaudio_to_wav(&source, t->offset, t->frames, t->stride, hash_sink,
                                       &sink, &error) &&
                         wo_sha256_end(sink.hash, got, &error),
                     &error);
            sink.hash = NULL;
            wo_source_close(&source);
            identical += wo_digest_eq(want, got) ? 1 : 0;
        }
        wo_sha256_abort(sink.hash);
        wo_free(installed);
    }
    printf("  (tracks.ini %s; %zu of %zu songs identical)\n",
           ini.len == expected.len && memcmp(ini.data, expected.data, ini.len) == 0 ? "identical"
                                                                                   : "DIFFERS",
           identical, songs);
    CHECK(identical == songs && songs > 0);
    wo_buf_free(&expected);
    wo_free(toc_path);
    wo_buf_free(&ini);
}

static const WoJson *manifest_audio_entry(const WoJson *manifest, const char *name) {
    const WoJson *audio = wo_json_get(manifest, "audio");
    for (size_t i = 0; i < wo_json_length(audio); i++) {
        if (wo_str_ieq(wo_json_str(wo_json_at(audio, i), "name", ""), name)) {
            return wo_json_at(audio, i);
        }
    }
    return NULL;
}

static bool listed(const WoJson *array, const char *hash) {
    for (size_t i = 0; i < wo_json_length(array); i++) {
        if (wo_digest_eq(wo_json_as_str(wo_json_at(array, i), ""), hash)) {
            return true;
        }
    }
    return false;
}

/* The hash recorded for name in a "NAME HASH" fixture, or false. */
static bool fixture_hash(const char *text, const char *name, char out[WO_SHA256_HEX_SIZE]) {
    size_t name_len = strlen(name);
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len >= name_len + 65 && line[name_len] == ' ' &&
            strncmp(line, name, name_len) == 0) {
            memcpy(out, line + name_len + 1, 64);
            out[64] = 0;
            return wo_is_hex_digest(out);
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}

/* The disc's effects are the manifest's sources and clean exactly as the
 * former audio-clean.exe did; its executable patches to the manifest's
 * output. */
static void check_pc_data(const WoTrackList *tracks) {
    WoError error = {0};
    WoJson *manifest = NULL;
    char *exe_hashes = NULL;
    CHECK_OK(wo_read_json("manifests/patches.json", 1 << 20, &manifest, &error), &error);
    CHECK_OK(wo_read_text(wo_test_fixture("golden/audio-clean-exe-disc.txt"), 1 << 20,
                          &exe_hashes, &error),
             &error);
    const WoTrack *data = wo_tracks_first_data(tracks);
    WoSource source = {0};
    WoIso iso;
    CHECK_OK(data && wo_source_open_file(&source, data->image, &error), &error);
    if (!data || !source.read_at || !exe_hashes) {
        wo_source_close(&source);
        wo_free(exe_hashes);
        wo_json_free(manifest);
        return;
    }
    CHECK_OK(wo_iso_open(&iso, &source, data->offset, &error), &error);
    size_t sources = 0;
    size_t cleaned = 0;
    size_t as_manifest = 0;
    size_t effects = 0;
    size_t patched = 0;
    for (size_t i = 0; i < iso.count; i++) {
        const WoIsoEntry *entry = &iso.entries[i];
        const char *name = wo_path_basename(entry->path);
        uint8_t *bytes = NULL;
        char hash[WO_SHA256_HEX_SIZE];
        const WoJson *effect = wo_str_istarts_with(entry->path, "SOUND/")
                                   ? manifest_audio_entry(manifest, name)
                                   : NULL;
        bool exe = wo_str_ieq(entry->path, "D3D.EXE") || wo_str_ieq(entry->path, "WIPEOUT2.EXE");
        if (entry->is_dir || (!effect && !exe)) {
            continue;
        }
        CHECK_OK(wo_iso_read_entry(&iso, entry, 128u << 20, &bytes, &error) &&
                     wo_sha256_mem(bytes, entry->length, hash, &error),
                 &error);
        if (effect) {
            effects++;
            if (bytes && listed(wo_json_get(effect, "source_hashes"), hash)) {
                sources++;
                uint8_t *out = NULL;
                char clean[WO_SHA256_HEX_SIZE];
                CHECK_OK(wo_audio_clean(bytes, entry->length, name, &out, &error) &&
                             wo_sha256_mem(out, entry->length, clean, &error),
                         &error);
                char expected[WO_SHA256_HEX_SIZE];
                if (out && fixture_hash(exe_hashes, name, expected) &&
                    wo_digest_eq(clean, expected)) {
                    cleaned++;
                }
                if (out && wo_digest_eq(clean, wo_json_str(effect, "output_sha256", ""))) {
                    as_manifest++;
                }
                wo_free(out);
            }
        }
        const WoJson *executables = wo_json_get(manifest, "executables");
        for (size_t j = 0; exe && bytes && j < wo_json_length(executables); j++) {
            const WoJson *variant = wo_json_at(executables, j);
            if (!wo_digest_eq(wo_json_str(variant, "source_sha256", ""), hash)) {
                continue;
            }
            WoBuf text = {0};
            WoPatchRecipe recipe = {0};
            uint8_t *out = NULL;
            char result[WO_SHA256_HEX_SIZE];
            CHECK_OK(wo_read_file(wo_json_str(variant, "patch", ""), 64 << 20, &text, &error) &&
                         wo_patch_parse((const char *)text.data, text.len, &recipe, &error) &&
                         wo_patch_apply_bytes(&recipe, bytes, entry->length, &out, &error) &&
                         wo_sha256_mem(out, recipe.target_length, result, &error),
                     &error);
            patched += out && wo_digest_eq(result, wo_json_str(manifest, "output_sha256", ""))
                           ? 1
                           : 0;
            wo_free(out);
            wo_patch_free(&recipe);
            wo_buf_free(&text);
        }
        wo_free(bytes);
    }
    printf("  (%zu effects on the disc: %zu match source_hashes, %zu clean exactly as "
           "audio-clean.exe, %zu to the manifest's output_sha256; %zu executable patched to the "
           "manifest output)\n",
           effects, sources, cleaned, as_manifest, patched);
    CHECK(effects == wo_json_length(wo_json_get(manifest, "audio")));
    CHECK(sources == effects && cleaned == effects);
    CHECK(patched == 1);
    wo_iso_close(&iso);
    wo_source_close(&source);
    wo_free(exe_hashes);
    wo_json_free(manifest);
}

static void test_pc_disc(void) {
    char *source = golden_root ? resolve_source("WO_GOLDEN_PC_DISC", "PcMusicSource", "golden-pc")
                               : NULL;
    char *descriptor = source ? find_descriptor(source, 0) : NULL;
    char *bank = golden_root ? under(golden_root, "wobble/music/pc") : NULL;
    WoTrackList tracks = {0};
    WoError error = {0};
    if (!descriptor || !bank || !wo_is_dir(bank)) {
        skip("no PC disc (set WO_GOLDEN_PC_DISC) or no wobble/music/pc");
    } else {
        CHECK_OK(wo_disc_tracks(descriptor, &tracks, &error), &error);
        CHECK(wo_tracks_audio_count(&tracks) == 9);
        check_music(&tracks, bank);
        check_pc_data(&tracks);
    }
    wo_tracks_free(&tracks);
    wo_free(bank);
    wo_free(descriptor);
    wo_free(source);
}

static void compare_ps1_import(const char *imported, const char *installed, size_t *same,
                               size_t *total) {
    WoDirList list = {0};
    if (!wo_list_dir(imported, &list, NULL)) {
        return;
    }
    for (size_t i = 0; i < list.count; i++) {
        char *a = under(imported, list.items[i].name);
        char *b = under(installed, list.items[i].name);
        if (list.items[i].is_dir) {
            compare_ps1_import(a, b, same, total);
        } else if (!wo_str_ieq(list.items[i].name, "source.json")) {
            char hash[WO_SHA256_HEX_SIZE];
            (*total)++;
            if (wo_sha256_file(a, hash, NULL) && wo_is_file(b) && same_file_hash(b, hash)) {
                (*same)++;
            }
        }
        wo_free(b);
        wo_free(a);
    }
    wo_dirlist_free(&list);
}

static void test_ps1_disc(void) {
    char *source = golden_root ? resolve_source("WO_GOLDEN_PS1_DISC", "PsxMusicSource",
                                                "golden-ps1")
                               : NULL;
    char *bank = golden_root ? under(golden_root, "wobble/music/psx") : NULL;
    char *installed = golden_root ? under(golden_root, "wobble/textures/ps1-source") : NULL;
    WoError error = {0};
    if (!source || !bank || !wo_is_dir(bank)) {
        skip("no PlayStation disc (set WO_GOLDEN_PS1_DISC) or no wobble/music/psx");
        wo_free(installed);
        wo_free(bank);
        wo_free(source);
        return;
    }
    char *descriptor = find_descriptor(source, 0);
    WoTrackList tracks = {0};
    CHECK_OK(descriptor && wo_disc_tracks(descriptor, &tracks, &error), &error);
    CHECK(wo_tracks_audio_count(&tracks) == 11);
    check_music(&tracks, bank);
    wo_tracks_free(&tracks);

    WoPs1Disc disc;
    bool found = false;
    CHECK_OK(wo_ps1_find_disc(source, &disc, &found, &error), &error);
    CHECK(found);
    wo_ps1_disc_free(&disc);
    WoJson *plan = NULL;
    CHECK_OK(wo_read_json("manifests/texture-selection.json", 32 << 20, &plan, &error), &error);
    const char *dir = wo_test_dir("golden-ps1-import");
    bool imported = false;
    CHECK_OK(dir && wo_ps1_import_textures(source, dir, plan, NULL, &imported, &error), &error);
    CHECK(imported);
    size_t same = 0;
    size_t total = 0;
    if (dir && installed && wo_is_dir(installed)) {
        compare_ps1_import(dir, installed, &same, &total);
        printf("  (%zu of %zu imported PlayStation files identical to ps1-source)\n", same, total);
        CHECK(same == total && total == 145); /* 137 texture files and 8 track archives */
    }
    wo_json_free(plan);
    wo_free(descriptor);
    wo_free(installed);
    wo_free(bank);
    wo_free(source);
}

int main(void) {
    find_golden_root();
    if (golden_root) {
        printf("  (golden data: %s)\n", golden_root);
    }
    RUN(test_audio_clean_matches_exe);
    RUN(test_hd_regions_match);
    RUN(test_native_sheets_match);
    RUN(test_sd_inventory_matches);
    RUN(test_pc_disc);
    RUN(test_ps1_disc);
    wo_json_free(setup);
    wo_free(golden_root);
    return wo_test_finish();
}
