/* wo4k-build fetch: download and unpack the pinned third-party build
 * dependencies listed in manifests/dependencies.json. Each archive and a
 * marker file inside it are verified by SHA-256; nothing is installed
 * outside the dependency folder. */
#include "core/wo_win.h"

#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"
#include "core/wo_net.h"
#include "core/wo_tree.h"
#include "core/wo_zip.h"
#include "tools/build_tool.h"

static bool file_matches(const char *path, const char *expected) {
    char digest[WO_SHA256_HEX_SIZE];
    return wo_is_file(path) && wo_sha256_file(path, digest, NULL) && wo_digest_eq(digest, expected);
}

/* Windows' tar.exe unpacks .tar.zst when a zstd executable is on PATH. */
static bool untar(const char *archive, const char *destination, const char *zstd_dir,
                  WoError *error) {
    wchar_t system[MAX_PATH];
    UINT len = GetSystemDirectoryW(system, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "Cannot locate the Windows system folder.");
    }
    char *system_utf8 = wo_utf8(system, error);
    char *tar = system_utf8 ? wo_format(error, "%s\\tar.exe", system_utf8) : NULL;
    char *command = tar ? wo_format(error, "\"%s\" -xf \"%s\" -C \"%s\"", tar, archive, destination)
                        : NULL;
    wchar_t *wide_tar = command ? wo_wide(tar, error) : NULL;
    wchar_t *wide_command = wide_tar ? wo_wide(command, error) : NULL;
    bool ok = wide_command != NULL;
    if (ok && zstd_dir) {
        wchar_t old_path[32767];
        DWORD old_len = GetEnvironmentVariableW(L"PATH", old_path, 32767);
        wchar_t *wide_zstd = wo_wide(zstd_dir, error);
        ok = wide_zstd != NULL;
        if (ok) {
            size_t total = wcslen(wide_zstd) + 2 + (old_len < 32767 ? old_len : 0);
            wchar_t *new_path = wo_alloc_array(total + 1, sizeof(wchar_t), error);
            if (new_path) {
                _snwprintf(new_path, total, L"%ls;%ls", wide_zstd, old_len < 32767 ? old_path : L"");
                new_path[total] = 0;
                SetEnvironmentVariableW(L"PATH", new_path);
                wo_free(new_path);
            }
        }
        wo_free(wide_zstd);
    }
    if (ok) {
        STARTUPINFOW startup;
        memset(&startup, 0, sizeof(startup));
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process;
        memset(&process, 0, sizeof(process));
        if (!CreateProcessW(wide_tar, wide_command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL,
                            &startup, &process)) {
            ok = wo_fail_win32(error, GetLastError(), "Cannot run tar.exe");
        } else {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            if (code != 0) {
                ok = wo_fail(error, WO_ERR_IO, "tar.exe failed (%lu) for %s.", (unsigned long)code,
                             archive);
            }
        }
    }
    wo_free(wide_command);
    wo_free(wide_tar);
    wo_free(command);
    wo_free(tar);
    wo_free(system_utf8);
    return ok;
}

static char *find_zstd(const char *deps) {
    WoTreeList files = {0};
    char *found = NULL;
    if (wo_tree_list(deps, &files, NULL)) {
        for (size_t i = 0; i < files.count && !found; i++) {
            if (!files.items[i].is_dir && wo_str_ieq(wo_path_basename(files.items[i].path), "zstd.exe")) {
                found = wo_path_parent(files.items[i].path, NULL);
            }
        }
    }
    wo_tree_free(&files);
    return found;
}

bool build_fetch(const char *manifest_path, const char *deps, WoError *error) {
    WoJson *manifest = NULL;
    if (!wo_read_json(manifest_path, 1u << 20, &manifest, error)) {
        return false;
    }
    char *archives = wo_path_join(deps, "archives", error);
    bool ok = archives && wo_mkdirs(archives, error);
    for (size_t i = 0; ok && i < wo_json_length(manifest); i++) {
        const WoJson *entry = wo_json_at(manifest, i);
        const char *name = wo_json_str(entry, "name", "?");
        const char *archive_name = wo_json_str(entry, "archive", "");
        const char *url = wo_json_str(entry, "url", "");
        const char *sha256 = wo_json_str(entry, "sha256", "");
        char *marker = wo_path_child(deps, wo_json_str(entry, "marker", ""), error);
        char *archive = marker ? wo_path_child(archives, archive_name, error) : NULL;
        ok = archive != NULL;
        if (ok && file_matches(marker, wo_json_str(entry, "marker_sha256", ""))) {
            printf("%s: up to date\n", name);
        } else if (ok) {
            if (!file_matches(archive, sha256)) {
                printf("%s: downloading %s\n", name, url);
                fflush(stdout);
                ok = wo_http_download(url, archive, 1ull << 30, 1800, NULL, error);
                if (ok && !file_matches(archive, sha256)) {
                    ok = wo_fail(error, WO_ERR_CHECKSUM, "Dependency checksum failed: %s", name);
                }
            }
            if (ok && wo_str_iends_with(archive_name, ".zip")) {
                WoZip *zip = NULL;
                WoZipLimits limits = {2ull << 30, 200000, false, false};
                ok = wo_zip_open(archive, &zip, error) && wo_zip_extract_all(zip, deps, &limits, error);
                wo_zip_close(zip);
            } else if (ok) {
                char *zstd = find_zstd(deps);
                ok = untar(archive, deps, zstd, error);
                wo_free(zstd);
            }
            if (ok && !file_matches(marker, wo_json_str(entry, "marker_sha256", ""))) {
                ok = wo_fail(error, WO_ERR_CHECKSUM, "Extracted dependency checksum failed: %s",
                             name);
            }
            if (ok) {
                printf("%s: verified\n", name);
            }
        }
        wo_free(archive);
        wo_free(marker);
    }
    wo_free(archives);
    wo_json_free(manifest);
    return ok;
}
