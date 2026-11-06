#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/wo_disctracks.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_music.h"
#include "wo_test.h"

#define TRACK_SECTORS 4u

static void put(const char *dir, const char *name, const void *data, size_t len) {
    WoError error = {0};
    char *path = wo_path_join(dir, name, &error);
    CHECK_OK(path && wo_write_file(path, data, len, &error), &error);
    wo_free(path);
}

static void put_text(const char *dir, const char *name, const char *text) {
    put(dir, name, text, strlen(text));
}

static bool has(const char *dir, const char *name) {
    char *path = wo_path_join(dir, name, NULL);
    bool exists = path && wo_is_file(path);
    wo_free(path);
    return exists;
}

static char *read_all(const char *dir, const char *name) {
    char *path = wo_path_join(dir, name, NULL);
    char *text = NULL;
    WoError error = {0};
    CHECK_OK(path && wo_read_text(path, 1 << 20, &text, &error), &error);
    wo_free(path);
    return text;
}

/* One BIN with a 3-sector data track and `songs` audio tracks, plus its CUE. */
static void write_disc(const char *dir, const char *stem, unsigned songs) {
    size_t sectors = 3 + (size_t)songs * TRACK_SECTORS;
    uint8_t *bin = wo_alloc(sectors * 2352, NULL);
    for (unsigned s = 0; s < songs; s++) {
        uint8_t *track = bin + (3 + (size_t)s * TRACK_SECTORS) * 2352;
        for (size_t i = 0; i + 1 < TRACK_SECTORS * 2352; i += 2) {
            int16_t v = (int16_t)(7000.0 * sin((double)i / (60.0 + s)));
            wo_put_le16(track + i, (uint16_t)v);
        }
    }
    char name[64];
    snprintf(name, sizeof(name), "%s.bin", stem);
    put(dir, name, bin, sectors * 2352);
    wo_free(bin);
    WoBuf cue = {0};
    wo_buf_printf(&cue, NULL, "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n"
                  "    INDEX 01 00:00:00\r\n", name);
    for (unsigned s = 0; s < songs; s++) {
        wo_buf_printf(&cue, NULL, "  TRACK %02u AUDIO\r\n    INDEX 01 00:00:%02u\r\n", s + 2,
                      3 + s * TRACK_SECTORS);
    }
    snprintf(name, sizeof(name), "%s.cue", stem);
    put(dir, name, cue.data, cue.len);
    wo_buf_free(&cue);
}

static void test_toc_matching(void) {
    const char *yes[] = {"Tracks=10", "[CD]\r\nTracks=10\r\n", "tracks = 10  \n", "x\nTRACKS\t=\t10",
                         "Tracks=10 \r\n[Track01]", "Tracks\r\n=\r\n10\r\n"};
    const char *no[] = {"Tracks=100", " Tracks=10", "Tracks=1", "Track=10", "Tracks=10 x",
                        "Tracks=10\r[Track01]", "", "#Tracks=10"};
    for (size_t i = 0; i < WO_COUNT(yes); i++) {
        if (!wo_music_toc_matches(yes[i], strlen(yes[i]), 10)) {
            wo_test_fail(__FILE__, __LINE__, yes[i], "should match");
        }
    }
    for (size_t i = 0; i < WO_COUNT(no); i++) {
        if (wo_music_toc_matches(no[i], strlen(no[i]), 10)) {
            wo_test_fail(__FILE__, __LINE__, no[i], "should not match");
        }
    }
    CHECK(wo_music_toc_matches("Tracks=12", 9, wo_music_bank_tracks(WO_MUSIC_PSX)));
}

static void test_find_and_copy_folder(void) {
    const char *root = wo_test_dir("music-folder");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *source = wo_path_join(root, "source", &error);
    char *psx = wo_path_join(source, "music/psx", &error);
    put_text(psx, "tracks.ini", "[CD]\r\nTracks=12\r\n");
    put_text(psx, "track02.wav", "RIFF");
    put_text(psx, "Track03.MP3", "ID3");
    put_text(psx, "track3.wav", "skip");
    put_text(psx, "notes.txt", "skip");
    put_text(source, "tracks.ini", "Tracks=10\r\n");
    char *folder = NULL;
    CHECK_OK(wo_music_find_folder(source, WO_MUSIC_PSX, &folder, &error), &error);
    CHECK(folder && wo_str_iends_with(folder, "music\\psx"));
    wo_free(folder);
    CHECK_OK(wo_music_find_folder(source, WO_MUSIC_PC, &folder, &error), &error);
    CHECK(folder && wo_str_iends_with(folder, "\\source"));
    wo_free(folder);
    CHECK_OK(wo_music_find_folder(psx, WO_MUSIC_PC, &folder, &error), &error);
    CHECK(folder == NULL);
    char *file = wo_path_join(source, "tracks.ini", &error);
    CHECK_OK(wo_music_find_folder(file, WO_MUSIC_PC, &folder, &error), &error);
    CHECK(folder == NULL); /* not a folder */
    CHECK_OK(wo_music_find_folder(NULL, WO_MUSIC_PC, &folder, &error), &error);
    CHECK(folder == NULL);
    wo_free(file);

    char *out = wo_path_join(root, "out", &error);
    bool extracted = false;
    CHECK_OK(wo_music_extract(source, out, WO_MUSIC_PSX, false, &extracted, &error), &error);
    CHECK(extracted && has(out, "tracks.ini") && has(out, "track02.wav") && has(out, "Track03.MP3"));
    CHECK(!has(out, "track3.wav") && !has(out, "notes.txt"));
    wo_free(out);
    wo_free(psx);
    wo_free(source);
}

static void test_extract_from_disc(void) {
    const char *root = wo_test_dir("music-disc");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *discs = wo_path_join(root, "discs", &error);
    char *sub = wo_path_join(discs, "z", &error);
    write_disc(discs, "short", 3); /* a disc with the wrong number of songs comes first */
    write_disc(sub, "pc", 9);
    char *out = wo_path_join(root, "pc", &error);
    bool extracted = false;
    CHECK_OK(wo_music_extract(discs, out, WO_MUSIC_PC, false, &extracted, &error), &error);
    CHECK(extracted);
    for (unsigned t = 2; t <= 10; t++) {
        char name[32];
        snprintf(name, sizeof(name), "track%02u.wav", t);
        char *path = wo_path_join(out, name, &error);
        WoFileInfo info;
        CHECK_OK(wo_stat(path, &info, &error), &error);
        CHECK(info.exists && info.size == 44 + TRACK_SECTORS * 2352);
        wo_free(path);
    }
    char *toc = read_all(out, "tracks.ini");
    CHECK(toc && wo_str_starts_with(
                     toc, "[CD]\r\nTracks=10\r\n\r\n[Track01]\r\nStartFrame=150\r\nFrames=3\r\n"));
    CHECK(toc && strstr(toc, "[Track10]\r\nStartFrame=185\r\nFrames=4\r\n\r\n"));
    wo_free(toc);

    /* The PlayStation bank needs 11 songs; there is none here. */
    char *psx = wo_path_join(root, "psx", &error);
    CHECK_FAILS(wo_music_extract(discs, psx, WO_MUSIC_PSX, false, &extracted, &error), &error,
                WO_ERR_NOT_FOUND);
    CHECK_OK(wo_music_extract(discs, psx, WO_MUSIC_PSX, true, &extracted, &error), &error);
    CHECK(!extracted && !wo_exists(psx));
    /* A lone .bin is skipped with allow_missing and an error otherwise. */
    char *lone = wo_path_join(root, "lone.bin", &error);
    put_text(root, "lone.bin", "data");
    CHECK_OK(wo_music_extract(lone, psx, WO_MUSIC_PC, true, &extracted, &error), &error);
    CHECK(!extracted);
    CHECK_FAILS(wo_music_extract(lone, psx, WO_MUSIC_PC, false, &extracted, &error), &error,
                WO_ERR_NOT_FOUND);
    char *nothing = wo_path_join(root, "nothing", &error);
    CHECK_FAILS(wo_music_extract(nothing, psx, WO_MUSIC_PC, true, &extracted, &error), &error,
                WO_ERR_NOT_FOUND);
    /* A broken descriptor is an error even with allow_missing. */
    put_text(sub, "broken.cue", "TRACK 01 AUDIO\n");
    CHECK_FAILS(wo_music_extract(discs, psx, WO_MUSIC_PSX, true, &extracted, &error), &error,
                WO_ERR_FORMAT);

    /* Installing the extracted bank. */
    char *game = wo_path_join(root, "game", &error);
    char *bank = wo_path_join(game, "music/pc", &error);
    put_text(bank, "track03.mp3", "ID3 stale");
    unsigned songs = 0;
    CHECK_OK(wo_music_install(out, game, WO_MUSIC_PC, &songs, &error), &error);
    CHECK(songs == 9 && has(bank, "track03.wav") && !has(bank, "track03.mp3"));
    char *installed = read_all(bank, "tracks.ini");
    char *source_toc = read_all(out, "tracks.ini");
    CHECK_STR(installed, source_toc);
    wo_free(installed);
    WoStrList files = {0};
    CHECK_OK(wo_music_files(game, WO_MUSIC_PC, &files, &error), &error);
    CHECK(files.count == 10 && wo_str_iends_with(files.items[0], "tracks.ini"));
    wo_strlist_free(&files);

    /* A new MP3 replaces the old WAV, which is removed afterwards. */
    char *wav = wo_path_join(out, "track04.wav", &error);
    CHECK_OK(wo_delete_file(wav, &error), &error);
    put_text(out, "track04.mp3", "\xFF\xFB\x90\x44 frame");
    CHECK_OK(wo_music_install(out, game, WO_MUSIC_PC, &songs, &error), &error);
    CHECK(has(bank, "track04.mp3") && !has(bank, "track04.wav"));
    CHECK_OK(wo_music_files(game, WO_MUSIC_PC, &files, &error), &error);
    CHECK(files.count == 10 && wo_str_iends_with(files.items[3], "track04.mp3"));
    wo_strlist_free(&files);

    /* Invalid installed banks are reported as empty. */
    put_text(bank, "track05.wav", "RIFFxxxxWAVX");
    CHECK_OK(wo_music_files(game, WO_MUSIC_PC, &files, &error), &error);
    CHECK(files.count == 0);
    put_text(bank, "track05.wav", "RIF");
    CHECK_OK(wo_music_files(game, WO_MUSIC_PC, &files, &error), &error);
    CHECK(files.count == 0);
    put_text(bank, "track04.mp3", "MP3 without a header");
    CHECK_OK(wo_music_files(game, WO_MUSIC_PC, &files, &error), &error);
    CHECK(files.count == 0);
    CHECK_OK(wo_music_files(game, WO_MUSIC_PSX, &files, &error), &error);
    CHECK(files.count == 0);
    char *bad_toc = wo_path_join(bank, "tracks.ini", &error);
    CHECK_OK(wo_write_file(bad_toc, "Tracks=11", 9, &error), &error);
    CHECK_OK(wo_music_files(game, WO_MUSIC_PC, &files, &error), &error);
    CHECK(files.count == 0);
    wo_free(bad_toc);

    /* Missing songs and sources. */
    char *song = wo_path_join(out, "track05.wav", &error);
    CHECK_OK(wo_delete_file(song, &error), &error);
    CHECK_FAILS(wo_music_install(out, game, WO_MUSIC_PC, &songs, &error), &error,
                WO_ERR_NOT_FOUND);
    CHECK(strstr(error.message, "track05"));
    CHECK_FAILS(wo_music_install(nothing, game, WO_MUSIC_PC, &songs, &error), &error,
                WO_ERR_NOT_FOUND);
    wo_free(song);
    wo_free(wav);
    wo_free(source_toc);
    wo_free(bank);
    wo_free(game);
    wo_free(nothing);
    wo_free(lone);
    wo_free(psx);
    wo_free(out);
    wo_free(sub);
    wo_free(discs);
}

static void test_toc_encodings(void) {
    /* UTF-16 and UTF-8 tables are read as the game's ASCII text. */
    const char *root = wo_test_dir("music-encodings");
    if (!root) {
        return;
    }
    WoError error = {0};
    static const uint8_t utf16[] = {0xFF, 0xFE, 'T', 0, 'r', 0, 'a', 0, 'c', 0, 'k', 0, 's', 0,
                                    '=',  0,    '1', 0, '2', 0, 0xE9, 0};
    char *dir = wo_path_join(root, "a", &error);
    put(dir, "tracks.ini", utf16, sizeof(utf16));
    char *folder = NULL;
    CHECK_OK(wo_music_find_folder(dir, WO_MUSIC_PSX, &folder, &error), &error);
    CHECK(folder == NULL); /* "Tracks=12?" */
    wo_free(dir);
    static const uint8_t utf16_ok[] = {0xFF, 0xFE, 'T', 0, 'r', 0, 'a', 0, 'c', 0, 'k', 0, 's', 0,
                                       '=',  0,    '1', 0, '2', 0, '\n', 0, 0xE9, 0};
    dir = wo_path_join(root, "b", &error);
    put(dir, "tracks.ini", utf16_ok, sizeof(utf16_ok));
    CHECK_OK(wo_music_find_folder(dir, WO_MUSIC_PSX, &folder, &error), &error);
    CHECK(folder != NULL);
    wo_free(folder);
    wo_free(dir);
}

int main(void) {
    RUN(test_toc_matching);
    RUN(test_find_and_copy_folder);
    RUN(test_extract_from_disc);
    RUN(test_toc_encodings);
    return wo_test_finish();
}
