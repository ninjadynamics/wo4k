/* Source handling. The probe checks against real game sources run only
 * when this checkout's .state/setup.json names sources that exist. */
#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"
#include "core/wo_mod.h"
#include "core/wo_ops.h"
#include "core/wo_sources.h"
#include "wo_test.h"

static void test_game_file_filter(void) {
    const char *yes[] = {"Common/MENU.DAT", "track01/TRACK.WAD", "TRACK13/x/y.tim", "Sound/ENGINES.WAV",
                         "Pics/Hi-res/HUD2.TGA", "Wipeout2.exe", "d3d.exe", "CONFIG.DAT",
                         "MPEG/WXLINTR.MPG", "movies/xtro4.mp4", "movies/intro.mp4", "Wipeout2097.ini"};
    const char *no[] = {"Track1/x", "Track001/x", "Data/MENU.DAT", "MPEG/intro.mp4", "MPEG/OTHER.MPG",
                        "movies/sub/XTRO1.MPG", "setup.exe", "Common", "x/Wipeout2.exe"};
    for (size_t i = 0; i < WO_COUNT(yes); i++) {
        if (!wo_game_file(yes[i])) {
            wo_test_fail(__FILE__, __LINE__, yes[i], "should be a game file");
        }
    }
    for (size_t i = 0; i < WO_COUNT(no); i++) {
        if (wo_game_file(no[i])) {
            wo_test_fail(__FILE__, __LINE__, no[i], "should not be a game file");
        }
    }
}

static void test_stamps(void) {
    const char *dir = wo_test_dir("sources-stamp");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *missing = wo_path_join(dir, "missing.zip", &error);
    char *stamp = wo_source_stamp(missing, &error);
    CHECK(stamp && wo_stamp_unavailable(stamp));
    wo_free(stamp);
    char *music = wo_path_join(dir, "music/tracks.ini", &error);
    char *ignored = wo_path_join(dir, "music/readme.txt", &error);
    CHECK_OK(wo_write_file(music, "[CD]", 4, &error), &error);
    CHECK_OK(wo_write_file(ignored, "x", 1, &error), &error);
    stamp = wo_source_stamp(dir, &error);
    CHECK(stamp && strstr(stamp, "|folder|") != NULL);
    CHECK(stamp && strstr(stamp, "tracks.ini|4|") != NULL);
    CHECK(stamp && strstr(stamp, "readme.txt") == NULL);
    wo_free(stamp);
    stamp = wo_source_stamp(music, &error);
    CHECK(stamp && wo_str_iends_with(stamp, "") && strstr(stamp, "tracks.ini|4|") != NULL);
    wo_free(stamp);
    wo_free(ignored);
    wo_free(music);
    wo_free(missing);
}

static void test_resolve_input(void) {
    const char *dir = wo_test_dir("sources-resolve");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *missing = wo_path_join(dir, "nope", &error);
    CHECK(wo_resolve_input(missing, dir, &error) == NULL && error.kind == WO_ERR_NOT_FOUND);
    char *resolved = wo_resolve_input(dir, dir, &error);
    CHECK(resolved && wo_str_ieq(resolved, dir));
    wo_free(resolved);
    wo_free(missing);
}

/* Only the retail disc's texture sheets are installed from. */
static void test_retail_sheets(void) {
    const char *dir = wo_test_dir("sources-retail");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char hash[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_mem("retail", 6, hash, &error), &error);
    char *text = wo_format(&error, "{\"sheets\":[{\"file\":\"Pics/HI-RES/NRACE06.TGA\","
                                   "\"sha256\":\"%s\"}]}", hash);
    WoJson *plan = NULL;
    CHECK_OK(wo_json_parse(text, strlen(text), &plan, &error), &error);
    CHECK(!wo_sheet_not_retail(plan, "PICS/hi-res/nrace06.tga", hash));
    CHECK(wo_sheet_not_retail(plan, "game/Pics/Hi-res/NRACE06.TGA", "00"));
    CHECK(!wo_sheet_not_retail(plan, "xPics/Hi-res/NRACE06.TGA", "00")); /* another file */
    CHECK(!wo_sheet_not_retail(plan, "Pics/Hi-res/NRACE01.TGA", "00"));  /* not pinned */
    char *sheet = wo_path_join(dir, "Pics/Hi-res/NRACE06.TGA", &error);
    CHECK_OK(wo_write_file(sheet, "retail", 6, &error), &error);
    CHECK_OK(wo_require_retail_sheets(plan, dir, &error), &error);
    CHECK_OK(wo_write_file(sheet, "earlier", 7, &error), &error);
    CHECK_FAILS(wo_require_retail_sheets(plan, dir, &error), &error, WO_ERR_UNSUPPORTED);
    /* A previous WO4K installation holds built sheets and is not judged. */
    char *installed = wo_path_join(dir, "textures/sd/marker.txt", &error);
    CHECK_OK(wo_write_file(installed, "x", 1, &error), &error);
    CHECK_OK(wo_require_retail_sheets(plan, dir, &error), &error);
    wo_free(installed);
    wo_free(sheet);
    wo_json_free(plan);
    wo_free(text);
}

static void test_real_sources(void) {
    WoMod mod;
    WoError error = {0};
    WoConfig config;
    if (!wo_mod_init(&mod, ".", &error) || !wo_config_load(&mod, &config, &error) ||
        !config.game_source[0] || !wo_exists(config.game_source)) {
        printf("  (skipped: no configured local game source)\n");
        wo_mod_free(&mod);
        return;
    }
    WoSourceInfo info;
    wo_source_probe(&mod, config.game_source, &info);
    CHECK(info.game);
    wo_source_info_free(&info);
    if (config.pc_music_source[0] && wo_exists(config.pc_music_source)) {
        wo_source_probe(&mod, config.pc_music_source, &info);
        CHECK(info.pc_music);
        wo_source_info_free(&info);
    }
    if (config.psx_source[0] && wo_exists(config.psx_source)) {
        wo_source_probe(&mod, config.psx_source, &info);
        CHECK(info.psx && info.psx_music);
        /* A ZIP of the PlayStation game is its disc image, data and all:
         * Setup installs from nothing less. */
        if (wo_str_iends_with(config.psx_source, ".zip")) {
            CHECK(info.psx_data);
        }
        wo_source_info_free(&info);
    }
    WoJson *manifest = NULL;
    char *manifest_path = wo_mod_path(&mod, "manifests/patches.json", &error);
    const char *work = wo_test_dir("sources-native");
    if (manifest_path && work && wo_read_json(manifest_path, 1 << 22, &manifest, &error)) {
        char *root = wo_native_root(config.game_source, work, manifest, &error);
        CHECK(root != NULL);
        wo_free(root);
    }
    wo_json_free(manifest);
    wo_free(manifest_path);
    wo_config_free(&config);
    wo_mod_free(&mod);
}

int main(void) {
    RUN(test_game_file_filter);
    RUN(test_stamps);
    RUN(test_resolve_input);
    RUN(test_retail_sheets);
    RUN(test_real_sources);
    return wo_test_finish();
}
