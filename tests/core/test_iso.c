#include <string.h>

#include "core/wo_iso.h"
#include "iso_builder.h"
#include "wo_test.h"

static const char *BOOT = "BOOT = cdrom:\\SLES_003.27;1\r\nTCB = 4\r\n";

typedef struct Collect {
    WoBuf buf;
    size_t calls;
} Collect;

static bool collect(void *context, const void *data, size_t len, WoError *error) {
    Collect *c = context;
    c->calls++;
    return wo_buf_append(&c->buf, data, len, error);
}

static void check_sample(const uint8_t *data, size_t len, uint64_t origin, uint32_t stride,
                         uint32_t payload) {
    WoError error = {0};
    WoSource source;
    wo_source_from_memory(&source, data, len, "sample.iso");
    WoIso iso;
    CHECK_OK(wo_iso_open(&iso, &source, origin, &error), &error);
    CHECK(iso.stride == stride && iso.payload == payload);
    CHECK(iso.count == 8);
    CHECK_STR(iso.entries[0].path, "README.TXT");
    CHECK_STR(iso.entries[2].path, "WIPEOUT2");
    CHECK(iso.entries[2].is_dir);
    CHECK_STR(iso.entries[3].path, "WIPEOUT2/COMMON");
    CHECK_STR(iso.entries[4].path, "WIPEOUT2/COMMON/MINE.CMP");
    const WoIsoEntry *cnf = wo_iso_find(&iso, "system.cnf");
    CHECK(cnf && !cnf->is_dir);
    uint8_t *text = NULL;
    if (cnf) {
        CHECK_OK(wo_iso_read_entry(&iso, cnf, 4096, &text, &error), &error);
        CHECK(text && memcmp(text, BOOT, strlen(BOOT)) == 0);
        wo_free(text);
        CHECK_FAILS(wo_iso_read_entry(&iso, cnf, 4, &text, &error), &error, WO_ERR_LIMIT);
    }
    const WoIsoEntry *explode = wo_iso_find(&iso, "WIPEOUT2/TEXTURES/EXPLODE.CMP");
    CHECK(explode && explode->length == 5000);
    if (explode) {
        Collect c = {{0}, 0};
        CHECK_OK(wo_iso_stream_entry(&iso, explode, collect, &c, &error), &error);
        CHECK(c.buf.len == 5000);
        bool same = c.buf.len == 5000;
        for (size_t i = 0; same && i < 5000; i++) {
            same = c.buf.data[i] == (uint8_t)(i * 7 + 3);
        }
        CHECK(same);
        wo_buf_free(&c.buf);
    }
    const WoIsoEntry *dir = wo_iso_find(&iso, "WIPEOUT2/TEXTURES");
    CHECK(dir && dir->is_dir);
    if (dir) {
        CHECK_FAILS(wo_iso_read_entry(&iso, dir, 4096, &text, &error), &error, WO_ERR_FORMAT);
    }
    CHECK(wo_iso_find(&iso, "WIPEOUT2/MISSING") == NULL);
    const WoIsoEntry *readme = wo_iso_find(&iso, "README.TXT");
    CHECK(readme != NULL);
    if (readme) {
        CHECK_OK(wo_iso_read_entry(&iso, readme, 100, &text, &error), &error);
        CHECK(text && memcmp(text, "hello", 5) == 0);
        wo_free(text);
    }
    wo_iso_close(&iso);
}

static void test_layouts(void) {
    IsoImage iso;
    iso_build_sample(&iso, BOOT);
    check_sample(iso.data, iso.sectors * ISO_SECTOR, 0, 2048, 0);
    static const uint32_t strides[] = {2352, 2448, 2352};
    static const uint32_t payloads[] = {16, 24, 24};
    for (size_t i = 0; i < WO_COUNT(strides); i++) {
        size_t len = 0;
        uint8_t *raw = iso_raw(&iso, strides[i], payloads[i], 0, &len);
        check_sample(raw, len, 0, strides[i], payloads[i]);
        wo_free(raw);
    }
    /* A data track that starts after other tracks (as in a BIN/MDF). */
    size_t len = 0;
    uint8_t *raw = iso_raw(&iso, 2352, 16, 2352 * 150, &len);
    check_sample(raw, len, 2352 * 150, 2352, 16);
    wo_free(raw);
    /* An image may end right after the last byte a file needs, without the
     * rest of that sector (README.TXT, 5 bytes, is in the last sector). */
    IsoImage shorter = iso;
    shorter.sectors = 29;
    raw = iso_raw(&shorter, 2352, 16, 2352 * 150, &len);
    check_sample(raw, len - (2352 - 16 - 5), 2352 * 150, 2352, 16);
    wo_free(raw);
    wo_free(iso.data);
}

static void expect_open_failure(IsoImage *iso, WoErrorKind kind) {
    WoError error = {0};
    WoSource source;
    wo_source_from_memory(&source, iso->data, iso->sectors * ISO_SECTOR, "bad.iso");
    WoIso opened;
    CHECK_FAILS(wo_iso_open(&opened, &source, 0, &error), &error, kind);
    CHECK(opened.entries == NULL && opened.count == 0);
}

static void test_rejects(void) {
    IsoImage iso;
    /* No volume descriptor. */
    iso_init(&iso, 20);
    expect_open_failure(&iso, WO_ERR_FORMAT);
    /* Too short to hold one. */
    WoError error = {0};
    WoSource tiny;
    wo_source_from_memory(&tiny, iso.data, 100, "tiny.iso");
    WoIso opened;
    CHECK_FAILS(wo_iso_open(&opened, &tiny, 0, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_iso_open(&opened, &tiny, 1000, &error), &error, WO_ERR_FORMAT);
    wo_free(iso.data);

    static const char *const unsafe[] = {"..", "A/B", "A\\B", "C:X", ".", "", "BAD\x01NAME",
                                         ";1"};
    for (size_t i = 0; i < WO_COUNT(unsafe); i++) {
        iso_init(&iso, 20);
        iso_pvd(&iso, 18, ISO_SECTOR);
        size_t at = iso_dir_begin(&iso, 18, ISO_SECTOR);
        iso_record(iso_sector(&iso, 18) + at, unsafe[i], strlen(unsafe[i]), 19, 1, false);
        expect_open_failure(&iso, WO_ERR_UNSAFE);
        wo_free(iso.data);
    }

    /* A record shorter than the fixed part, or longer than the directory. */
    iso_init(&iso, 20);
    iso_pvd(&iso, 18, ISO_SECTOR);
    size_t at = iso_dir_begin(&iso, 18, ISO_SECTOR);
    iso_sector(&iso, 18)[at] = 20;
    expect_open_failure(&iso, WO_ERR_FORMAT);
    iso_sector(&iso, 18)[at] = 34;
    iso_sector(&iso, 18)[at + 32] = 5; /* name longer than the record */
    expect_open_failure(&iso, WO_ERR_FORMAT);
    wo_free(iso.data);

    /* A directory that contains itself recurses until the depth limit. */
    iso_init(&iso, 20);
    iso_pvd(&iso, 18, ISO_SECTOR);
    at = iso_dir_begin(&iso, 18, ISO_SECTOR);
    iso_record(iso_sector(&iso, 18) + at, "LOOP", 4, 18, ISO_SECTOR, true);
    expect_open_failure(&iso, WO_ERR_LIMIT);
    wo_free(iso.data);

    /* An oversized directory extent. */
    iso_init(&iso, 20);
    iso_pvd(&iso, 18, 0x7FFFFFFFu);
    expect_open_failure(&iso, WO_ERR_LIMIT);
    wo_free(iso.data);

    /* A directory extent past the end of the image. */
    iso_init(&iso, 20);
    iso_pvd(&iso, 18, ISO_SECTOR * 4);
    iso_dir_begin(&iso, 18, ISO_SECTOR * 4);
    expect_open_failure(&iso, WO_ERR_FORMAT);
    wo_free(iso.data);
}

static void test_entry_limit(void) {
    /* 50001 one-letter names (34 bytes each, 60 per sector). */
    size_t records = WO_ISO_MAX_ENTRIES + 1;
    size_t dir_sectors = (records + 2 + 59) / 60;
    IsoImage iso;
    iso_init(&iso, 19 + dir_sectors);
    iso_pvd(&iso, 18, (uint32_t)(dir_sectors * ISO_SECTOR));
    size_t at = iso_dir_begin(&iso, 18, (uint32_t)(dir_sectors * ISO_SECTOR));
    uint8_t *dir = iso_sector(&iso, 18);
    size_t written = 0;
    for (size_t sector = 0; sector < dir_sectors && written < records; sector++) {
        size_t pos = sector == 0 ? at : 0;
        while (pos + 34 <= ISO_SECTOR && written < records) {
            pos += iso_record(dir + sector * ISO_SECTOR + pos, "A", 1, 18, 0, false);
            written++;
        }
    }
    expect_open_failure(&iso, WO_ERR_LIMIT);
    wo_free(iso.data);
}

static void test_truncated_file(void) {
    IsoImage iso;
    iso_build_sample(&iso, BOOT);
    /* EXPLODE.CMP claims more than the image holds. */
    wo_put_le32(iso_sector(&iso, 21) + 68 + 10, 1000000);
    WoError error = {0};
    WoSource source;
    wo_source_from_memory(&source, iso.data, iso.sectors * ISO_SECTOR, "short.iso");
    WoIso opened;
    CHECK_OK(wo_iso_open(&opened, &source, 0, &error), &error);
    const WoIsoEntry *explode = wo_iso_find(&opened, "WIPEOUT2/TEXTURES/EXPLODE.CMP");
    CHECK(explode && explode->length == 1000000);
    if (explode) {
        Collect c = {{0}, 0};
        CHECK_FAILS(wo_iso_stream_entry(&opened, explode, collect, &c, &error), &error,
                    WO_ERR_FORMAT);
        wo_buf_free(&c.buf);
    }
    wo_iso_close(&opened);
    wo_free(iso.data);
}

int main(void) {
    RUN(test_layouts);
    RUN(test_rejects);
    RUN(test_entry_limit);
    RUN(test_truncated_file);
    return wo_test_finish();
}
