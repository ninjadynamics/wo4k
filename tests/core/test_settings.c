#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_settings.h"
#include "wo_test.h"

static char *write_temp(const char *dir, const char *name, const char *text) {
    WoError error = {0};
    char *path = wo_path_join(dir, name, &error);
    CHECK_OK(wo_write_file(path, text, strlen(text), &error), &error);
    return path;
}

static char *read_back(const char *path) {
    char *text = NULL;
    WoError error = {0};
    CHECK_OK(wo_read_text(path, 1 << 16, &text, &error), &error);
    return text;
}

static void test_fullscreen(void) {
    const char *dir = wo_test_dir("settings-fullscreen");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *missing = wo_path_join(dir, "missing.ini", &error);
    CHECK(wo_settings_get_fullscreen(missing) == WO_FULLSCREEN_BORDERLESS);
    char *path = write_temp(dir, "ddraw.ini",
                            "LogLevel = none\r\nFullscreenMode = windowed\r\n"
                            "FULLSCREENMODE = Exclusive( 1 ) # note\r\nVSync = on(1)\r\n");
    CHECK(wo_settings_get_fullscreen(path) == WO_FULLSCREEN_EXCLUSIVE);
    CHECK_OK(wo_settings_set_fullscreen(path, WO_FULLSCREEN_BORDERLESS, &error), &error);
    char *text = read_back(path);
    CHECK_STR(text, "LogLevel = none\r\nFullscreenMode = borderless\r\nVSync = on(1)\r\n");
    wo_free(text);
    CHECK_OK(wo_settings_set_log_level(path, "info", &error), &error);
    text = read_back(path);
    CHECK(text && strstr(text, "LogLevel = info\r\n") == text);
    wo_free(text);
    CHECK_OK(wo_settings_set_fullscreen(missing, WO_FULLSCREEN_EXCLUSIVE, &error), &error);
    text = read_back(missing);
    CHECK_STR(text, "FullscreenMode = exclusive\r\n");
    wo_free(text);
    wo_free(path);
    wo_free(missing);
}

/* The frame-rate and lighting settings Setup shows: read with a fallback,
 * written in place, the game's other settings untouched. */
static void test_render_settings(void) {
    const char *dir = wo_test_dir("settings-render");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = write_temp(dir, "game.ini",
                            "[Render]\nShowFPS=1\nShadowLiftPercent=200\nHighlightsPercent=abc\n"
                            "[Audio]\nShowFPS=0\n");
    CHECK(wo_settings_get_render(path, "ShowFPS", 1, 0) == 1);
    CHECK(wo_settings_get_render(path, "ShadowLiftPercent", WO_RENDER_PERCENT_MAX, 0) == 200);
    CHECK(wo_settings_get_render(path, "HighlightsPercent", WO_RENDER_PERCENT_MAX, 100) == 100);
    CHECK(wo_settings_get_render(path, "Missing", 1, 1) == 1);
    CHECK(wo_settings_get_render("no-such-file.ini", "ShowFPS", 1, 0) == 0);
    CHECK_OK(wo_settings_set_render(path, "ShowFPS", 1, 0, &error), &error);
    CHECK_OK(wo_settings_set_render(path, "HighlightsPercent", WO_RENDER_PERCENT_MAX, 250, &error),
             &error);
    CHECK_FAILS(wo_settings_set_render(path, "ShadowLiftPercent", WO_RENDER_PERCENT_MAX, 301,
                                       &error),
                &error, WO_ERR_UNSUPPORTED);
    char *text = NULL;
    CHECK_OK(wo_read_text(path, 1 << 16, &text, &error), &error);
    CHECK_STR(text, "[Render]\r\nShowFPS=0\r\nShadowLiftPercent=200\r\nHighlightsPercent=250\r\n"
                    "[Audio]\r\nShowFPS=0\r\n");
    /* A value beyond the limit in the file is not believed. */
    char *wild = write_temp(dir, "wild.ini", "[Render]\nShadowLiftPercent=99999999999\n");
    CHECK(wo_settings_get_render(wild, "ShadowLiftPercent", WO_RENDER_PERCENT_MAX, 7) == 7);
    wo_free(wild);
    wo_free(text);
    wo_free(path);
}

static void test_sound_mode(void) {
    const char *dir = wo_test_dir("settings-sound");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = write_temp(dir, "game.ini",
                            "[Render]\nSoundMode=5\n[Audio]\nSoundtrack=1\nSoundMode = 1 \n"
                            "SoundMode=9\n\n[Other]\nX=1\n");
    CHECK(wo_settings_get_sound_mode(path) == 1);
    CHECK_OK(wo_settings_set_sound_mode(path, 5, &error), &error);
    char *text = read_back(path);
    CHECK_STR(text, "[Render]\r\nSoundMode=5\r\n[Audio]\r\nSoundtrack=1\r\nSoundMode=5\r\n\r\n"
                    "[Other]\r\nX=1\r\n");
    wo_free(text);
    CHECK_FAILS(wo_settings_set_sound_mode(path, 3, &error), &error, WO_ERR_UNSUPPORTED);

    char *bare = write_temp(dir, "bare.ini", "[Render]\nShowFPS=1\n");
    CHECK(wo_settings_get_sound_mode(bare) == 2);
    CHECK_OK(wo_settings_set_sound_mode(bare, 1, &error), &error);
    text = read_back(bare);
    CHECK_STR(text, "[Render]\r\nShowFPS=1\r\n\r\n[Audio]\r\nSoundMode=1\r\n");
    wo_free(text);
    wo_free(bare);

    /* Comments after a section name or a value, as Windows reads them. */
    char *commented = write_temp(dir, "commented.ini",
                                 "[Audio] ; sound\nSoundMode=5 ; surround\n");
    CHECK(wo_settings_get_sound_mode(commented) == 5);
    CHECK_OK(wo_settings_set_sound_mode(commented, 1, &error), &error);
    text = read_back(commented);
    CHECK_STR(text, "[Audio] ; sound\r\nSoundMode=1\r\n");
    wo_free(text);
    wo_free(commented);
    wo_free(path);
}

static void test_soundtrack(void) {
    const char *dir = wo_test_dir("settings-soundtrack");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = write_temp(dir, "game.ini", "[Audio]\nSoundtrack=0\nMusicVolume=80\n");
    CHECK_OK(wo_settings_apply_soundtrack(path, WO_SOUNDTRACK_NONE, false, &error), &error);
    char *text = read_back(path);
    CHECK_STR(text, "[Audio]\r\nSoundtrack=0\r\nMusicVolume=0\r\n");
    wo_free(text);
    CHECK_OK(wo_settings_apply_soundtrack(path, WO_SOUNDTRACK_PSX, true, &error), &error);
    text = read_back(path);
    CHECK_STR(text, "[Audio]\r\nSoundtrack=1\r\nMusicVolume=100\r\n");
    wo_free(text);
    char *empty = write_temp(dir, "empty.ini", "[Render]\nShowFPS=1\n");
    CHECK_OK(wo_settings_apply_soundtrack(empty, WO_SOUNDTRACK_PSX, false, &error), &error);
    text = read_back(empty);
    CHECK_STR(text, "[Render]\r\nShowFPS=1\r\n\r\n[Audio]\r\nSoundtrack=1\r\n");
    wo_free(text);
    wo_free(empty);
    wo_free(path);
}

static void test_merge_defaults(void) {
    WoIni current;
    WoIni defaults;
    WoError error = {0};
    CHECK_OK(wo_ini_parse("[Render]\nShowFPS=0\n\n[Audio]\nTrack=10\n", &current, &error), &error);
    CHECK_OK(wo_ini_parse("[Render]\nShowFPS=1\nEngineTrails=2\n[Audio]\nTrack=1\nSoundMode=2\n"
                          "[New]\nKey=Value\n",
                          &defaults, &error),
             &error);
    int added = 0;
    CHECK_OK(wo_ini_merge_defaults(&current, &defaults, true, &added, &error), &error);
    CHECK(added == 3);
    char *trails = wo_ini_get(&current, "Render", "EngineTrails", &error);
    CHECK_STR(trails, "2");
    wo_free(trails);
    char *fps = wo_ini_get(&current, "render", "showfps", &error);
    CHECK_STR(fps, "0");
    wo_free(fps);
    char *track = wo_ini_get(&current, "Audio", "Track", &error);
    CHECK_STR(track, "10");
    wo_free(track);
    char *key = wo_ini_get(&current, "New", "Key", &error);
    CHECK_STR(key, "Value");
    wo_free(key);
    wo_ini_free(&current);
    wo_ini_free(&defaults);

    WoIni ddraw;
    WoIni ddraw_defaults;
    CHECK_OK(wo_ini_parse("FullscreenMode = exclusive\n", &ddraw, &error), &error);
    CHECK_OK(wo_ini_parse("FullscreenMode = borderless\nVSync = on(1)\n; comment = no\n",
                          &ddraw_defaults, &error),
             &error);
    CHECK_OK(wo_ini_merge_defaults(&ddraw, &ddraw_defaults, false, &added, &error), &error);
    CHECK(added == 1);
    CHECK(ddraw.lines.count == 2);
    CHECK_STR(ddraw.lines.items[1], "VSync = on(1)");
    wo_ini_free(&ddraw);
    wo_ini_free(&ddraw_defaults);
}

/* The game treats any lost surface as fatal (Flip at its line 1573 returns
 * DDERR_SURFACELOST after Alt+Tab), so the shipped DDrawCompat settings keep
 * video memory and the primary surface, and an upgrade adds that to an
 * installed file without touching the player's own choices. */
static void test_alt_tab_template(void) {
    WoIni shipped;
    WoIni installed;
    WoError error = {0};
    CHECK_OK(wo_ini_load("runtime/DDrawCompat-Wipeout2097.ini", &shipped, &error), &error);
    char *fix = wo_ini_get(&shipped, NULL, "AltTabFix", &error);
    CHECK_STR(fix, "keepvidmem(1)");
    wo_free(fix);
    CHECK_OK(wo_ini_parse("FullscreenMode = exclusive\r\nLogLevel = info\r\n", &installed, &error),
             &error);
    int added = 0;
    CHECK_OK(wo_ini_merge_defaults(&installed, &shipped, false, &added, &error), &error);
    fix = wo_ini_get(&installed, NULL, "AltTabFix", &error);
    CHECK_STR(fix, "keepvidmem(1)");
    wo_free(fix);
    char *mode = wo_ini_get(&installed, NULL, "FullscreenMode", &error);
    CHECK_STR(mode, "exclusive");
    wo_free(mode);
    wo_ini_free(&installed);
    wo_ini_free(&shipped);
}

static void test_snapshot(void) {
    const char *dir = wo_test_dir("settings-snapshot");
    if (!dir) {
        return;
    }
    char *game = write_temp(dir, "Wipeout2097.ini",
                            "[Render]\nShowFPS=1\nSecret=C:\\Users\\x\n[Audio]\nTrack=3 ; c\n");
    WoJson *snapshot = wo_settings_snapshot(dir);
    CHECK(wo_json_length(snapshot) == 1);
    WoJson *settings = wo_json_get(wo_json_at(snapshot, 0), "settings");
    CHECK_STR(wo_json_str(settings, "Render/ShowFPS", NULL), "1");
    CHECK_STR(wo_json_str(settings, "Audio/Track", NULL), "3");
    CHECK(!wo_json_has(settings, "Render/Secret"));
    wo_json_free(snapshot);
    char *ddraw = write_temp(dir, "DDrawCompat-Wipeout2097.ini", "AltTabFix = keepvidmem(1)\n");
    snapshot = wo_settings_snapshot(dir);
    CHECK(wo_json_length(snapshot) == 2);
    settings = wo_json_get(wo_json_at(snapshot, 1), "settings");
    CHECK_STR(wo_json_str(settings, "/AltTabFix", NULL), "keepvidmem(1)");
    wo_json_free(snapshot);
    wo_free(ddraw);
    wo_free(game);
}

int main(void) {
    RUN(test_fullscreen);
    RUN(test_sound_mode);
    RUN(test_render_settings);
    RUN(test_soundtrack);
    RUN(test_merge_defaults);
    RUN(test_alt_tab_template);
    RUN(test_snapshot);
    return wo_test_finish();
}
