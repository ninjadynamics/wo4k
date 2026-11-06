#include <string.h>

#include "core/wo_disctracks.h"
#include "wo_test.h"

/* --- MDS ---------------------------------------------------------------------- */

typedef struct MdsTrack {
    uint8_t mode;
    uint8_t number;
    uint8_t msf[3];
    uint16_t stride;
    uint64_t offset;
    uint32_t frames;
} MdsTrack;

/* One session; track blocks follow the session table, extras follow those. */
static void build_mds(WoBuf *b, const MdsTrack *tracks, size_t count) {
    size_t table = 88;
    size_t blocks = table + 24;
    size_t extras = blocks + count * 80;
    size_t total = extras + count * 8;
    wo_buf_reserve(b, total, NULL);
    memset(b->data, 0, total);
    b->len = total;
    memcpy(b->data, "MEDIA DESCRIPTOR", 16);
    b->data[16] = 1;
    wo_put_le16(b->data + 20, 1);
    wo_put_le32(b->data + 80, (uint32_t)table);
    b->data[table + 10] = (uint8_t)count;
    wo_put_le32(b->data + table + 20, (uint32_t)blocks);
    for (size_t i = 0; i < count; i++) {
        uint8_t *t = b->data + blocks + i * 80;
        t[0] = tracks[i].mode;
        t[4] = tracks[i].number;
        memcpy(t + 9, tracks[i].msf, 3);
        wo_put_le32(t + 12, (uint32_t)(extras + i * 8));
        wo_put_le16(t + 16, tracks[i].stride);
        wo_put_le32(t + 40, (uint32_t)tracks[i].offset);
        wo_put_le32(t + 44, (uint32_t)(tracks[i].offset >> 32));
        wo_put_le32(b->data + extras + i * 8 + 4, tracks[i].frames);
    }
}

static void test_mds(void) {
    MdsTrack tracks[] = {
        {0xAA, 1, {0, 2, 0}, 2352, 0, 1000},
        {0xA9, 2, {0, 15, 40}, 2352, 2352000, 500},
        {0xA9, 3, {1, 0, 1}, 2448, 0x100000000ull, 300},
        {0x00, 0xA0, {0, 0, 0}, 0, 0, 0}, /* session entry, skipped */
    };
    WoBuf b = {0};
    build_mds(&b, tracks, WO_COUNT(tracks));
    WoError error = {0};
    WoTrackList list;
    CHECK_OK(wo_mds_parse(b.data, b.len, "C:\\disc\\game.mdf", &list, &error), &error);
    CHECK(list.count == 3);
    if (list.count == 3) {
        CHECK(!list.items[0].audio && list.items[0].number == 1);
        CHECK(list.items[0].start_frame == 150 && list.items[0].frames == 1000);
        CHECK(list.items[1].audio && list.items[1].offset == 2352000);
        CHECK(list.items[1].start_frame == (0 * 60 + 15) * 75 + 40);
        CHECK(list.items[2].stride == 2448 && list.items[2].offset == 0x100000000ull);
        CHECK(list.items[2].start_frame == (1 * 60 + 0) * 75 + 1);
        CHECK_STR(list.items[2].image, "C:\\disc\\game.mdf");
        CHECK(wo_tracks_audio_count(&list) == 2);
        CHECK(wo_tracks_first_data(&list) == &list.items[0]);
    }
    wo_tracks_free(&list);

    /* Rejections. */
    CHECK_FAILS(wo_mds_parse(b.data, 87, "x.mdf", &list, &error), &error, WO_ERR_UNSUPPORTED);
    b.data[16] = 2;
    CHECK_FAILS(wo_mds_parse(b.data, b.len, "x.mdf", &list, &error), &error, WO_ERR_UNSUPPORTED);
    b.data[16] = 1;
    b.data[0] = 'm';
    CHECK_FAILS(wo_mds_parse(b.data, b.len, "x.mdf", &list, &error), &error, WO_ERR_UNSUPPORTED);
    b.data[0] = 'M';
    /* Track extras cut off. */
    CHECK_FAILS(wo_mds_parse(b.data, b.len - 9, "x.mdf", &list, &error), &error, WO_ERR_FORMAT);
    /* Track table cut off. */
    CHECK_FAILS(wo_mds_parse(b.data, 88 + 24 + 100, "x.mdf", &list, &error), &error,
                WO_ERR_FORMAT);
    /* Session table outside the file. */
    wo_put_le32(b.data + 80, 0xFFFFFFF0u);
    CHECK_FAILS(wo_mds_parse(b.data, b.len, "x.mdf", &list, &error), &error, WO_ERR_FORMAT);
    wo_put_le32(b.data + 80, 88);
    /* Many sessions repeating the same tracks hit the track limit. */
    uint8_t session[24];
    memcpy(session, b.data + 88, sizeof(session));
    size_t table = b.len;
    for (int s = 0; s < 60; s++) {
        wo_buf_append(&b, session, sizeof(session), NULL);
    }
    wo_put_le16(b.data + 20, 60);
    wo_put_le32(b.data + 80, (uint32_t)table);
    CHECK_FAILS(wo_mds_parse(b.data, b.len, "x.mdf", &list, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
}

/* --- CUE ---------------------------------------------------------------------- */

typedef struct FakeFile {
    const char *name;
    uint64_t size;
} FakeFile;

typedef struct FakeDisc {
    const FakeFile *files;
    size_t count;
    size_t calls;
} FakeDisc;

static bool resolve(void *context, const char *name, char **path, uint64_t *size,
                    WoError *error) {
    FakeDisc *disc = context;
    disc->calls++;
    for (size_t i = 0; i < disc->count; i++) {
        if (wo_str_ieq(disc->files[i].name, name)) {
            *path = wo_format(error, "D:\\%s", disc->files[i].name);
            *size = disc->files[i].size;
            return *path != NULL;
        }
    }
    return wo_fail(error, WO_ERR_NOT_FOUND, "The CUE sheet refers to a missing file: %s", name);
}

static bool parse_cue(const char *text, const FakeFile *files, size_t count, WoTrackList *list,
                      WoError *error) {
    FakeDisc disc = {files, count, 0};
    return wo_cue_parse(text, strlen(text), resolve, &disc, list, error);
}

static void test_cue_multi_file(void) {
    /* The PlayStation disc's layout: one file per track, 2-second pregaps. */
    static const FakeFile files[] = {
        {"Track 01.bin", 86327808}, {"Track 02.bin", 62269200}, {"Track 03.bin", 47606832}};
    const char *cue = "\xEF\xBB\xBF"
                      "FILE \"Track 01.bin\" BINARY\r\n"
                      "  TRACK 01 MODE2/2352\r\n"
                      "    INDEX 01 00:00:00\r\n"
                      "FILE \"Track 02.bin\" BINARY\r\n"
                      "  TRACK 02 AUDIO\r\n"
                      "    INDEX 00 00:00:00\r\n"
                      "    INDEX 01 00:02:00\r\n"
                      "file \"track 03.BIN\" binary\n"
                      "  track 03 audio\n"
                      "    REM INDEX 01 99:99:99\n"
                      "    INDEX 00 00:00:00\r"
                      "    INDEX 01 00:02:00\n";
    WoError error = {0};
    WoTrackList list;
    CHECK_OK(parse_cue(cue, files, WO_COUNT(files), &list, &error), &error);
    CHECK(list.count == 3);
    if (list.count == 3) {
        CHECK(!list.items[0].audio && list.items[0].stride == 2352);
        CHECK(list.items[0].start_frame == 150 && list.items[0].frames == 36704);
        CHECK(list.items[1].audio && list.items[1].start_frame == 37004);
        CHECK(list.items[1].frames == 26325 && list.items[1].offset == 150 * 2352);
        CHECK_STR(list.items[1].image, "D:\\Track 02.bin");
        CHECK(list.items[2].audio && list.items[2].start_frame == 63479);
        CHECK(list.items[2].frames == 20091);
    }
    WoBuf ini = {0};
    CHECK_OK(wo_tracks_write_ini(&list, &ini, &error), &error);
    wo_buf_terminate(&ini, NULL);
    CHECK_STR((const char *)ini.data,
              "[CD]\r\nTracks=3\r\n\r\n[Track01]\r\nStartFrame=150\r\nFrames=36704\r\n\r\n"
              "[Track02]\r\nStartFrame=37004\r\nFrames=26325\r\n\r\n"
              "[Track03]\r\nStartFrame=63479\r\nFrames=20091\r\n\r\n");
    wo_buf_free(&ini);
    wo_tracks_free(&list);
}

static void test_cue_single_file(void) {
    /* One BIN: track ends where the next track's INDEX 00 (or 01) starts;
     * MODE1/2048 sets the stride. 1001 sectors and a half: rounds to even. */
    static const FakeFile files[] = {{"disc.bin", 2048 * 1000 + 1024}};
    const char *cue = "FILE \"disc.bin\" BINARY\n"
                      "TRACK 01 MODE1/2048\n"
                      "INDEX 01 00:00:00\n"
                      "TRACK 02 AUDIO\n"
                      "INDEX 01 00:04:00\n"
                      "TRACK 03 AUDIO\n"
                      "INDEX 00 00:06:00\n"
                      "INDEX 01 00:08:00\n";
    WoError error = {0};
    WoTrackList list;
    CHECK_OK(parse_cue(cue, files, 1, &list, &error), &error);
    CHECK(list.count == 3);
    if (list.count == 3) {
        CHECK(list.items[0].stride == 2048 && list.items[0].frames == 300);
        CHECK(list.items[1].stride == 2352 && list.items[1].frames == 150);
        CHECK(list.items[1].start_frame == 150 + 300 && list.items[1].offset == 300 * 2352);
        /* (2049024 / 2048 = 1000.5 rounds to 1000) - 600 */
        CHECK(list.items[2].frames == 400 && list.items[2].start_frame == 750);
    }
    wo_tracks_free(&list);
    static const FakeFile odd[] = {{"disc.bin", 2048 * 1001 + 1024}};
    CHECK_OK(parse_cue(cue, odd, 1, &list, &error), &error);
    CHECK(list.count == 3 && list.items[2].frames == 1002 - 600);
    wo_tracks_free(&list);
}

static void expect_cue_failure(const char *cue, WoErrorKind kind) {
    static const FakeFile files[] = {{"a.bin", 2352 * 100}, {"b.bin", 2352 * 100}};
    WoError error = {0};
    WoTrackList list;
    FakeDisc disc = {files, WO_COUNT(files), 0};
    if (wo_cue_parse(cue, strlen(cue), resolve, &disc, &list, &error)) {
        wo_test_fail(__FILE__, __LINE__, cue, "was accepted");
        wo_tracks_free(&list);
    } else if (error.kind != kind) {
        wo_test_fail(__FILE__, __LINE__, cue, error.message);
    }
}

static void test_cue_rejects(void) {
    expect_cue_failure("FILE \"missing.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n",
                       WO_ERR_NOT_FOUND);
    expect_cue_failure("FILE \"a.wav\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n",
                       WO_ERR_UNSUPPORTED);
    expect_cue_failure("FILE a.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n",
                       WO_ERR_UNSUPPORTED);
    expect_cue_failure("FILE \"\" BINARY\n", WO_ERR_UNSUPPORTED);
    expect_cue_failure("TRACK 01 AUDIO\nINDEX 01 00:00:00\n", WO_ERR_FORMAT);
    expect_cue_failure("INDEX 01 00:00:00\n", WO_ERR_FORMAT);
    expect_cue_failure("FILE \"a.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 00 00:00:00\n",
                       WO_ERR_FORMAT);
    expect_cue_failure("FILE \"a.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n"
                       "TRACK 02 AUDIO\nINDEX 01 00:00:00\n",
                       WO_ERR_FORMAT); /* zero-length track 1 */
    expect_cue_failure("FILE \"a.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:02:00\n",
                       WO_ERR_FORMAT); /* starts past the end of a 100-sector file */
    expect_cue_failure("FILE \"a.bin\" BINARY\nTRACK 100 AUDIO\nINDEX 01 00:00:00\n",
                       WO_ERR_FORMAT);
    expect_cue_failure("FILE \"a.bin\" BINARY\nTRACK 01\nINDEX 01 00:00:00\n", WO_ERR_FORMAT);
    expect_cue_failure("FILE \"a.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 0000000000:00:00\n",
                       WO_ERR_FORMAT);
    expect_cue_failure("FILE \"../a.bin\" BINARY\n", WO_ERR_NOT_FOUND);
    /* More than 99 tracks. */
    WoBuf many = {0};
    wo_buf_append_str(&many, "FILE \"a.bin\" BINARY\n", NULL);
    for (int i = 0; i < 100; i++) {
        wo_buf_printf(&many, NULL, "TRACK %02d AUDIO\nINDEX 01 00:00:%02d\n", i % 100, i % 75);
    }
    wo_buf_terminate(&many, NULL);
    expect_cue_failure((const char *)many.data, WO_ERR_FORMAT);
    wo_buf_free(&many);
    /* Oversized sheet. */
    WoBuf big = {0};
    wo_buf_reserve(&big, WO_DESCRIPTOR_MAX_BYTES + 1, NULL);
    memset(big.data, ' ', WO_DESCRIPTOR_MAX_BYTES + 1);
    WoError error = {0};
    WoTrackList list;
    FakeDisc disc = {NULL, 0, 0};
    CHECK_FAILS(wo_cue_parse((const char *)big.data, WO_DESCRIPTOR_MAX_BYTES + 1, resolve, &disc,
                             &list, &error),
                &error, WO_ERR_LIMIT);
    wo_buf_free(&big);
}

static void test_cue_ignored_lines(void) {
    /* Unknown commands, other indexes and malformed INDEX lines are ignored,
     * as before. A NUL byte inside the text does not end parsing. */
    static const FakeFile files[] = {{"a.bin", 2352 * 100}};
    const char cue[] = "REM GENRE Game\nCATALOG 0000000000000\nFILE \"a.bin\" BINARY\n"
                       "TRACK 01 AUDIO\nFLAGS DCP\nPREGAP 00:02:00\nINDEX 02 00:01:00\n"
                       "INDEX 1 00:01:00\nINDEX 01 00:00:10 trailing\n\0\nTITLE x\n";
    WoError error = {0};
    WoTrackList list;
    FakeDisc disc = {files, 1, 0};
    CHECK_OK(wo_cue_parse(cue, sizeof(cue) - 1, resolve, &disc, &list, &error), &error);
    CHECK(list.count == 1 && list.items[0].frames == 90 && list.items[0].offset == 10 * 2352);
    CHECK(disc.calls == 1);
    wo_tracks_free(&list);
}

int main(void) {
    RUN(test_mds);
    RUN(test_cue_multi_file);
    RUN(test_cue_single_file);
    RUN(test_cue_rejects);
    RUN(test_cue_ignored_lines);
    return wo_test_finish();
}
