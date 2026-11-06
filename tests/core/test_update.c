#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_update.h"
#include "core/wo_zip.h"
#include "wo_test.h"

typedef struct PackageFile {
    const char *path;
    const char *content;
} PackageFile;

static void put(const char *root, const char *relative, const char *content) {
    WoError error = {0};
    char *path = wo_path_child(root, relative, &error);
    CHECK_OK(wo_write_file(path, content, strlen(content), &error), &error);
    wo_free(path);
}

static char *read_back(const char *root, const char *relative) {
    WoError error = {0};
    char *path = wo_path_child(root, relative, &error);
    char *text = NULL;
    if (path && wo_is_file(path)) {
        wo_read_text(path, 1 << 20, &text, &error);
    }
    wo_free(path);
    return text;
}

static WoJson *inventory_json(const char *version, const PackageFile *files, size_t count) {
    WoJson *inventory = wo_json_new_object();
    wo_json_put_str(inventory, "version", version);
    WoJson *list = wo_json_new_array();
    for (size_t i = 0; i < count; i++) {
        char digest[WO_SHA256_HEX_SIZE];
        WoError error = {0};
        CHECK_OK(wo_sha256_mem(files[i].content, strlen(files[i].content), digest, &error), &error);
        WoJson *entry = wo_json_new_object();
        wo_json_put_str(entry, "path", files[i].path);
        wo_json_put_int(entry, "bytes", (int64_t)strlen(files[i].content));
        wo_json_put_str(entry, "sha256", digest);
        wo_json_push(list, entry);
    }
    wo_json_put(inventory, "files", list);
    return inventory;
}

static void write_package(const char *zip_path, const char *version, const PackageFile *files,
                          size_t count, const char *extra_name) {
    WoError error = {0};
    WoJson *inventory = inventory_json(version, files, count);
    WoBuf text = {0};
    CHECK_OK(wo_json_write(inventory, true, &text, &error), &error);
    WoZipWriter *writer = NULL;
    CHECK_OK(wo_zip_writer_begin(1767225600, &writer, &error), &error);
    for (size_t i = 0; i < count; i++) {
        CHECK_OK(wo_zip_writer_add(writer, files[i].path, files[i].content, strlen(files[i].content),
                                   &error),
                 &error);
    }
    if (extra_name) {
        CHECK_OK(wo_zip_writer_add(writer, extra_name, "x", 1, &error), &error);
    }
    CHECK_OK(wo_zip_writer_add(writer, "release-files.json", text.data, text.len, &error), &error);
    CHECK_OK(wo_zip_writer_finish(writer, zip_path, &error), &error);
    wo_buf_free(&text);
    wo_json_free(inventory);
}

static const PackageFile old_files[] = {
    {"Setup.exe", "old setup"},
    {"Play.exe", "old play"},
    {"release.url", "https://github.com/ninjadynamics/wo4k/releases/latest"},
    {"runtime/wohr.dll", "old runtime"},
    {"manifests/patches.json", "{}"},
    {"scripts/Core.ps1", "old script"},
    {"scripts/Modified.ps1", "old script 2"},
    {"Setup.ps1", "old launcher script"},
};

static const PackageFile new_files[] = {
    {"Setup.exe", "new setup"},
    {"Play.exe", "old play"},
    {"release.url", "https://github.com/other/fork/releases/latest"},
    {"runtime/wohr.dll", "new runtime"},
    {"runtime/wo4k-video.exe", "video player"},
    {"manifests/patches.json", "{}"},
};

static void prepare_old_install(const char *root) {
    for (size_t i = 0; i < WO_COUNT(old_files); i++) {
        put(root, old_files[i].path, old_files[i].content);
    }
    /* The user customised one retired script; it must survive cleanup. */
    put(root, "scripts/Modified.ps1", "edited by the user");
    WoJson *inventory = inventory_json("0.1.0-beta.24", old_files, WO_COUNT(old_files));
    WoError error = {0};
    char *path = wo_path_join(root, "release-files.json", &error);
    CHECK_OK(wo_write_json(path, inventory, &error), &error);
    wo_free(path);
    wo_json_free(inventory);
}

static void test_update_and_cleanup(void) {
    const char *root = wo_test_dir("update-install");
    const char *downloads = wo_test_dir("update-downloads");
    if (!root || !downloads) {
        return;
    }
    prepare_old_install(root);
    WoError error = {0};
    char *zip = wo_path_join(downloads, "wo4k-0.2.0.zip", &error);
    write_package(zip, "0.2.0", new_files, WO_COUNT(new_files), NULL);

    WoMod mod;
    CHECK_OK(wo_mod_init(&mod, root, &error), &error);
    WoUpdateResult result;
    CHECK_OK(wo_update_from_zip(&mod, zip, NULL, NULL, &result, &error), &error);
    CHECK(result.outcome == WO_UPDATE_INSTALLED);
    CHECK_STR(result.version, "0.2.0");
    /* Setup.exe, wohr.dll, wo4k-video.exe and release-files.json changed. */
    CHECK(result.files_changed == 4);

    char *text = read_back(root, "Setup.exe");
    CHECK_STR(text, "new setup");
    wo_free(text);
    text = read_back(root, "runtime/wo4k-video.exe");
    CHECK_STR(text, "video player");
    wo_free(text);
    text = read_back(root, "release.url");
    CHECK_STR(text, "https://github.com/ninjadynamics/wo4k/releases/latest");
    wo_free(text);
    /* Replaced programs were renamed aside first. */
    text = read_back(root, "Setup.exe.wo4k-old");
    CHECK_STR(text, "old setup");
    wo_free(text);
    wo_update_remove_replaced(&mod);
    char *aside = wo_path_join(root, "Setup.exe.wo4k-old", &error);
    CHECK(!wo_exists(aside));
    wo_free(aside);

    /* Retired, unchanged files go; edited ones stay. */
    int removed = 0;
    int kept = 0;
    CHECK_OK(wo_cleanup_retired(&mod, &removed, &kept, &error), &error);
    CHECK(removed == 2 && kept == 1);
    char *script = wo_path_join(root, "scripts/Core.ps1", &error);
    CHECK(!wo_exists(script));
    wo_free(script);
    char *launcher = wo_path_join(root, "Setup.ps1", &error);
    CHECK(!wo_exists(launcher));
    wo_free(launcher);
    text = read_back(root, "scripts/Modified.ps1");
    CHECK_STR(text, "edited by the user");
    wo_free(text);
    CHECK_OK(wo_cleanup_retired(&mod, &removed, &kept, &error), &error);
    CHECK(removed == 0);

    /* Re-applying the same release changes nothing but the inventory. */
    CHECK_OK(wo_update_from_zip(&mod, zip, NULL, NULL, &result, &error), &error);
    CHECK(result.files_changed == 1);

    wo_mod_free(&mod);
    wo_free(zip);
}

static void test_rejected_packages(void) {
    const char *root = wo_test_dir("update-reject");
    const char *downloads = wo_test_dir("update-reject-downloads");
    if (!root || !downloads) {
        return;
    }
    prepare_old_install(root);
    WoError error = {0};
    WoMod mod;
    CHECK_OK(wo_mod_init(&mod, root, &error), &error);
    WoUpdateResult result;

    char *undeclared = wo_path_join(downloads, "undeclared.zip", &error);
    write_package(undeclared, "0.2.0", new_files, WO_COUNT(new_files), "docs/extra.md");
    CHECK_FAILS(wo_update_from_zip(&mod, undeclared, NULL, NULL, &result, &error), &error,
                WO_ERR_FORMAT);

    PackageFile scripted[WO_COUNT(new_files) + 1];
    memcpy(scripted, new_files, sizeof(new_files));
    scripted[WO_COUNT(new_files)] = (PackageFile){"scripts/Evil.ps1", "x"};
    char *with_script = wo_path_join(downloads, "script.zip", &error);
    write_package(with_script, "0.2.0", scripted, WO_COUNT(scripted), NULL);
    CHECK_FAILS(wo_update_from_zip(&mod, with_script, NULL, NULL, &result, &error), &error,
                WO_ERR_UNSAFE);

    PackageFile incomplete[] = {{"Setup.exe", "x"}, {"Play.exe", "y"}};
    char *missing = wo_path_join(downloads, "incomplete.zip", &error);
    write_package(missing, "0.2.0", incomplete, WO_COUNT(incomplete), NULL);
    CHECK_FAILS(wo_update_from_zip(&mod, missing, NULL, NULL, &result, &error), &error,
                WO_ERR_FORMAT);

    char *older = wo_path_join(downloads, "older.zip", &error);
    write_package(older, "0.1.0-beta.3", new_files, WO_COUNT(new_files), NULL);
    CHECK_OK(wo_update_from_zip(&mod, older, NULL, NULL, &result, &error), &error);
    CHECK(result.outcome == WO_UPDATE_NOT_NEWER && result.files_changed == 0);
    char *text = read_back(root, "Setup.exe");
    CHECK_STR(text, "old setup");
    wo_free(text);

    wo_free(older);
    wo_free(missing);
    wo_free(with_script);
    wo_free(undeclared);
    wo_mod_free(&mod);
}

int main(void) {
    RUN(test_update_and_cleanup);
    RUN(test_rejected_packages);
    return wo_test_finish();
}
