#include "core/wo_win.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

wchar_t *wo_wide(const char *utf8, WoError *error) {
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if (needed <= 0) {
        wo_fail_win32(error, GetLastError(), "Invalid UTF-8 text");
        return NULL;
    }
    wchar_t *wide = wo_alloc_array((size_t)needed, sizeof(wchar_t), error);
    if (wide && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, needed) <= 0) {
        wo_fail_win32(error, GetLastError(), "Invalid UTF-8 text");
        wo_free(wide);
        return NULL;
    }
    return wide;
}

char *wo_utf8_n(const wchar_t *wide, size_t len, WoError *error) {
    if (len == 0) {
        return wo_strdup("", error);
    }
    if (len > INT32_MAX) {
        wo_fail(error, WO_ERR_LIMIT, "Text is too long.");
        return NULL;
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, (int)len, NULL, 0, NULL, NULL);
    if (needed <= 0) {
        wo_fail_win32(error, GetLastError(), "Text conversion failed");
        return NULL;
    }
    char *utf8 = wo_alloc((size_t)needed + 1, error);
    if (utf8 && WideCharToMultiByte(CP_UTF8, 0, wide, (int)len, utf8, needed, NULL, NULL) <= 0) {
        wo_fail_win32(error, GetLastError(), "Text conversion failed");
        wo_free(utf8);
        return NULL;
    }
    return utf8;
}

char *wo_utf8(const wchar_t *wide, WoError *error) {
    return wo_utf8_n(wide, wcslen(wide), error);
}

WoErrorKind wo_error_kind_from_win32(DWORD code) {
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_NOT_READY:
        return WO_ERR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
    case ERROR_VIRUS_INFECTED:
    case ERROR_VIRUS_DELETED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return WO_ERR_ACCESS;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_USER_MAPPED_FILE:
        return WO_ERR_SHARING;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return WO_ERR_LIMIT;
    case ERROR_OPERATION_ABORTED:
    case ERROR_CANCELLED:
        return WO_ERR_CANCELLED;
    default:
        return WO_ERR_IO;
    }
}

bool wo_fail_win32(WoError *error, DWORD code, const char *format, ...) {
    if (!error) {
        return false;
    }
    char prefix[WO_ERROR_MESSAGE_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(prefix, sizeof(prefix), format, args);
    va_end(args);

    wchar_t system[512];
    char detail[1024] = "";
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                             code, 0, system, (DWORD)WO_COUNT(system), NULL);
    while (n > 0 && (system[n - 1] == L'\r' || system[n - 1] == L'\n' || system[n - 1] == L' ' ||
                     system[n - 1] == L'.')) {
        system[--n] = 0;
    }
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, system, -1, detail, (int)sizeof(detail), NULL, NULL);
    }
    error->kind = wo_error_kind_from_win32(code);
    error->code = code;
    if (detail[0]) {
        snprintf(error->message, sizeof(error->message), "%s: %s (Windows error %lu).", prefix,
                 detail, (unsigned long)code);
    } else {
        snprintf(error->message, sizeof(error->message), "%s (Windows error %lu).", prefix,
                 (unsigned long)code);
    }
    return false;
}

wchar_t *wo_wide_path(const char *path, WoError *error) {
    wchar_t *input = wo_wide(path, error);
    if (!input) {
        return NULL;
    }
    DWORD needed = GetFullPathNameW(input, 0, NULL, NULL);
    if (needed == 0) {
        wo_fail_win32(error, GetLastError(), "Invalid path \"%s\"", path);
        wo_free(input);
        return NULL;
    }
    /* Room for the longest prefix (\\?\UNC\ replaces the leading \\). */
    size_t cap = (size_t)needed + 8;
    wchar_t *full = wo_alloc_array(cap, sizeof(wchar_t), error);
    if (!full) {
        wo_free(input);
        return NULL;
    }
    DWORD len = GetFullPathNameW(input, needed, full + 8, NULL);
    wo_free(input);
    if (len == 0 || len >= needed) {
        wo_fail_win32(error, len ? ERROR_BUFFER_OVERFLOW : GetLastError(), "Invalid path \"%s\"",
                      path);
        wo_free(full);
        return NULL;
    }
    wchar_t *text = full + 8;
    bool already_prefixed = wcsncmp(text, L"\\\\?\\", 4) == 0 || wcsncmp(text, L"\\\\.\\", 4) == 0;
    if (len < MAX_PATH - 12 || already_prefixed) {
        memmove(full, text, ((size_t)len + 1) * sizeof(wchar_t));
        return full;
    }
    if (text[0] == L'\\' && text[1] == L'\\') {
        /* \\server\share\x -> \\?\UNC\server\share\x */
        memmove(full + 8, text + 2, ((size_t)len - 1) * sizeof(wchar_t));
        memcpy(full, L"\\\\?\\UNC\\", 8 * sizeof(wchar_t));
    } else {
        memmove(full + 4, text, ((size_t)len + 1) * sizeof(wchar_t));
        memcpy(full, L"\\\\?\\", 4 * sizeof(wchar_t));
    }
    return full;
}
