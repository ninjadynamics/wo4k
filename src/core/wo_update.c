#include "core/wo_update.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_log.h"
#include "core/wo_net.h"
#include "core/wo_release.h"
#include "core/wo_zip.h"

#define METADATA_MAX_BYTES (4u << 20)
#define PACKAGE_MAX_BYTES (256ull << 20)
#define PACKAGE_MAX_EXTRACTED (512ull << 20)
#define PACKAGE_MAX_ENTRIES 2000
#define REPLACED_SUFFIX ".wo4k-old"

static const char *const signed_binaries[] = {"Setup.exe", "Play.exe", "runtime/wohr.dll",
                                              "runtime/wo2097-controller.exe",
                                              "runtime/wo4k-video.exe"};
static const char *const required_files[] = {"Setup.exe", "Play.exe", "runtime/wohr.dll",
                                             "manifests/patches.json"};

static void report(const WoUpdateCallbacks *callbacks, int percent, const char *message) {
    WoJson *data = wo_json_new_object();
    wo_json_put_int(data, "percent", percent);
    wo_json_put_str(data, "message", message);
    wo_log_event("update_progress", data);
    if (callbacks && callbacks->status) {
        callbacks->status(callbacks->context, percent, message);
    }
}

static bool cancelled(const WoUpdateCallbacks *callbacks, WoError *error) {
    if (callbacks && callbacks->cancel && *callbacks->cancel) {
        return !wo_fail(error, WO_ERR_CANCELLED, "The update was cancelled.");
    }
    return false;
}

typedef struct DownloadProgress {
    const WoUpdateCallbacks *callbacks;
    int last_percent;
} DownloadProgress;

static void download_progress(void *context, uint64_t received, uint64_t total) {
    DownloadProgress *progress = context;
    if (total == 0) {
        return;
    }
    int percent = 20 + (int)(received * 30 / total);
    if (percent != progress->last_percent && progress->callbacks && progress->callbacks->status) {
        progress->last_percent = percent;
        char message[96];
        snprintf(message, sizeof(message), "Downloading the latest mod release (%llu of %llu MB)...",
                 (unsigned long long)(received >> 20), (unsigned long long)(total >> 20));
        progress->callbacks->status(progress->callbacks->context, percent, message);
    }
}

static char *read_release_link(const WoMod *mod, WoError *error) {
    char *path = wo_mod_path(mod, "release.url", error);
    char *text = NULL;
    char *link = NULL;
    if (path && wo_is_file(path) && wo_read_text(path, 4096, &text, NULL)) {
        link = wo_link_from_text(text, WO_LINK_RELEASE);
    }
    wo_free(text);
    wo_free(path);
    if (!link) {
        wo_fail(error, WO_ERR_NOT_FOUND,
                "Update is unavailable: release.url is missing, empty or invalid.");
    }
    return link;
}

/* Find the package URL and the checksum it must match (if published). */
static bool resolve_download(const char *endpoint, const WoNetProgress *net, char **url,
                             char expected[65], WoError *error) {
    expected[0] = 0;
    if (wo_release_endpoint_is_zip(endpoint)) {
        *url = wo_strdup(endpoint, error);
        return *url != NULL;
    }
    WoBuf body = {0};
    WoJson *release = NULL;
    WoReleaseAsset asset = {0};
    bool ok = wo_http_get(endpoint, "application/vnd.github+json", METADATA_MAX_BYTES, 60, net,
                          &body, error) &&
              wo_json_parse((const char *)body.data, body.len, &release, error) &&
              wo_release_pick_asset(release, &asset, error);
    if (ok && asset.sha256) {
        memcpy(expected, asset.sha256, 65);
    }
    if (ok && asset.checksum_url) {
        WoBuf sidecar = {0};
        char sidecar_digest[65];
        ok = wo_http_get(asset.checksum_url, NULL, 4096, 60, net, &sidecar, error) &&
             wo_release_parse_checksum((const char *)sidecar.data, sidecar_digest, error);
        if (ok && expected[0] && !wo_str_eq(expected, sidecar_digest)) {
            ok = wo_fail(error, WO_ERR_CHECKSUM, "Release checksums disagree.");
        }
        if (ok) {
            memcpy(expected, sidecar_digest, 65);
        }
        wo_buf_free(&sidecar);
    }
    if (ok) {
        *url = wo_strdup(asset.url, error);
        ok = *url != NULL;
    }
    wo_release_asset_free(&asset);
    wo_json_free(release);
    wo_buf_free(&body);
    return ok;
}

/* Extract and verify a downloaded package into stage: every ZIP entry must
 * be declared in release-files.json with matching size and hash. */
static bool verify_package(const char *zip_path, const char *stage, WoInventory *inventory,
                           WoError *error) {
    WoZip *zip = NULL;
    if (!wo_zip_open(zip_path, &zip, error)) {
        return false;
    }
    WoZipLimits limits = {PACKAGE_MAX_EXTRACTED, PACKAGE_MAX_ENTRIES, true, false};
    bool ok = wo_zip_extract_all(zip, stage, &limits, error);
    size_t file_entries = 0;
    for (size_t i = 0; ok && i < wo_zip_count(zip); i++) {
        if (!wo_zip_entry(zip, i)->is_dir) {
            file_entries++;
        }
    }
    wo_zip_close(zip);
    char *inventory_path = ok ? wo_path_join(stage, "release-files.json", error) : NULL;
    WoJson *json = NULL;
    ok = inventory_path != NULL;
    if (ok && !wo_is_file(inventory_path)) {
        ok = wo_fail(error, WO_ERR_FORMAT, "The release ZIP has no file inventory.");
    }
    ok = ok && wo_read_json(inventory_path, METADATA_MAX_BYTES, &json, error) &&
         wo_inventory_parse(json, inventory, error);
    wo_json_free(json);
    wo_free(inventory_path);
    if (!ok) {
        return false;
    }
    if (file_entries != inventory->count + 1) {
        wo_inventory_free(inventory);
        return wo_fail(error, WO_ERR_FORMAT, "The release ZIP contains undeclared files.");
    }
    for (size_t i = 0; ok && i < inventory->count; i++) {
        const WoInventoryFile *file = &inventory->files[i];
        char *path = wo_path_child(stage, file->path, error);
        WoFileInfo info;
        char digest[WO_SHA256_HEX_SIZE];
        ok = path && wo_stat(path, &info, error);
        if (ok && (!info.exists || info.is_dir || info.size != file->bytes)) {
            ok = wo_fail(error, WO_ERR_CHECKSUM, "Release checksum failed: %s", file->path);
        }
        ok = ok && wo_sha256_file(path, digest, error);
        if (ok && !wo_digest_eq(digest, file->sha256)) {
            ok = wo_fail(error, WO_ERR_CHECKSUM, "Release checksum failed: %s", file->path);
        }
        wo_free(path);
    }
    for (size_t i = 0; ok && i < WO_COUNT(required_files); i++) {
        if (!wo_inventory_find(inventory, required_files[i])) {
            ok = wo_fail(error, WO_ERR_FORMAT, "Incomplete release: %s", required_files[i]);
        }
    }
    if (!ok) {
        wo_inventory_free(inventory);
    }
    return ok;
}

/* A signed WO4K may only be replaced by binaries from the same publisher. */
static bool verify_publisher(const char *running_exe, const char *stage,
                             const WoInventory *inventory, WoError *error) {
    WoSignature current;
    if (!running_exe || !wo_signature_check(running_exe, &current, error)) {
        return running_exe == NULL;
    }
    if (!current.trusted) {
        wo_log_text("update_signature_policy", "This copy is unsigned; publisher check skipped.");
        return true;
    }
    for (size_t i = 0; i < WO_COUNT(signed_binaries); i++) {
        if (!wo_inventory_find(inventory, signed_binaries[i])) {
            continue;
        }
        char *path = wo_path_child(stage, signed_binaries[i], error);
        WoSignature next;
        bool ok = path && wo_signature_check(path, &next, error);
        wo_free(path);
        if (!ok) {
            return false;
        }
        if (!next.trusted || !wo_str_eq(next.subject, current.subject)) {
            return wo_fail(error, WO_ERR_UNSAFE,
                           "The downloaded update is not signed by the same publisher as this "
                           "copy of WO4K (%s).",
                           signed_binaries[i]);
        }
    }
    return true;
}

static bool installed_version(const WoMod *mod, char **version, WoError *error) {
    *version = NULL;
    char *path = wo_mod_path(mod, "release-files.json", error);
    WoJson *json = NULL;
    bool ok = path != NULL;
    if (ok && wo_is_file(path) && wo_read_json(path, METADATA_MAX_BYTES, &json, NULL)) {
        const char *text = wo_json_str(json, "version", NULL);
        if (text) {
            *version = wo_strdup(text, error);
        }
    }
    wo_json_free(json);
    wo_free(path);
    return ok;
}

static bool is_program(const char *path) {
    return wo_str_iends_with(path, ".exe") || wo_str_iends_with(path, ".dll");
}

typedef struct Change {
    const char *relative;
    const char *sha256; /* expected digest; NULL for release-files.json itself */
    char *source;
    char *target;
    char *backup;       /* copy of the previous file, or NULL when it did not exist */
    bool moved;         /* a running program was renamed aside */
    bool touched;       /* the target may have been modified */
} Change;

static void free_changes(Change *changes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        wo_free(changes[i].source);
        wo_free(changes[i].target);
        wo_free(changes[i].backup);
    }
    wo_free(changes);
}

static bool replace_one(Change *change, WoError *error) {
    if (change->backup && is_program(change->target)) {
        /* Programs may be running (Setup.exe is): rename them aside; the
         * restarted Setup removes the old copy. */
        char *aside = wo_format(error, "%s" REPLACED_SUFFIX, change->target);
        bool ok = aside && wo_delete_file(aside, error) && wo_move(change->target, aside, error);
        wo_free(aside);
        if (!ok) {
            return false;
        }
        change->moved = true;
    }
    change->touched = true;
    bool ok = wo_copy_file(change->source, change->target, error);
    if (ok && change->sha256) {
        char digest[WO_SHA256_HEX_SIZE];
        ok = wo_sha256_file(change->target, digest, error);
        if (ok && !wo_digest_eq(digest, change->sha256)) {
            ok = wo_fail(error, WO_ERR_CHECKSUM, "Updated file verification failed: %s",
                         change->relative);
        }
    }
    return ok;
}

static void roll_back(Change *changes, size_t count) {
    for (size_t i = count; i > 0; i--) {
        Change *change = &changes[i - 1];
        if (!change->touched && !change->moved) {
            continue;
        }
        WoError error = {0};
        bool ok;
        if (change->backup) {
            ok = wo_copy_file(change->backup, change->target, &error);
        } else {
            ok = wo_delete_file(change->target, &error);
        }
        if (!ok) {
            wo_log_failure("Rolling back an update", &error);
        }
    }
}

static bool save_ownership(const WoMod *mod, WoError *error) {
    /* Keep the inventory that described the files before this update, so
     * cleanup can later tell which files the old release owned. */
    WoJson *existing = NULL;
    if (!wo_state_load(mod, "release-inventory.json", &existing, error)) {
        return false;
    }
    bool ok = true;
    if (!existing) {
        char *path = wo_mod_path(mod, "release-files.json", error);
        WoJson *current = NULL;
        ok = path != NULL;
        if (ok && wo_is_file(path)) {
            ok = wo_read_json(path, METADATA_MAX_BYTES, &current, error) &&
                 wo_state_save(mod, "release-inventory.json", current, error);
        }
        wo_json_free(current);
        wo_free(path);
    }
    wo_json_free(existing);
    return ok;
}

static bool install_files(const WoMod *mod, const char *stage, const WoInventory *inventory,
                          size_t *changed, WoError *error) {
    *changed = 0;
    if (wo_mod_game_running(mod)) {
        return wo_fail(error, WO_ERR_BUSY, "Close the game before updating the mod.");
    }
    char stamp[32];
    time_t now = time(NULL);
    struct tm local;
    localtime_s(&local, &now);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    char *backup_name = wo_format(error, "backups/update-%s", stamp);
    char *backup_root = backup_name ? wo_mod_path(mod, backup_name, error) : NULL;
    wo_free(backup_name);
    /* The inventory itself is committed last. */
    size_t total = inventory->count + 1;
    Change *changes = backup_root ? wo_alloc_array(total, sizeof(Change), error) : NULL;
    size_t count = 0;
    bool ok = changes != NULL;
    for (size_t i = 0; ok && i < total; i++) {
        bool is_inventory = i == inventory->count;
        const char *relative = is_inventory ? "release-files.json" : inventory->files[i].path;
        const char *expected = is_inventory ? NULL : inventory->files[i].sha256;
        char *target = wo_mod_path(mod, relative, error);
        char *source = target ? wo_path_child(stage, relative, error) : NULL;
        if (!source) {
            wo_free(target);
            ok = false;
            break;
        }
        if (wo_chain_has_link(mod->root, target)) {
            ok = wo_fail(error, WO_ERR_UNSAFE, "An update path contains a filesystem link: %s",
                         relative);
        }
        /* Keep the user's own link configuration. */
        bool exists = wo_is_file(target);
        bool keep = exists && (wo_str_ieq(relative, "release.url") ||
                               wo_str_ieq(relative, "source.url"));
        char digest[WO_SHA256_HEX_SIZE] = "";
        if (ok && exists && expected && !keep) {
            ok = wo_sha256_file(target, digest, error);
        }
        bool unchanged = exists && expected && wo_digest_eq(digest, expected);
        if (!ok || keep || unchanged) {
            wo_free(target);
            wo_free(source);
            continue;
        }
        Change *change = &changes[count++];
        change->relative = relative;
        change->sha256 = expected;
        change->source = source;
        change->target = target;
        if (exists) {
            change->backup = wo_path_child(backup_root, relative, error);
            ok = change->backup && wo_copy_file(target, change->backup, error);
        }
    }
    ok = ok && save_ownership(mod, error);
    for (size_t i = 0; ok && i < count; i++) {
        ok = replace_one(&changes[i], error);
    }
    if (!ok && changes) {
        wo_error_context(error, "The update was rolled back: ");
        roll_back(changes, count);
    }
    if (ok) {
        wo_mod_prune(mod);
    }
    *changed = count;
    free_changes(changes, count);
    wo_free(backup_root);
    return ok;
}

static bool apply_package(const WoMod *mod, const char *zip_path, const char *session,
                          const char *running_exe, const WoUpdateCallbacks *callbacks,
                          WoUpdateResult *result, bool *keep_session, WoError *error);

static bool update_run_locked(const WoMod *mod, const char *running_exe,
                              const WoUpdateCallbacks *callbacks, WoUpdateResult *result,
                              WoError *error);
static bool update_zip_locked(const WoMod *mod, const char *zip_path, const char *running_exe,
                              const WoUpdateCallbacks *callbacks, WoUpdateResult *result,
                              WoError *error);

bool wo_update_run(const WoMod *mod, const char *running_exe, const WoUpdateCallbacks *callbacks,
                   WoUpdateResult *result, WoError *error) {
    WoModLock lock;
    memset(result, 0, sizeof(*result));
    if (!wo_mod_lock(mod, 0, &lock, error)) {
        return false;
    }
    bool ok = update_run_locked(mod, running_exe, callbacks, result, error);
    wo_mod_unlock(&lock);
    return ok;
}

bool wo_update_from_zip(const WoMod *mod, const char *zip_path, const char *running_exe,
                        const WoUpdateCallbacks *callbacks, WoUpdateResult *result,
                        WoError *error) {
    WoModLock lock;
    memset(result, 0, sizeof(*result));
    if (!wo_mod_lock(mod, 0, &lock, error)) {
        return false;
    }
    bool ok = update_zip_locked(mod, zip_path, running_exe, callbacks, result, error);
    wo_mod_unlock(&lock);
    return ok;
}

static bool update_run_locked(const WoMod *mod, const char *running_exe,
                              const WoUpdateCallbacks *callbacks, WoUpdateResult *result,
                              WoError *error) {
    report(callbacks, 5, "Checking the latest release...");
    char *link = read_release_link(mod, error);
    char *endpoint = link ? wo_release_endpoint(link) : NULL;
    wo_free(link);
    if (!endpoint) {
        if (!wo_error_is_set(error)) {
            wo_fail(error, WO_ERR_NOT_FOUND, "Update is unavailable: release.url is invalid.");
        }
        return false;
    }
    char *session = wo_mod_new_work_dir(mod, "update-", error);
    DownloadProgress progress = {callbacks, -1};
    WoNetProgress net = {download_progress, &progress, callbacks ? callbacks->cancel : NULL};
    WoNetProgress quiet = {NULL, NULL, callbacks ? callbacks->cancel : NULL};
    char *url = NULL;
    char expected[65] = "";
    char *zip_path = session ? wo_path_join(session, "release.zip", error) : NULL;
    bool keep_session = false;
    bool ok = zip_path && resolve_download(endpoint, &quiet, &url, expected, error) &&
              !cancelled(callbacks, error);
    if (ok) {
        report(callbacks, 20, "Downloading the latest mod release...");
        ok = wo_http_download(url, NULL, zip_path, PACKAGE_MAX_BYTES, 900, &net, error);
    }
    if (ok && expected[0]) {
        char digest[WO_SHA256_HEX_SIZE];
        ok = wo_sha256_file(zip_path, digest, error);
        if (ok && !wo_digest_eq(digest, expected)) {
            ok = wo_fail(error, WO_ERR_CHECKSUM, "Downloaded ZIP checksum failed.");
        }
    }
    ok = ok && apply_package(mod, zip_path, session, running_exe, callbacks, result, &keep_session,
                             error);
    if (session && !keep_session) {
        wo_mod_remove_work_dir(mod, session);
    }
    wo_free(zip_path);
    wo_free(url);
    wo_free(session);
    wo_free(endpoint);
    return ok;
}

static bool update_zip_locked(const WoMod *mod, const char *zip_path, const char *running_exe,
                              const WoUpdateCallbacks *callbacks, WoUpdateResult *result,
                              WoError *error) {
    char *session = wo_mod_new_work_dir(mod, "update-", error);
    bool keep_session = false;
    bool ok = session && apply_package(mod, zip_path, session, running_exe, callbacks, result,
                                       &keep_session, error);
    if (session && !keep_session) {
        wo_mod_remove_work_dir(mod, session);
    }
    wo_free(session);
    return ok;
}

/* Verify a release ZIP, then install it unless it is older than the
 * installed release. */
static bool apply_package(const WoMod *mod, const char *zip_path, const char *session,
                          const char *running_exe, const WoUpdateCallbacks *callbacks,
                          WoUpdateResult *result, bool *keep_session, WoError *error) {
    char *stage = wo_path_join(session, "package", error);
    WoInventory inventory = {0};
    bool ok = stage != NULL && !cancelled(callbacks, error);
    if (ok) {
        report(callbacks, 55, "Verifying the release package...");
        ok = verify_package(zip_path, stage, &inventory, error) &&
             verify_publisher(running_exe, stage, &inventory, error);
    }
    char *current = NULL;
    int order = 1;
    if (ok) {
        ok = installed_version(mod, &current, error);
        if (ok && current && !wo_version_compare(inventory.version, current, &order)) {
            order = 1; /* an unreadable local version never blocks an update */
        }
        wo_copy_str(result->version, sizeof(result->version), inventory.version);
    }
    if (ok && order < 0) {
        result->outcome = WO_UPDATE_NOT_NEWER;
        report(callbacks, 100, "This WO4K is newer than the latest release; nothing was changed.");
    } else if (ok) {
        /* The installed executable must still be supported by the new
         * release, otherwise Install / repair with the original source is
         * needed first. */
        char *exe = wo_mod_game_path(mod, "Wipeout2097.exe", error);
        if (exe && wo_is_file(exe)) {
            char *manifest_path = wo_path_child(stage, "manifests/patches.json", error);
            WoJson *manifest = NULL;
            char digest[WO_SHA256_HEX_SIZE];
            ok = manifest_path && wo_read_json(manifest_path, METADATA_MAX_BYTES, &manifest, error) &&
                 wo_sha256_file(exe, digest, error);
            if (ok && !wo_digest_eq(digest, wo_json_str(manifest, "output_sha256", ""))) {
                *keep_session = true;
                ok = wo_fail(error, WO_ERR_UNSUPPORTED,
                             "This release requires Install / repair with your original game "
                             "source. The downloaded ZIP is kept in %s",
                             session);
            }
            wo_json_free(manifest);
            wo_free(manifest_path);
        }
        wo_free(exe);
        if (ok && !cancelled(callbacks, error)) {
            report(callbacks, 75, "Installing the update; keeping your game files and preferences...");
            ok = install_files(mod, stage, &inventory, &result->files_changed, error);
        } else {
            ok = false;
        }
        if (ok) {
            result->outcome = WO_UPDATE_INSTALLED;
            report(callbacks, 90, "Update installed. Restarting Setup to finish...");
        }
    }
    wo_free(current);
    wo_inventory_free(&inventory);
    wo_free(stage);
    return ok;
}

void wo_update_remove_replaced(const WoMod *mod) {
    static const char *const folders[] = {"", "runtime"};
    for (size_t f = 0; f < WO_COUNT(folders); f++) {
        char *dir = folders[f][0] ? wo_mod_path(mod, folders[f], NULL) : wo_strdup(mod->root, NULL);
        WoDirList list = {0};
        if (dir && wo_list_dir(dir, &list, NULL)) {
            for (size_t i = 0; i < list.count; i++) {
                if (!list.items[i].is_dir && wo_str_iends_with(list.items[i].name, REPLACED_SUFFIX)) {
                    char *path = wo_path_join(dir, list.items[i].name, NULL);
                    WoError error = {0};
                    if (path && !wo_delete_file(path, &error)) {
                        wo_log_text("replaced_file_retained", error.message);
                    }
                    wo_free(path);
                }
            }
        }
        wo_dirlist_free(&list);
        wo_free(dir);
    }
}

/* --- Retired files ------------------------------------------------------------- */

/* Earlier releases kept their PowerShell implementation here; files under
 * these folders (or with these names) may be removed once retired. */
static const char *const owned_prefixes[] = {"scripts/", "src/",      "runtime/", "assets/",
                                             "manifests/", "licenses/", "docs/",  "tests/",
                                             "patches/"};
static const char *const owned_top_level[] = {"Setup.ps1", "Play.ps1",          "compile.ps1",
                                              "Makefile",  "HOW-TO-COMPILE.md", ".gitignore"};

static bool owned_by_releases(const char *path) {
    for (size_t i = 0; i < WO_COUNT(owned_prefixes); i++) {
        if (wo_str_istarts_with(path, owned_prefixes[i])) {
            return true;
        }
    }
    for (size_t i = 0; i < WO_COUNT(owned_top_level); i++) {
        if (wo_str_ieq(path, owned_top_level[i])) {
            return true;
        }
    }
    return false;
}

static bool lenient_path_ok(const char *path) {
    bool safe = false;
    char *normal = wo_zip_normalise_name(path, true, &safe, NULL);
    bool same = normal && wo_str_eq(normal, path);
    wo_free(normal);
    return safe && same;
}

static bool listed_in(const WoJson *inventory, const char *path) {
    const WoJson *files = wo_json_get(inventory, "files");
    for (size_t i = 0; i < wo_json_length(files); i++) {
        if (wo_str_ieq(wo_json_str(wo_json_at(files, i), "path", ""), path)) {
            return true;
        }
    }
    return false;
}

static void remove_empty_parents(const WoMod *mod, const char *path) {
    char *parent = wo_path_parent(path, NULL);
    while (parent && wo_path_is_within(mod->root, parent, false) && !wo_chain_has_link(mod->root, parent) &&
           wo_is_dir(parent) && wo_dir_is_empty(parent)) {
        if (!wo_delete_empty_dir(parent, NULL)) {
            break;
        }
        char *next = wo_path_parent(parent, NULL);
        wo_free(parent);
        parent = next;
    }
    wo_free(parent);
}

static void retire(const WoMod *mod, const char *relative, const char *digest, int *removed,
                   int *kept) {
    char *path = wo_mod_path(mod, relative, NULL);
    if (!path || wo_chain_has_link(mod->root, path) || !wo_is_file(path)) {
        wo_free(path);
        return;
    }
    char actual[WO_SHA256_HEX_SIZE];
    WoError error = {0};
    if (!wo_sha256_file(path, actual, &error) || !wo_digest_eq(actual, digest)) {
        (*kept)++;
    } else if (wo_delete_file(path, &error)) {
        (*removed)++;
        remove_empty_parents(mod, path);
    } else {
        wo_log_text("retired_file_kept", error.message);
    }
    wo_free(path);
}

bool wo_cleanup_retired(const WoMod *mod, int *removed, int *kept, WoError *error) {
    *removed = 0;
    *kept = 0;
    char *current_path = wo_mod_path(mod, "release-files.json", error);
    if (!current_path) {
        return false;
    }
    if (!wo_is_file(current_path)) {
        wo_free(current_path);
        return true;
    }
    WoJson *current = NULL;
    WoJson *previous = NULL;
    bool ok = wo_read_json(current_path, METADATA_MAX_BYTES, &current, error) &&
              wo_state_load(mod, "release-inventory.json", &previous, error);
    const WoJson *files = wo_json_get(previous, "files");
    for (size_t i = 0; ok && i < wo_json_length(files); i++) {
        const WoJson *entry = wo_json_at(files, i);
        const char *relative = wo_json_str(entry, "path", "");
        const char *digest = wo_json_str(entry, "sha256", "");
        if (!lenient_path_ok(relative)) {
            ok = wo_fail(error, WO_ERR_UNSAFE, "Invalid path in the saved release inventory.");
            break;
        }
        if (listed_in(current, relative) || wo_str_ieq(relative, "release.url") ||
            wo_str_ieq(relative, "source.url") || !owned_by_releases(relative) ||
            !wo_is_hex_digest(digest)) {
            continue;
        }
        retire(mod, relative, digest, removed, kept);
        /* Runtime files were also deployed into the game folder. */
        const char *name = relative + strlen("runtime/");
        if (wo_str_istarts_with(relative, "runtime/") && !strchr(name, '/') &&
            (wo_str_iends_with(name, ".dll") || wo_str_iends_with(name, ".exe"))) {
            char *deployed = wo_format(NULL, "wobble/%s", name);
            if (deployed) {
                retire(mod, deployed, digest, removed, kept);
            }
            wo_free(deployed);
        }
    }
    ok = ok && wo_state_save(mod, "release-inventory.json", current, error);
    wo_json_free(previous);
    wo_json_free(current);
    wo_free(current_path);
    return ok;
}
