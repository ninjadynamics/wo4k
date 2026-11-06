#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_miniz.h"
#include "core/wo_zip.h"
#include "wo_test.h"

/* Build an archive with arbitrary (possibly hostile) names using miniz
 * directly, bypassing the writer's own validation. */
static bool raw_zip(const char *path, const char *const *names, size_t count) {
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));
    if (!mz_zip_writer_init_heap(&archive, 0, 4096)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < count; i++) {
        ok = mz_zip_writer_add_mem(&archive, names[i], "data", 4, MZ_DEFAULT_COMPRESSION);
    }
    void *buffer = NULL;
    size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size);
    ok = ok && wo_write_file(path, buffer, size, NULL);
    if (buffer) {
        archive.m_pFree(archive.m_pAlloc_opaque, buffer);
    }
    mz_zip_writer_end(&archive);
    return ok;
}

static void test_round_trip(void) {
    const char *dir = wo_test_dir("zip-round-trip");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = wo_path_join(dir, "pack.zip", &error);
    WoZipWriter *writer = NULL;
    CHECK_OK(wo_zip_writer_begin(1767225600, &writer, &error), &error);
    CHECK_OK(wo_zip_writer_add(writer, "Setup.exe", "MZ", 2, &error), &error);
    CHECK_OK(wo_zip_writer_add(writer, "runtime/wohr.dll", "dll-bytes", 9, &error), &error);
    CHECK_FAILS(wo_zip_writer_add(writer, "../evil", "x", 1, &error), &error, WO_ERR_UNSAFE);
    CHECK_FAILS(wo_zip_writer_add(writer, "a\\b", "x", 1, &error), &error, WO_ERR_UNSAFE);
    CHECK_OK(wo_zip_writer_finish(writer, path, &error), &error);

    WoZip *zip = NULL;
    CHECK_OK(wo_zip_open(path, &zip, &error), &error);
    if (!zip) {
        wo_free(path);
        return;
    }
    CHECK(wo_zip_count(zip) == 2);
    size_t index = wo_zip_find(zip, "RUNTIME/WOHR.DLL");
    CHECK(index != SIZE_MAX);
    WoBuf buf = {0};
    CHECK_OK(wo_zip_read(zip, index, 100, &buf, &error), &error);
    CHECK(buf.len == 9 && memcmp(buf.data, "dll-bytes", 9) == 0);
    CHECK_FAILS(wo_zip_read(zip, index, 8, &buf, &error), &error, WO_ERR_LIMIT);
    wo_buf_free(&buf);

    char *out = wo_path_join(dir, "out", &error);
    WoZipLimits limits = {1024, 10, true};
    CHECK_OK(wo_zip_extract_all(zip, out, &limits, &error), &error);
    char *extracted = wo_path_join(out, "runtime/wohr.dll", &error);
    CHECK(wo_is_file(extracted));
    WoZipLimits tight = {10, 10, true};
    CHECK_FAILS(wo_zip_extract_all(zip, out, &tight, &error), &error, WO_ERR_LIMIT);
    WoZipLimits few = {0, 1, false};
    CHECK_FAILS(wo_zip_extract_all(zip, out, &few, &error), &error, WO_ERR_LIMIT);
    wo_zip_close(zip);
    wo_free(extracted);
    wo_free(out);
    wo_free(path);
}

static void test_hostile_names(void) {
    const char *dir = wo_test_dir("zip-hostile");
    if (!dir) {
        return;
    }
    const char *hostile[] = {"../escape.txt", "a/../../escape.txt", "C:/x.txt",
                             "file.txt:ads", "con.txt", "trail./x", "a//b", "q?.txt"};
    WoError error = {0};
    /* miniz cannot even write a leading-slash name; check the normaliser. */
    bool absolute_safe = true;
    char *absolute = wo_zip_normalise_name("/abs.txt", true, &absolute_safe, &error);
    CHECK(absolute && !absolute_safe);
    wo_free(absolute);
    for (size_t i = 0; i < WO_COUNT(hostile); i++) {
        char name[32];
        snprintf(name, sizeof(name), "hostile-%u.zip", (unsigned)i);
        char *path = wo_path_join(dir, name, &error);
        const char *names[] = {"ok.txt", hostile[i]};
        CHECK(raw_zip(path, names, 2));
        WoZip *zip = NULL;
        CHECK_OK(wo_zip_open(path, &zip, &error), &error);
        if (zip) {
            char *out = wo_path_join(dir, "out", &error);
            WoZipLimits limits = {0, 0, true};
            if (wo_zip_extract_all(zip, out, &limits, &error)) {
                wo_test_fail(__FILE__, __LINE__, hostile[i], "was extracted");
            } else {
                CHECK(error.kind == WO_ERR_UNSAFE);
            }
            /* Nothing is written when an archive is rejected up front. */
            CHECK(!wo_exists(out));
            wo_free(out);
            wo_zip_close(zip);
        }
        wo_free(path);
    }
    char *escape = wo_path_join(dir, "escape.txt", &error);
    CHECK(!wo_exists(escape));
    wo_free(escape);
}

static void test_duplicates_and_cp437(void) {
    const char *dir = wo_test_dir("zip-dupes");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = wo_path_join(dir, "dupe.zip", &error);
    const char *names[] = {"Track01/TRACK.WAD", "track01/track.wad"};
    CHECK(raw_zip(path, names, 2));
    WoZip *zip = NULL;
    CHECK_OK(wo_zip_open(path, &zip, &error), &error);
    if (zip) {
        char *out = wo_path_join(dir, "out", &error);
        WoZipLimits strict = {0, 0, true};
        CHECK_FAILS(wo_zip_extract_all(zip, out, &strict, &error), &error, WO_ERR_FORMAT);
        wo_free(out);
        wo_zip_close(zip);
    }
    bool safe = false;
    char *name = wo_zip_normalise_name("M\x81SIC\\track02.wav", false, &safe, &error);
    CHECK_STR(name, "M\xC3\xBCSIC/track02.wav");
    CHECK(safe);
    wo_free(name);
    name = wo_zip_normalise_name("bad\xff", true, &safe, &error);
    CHECK(!safe);
    wo_free(name);
    wo_free(path);
}

static void test_not_a_zip(void) {
    const char *dir = wo_test_dir("zip-garbage");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = wo_path_join(dir, "garbage.zip", &error);
    CHECK_OK(wo_write_file(path, "PK\x03\x04 not really", 16, &error), &error);
    WoZip *zip = NULL;
    CHECK_FAILS(wo_zip_open(path, &zip, &error), &error, WO_ERR_FORMAT);
    wo_free(path);
}

/* Entry times are written as the UTC wall-clock time on every machine, so a
 * package is byte-identical whatever the builder's time zone. */
static void test_entry_time_is_utc(void) {
    const char *dir = wo_test_dir("zip-time");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = wo_path_join(dir, "time.zip", &error);
    WoZipWriter *writer = NULL;
    CHECK_OK(wo_zip_writer_begin(1767225600, &writer, &error), &error); /* 2026-01-01 UTC */
    CHECK_OK(wo_zip_writer_add(writer, "a.txt", "a", 1, &error), &error);
    CHECK_OK(wo_zip_writer_finish(writer, path, &error), &error);
    WoBuf data = {0};
    CHECK_OK(wo_read_file(path, 1 << 20, &data, &error), &error);
    /* Local file header: DOS time at offset 10, date at 12 (little-endian). */
    CHECK(data.len > 14 && memcmp(data.data, "PK\x03\x04", 4) == 0);
    if (data.len > 14) {
        unsigned dos_time = data.data[10] | (unsigned)data.data[11] << 8;
        unsigned dos_date = data.data[12] | (unsigned)data.data[13] << 8;
        CHECK(dos_time == 0);
        CHECK(dos_date == ((2026u - 1980u) << 9 | 1u << 5 | 1u));
    }
    wo_buf_free(&data);
    wo_free(path);
}

int main(void) {
    RUN(test_round_trip);
    RUN(test_hostile_names);
    RUN(test_duplicates_and_cp437);
    RUN(test_not_a_zip);
    RUN(test_entry_time_is_utc);
    return wo_test_finish();
}
