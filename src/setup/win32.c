#include "setup/win32.h"

#include "core/wo_win.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wchar.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"

static HANDLE instance_mutex;
static bool com_ready;

bool win32_init(void) {
    com_ready = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    return com_ready;
}

void win32_shutdown(void) {
    if (com_ready) {
        CoUninitialize();
        com_ready = false;
    }
    if (instance_mutex) {
        CloseHandle(instance_mutex);
        instance_mutex = NULL;
    }
}

static void set_initial_folder(IFileDialog *dialog, const char *initial_path) {
    if (!initial_path || !*initial_path) {
        return;
    }
    char *folder = wo_is_dir(initial_path) ? wo_strdup(initial_path, NULL)
                                           : wo_path_parent(initial_path, NULL);
    wchar_t *wide = (folder && wo_is_dir(folder)) ? wo_wide(folder, NULL) : NULL;
    IShellItem *item = NULL;
    if (wide && SUCCEEDED(SHCreateItemFromParsingName(wide, NULL, &IID_IShellItem, (void **)&item))) {
        IFileDialog_SetFolder(dialog, item);
        IShellItem_Release(item);
    }
    wo_free(wide);
    wo_free(folder);
}

static char *pick(void *owner, const char *initial_path, bool folders) {
    IFileDialog *dialog = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileDialog, (void **)&dialog))) {
        return NULL;
    }
    FILEOPENDIALOGOPTIONS options = 0;
    IFileDialog_GetOptions(dialog, &options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    options |= folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
    IFileDialog_SetOptions(dialog, options);
    if (folders) {
        IFileDialog_SetTitle(dialog, L"Select your existing game or extracted music folder");
    } else {
        static const COMDLG_FILTERSPEC types[] = {
            {L"Game sources", L"*.zip;*.iso;*.mds;*.mdf;*.cue;*.bin"}, {L"All files", L"*.*"}};
        IFileDialog_SetFileTypes(dialog, 2, types);
        IFileDialog_SetTitle(dialog, L"Select a game source");
    }
    set_initial_folder(dialog, initial_path);
    char *result = NULL;
    IShellItem *item = NULL;
    if (SUCCEEDED(IFileDialog_Show(dialog, (HWND)owner)) &&
        SUCCEEDED(IFileDialog_GetResult(dialog, &item))) {
        wchar_t *path = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            result = wo_utf8(path, NULL);
            CoTaskMemFree(path);
        }
        IShellItem_Release(item);
    }
    IFileDialog_Release(dialog);
    return result;
}

char *win32_pick_file(void *owner, const char *initial_path) {
    return pick(owner, initial_path, false);
}

char *win32_pick_folder(void *owner, const char *initial_path) {
    return pick(owner, initial_path, true);
}

void win32_message(void *owner, const char *title, const char *text, bool is_error) {
    wchar_t *wide_title = wo_wide(title, NULL);
    wchar_t *wide_text = wo_wide(text, NULL);
    MessageBoxW((HWND)owner, wide_text ? wide_text : L"", wide_title ? wide_title : L"WO4K",
                MB_OK | (is_error ? MB_ICONERROR : MB_ICONINFORMATION));
    wo_free(wide_text);
    wo_free(wide_title);
}

bool win32_open_url(const char *url) {
    if (!wo_str_istarts_with(url, "https://")) {
        return false;
    }
    wchar_t *wide = wo_wide(url, NULL);
    bool ok = wide && (INT_PTR)ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL) > 32;
    wo_free(wide);
    return ok;
}

bool win32_start_program(const char *exe, const char *arguments, const char *directory,
                         WoError *error) {
    char *command = wo_format(error, "\"%s\"%s%s", exe, arguments ? " " : "",
                              arguments ? arguments : "");
    wchar_t *wide_exe = command ? wo_wide(exe, error) : NULL;
    wchar_t *wide_command = wide_exe ? wo_wide(command, error) : NULL;
    wchar_t *wide_directory = (wide_command && directory) ? wo_wide(directory, error) : NULL;
    bool ok = wide_command && (!directory || wide_directory);
    if (ok) {
        STARTUPINFOW startup;
        memset(&startup, 0, sizeof(startup));
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process;
        memset(&process, 0, sizeof(process));
        if (CreateProcessW(wide_exe, wide_command, NULL, NULL, FALSE, 0, NULL, wide_directory,
                           &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            ok = wo_fail_win32(error, GetLastError(), "Cannot start \"%s\"", exe);
        }
    }
    wo_free(wide_directory);
    wo_free(wide_command);
    wo_free(wide_exe);
    wo_free(command);
    return ok;
}

char *win32_exe_path(void) {
    for (DWORD cap = MAX_PATH; cap <= 32768; cap *= 2) {
        wchar_t *buffer = wo_alloc_array(cap, sizeof(wchar_t), NULL);
        if (!buffer) {
            return NULL;
        }
        DWORD len = GetModuleFileNameW(NULL, buffer, cap);
        if (len > 0 && len < cap) {
            char *path = wo_utf8(buffer, NULL);
            wo_free(buffer);
            return path;
        }
        wo_free(buffer);
        if (len == 0) {
            return NULL;
        }
    }
    return NULL;
}

uint32_t win32_current_pid(void) {
    return GetCurrentProcessId();
}

void win32_wait_for_process(uint32_t pid, uint32_t timeout_ms) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process) {
        WaitForSingleObject(process, timeout_ms);
        CloseHandle(process);
    }
}

bool win32_claim_single_instance(const char *mod_root, const char *window_title) {
    char *lower = wo_strdup(mod_root, NULL);
    char digest[WO_SHA256_HEX_SIZE] = "0000000000000000";
    if (lower) {
        for (char *p = lower; *p; p++) {
            *p = wo_ascii_lower(*p);
        }
        wo_sha256_mem(lower, strlen(lower), digest, NULL);
        wo_free(lower);
    }
    wchar_t name[64];
    _snwprintf(name, WO_COUNT(name) - 1, L"Local\\WO4K-Setup-%.16hs", digest);
    name[WO_COUNT(name) - 1] = 0;
    instance_mutex = CreateMutexW(NULL, TRUE, name);
    if (instance_mutex && GetLastError() != ERROR_ALREADY_EXISTS) {
        return true;
    }
    if (instance_mutex) {
        CloseHandle(instance_mutex);
        instance_mutex = NULL;
    }
    wchar_t *title = wo_wide(window_title, NULL);
    HWND other = title ? FindWindowW(NULL, title) : NULL;
    if (other) {
        ShowWindow(other, SW_RESTORE);
        SetForegroundWindow(other);
    }
    wo_free(title);
    return false;
}

void win32_work_area(void *window, int *x, int *y, int *width, int *height) {
    HMONITOR monitor = window ? MonitorFromWindow((HWND)window, MONITOR_DEFAULTTOPRIMARY)
                              : MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info;
    info.cbSize = sizeof(info);
    RECT work = {0, 0, 1280, 720};
    if (GetMonitorInfoW(monitor, &info)) {
        work = info.rcWork;
    }
    *x = work.left;
    *y = work.top;
    *width = work.right - work.left;
    *height = work.bottom - work.top;
}

char *win32_font_path(const char *file_name) {
    wchar_t windows[MAX_PATH];
    UINT len = GetWindowsDirectoryW(windows, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return NULL;
    }
    char *root = wo_utf8(windows, NULL);
    char *path = root ? wo_format(NULL, "%s\\Fonts\\%s", root, file_name) : NULL;
    wo_free(root);
    if (path && !wo_is_file(path)) {
        wo_free(path);
        path = NULL;
    }
    return path;
}
