/* The integrity cache: unchanged files are not read again, changed or
 * replaced ones always are, and a damaged cache falls back to verifying
 * every file. */
#include <windows.h>

#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hashcache.h"
#include "core/wo_win.h"
#include "wo_test.h"

/* Dates every time stamp of path, the change time included, an hour back,
 * as a file untouched since an earlier launch. last_write, when not 0, is
 * used for the last-write time instead. */
static bool backdate(const char *path, uint64_t last_write) {
    wchar_t *wide = wo_wide_path(path, NULL);
    HANDLE handle = wide ? CreateFileW(wide, FILE_WRITE_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
                         : INVALID_HANDLE_VALUE;
    wo_free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    FILE_BASIC_INFO info;
    bool ok = GetFileInformationByHandleEx(handle, FileBasicInfo, &info, sizeof(info)) != 0;
    LONGLONG then = (LONGLONG)(wo_fs_now() - 36000000000ull);
    info.CreationTime.QuadPart = then;
    info.LastAccessTime.QuadPart = then;
    info.LastWriteTime.QuadPart = last_write ? (LONGLONG)last_write : then;
    info.ChangeTime.QuadPart = then;
    ok = ok && SetFileInformationByHandle(handle, FileBasicInfo, &info, sizeof(info)) != 0;
    CloseHandle(handle);
    return ok;
}

static char *write_file(const char *dir, const char *name, const char *text, bool old) {
    WoError error = {0};
    char *path = wo_path_join(dir, name, &error);
    CHECK_OK(path && wo_write_file(path, text, strlen(text), &error), &error);
    if (old) {
        CHECK(path && backdate(path, 0));
    }
    return path;
}

static void digest_of(const char *text, char hex[WO_SHA256_HEX_SIZE]) {
    WoError error = {0};
    CHECK_OK(wo_sha256_mem(text, strlen(text), hex, &error), &error);
}

static WoHashCache *open_cache(const char *file, const char *root, bool fresh,
                               const char **note) {
    WoError error = {0};
    const char *ignored = NULL;
    WoHashCache *cache = wo_hash_cache_open(file, root, fresh, note ? note : &ignored, &error);
    CHECK(cache != NULL);
    return cache;
}

static void stats(const WoHashCache *cache, WoHashCacheStats *out) {
    wo_hash_cache_stats(cache, out);
}

/* An unchanged file is not read again, in this session or the next. */
static void test_unchanged_reused(void) {
    const char *dir = wo_test_dir("hashcache-unchanged");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *file = wo_path_join(dir, "integrity.json", &error);
    char *a = write_file(dir, "a.bin", "first file", true);
    char expected[WO_SHA256_HEX_SIZE];
    char hex[WO_SHA256_HEX_SIZE];
    digest_of("first file", expected);

    WoHashCache *cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_STR(hex, expected);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_STR(hex, expected);
    WoHashCacheStats s;
    stats(cache, &s);
    CHECK(!s.loaded && s.hashed == 1 && s.hits == 1 && s.hashed_bytes == 10 && s.racy == 0);
    CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
    wo_hash_cache_free(cache);

    cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_STR(hex, expected);
    stats(cache, &s);
    CHECK(s.loaded && s.hashed == 0 && s.hits == 1 && s.entries == 1);
    wo_hash_cache_free(cache);

    /* Keys are stored relative to the root, so a moved folder still works. */
    char *text = NULL;
    CHECK_OK(wo_read_text(file, 1u << 20, &text, &error), &error);
    CHECK(text && strstr(text, "\"path\": \"a.bin\"") != NULL);
    CHECK(text && strstr(text, "test-work") == NULL);
    wo_free(text);
    wo_free(a);
    wo_free(file);
}

/* Changed contents are always read, even with the old size and last-write
 * time put back, and so is a file replaced by another. */
static void test_changes_detected(void) {
    const char *dir = wo_test_dir("hashcache-changes");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *file = wo_path_join(dir, "integrity.json", &error);
    char *a = write_file(dir, "a.bin", "contents 1", true);
    char *b = write_file(dir, "b.bin", "stays same", true);
    char hex[WO_SHA256_HEX_SIZE];
    char expected[WO_SHA256_HEX_SIZE];
    WoHashCache *cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_OK(wo_hash_cache_file(cache, b, hex, &error), &error);
    CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
    wo_hash_cache_free(cache);
    WoFileIdentity before;
    CHECK_OK(wo_file_identity(a, &before, &error), &error);

    /* Same size, last-write time restored: the change time still moves. */
    CHECK_OK(wo_write_file(a, "contents 2", 10, &error), &error);
    wchar_t *wide = wo_wide_path(a, NULL);
    HANDLE handle = wide ? CreateFileW(wide, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
                         : INVALID_HANDLE_VALUE;
    wo_free(wide);
    FILETIME restored;
    restored.dwLowDateTime = (DWORD)before.mtime;
    restored.dwHighDateTime = (DWORD)(before.mtime >> 32);
    CHECK(handle != INVALID_HANDLE_VALUE && SetFileTime(handle, NULL, NULL, &restored));
    CloseHandle(handle);
    WoFileIdentity after;
    CHECK_OK(wo_file_identity(a, &after, &error), &error);
    CHECK(after.size == before.size && after.mtime == before.mtime);
    CHECK(!wo_file_identity_eq(&before, &after));

    cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    digest_of("contents 2", expected);
    CHECK_STR(hex, expected);
    CHECK_OK(wo_hash_cache_file(cache, b, hex, &error), &error);
    digest_of("stays same", expected);
    CHECK_STR(hex, expected);
    WoHashCacheStats s;
    stats(cache, &s);
    CHECK(s.hashed == 1 && s.hits == 1);
    wo_hash_cache_free(cache);

    /* Replaced by another file with the same size and all times: new id. */
    cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, b, hex, &error), &error);
    CHECK_OK(wo_hash_cache_save(cache, false, &error), &error);
    wo_hash_cache_free(cache);
    char *c = write_file(dir, "c.bin", "other file", false);
    CHECK_OK(wo_delete_file(b, &error), &error);
    CHECK_OK(wo_move(c, b, &error), &error);
    CHECK(backdate(b, 0));
    cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, b, hex, &error), &error);
    digest_of("other file", expected);
    CHECK_STR(hex, expected);
    stats(cache, &s);
    CHECK(s.hashed == 1 && s.hits == 0);

    /* A file that is gone is an error, never a cached digest. */
    CHECK_OK(wo_delete_file(b, &error), &error);
    CHECK_FAILS(wo_hash_cache_file(cache, b, hex, &error), &error, WO_ERR_NOT_FOUND);
    wo_hash_cache_free(cache);
    wo_free(c);
    wo_free(b);
    wo_free(a);
    wo_free(file);
}

/* A just-written file could change again within the same time stamp, so
 * it is hashed every time until it has been left alone for a while. */
static void test_recent_files_not_remembered(void) {
    const char *dir = wo_test_dir("hashcache-recent");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *file = wo_path_join(dir, "integrity.json", &error);
    char *a = write_file(dir, "a.bin", "just written", false);
    char hex[WO_SHA256_HEX_SIZE];
    WoHashCache *cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    WoHashCacheStats s;
    stats(cache, &s);
    CHECK(s.hashed == 2 && s.hits == 0 && s.racy == 2 && s.entries == 0);
    CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
    wo_hash_cache_free(cache);
    cache = open_cache(file, dir, false, NULL);
    stats(cache, &s);
    CHECK(!s.loaded && s.entries == 0);
    wo_hash_cache_free(cache);
    wo_free(a);
    wo_free(file);
}

/* A damaged, foreign or unreadable cache is ignored: every file is hashed
 * again and a sound cache is written back. So is the cache after Install /
 * repair (fresh). */
static void test_bad_cache_full_verification(void) {
    const char *dir = wo_test_dir("hashcache-bad");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *file = wo_path_join(dir, "integrity.json", &error);
    char *a = write_file(dir, "a.bin", "verify me", true);
    char hex[WO_SHA256_HEX_SIZE];
    char expected[WO_SHA256_HEX_SIZE];
    digest_of("verify me", expected);
    WoHashCache *cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
    wo_hash_cache_free(cache);
    char *good = NULL;
    CHECK_OK(wo_read_text(file, 1u << 20, &good, &error), &error);

    static const char *const damaged[] = {
        "{\"format\": 1, \"files\": [ {\"path\": \"a.bin\"",               /* truncated */
        "\x01\x02 not json",                                                 /* garbage */
        "{\"format\": 2, \"files\": []}",                                    /* foreign */
        "{\"format\": 1, \"files\": [{\"path\": \"a.bin\", \"size\": 9}]}", /* incomplete */
    };
    for (size_t i = 0; i < WO_COUNT(damaged); i++) {
        CHECK_OK(wo_write_file(file, damaged[i], strlen(damaged[i]), &error), &error);
        const char *note = NULL;
        cache = open_cache(file, dir, false, &note);
        CHECK(note != NULL);
        CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
        CHECK_STR(hex, expected);
        WoHashCacheStats s;
        stats(cache, &s);
        CHECK(!s.loaded && s.hashed == 1 && s.hits == 0);
        CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
        wo_hash_cache_free(cache);
    }
    /* Rebuilt: the next session reuses it again. */
    cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    WoHashCacheStats s;
    stats(cache, &s);
    CHECK(s.loaded && s.hits == 1 && s.hashed == 0);
    wo_hash_cache_free(cache);

    /* A digest edited in the cache is not trusted to clear a damaged file:
     * it can only ever make a sound file look changed, and that repairs. */
    char *edited = good ? wo_strdup(good, &error) : NULL;
    char *at = edited ? strstr(edited, expected) : NULL;
    CHECK(at != NULL);
    if (at) {
        at[0] = at[0] == '0' ? '1' : '0';
        CHECK_OK(wo_write_file(file, edited, strlen(edited), &error), &error);
        cache = open_cache(file, dir, false, NULL);
        CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
        CHECK(!wo_digest_eq(hex, expected));
        wo_hash_cache_free(cache);
    }

    /* Fresh: the entries are ignored and the file is read. */
    cache = open_cache(file, dir, true, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, hex, &error), &error);
    CHECK_STR(hex, expected);
    stats(cache, &s);
    CHECK(!s.loaded && s.hashed == 1 && s.hits == 0);
    wo_hash_cache_free(cache);

    /* No cache at all hashes every time. */
    CHECK_OK(wo_hash_cache_file(NULL, a, hex, &error), &error);
    CHECK_STR(hex, expected);
    wo_free(edited);
    wo_free(good);
    wo_free(a);
    wo_free(file);
}

/* Unused entries are dropped by a pruning save; facts follow their digest. */
static void test_prune_and_facts(void) {
    const char *dir = wo_test_dir("hashcache-facts");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *file = wo_path_join(dir, "integrity.json", &error);
    char *a = write_file(dir, "a.bin", "kept file", true);
    char *b = write_file(dir, "b.bin", "dropped file", true);
    char ha[WO_SHA256_HEX_SIZE];
    char hb[WO_SHA256_HEX_SIZE];
    WoHashCache *cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, ha, &error), &error);
    CHECK_OK(wo_hash_cache_file(cache, b, hb, &error), &error);
    WoJson *facts = wo_json_new_object();
    wo_json_put_int(facts, "width", 64);
    wo_hash_cache_set_facts(cache, ha, "png", facts);
    facts = wo_json_new_object();
    wo_json_put_int(facts, "width", 32);
    wo_hash_cache_set_facts(cache, hb, "png", facts);
    CHECK(wo_json_int(wo_hash_cache_facts(cache, ha, "png"), "width", 0) == 64);
    CHECK(wo_hash_cache_facts(cache, ha, "signature") == NULL);
    CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
    wo_hash_cache_free(cache);

    /* Only a is checked now; a non-pruning save keeps b. */
    cache = open_cache(file, dir, false, NULL);
    CHECK_OK(wo_hash_cache_file(cache, a, ha, &error), &error);
    CHECK(wo_json_int(wo_hash_cache_facts(cache, ha, "png"), "width", 0) == 64);
    CHECK_OK(wo_hash_cache_save(cache, false, &error), &error);
    wo_hash_cache_free(cache);
    cache = open_cache(file, dir, false, NULL);
    WoHashCacheStats s;
    stats(cache, &s);
    CHECK(s.entries == 2);
    CHECK_OK(wo_hash_cache_file(cache, a, ha, &error), &error);
    CHECK_OK(wo_hash_cache_save(cache, true, &error), &error);
    wo_hash_cache_free(cache);
    cache = open_cache(file, dir, false, NULL);
    stats(cache, &s);
    CHECK(s.entries == 1);
    CHECK(wo_hash_cache_facts(cache, ha, "png") != NULL);
    CHECK(wo_hash_cache_facts(cache, hb, "png") == NULL);
    wo_hash_cache_free(cache);
    wo_free(b);
    wo_free(a);
    wo_free(file);
}

/* An identical JSON file is left alone; a different one is replaced. */
static void test_write_json_if_changed(void) {
    const char *dir = wo_test_dir("hashcache-json");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = wo_path_join(dir, "state.json", &error);
    WoJson *value = wo_json_new_object();
    wo_json_put_int(value, "revision", 1);
    bool written = false;
    CHECK_OK(wo_write_json_if_changed(path, value, &written, &error), &error);
    CHECK(written);
    CHECK(backdate(path, 0));
    WoFileIdentity before;
    WoFileIdentity after;
    CHECK_OK(wo_file_identity(path, &before, &error), &error);
    CHECK_OK(wo_write_json_if_changed(path, value, &written, &error), &error);
    CHECK(!written);
    CHECK_OK(wo_file_identity(path, &after, &error), &error);
    CHECK(wo_file_identity_eq(&before, &after));
    wo_json_put_int(value, "revision", 2);
    CHECK_OK(wo_write_json_if_changed(path, value, &written, &error), &error);
    CHECK(written);
    WoJson *read = NULL;
    CHECK_OK(wo_read_json(path, 1u << 20, &read, &error), &error);
    CHECK(wo_json_int(read, "revision", 0) == 2);
    wo_json_free(read);
    wo_json_free(value);
    wo_free(path);
}

int main(void) {
    RUN(test_unchanged_reused);
    RUN(test_changes_detected);
    RUN(test_recent_files_not_remembered);
    RUN(test_bad_cache_full_verification);
    RUN(test_prune_and_facts);
    RUN(test_write_json_if_changed);
    return wo_test_finish();
}
