#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "iso_builder.h"
#include "wo_test.h"

#define AUDIO_SECTORS 8u

static void write_bytes(const char *dir, const char *name, const void *data, size_t len) {
    WoError error = {0};
    char *path = wo_path_join(dir, name, &error);
    CHECK_OK(path && wo_write_file(path, data, len, &error), &error);
    wo_free(path);
}

/* Raw MODE2/2352 data track (the sample ISO) followed by one audio track. */
static size_t write_bin(const char *dir, const char *name) {
    IsoImage iso;
    iso_build_sample(&iso, "BOOT = cdrom:\\SLES_003.27;1\r\n");
    size_t data_len = 0;
    uint8_t *data = iso_raw(&iso, 2352, 24, 0, &data_len);
    size_t total = data_len + AUDIO_SECTORS * 2352;
    uint8_t *bin = wo_alloc(total, NULL);
    memcpy(bin, data, data_len);
    for (size_t i = 0; i + 1 < AUDIO_SECTORS * 2352; i += 2) {
        int16_t v = (int16_t)(8000.0 * sin((double)i / 90.0));
        wo_put_le16(bin + data_len + i, (uint16_t)v);
    }
    write_bytes(dir, name, bin, total);
    wo_free(bin);
    wo_free(data);
    wo_free(iso.data);
    return data_len / 2352;
}

static bool common_only(void *context, const char *relative) {
    size_t *calls = context;
    (*calls)++;
    return wo_str_istarts_with(relative, "WIPEOUT2/COMMON/");
}

static void test_cue_bin(void) {
    const char *dir = wo_test_dir("disc-cue");
    if (!dir) {
        return;
    }
    size_t data_sectors = write_bin(dir, "game.bin");
    char cue[256];
    snprintf(cue, sizeof(cue),
             "FILE \"game.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
             "  TRACK 02 AUDIO\r\n    INDEX 01 00:00:%02u\r\n",
             (unsigned)data_sectors);
    write_bytes(dir, "game.cue", cue, strlen(cue));
    WoError error = {0};
    char *cue_path = wo_path_join(dir, "game.cue", &error);
    char *bin_path = wo_path_join(dir, "game.bin", &error);
    WoTrackList tracks;
    CHECK_OK(wo_disc_tracks(bin_path, &tracks, &error), &error); /* .bin reads its .cue */
    CHECK(tracks.count == 2 && wo_tracks_audio_count(&tracks) == 1);
    if (tracks.count == 2) {
        CHECK(tracks.items[0].frames == data_sectors && tracks.items[1].frames == AUDIO_SECTORS);
        CHECK(wo_str_ieq(tracks.items[1].image, bin_path));
        char *wav = wo_path_join(dir, "out/track02.wav", &error);
        const WoTrack *t = &tracks.items[1];
        CHECK_OK(wo_cdaudio_extract(t->image, t->offset, t->frames, t->stride, wav, &error),
                 &error);
        WoFileInfo info;
        CHECK_OK(wo_stat(wav, &info, &error), &error);
        CHECK(info.exists && info.size == 44 + AUDIO_SECTORS * 2352);
        /* A track past the end of the image leaves no partial file. */
        char *bad = wo_path_join(dir, "out/bad.wav", &error);
        CHECK_FAILS(wo_cdaudio_extract(t->image, t->offset, t->frames + 1, t->stride, bad, &error),
                    &error, WO_ERR_FORMAT);
        CHECK(!wo_exists(bad));
        wo_free(bad);
        wo_free(wav);
    }
    wo_tracks_free(&tracks);

    char *image = NULL;
    uint64_t offset = 1;
    CHECK_OK(wo_disc_data_track(cue_path, &image, &offset, &error), &error);
    CHECK(image && wo_str_ieq(image, bin_path) && offset == 0);
    wo_free(image);
    char *out = wo_path_join(dir, "imported", &error);
    size_t calls = 0;
    size_t imported = 0;
    CHECK_OK(wo_disc_import_image(cue_path, out, common_only, &calls, &imported, &error), &error);
    CHECK(imported == 2 && calls == 5); /* every file is offered to the filter */
    char *mine = wo_path_join(out, "WIPEOUT2/COMMON/MINE.CMP", &error);
    char *text = NULL;
    CHECK_OK(wo_read_text(mine, 100, &text, &error), &error);
    CHECK_STR(text, "MINE");
    wo_free(text);
    wo_free(mine);
    wo_free(out);

    /* Audio-only descriptors have no data track. */
    const char *audio = "FILE \"game.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    write_bytes(dir, "audio.cue", audio, strlen(audio));
    char *audio_cue = wo_path_join(dir, "audio.cue", &error);
    CHECK_FAILS(wo_disc_data_track(audio_cue, &image, &offset, &error), &error, WO_ERR_NOT_FOUND);
    wo_free(audio_cue);
    /* FILE names stay inside the sheet's folder and must exist. */
    const char *escape = "FILE \"..\\game.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    write_bytes(dir, "escape.cue", escape, strlen(escape));
    char *escape_cue = wo_path_join(dir, "escape.cue", &error);
    CHECK_FAILS(wo_disc_tracks(escape_cue, &tracks, &error), &error, WO_ERR_UNSAFE);
    wo_free(escape_cue);
    const char *absolute = "FILE \"C:\\game.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    write_bytes(dir, "absolute.cue", absolute, strlen(absolute));
    char *absolute_cue = wo_path_join(dir, "absolute.cue", &error);
    CHECK_FAILS(wo_disc_tracks(absolute_cue, &tracks, &error), &error, WO_ERR_UNSAFE);
    wo_free(absolute_cue);
    const char *missing = "FILE \"gone.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    write_bytes(dir, "missing.cue", missing, strlen(missing));
    char *missing_cue = wo_path_join(dir, "missing.cue", &error);
    CHECK_FAILS(wo_disc_tracks(missing_cue, &tracks, &error), &error, WO_ERR_NOT_FOUND);
    CHECK(strstr(error.message, "missing.cue") && strstr(error.message, "gone.bin"));
    wo_free(missing_cue);
    char *lone = wo_path_join(dir, "lone.bin", &error);
    CHECK_FAILS(wo_disc_tracks(lone, &tracks, &error), &error, WO_ERR_NOT_FOUND);
    wo_free(lone);
    wo_free(bin_path);
    wo_free(cue_path);
}

static void test_iso_and_mds(void) {
    const char *dir = wo_test_dir("disc-iso");
    if (!dir) {
        return;
    }
    IsoImage iso;
    iso_build_sample(&iso, "BOOT = cdrom:\\SLES_003.27;1\r\n");
    write_bytes(dir, "game.iso", iso.data, iso.sectors * ISO_SECTOR);
    wo_free(iso.data);
    WoError error = {0};
    char *path = wo_path_join(dir, "game.iso", &error);
    WoTrackList tracks;
    CHECK_OK(wo_disc_tracks(path, &tracks, &error), &error);
    CHECK(tracks.count == 0); /* plain images have no track table */
    char *image = NULL;
    uint64_t offset = 7;
    CHECK_OK(wo_disc_data_track(path, &image, &offset, &error), &error);
    CHECK(image && offset == 0);
    wo_free(image);
    char *out = wo_path_join(dir, "out", &error);
    size_t calls = 0;
    size_t imported = 0;
    CHECK_OK(wo_disc_import_image(path, out, common_only, &calls, &imported, &error), &error);
    CHECK(imported == 2);
    wo_free(out);
    /* A descriptor without its image data. */
    uint8_t mds[88] = {0};
    memcpy(mds, "MEDIA DESCRIPTOR", 16);
    mds[16] = 1;
    write_bytes(dir, "lonely.mds", mds, sizeof(mds));
    char *mds_path = wo_path_join(dir, "lonely.mdf", &error);
    CHECK_FAILS(wo_disc_tracks(mds_path, &tracks, &error), &error, WO_ERR_NOT_FOUND);
    wo_free(mds_path);
    /* Not a disc at all. */
    char *text = wo_path_join(dir, "notes.iso", &error);
    write_bytes(dir, "notes.iso", "hello", 5);
    CHECK_FAILS(wo_disc_import_image(text, dir, common_only, &calls, &imported, &error), &error,
                WO_ERR_FORMAT);
    wo_free(text);
    wo_free(path);
}

int main(void) {
    RUN(test_cue_bin);
    RUN(test_iso_and_mds);
    return wo_test_finish();
}
