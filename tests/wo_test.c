#include "wo_test.h"

#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"

static int failures;
static int checks_failed_in_test;
static const char *current_test = "";
static WoStrList owned_paths;

void wo_test_fail(const char *file, int line, const char *expression, const char *detail) {
    failures++;
    checks_failed_in_test++;
    fprintf(stderr, "  FAIL %s (%s:%d): %s", current_test, file, line, expression);
    if (detail && *detail) {
        fprintf(stderr, " -- %s", detail);
    }
    fputc('\n', stderr);
}

void wo_test_run(const char *name, void (*test)(void)) {
    current_test = name;
    checks_failed_in_test = 0;
    test();
    printf("  %s %s\n", checks_failed_in_test ? "FAIL" : "ok  ", name);
    fflush(stdout);
}

int wo_test_finish(void) {
    wo_strlist_free(&owned_paths);
    if (failures) {
        fprintf(stderr, "%d check(s) failed.\n", failures);
        return 1;
    }
    return 0;
}

bool wo_test_str_eq(const char *a, const char *b) {
    if (!a || !b) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

const char *wo_test_dir(const char *name) {
    WoError error = {0};
    char *base = wo_path_full("build/test-work", &error);
    char *dir = base ? wo_path_join(base, name, &error) : NULL;
    bool ok = dir && wo_mkdirs(base, &error) && wo_delete_tree(base, dir, &error) &&
              wo_mkdirs(dir, &error);
    wo_free(base);
    if (!ok) {
        wo_test_fail(__FILE__, __LINE__, "wo_test_dir", error.message);
        wo_free(dir);
        return NULL;
    }
    wo_strlist_push_owned(&owned_paths, dir, NULL);
    return dir;
}

const char *wo_test_fixture(const char *relative) {
    WoError error = {0};
    char *path = wo_path_child("tests/fixtures", relative, &error);
    if (!path) {
        wo_test_fail(__FILE__, __LINE__, "wo_test_fixture", error.message);
        return NULL;
    }
    wo_strlist_push_owned(&owned_paths, path, NULL);
    return path;
}
