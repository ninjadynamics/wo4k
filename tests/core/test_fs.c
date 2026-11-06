#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "wo_test.h"

static void test_child_paths(void) {
    WoError error = {0};
    char *root = wo_path_full("build/test-work/root", &error);
    CHECK(root != NULL);
    char *child = wo_path_child(root, "a/b/../c.txt", &error);
    CHECK(child && wo_path_is_within(root, child, false));
    CHECK(child && wo_str_iends_with(child, "\\root\\a\\c.txt"));
    wo_free(child);

    const char *bad[] = {"../x", "a/../../x", "C:\\Windows", "C:x", "\\x", "/x",
                         "file.txt:stream", "", "a/../.."};
    for (size_t i = 0; i < WO_COUNT(bad); i++) {
        char *escaped = wo_path_child(root, bad[i], &error);
        if (escaped) {
            wo_test_fail(__FILE__, __LINE__, bad[i], escaped);
            wo_free(escaped);
        } else {
            CHECK(error.kind == WO_ERR_UNSAFE);
        }
    }
    /* Prefix tricks: root2 is not inside root. */
    char *sibling = wo_path_full("build/test-work/root2/x", &error);
    CHECK(sibling && !wo_path_is_within(root, sibling, true));
    wo_free(sibling);
    wo_free(root);
}

static void test_path_parts(void) {
    WoError error = {0};
    CHECK_STR(wo_path_basename("C:\\a\\b.txt"), "b.txt");
    CHECK_STR(wo_path_basename("a/b/c"), "c");
    CHECK_STR(wo_path_extension("C:\\a.b\\file.ZIP"), ".ZIP");
    CHECK_STR(wo_path_extension("C:\\a.b\\file"), "");
    CHECK_STR(wo_path_extension(".hidden"), "");
    char *changed = wo_path_change_extension("C:\\x\\disc.mds", ".mdf", &error);
    CHECK_STR(changed, "C:\\x\\disc.mdf");
    wo_free(changed);
    char *parent = wo_path_parent("C:\\a\\b\\", &error);
    CHECK_STR(parent, "C:\\a");
    wo_free(parent);
    parent = wo_path_parent("C:\\a", &error);
    CHECK_STR(parent, "C:\\");
    wo_free(parent);
    CHECK(wo_path_parent("C:\\", &error) == NULL);
    CHECK(wo_path_is_absolute("C:\\x") && wo_path_is_absolute("\\\\server\\share"));
    CHECK(!wo_path_is_absolute("x\\y") && !wo_path_is_absolute("C:x"));
    char *relative = wo_path_relative("C:\\mod", "C:\\mod\\wobble\\a.dat", &error);
    CHECK_STR(relative, "wobble/a.dat");
    wo_free(relative);
    CHECK(wo_path_relative("C:\\mod", "C:\\modx\\a", &error) == NULL);
}

static void test_files(void) {
    const char *dir = wo_test_dir("fs-files");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *file = wo_path_join(dir, "nested/deeper/data.bin", &error);
    CHECK_OK(wo_write_file(file, "hello", 5, &error), &error);
    WoBuf buf = {0};
    CHECK_OK(wo_read_file(file, 100, &buf, &error), &error);
    CHECK(buf.len == 5 && memcmp(buf.data, "hello", 5) == 0);
    wo_buf_free(&buf);
    CHECK_FAILS(wo_read_file(file, 4, &buf, &error), &error, WO_ERR_LIMIT);
    wo_buf_free(&buf);

    CHECK_OK(wo_write_file_atomic(file, "replaced", 8, &error), &error);
    char *text = NULL;
    CHECK_OK(wo_read_text(file, 100, &text, &error), &error);
    CHECK_STR(text, "replaced");
    wo_free(text);

    char *copy = wo_path_join(dir, "copy/data.bin", &error);
    CHECK_OK(wo_copy_file(file, copy, &error), &error);
    char a[WO_SHA256_HEX_SIZE];
    char b[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_file(file, a, &error), &error);
    CHECK_OK(wo_sha256_file(copy, b, &error), &error);
    CHECK(wo_digest_eq(a, b));

    char *moved = wo_path_join(dir, "moved.bin", &error);
    CHECK_OK(wo_move(copy, moved, &error), &error);
    CHECK(!wo_exists(copy) && wo_is_file(moved));
    CHECK(!wo_move(file, moved, &error)); /* destination exists */

    CHECK_OK(wo_delete_file(moved, &error), &error);
    CHECK_OK(wo_delete_file(moved, &error), &error); /* already gone */
    CHECK(!wo_exists(moved));

    char *missing = wo_path_join(dir, "missing.txt", &error);
    CHECK_FAILS(wo_read_file(missing, 10, &buf, &error), &error, WO_ERR_NOT_FOUND);
    wo_buf_free(&buf);

    wo_free(missing);
    wo_free(moved);
    wo_free(copy);
    wo_free(file);
}

static void test_json_files(void) {
    const char *dir = wo_test_dir("fs-json");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *path = wo_path_join(dir, "state.json", &error);
    WoJson *value = wo_json_new_object();
    wo_json_put_str(value, "name", "WO4K");
    CHECK_OK(wo_write_json(path, value, &error), &error);
    wo_json_free(value);
    WoJson *read = NULL;
    CHECK_OK(wo_read_json(path, 1024, &read, &error), &error);
    CHECK_STR(wo_json_str(read, "name", NULL), "WO4K");
    wo_json_free(read);
    CHECK_OK(wo_write_file(path, "{broken", 7, &error), &error);
    CHECK_FAILS(wo_read_json(path, 1024, &read, &error), &error, WO_ERR_FORMAT);
    wo_free(path);
}

typedef struct WalkLog {
    WoStrList seen;
} WalkLog;

static bool record(void *context, const char *path, const char *relative,
                   const WoDirEntry *entry, bool *descend, WoError *error) {
    (void)path;
    WalkLog *log = context;
    if (entry->is_dir && wo_str_eq(entry->name, "skip")) {
        *descend = false;
    }
    return wo_strlist_push(&log->seen, relative, error);
}

static void test_walk_and_delete_tree(void) {
    const char *dir = wo_test_dir("fs-walk");
    if (!dir) {
        return;
    }
    WoError error = {0};
    const char *files[] = {"B/two.txt", "a/one.txt", "skip/hidden.txt", "a/c/three.txt"};
    for (size_t i = 0; i < WO_COUNT(files); i++) {
        char *path = wo_path_join(dir, files[i], &error);
        CHECK_OK(wo_write_file(path, "x", 1, &error), &error);
        wo_free(path);
    }
    WalkLog log = {0};
    CHECK_OK(wo_walk(dir, -1, record, &log, &error), &error);
    const char *expected[] = {"a", "a/c", "a/c/three.txt", "a/one.txt", "B", "B/two.txt", "skip"};
    CHECK(log.seen.count == WO_COUNT(expected));
    for (size_t i = 0; i < WO_COUNT(expected) && i < log.seen.count; i++) {
        CHECK_STR(log.seen.items[i], expected[i]);
    }
    wo_strlist_free(&log.seen);

    WalkLog shallow = {0};
    CHECK_OK(wo_walk(dir, 0, record, &shallow, &error), &error);
    CHECK(shallow.seen.count == 3);
    wo_strlist_free(&shallow.seen);

    char *outside = wo_path_full("build", &error);
    CHECK_FAILS(wo_delete_tree(dir, outside, &error), &error, WO_ERR_UNSAFE);
    CHECK_FAILS(wo_delete_tree(dir, dir, &error), &error, WO_ERR_UNSAFE);
    wo_free(outside);

    char *a = wo_path_join(dir, "a", &error);
    CHECK_OK(wo_delete_tree(dir, a, &error), &error);
    CHECK(!wo_exists(a));
    wo_free(a);
}

static void test_long_paths(void) {
    const char *dir = wo_test_dir("fs-long");
    if (!dir) {
        return;
    }
    WoError error = {0};
    WoBuf relative = {0};
    for (int i = 0; i < 12; i++) {
        wo_buf_printf(&relative, &error, "%s%s", i ? "/" : "",
                      "a-rather-long-folder-name-for-testing-long-paths");
    }
    wo_buf_append_str(&relative, "/file.txt", &error);
    wo_buf_terminate(&relative, &error);
    char *path = wo_path_join(dir, (const char *)relative.data, &error);
    CHECK(path && strlen(path) > 400);
    CHECK_OK(wo_write_file(path, "long", 4, &error), &error);
    CHECK(wo_is_file(path));
    char *top = wo_path_join(dir, "a-rather-long-folder-name-for-testing-long-paths", &error);
    CHECK_OK(wo_delete_tree(dir, top, &error), &error);
    CHECK(!wo_exists(top));
    wo_free(top);
    wo_free(path);
    wo_buf_free(&relative);
}

static void test_hash_vectors(void) {
    char hex[WO_SHA256_HEX_SIZE];
    WoError error = {0};
    CHECK_OK(wo_sha256_mem("abc", 3, hex, &error), &error);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_OK(wo_sha256_mem("", 0, hex, &error), &error);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(wo_digest_eq(hex, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"));
    CHECK(!wo_digest_eq(hex, "e3b0"));
    uint8_t random[16] = {0};
    CHECK_OK(wo_random_bytes(random, sizeof(random), &error), &error);
}

int main(void) {
    RUN(test_child_paths);
    RUN(test_path_parts);
    RUN(test_files);
    RUN(test_json_files);
    RUN(test_walk_and_delete_tree);
    RUN(test_long_paths);
    RUN(test_hash_vectors);
    return wo_test_finish();
}
