#include "core/wo_mod.h"
#include "core/wo_fs.h"
#include "core/wo_win.h"

#include <tlhelp32.h>
#include <wctype.h>
#include <wchar.h>

bool wo_mod_game_running(const WoMod *mod) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool running = false;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !running;
         more = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, L"Wipeout2097.exe") != 0) {
            continue;
        }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) {
            running = true; /* cannot tell where it runs from: assume ours */
            break;
        }
        wchar_t image[32768];
        DWORD size = (DWORD)WO_COUNT(image);
        if (QueryFullProcessImageNameW(process, 0, image, &size)) {
            char *path = wo_utf8(image, NULL);
            running = !path || wo_path_is_within(mod->game, path, false);
            wo_free(path);
        } else {
            running = true;
        }
        CloseHandle(process);
    }
    CloseHandle(snapshot);
    return running;
}

/* Local\WO4K-installation-<FNV-1a of the long, lower-case folder path>:
 * the same folder reached by any spelling gives the same name. */
bool wo_mod_lock(const WoMod *mod, uint32_t wait_ms, WoModLock *lock, WoError *error) {
    lock->handle = NULL;
    wchar_t *root = wo_wide_path(mod->root, error);
    if (!root) {
        return false;
    }
    wchar_t long_root[32768];
    DWORD length = GetLongPathNameW(root, long_root, (DWORD)WO_COUNT(long_root));
    const wchar_t *path = length > 0 && length < WO_COUNT(long_root) ? long_root : root;
    uint64_t hash = 14695981039346656037ull;
    for (const wchar_t *c = path; *c; c++) {
        hash = (hash ^ (uint64_t)towlower(*c)) * 1099511628211ull;
    }
    wo_free(root);
    wchar_t name[64];
    _snwprintf(name, WO_COUNT(name) - 1, L"Local\\WO4K-installation-%016llx",
               (unsigned long long)hash);
    name[WO_COUNT(name) - 1] = 0;
    HANDLE mutex = CreateMutexW(NULL, FALSE, name);
    if (!mutex) {
        return wo_fail_win32(error, GetLastError(), "WO4K could not lock its installation");
    }
    /* Abandoned: a holder that died; its leftovers are only scratch. */
    DWORD waited = WaitForSingleObject(mutex, wait_ms);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
        CloseHandle(mutex);
        return wo_fail(error, WO_ERR_BUSY,
                       "Setup or Play is already changing this installation. "
                       "Try again when it has finished.");
    }
    lock->handle = mutex;
    return true;
}

void wo_mod_unlock(WoModLock *lock) {
    if (lock->handle) {
        ReleaseMutex(lock->handle);
        CloseHandle(lock->handle);
        lock->handle = NULL;
    }
}
