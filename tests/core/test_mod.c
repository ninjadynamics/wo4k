#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_mod.h"
#include "wo_test.h"

static void write_text(const char *root, const char *relative, const char *text) {
    WoError error = {0};
    char *path = wo_path_child(root, relative, &error);
    CHECK_OK(wo_write_file(path, text, strlen(text), &error), &error);
    wo_free(path);
}

static void test_defaults_without_state(void) {
    const char *dir = wo_test_dir("mod-defaults");
    if (!dir) {
        return;
    }
    WoMod mod;
    WoError error = {0};
    CHECK_OK(wo_mod_init(&mod, dir, &error), &error);
    WoConfig config;
    CHECK_OK(wo_config_load(&mod, &config, &error), &error);
    CHECK_STR(config.game_source, "");
    CHECK(config.soundtrack == WO_SOUNDTRACK_PC);
    CHECK(config.hd_textures && config.clean_audio);
    CHECK(config.sound_mode == 2 && config.fullscreen == WO_FULLSCREEN_BORDERLESS);
    wo_config_free(&config);

    /* An installed game without saved settings: sources default to wobble,
     * preferences come from the receipt and the INI files. */
    write_text(dir, "wobble/installation.json",
               "{\"default_soundtrack\":\"None\",\"hd_textures\":false}");
    write_text(dir, "wobble/Wipeout2097.ini", "[Audio]\r\nSoundMode=5\r\n");
    write_text(dir, "wobble/DDrawCompat-Wipeout2097.ini", "FullscreenMode = exclusive\r\n");
    CHECK_OK(wo_config_load(&mod, &config, &error), &error);
    CHECK(wo_str_iends_with(config.game_source, "\\mod-defaults\\wobble"));
    CHECK(config.soundtrack == WO_SOUNDTRACK_NONE && !config.hd_textures);
    CHECK(config.sound_mode == 5 && config.fullscreen == WO_FULLSCREEN_EXCLUSIVE);
    wo_config_free(&config);
    wo_mod_free(&mod);
}

static void test_save_round_trip_and_revision(void) {
    const char *dir = wo_test_dir("mod-save");
    if (!dir) {
        return;
    }
    WoMod mod;
    WoError error = {0};
    CHECK_OK(wo_mod_init(&mod, dir, &error), &error);
    char *inside = wo_path_join(dir, "sources\\pc", &error);
    WoConfig config = {0};
    config.game_source = inside;
    config.pc_music_source = "";
    config.psx_source = "D:\\Games\\WipEout 2097 (Europe).cue";
    config.soundtrack = WO_SOUNDTRACK_PSX;
    config.hd_textures = true;
    config.clean_audio = false;
    config.sound_mode = 1;
    config.fullscreen = WO_FULLSCREEN_EXCLUSIVE;
    WoConfig saved;
    CHECK_OK(wo_config_save(&mod, &config, &saved, &error), &error);
    CHECK(saved.revision == 1);
    CHECK_STR(saved.game_source, inside);
    CHECK_STR(saved.psx_source, "D:\\Games\\WipEout 2097 (Europe).cue");
    CHECK(saved.soundtrack == WO_SOUNDTRACK_PSX && !saved.clean_audio && saved.sound_mode == 1);

    WoJson *stored = NULL;
    CHECK_OK(wo_state_load(&mod, "setup.json", &stored, &error), &error);
    CHECK_STR(wo_json_str(stored, "GameSource", NULL), "sources/pc");
    wo_json_free(stored);

    WoConfig again;
    CHECK_OK(wo_config_save(&mod, &saved, &again, &error), &error);
    CHECK(again.revision == 1); /* unchanged: not rewritten */
    again.sound_mode = 2;
    WoConfig third;
    CHECK_OK(wo_config_save(&mod, &again, &third, &error), &error);
    CHECK(third.revision == 2 && third.sound_mode == 2);

    again.sound_mode = 3;
    CHECK_FAILS(wo_config_save(&mod, &again, NULL, &error), &error, WO_ERR_FORMAT);

    wo_config_free(&third);
    wo_config_free(&again);
    wo_config_free(&saved);
    wo_free(inside);
    wo_mod_free(&mod);
}

static void test_rejects_bad_state(void) {
    const char *dir = wo_test_dir("mod-bad");
    if (!dir) {
        return;
    }
    WoMod mod;
    WoError error = {0};
    CHECK_OK(wo_mod_init(&mod, dir, &error), &error);
    WoConfig config;
    write_text(dir, ".state/setup.json", "{\"format\":2}");
    CHECK_FAILS(wo_config_load(&mod, &config, &error), &error, WO_ERR_UNSUPPORTED);
    write_text(dir, ".state/setup.json", "{\"format\":1,\"DefaultSoundtrack\":\"Radio\"}");
    CHECK_FAILS(wo_config_load(&mod, &config, &error), &error, WO_ERR_FORMAT);
    write_text(dir, ".state/setup.json", "{\"format\":1,\"GameSource\":\"../outside\"}");
    CHECK_FAILS(wo_config_load(&mod, &config, &error), &error, WO_ERR_UNSAFE);
    wo_mod_free(&mod);
}

static void test_work_dirs(void) {
    const char *dir = wo_test_dir("mod-work");
    if (!dir) {
        return;
    }
    WoMod mod;
    WoError error = {0};
    CHECK_OK(wo_mod_init(&mod, dir, &error), &error);
    char *work = wo_mod_new_work_dir(&mod, "i-", &error);
    CHECK(work && wo_is_dir(work) && strstr(work, "\\.work\\i-") != NULL);
    write_text(work, "x/y.txt", "y");
    wo_mod_remove_work_dir(&mod, work);
    CHECK(!wo_exists(work));
    wo_mod_remove_work_dir(&mod, dir); /* refused: not a work folder */
    CHECK(wo_is_dir(dir));
    CHECK(!wo_mod_game_running(&mod));
    wo_free(work);
    wo_mod_free(&mod);
}

int main(void) {
    RUN(test_defaults_without_state);
    RUN(test_save_round_trip_and_revision);
    RUN(test_rejects_bad_state);
    RUN(test_work_dirs);
    return wo_test_finish();
}
