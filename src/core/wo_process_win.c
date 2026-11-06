#include "core/wo_mod.h"
#include "core/wo_fs.h"
#include "core/wo_win.h"

#include <tlhelp32.h>
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
