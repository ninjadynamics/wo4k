#include "core/wo_fs.h"
#include "core/wo_win.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Antivirus scanners, indexers and Explorer briefly hold files open. Retry
 * those transient refusals for about half a second before failing. */
#define RETRY_ATTEMPTS 10
#define RETRY_DELAY_MS 50

static bool is_transient(DWORD code) {
    return code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION ||
           code == ERROR_ACCESS_DENIED;
}

static bool is_separator(char c) {
    return c == '\\' || c == '/';
}

/* --- Paths --------------------------------------------------------------------- */

char *wo_path_full(const char *path, WoError *error) {
    if (!path || !*path) {
        wo_fail(error, WO_ERR_NOT_FOUND, "No path was given.");
        return NULL;
    }
    wchar_t *input = wo_wide(path, error);
    if (!input) {
        return NULL;
    }
    DWORD needed = GetFullPathNameW(input, 0, NULL, NULL);
    wchar_t *full = needed ? wo_alloc_array(needed, sizeof(wchar_t), error) : NULL;
    DWORD len = full ? GetFullPathNameW(input, needed, full, NULL) : 0;
    wo_free(input);
    if (!full || len == 0 || len >= needed) {
        if (full || needed == 0) {
            wo_fail_win32(error, GetLastError(), "Invalid path \"%s\"", path);
        }
        wo_free(full);
        return NULL;
    }
    /* Drop trailing separators except for a drive root such as C:\ */
    while (len > 3 && (full[len - 1] == L'\\' || full[len - 1] == L'/')) {
        full[--len] = 0;
    }
    char *utf8 = wo_utf8_n(full, len, error);
    wo_free(full);
    return utf8;
}

char *wo_path_join(const char *base, const char *name, WoError *error) {
    size_t base_len = strlen(base);
    while (base_len > 0 && is_separator(base[base_len - 1])) {
        base_len--;
    }
    char *joined = wo_format(error, "%.*s\\%s", (int)base_len, base, name);
    if (joined) {
        for (char *p = joined; *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
    }
    return joined;
}

static bool ordinal_ieq(const char *a, size_t a_len, const char *b, size_t b_len) {
    if (a_len > INT32_MAX || b_len > INT32_MAX) {
        return false;
    }
    int wa = MultiByteToWideChar(CP_UTF8, 0, a, (int)a_len, NULL, 0);
    int wb = MultiByteToWideChar(CP_UTF8, 0, b, (int)b_len, NULL, 0);
    if (wa != wb) {
        return false;
    }
    if (wa == 0) {
        return a_len == 0 && b_len == 0;
    }
    wchar_t *left = wo_alloc_array((size_t)wa, sizeof(wchar_t), NULL);
    wchar_t *right = wo_alloc_array((size_t)wb, sizeof(wchar_t), NULL);
    bool same = false;
    if (left && right) {
        MultiByteToWideChar(CP_UTF8, 0, a, (int)a_len, left, wa);
        MultiByteToWideChar(CP_UTF8, 0, b, (int)b_len, right, wb);
        same = CompareStringOrdinal(left, wa, right, wb, TRUE) == CSTR_EQUAL;
    }
    wo_free(left);
    wo_free(right);
    return same;
}

bool wo_path_is_within(const char *root, const char *path, bool allow_equal) {
    size_t root_len = strlen(root);
    size_t path_len = strlen(path);
    bool root_has_separator = root_len > 0 && is_separator(root[root_len - 1]);
    if (path_len < root_len) {
        return false;
    }
    if (!ordinal_ieq(root, root_len, path, root_len)) {
        return false;
    }
    if (path_len == root_len) {
        return allow_equal;
    }
    return root_has_separator || is_separator(path[root_len]);
}

char *wo_path_child(const char *root, const char *relative, WoError *error) {
    if (!relative || !*relative) {
        wo_fail(error, WO_ERR_UNSAFE, "An empty relative path was given.");
        return NULL;
    }
    if (strchr(relative, ':') || is_separator(relative[0])) {
        wo_fail(error, WO_ERR_UNSAFE, "Path is not relative: %s", relative);
        return NULL;
    }
    char *base = wo_path_full(root, error);
    if (!base) {
        return NULL;
    }
    char *joined = wo_path_join(base, relative, error);
    char *full = joined ? wo_path_full(joined, error) : NULL;
    wo_free(joined);
    if (full && !wo_path_is_within(base, full, false)) {
        wo_fail(error, WO_ERR_UNSAFE, "Path leaves its root: %s", relative);
        wo_free(full);
        full = NULL;
    }
    wo_free(base);
    return full;
}

const char *wo_path_basename(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (is_separator(*p) || (*p == ':' && p == path + 1)) {
            name = p + 1;
        }
    }
    return name;
}

static size_t root_length(const char *path) {
    /* "C:\" -> 3, "\\server\share\" -> through the share, else 0. */
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        return is_separator(path[2]) ? 3 : 2;
    }
    if (is_separator(path[0]) && is_separator(path[1])) {
        size_t i = 2;
        int parts = 0;
        while (path[i] && parts < 2) {
            if (is_separator(path[i])) {
                parts++;
            }
            i++;
        }
        return i;
    }
    return 0;
}

char *wo_path_parent(const char *path, WoError *error) {
    size_t len = strlen(path);
    size_t root = root_length(path);
    while (len > root && is_separator(path[len - 1])) {
        len--;
    }
    size_t cut = len;
    while (cut > root && !is_separator(path[cut - 1])) {
        cut--;
    }
    if (cut == len) {
        return NULL;
    }
    size_t keep = cut;
    while (keep > root && is_separator(path[keep - 1])) {
        keep--;
    }
    if (keep == 0) {
        return NULL;
    }
    return wo_strndup(path, keep, error);
}

const char *wo_path_extension(const char *path) {
    const char *name = wo_path_basename(path);
    const char *dot = strrchr(name, '.');
    return (dot && dot != name) ? dot : name + strlen(name);
}

char *wo_path_change_extension(const char *path, const char *extension, WoError *error) {
    const char *old = wo_path_extension(path);
    return wo_format(error, "%.*s%s", (int)(old - path), path, extension);
}

bool wo_path_is_absolute(const char *path) {
    size_t root = root_length(path);
    return root >= 3 || (root > 0 && is_separator(path[0]));
}

char *wo_path_relative(const char *root, const char *path, WoError *error) {
    if (!wo_path_is_within(root, path, false)) {
        return NULL;
    }
    size_t root_len = strlen(root);
    if (root_len > 0 && !is_separator(root[root_len - 1])) {
        root_len++;
    }
    char *relative = wo_strdup(path + root_len, error);
    if (relative) {
        for (char *p = relative; *p; p++) {
            if (*p == '\\') {
                *p = '/';
            }
        }
    }
    return relative;
}

/* --- Queries ------------------------------------------------------------------- */

static bool reparse_is_link(const wchar_t *wide) {
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(wide, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    FindClose(find);
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           IsReparseTagNameSurrogate(data.dwReserved0);
}

static uint64_t filetime_ticks(FILETIME time) {
    return ((uint64_t)time.dwHighDateTime << 32) | time.dwLowDateTime;
}

bool wo_stat(const char *path, WoFileInfo *info, WoError *error) {
    memset(info, 0, sizeof(*info));
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &data)) {
        DWORD code = GetLastError();
        wo_free(wide);
        if (wo_error_kind_from_win32(code) == WO_ERR_NOT_FOUND || code == ERROR_INVALID_NAME) {
            return true;
        }
        return wo_fail_win32(error, code, "Cannot read the attributes of \"%s\"", path);
    }
    info->exists = true;
    info->attributes = data.dwFileAttributes;
    info->is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info->size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    info->mtime = filetime_ticks(data.ftLastWriteTime);
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        info->is_link = reparse_is_link(wide);
    }
    wo_free(wide);
    return true;
}

bool wo_exists(const char *path) {
    WoFileInfo info;
    return wo_stat(path, &info, NULL) && info.exists;
}

bool wo_is_file(const char *path) {
    WoFileInfo info;
    return wo_stat(path, &info, NULL) && info.exists && !info.is_dir;
}

bool wo_is_dir(const char *path) {
    WoFileInfo info;
    return wo_stat(path, &info, NULL) && info.exists && info.is_dir;
}

bool wo_is_link(const char *path) {
    WoFileInfo info;
    return wo_stat(path, &info, NULL) && info.is_link;
}

bool wo_chain_has_link(const char *root, const char *path) {
    char *full_root = wo_path_full(root, NULL);
    char *cursor = wo_path_full(path, NULL);
    bool found = !full_root || !cursor;
    while (!found && cursor && wo_path_is_within(full_root, cursor, true)) {
        if (wo_is_link(cursor)) {
            found = true;
            break;
        }
        if (strlen(cursor) <= strlen(full_root)) {
            break;
        }
        char *parent = wo_path_parent(cursor, NULL);
        wo_free(cursor);
        cursor = parent;
    }
    wo_free(cursor);
    wo_free(full_root);
    return found;
}

bool wo_dir_is_empty(const char *path) {
    char *pattern = wo_path_join(path, "*", NULL);
    wchar_t *wide = pattern ? wo_wide_path(pattern, NULL) : NULL;
    wo_free(pattern);
    if (!wide) {
        return false;
    }
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(wide, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0);
    wo_free(wide);
    if (find == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    bool empty = true;
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return empty;
}

/* --- Streaming files ------------------------------------------------------------- */

struct WoFile {
    HANDLE handle;
    uint64_t size;
    char *path;
};

static bool open_handle(const char *path, DWORD access, DWORD share, DWORD disposition,
                        DWORD flags, HANDLE *out, WoError *error) {
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD code = 0;
    for (int attempt = 0; attempt < RETRY_ATTEMPTS; attempt++) {
        handle = CreateFileW(wide, access, share, NULL, disposition, flags, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        code = GetLastError();
        if (!is_transient(code)) {
            break;
        }
        Sleep(RETRY_DELAY_MS);
    }
    wo_free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return wo_fail_win32(error, code, "Cannot open \"%s\"", path);
    }
    *out = handle;
    return true;
}

bool wo_file_open_read(const char *path, WoFile **out, WoError *error) {
    *out = NULL;
    HANDLE handle;
    if (!open_handle(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, &handle, error)) {
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size)) {
        DWORD code = GetLastError();
        CloseHandle(handle);
        return wo_fail_win32(error, code, "Cannot read the size of \"%s\"", path);
    }
    WoFile *file = wo_alloc(sizeof(*file), error);
    char *copy = file ? wo_strdup(path, error) : NULL;
    if (!copy) {
        wo_free(file);
        CloseHandle(handle);
        return false;
    }
    file->handle = handle;
    file->size = (uint64_t)size.QuadPart;
    file->path = copy;
    *out = file;
    return true;
}

bool wo_file_create(const char *path, WoFile **out, WoError *error) {
    *out = NULL;
    if (!wo_mkparent(path, error)) {
        return false;
    }
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(wide, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
    wo_free(wide);
    HANDLE handle;
    if (!open_handle(path, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                     &handle, error)) {
        return false;
    }
    WoFile *file = wo_alloc(sizeof(*file), error);
    char *copy = file ? wo_strdup(path, error) : NULL;
    if (!copy) {
        wo_free(file);
        CloseHandle(handle);
        return false;
    }
    file->handle = handle;
    file->path = copy;
    *out = file;
    return true;
}

bool wo_file_read_at(WoFile *file, uint64_t offset, void *dst, size_t len, WoError *error) {
    uint8_t *out = dst;
    while (len > 0) {
        DWORD chunk = len > (1u << 24) ? (1u << 24) : (DWORD)len;
        OVERLAPPED position;
        memset(&position, 0, sizeof(position));
        position.Offset = (DWORD)offset;
        position.OffsetHigh = (DWORD)(offset >> 32);
        DWORD got = 0;
        if (!ReadFile(file->handle, out, chunk, &got, &position)) {
            DWORD code = GetLastError();
            if (code != ERROR_HANDLE_EOF) {
                return wo_fail_win32(error, code, "Cannot read \"%s\"", file->path);
            }
        }
        if (got == 0) {
            return wo_fail(error, WO_ERR_FORMAT, "\"%s\" is truncated.", file->path);
        }
        out += got;
        offset += got;
        len -= got;
    }
    return true;
}

bool wo_file_read(WoFile *file, void *dst, size_t len, size_t *got, WoError *error) {
    DWORD chunk = len > (1u << 24) ? (1u << 24) : (DWORD)len;
    DWORD read = 0;
    if (!ReadFile(file->handle, dst, chunk, &read, NULL)) {
        DWORD code = GetLastError();
        if (code != ERROR_HANDLE_EOF) {
            return wo_fail_win32(error, code, "Cannot read \"%s\"", file->path);
        }
    }
    *got = read;
    return true;
}

bool wo_file_write(WoFile *file, const void *data, size_t len, WoError *error) {
    const uint8_t *in = data;
    while (len > 0) {
        DWORD chunk = len > (1u << 24) ? (1u << 24) : (DWORD)len;
        DWORD wrote = 0;
        if (!WriteFile(file->handle, in, chunk, &wrote, NULL) || wrote == 0) {
            return wo_fail_win32(error, GetLastError(), "Cannot write \"%s\"", file->path);
        }
        in += wrote;
        len -= wrote;
        file->size += wrote;
    }
    return true;
}

bool wo_file_flush(WoFile *file, WoError *error) {
    if (!FlushFileBuffers(file->handle)) {
        return wo_fail_win32(error, GetLastError(), "Cannot flush \"%s\"", file->path);
    }
    return true;
}

uint64_t wo_file_size(const WoFile *file) {
    return file->size;
}

const char *wo_file_path(const WoFile *file) {
    return file->path;
}

bool wo_file_close(WoFile *file, WoError *error) {
    if (!file) {
        return true;
    }
    bool ok = true;
    if (!CloseHandle(file->handle)) {
        ok = wo_fail_win32(error, GetLastError(), "Cannot close \"%s\"", file->path);
    }
    wo_free(file->path);
    wo_free(file);
    return ok;
}

static bool file_source_read(const WoSource *source, uint64_t offset, void *dst, size_t len,
                             WoError *error) {
    return wo_file_read_at(source->context, offset, dst, len, error);
}

static void file_source_close(WoSource *source) {
    wo_file_close(source->context, NULL);
    source->context = NULL;
}

bool wo_source_open_file(WoSource *source, const char *path, WoError *error) {
    memset(source, 0, sizeof(*source));
    WoFile *file;
    if (!wo_file_open_read(path, &file, error)) {
        return false;
    }
    source->size = file->size;
    source->context = file;
    source->read_at = file_source_read;
    source->close = file_source_close;
    wo_copy_str(source->name, sizeof(source->name), wo_path_basename(path));
    return true;
}

/* --- Whole files ------------------------------------------------------------------- */

bool wo_read_file(const char *path, size_t max_bytes, WoBuf *out, WoError *error) {
    WoFile *file;
    if (!wo_file_open_read(path, &file, error)) {
        return false;
    }
    bool ok = true;
    if (file->size > max_bytes) {
        ok = wo_fail(error, WO_ERR_LIMIT, "\"%s\" is larger than the %zu-byte limit.", path,
                     max_bytes);
    }
    size_t size = (size_t)file->size;
    ok = ok && wo_buf_reserve(out, size, error);
    ok = ok && wo_file_read_at(file, 0, out->data + out->len, size, error);
    if (ok) {
        out->len += size;
        ok = wo_buf_terminate(out, error);
    }
    wo_file_close(file, NULL);
    return ok;
}

bool wo_read_text(const char *path, size_t max_bytes, char **out, WoError *error) {
    *out = NULL;
    WoBuf buf = {0};
    if (!wo_read_file(path, max_bytes, &buf, error)) {
        wo_buf_free(&buf);
        return false;
    }
    if (memchr(buf.data, 0, buf.len)) {
        wo_buf_free(&buf);
        return wo_fail(error, WO_ERR_FORMAT, "\"%s\" is not a text file.", path);
    }
    if (buf.len >= 3 && buf.data[0] == 0xEF && buf.data[1] == 0xBB && buf.data[2] == 0xBF) {
        memmove(buf.data, buf.data + 3, buf.len - 3);
        buf.len -= 3;
    }
    *out = wo_buf_detach(&buf);
    return *out != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
}

bool wo_write_file(const char *path, const void *data, size_t len, WoError *error) {
    WoFile *file;
    if (!wo_file_create(path, &file, error)) {
        return false;
    }
    bool ok = wo_file_write(file, data, len, error);
    bool closed = wo_file_close(file, ok ? error : NULL);
    return ok && closed;
}

static volatile LONG temp_counter;

bool wo_write_file_atomic(const char *path, const void *data, size_t len, WoError *error) {
    if (!wo_mkparent(path, error)) {
        return false;
    }
    char *temp = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16 && handle == INVALID_HANDLE_VALUE; attempt++) {
        wo_free(temp);
        temp = wo_format(error, "%s.%lx%lx%lx.tmp", path, (unsigned long)GetCurrentProcessId(),
                         (unsigned long)InterlockedIncrement(&temp_counter),
                         (unsigned long)GetTickCount());
        wchar_t *wide = temp ? wo_wide_path(temp, error) : NULL;
        if (!wide) {
            wo_free(temp);
            return false;
        }
        handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        wo_free(wide);
        if (handle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) {
            wo_fail_win32(error, GetLastError(), "Cannot create a temporary file beside \"%s\"",
                          path);
            wo_free(temp);
            return false;
        }
    }
    if (handle == INVALID_HANDLE_VALUE) {
        wo_free(temp);
        return wo_fail(error, WO_ERR_IO, "Cannot create a temporary file beside \"%s\".", path);
    }
    WoFile file = {handle, 0, temp};
    bool ok = wo_file_write(&file, data, len, error) && wo_file_flush(&file, error);
    if (!CloseHandle(handle) && ok) {
        ok = wo_fail_win32(error, GetLastError(), "Cannot close \"%s\"", temp);
    }
    ok = ok && wo_replace_file(temp, path, error);
    if (!ok) {
        wo_delete_file(temp, NULL);
    }
    wo_free(temp);
    return ok;
}

bool wo_read_json(const char *path, size_t max_bytes, WoJson **out, WoError *error) {
    *out = NULL;
    char *text = NULL;
    if (!wo_read_text(path, max_bytes, &text, error)) {
        return false;
    }
    bool ok = wo_json_parse(text, strlen(text), out, error);
    if (!ok) {
        wo_error_context(error, "%s: ", wo_path_basename(path));
    }
    wo_free(text);
    return ok;
}

bool wo_write_json(const char *path, const WoJson *value, WoError *error) {
    WoBuf buf = {0};
    bool ok = wo_json_write(value, true, &buf, error) &&
              wo_write_file_atomic(path, buf.data, buf.len, error);
    wo_buf_free(&buf);
    return ok;
}

bool wo_write_json_if_changed(const char *path, const WoJson *value, bool *written,
                              WoError *error) {
    if (written) {
        *written = false;
    }
    WoBuf buf = {0};
    if (!wo_json_write(value, true, &buf, error)) {
        wo_buf_free(&buf);
        return false;
    }
    bool same = false;
    WoFileInfo info;
    if (wo_stat(path, &info, NULL) && info.exists && !info.is_dir && info.size == buf.len) {
        WoBuf old = {0};
        /* An unreadable file is simply rewritten. */
        same = wo_read_file(path, buf.len, &old, NULL) && old.len == buf.len &&
               memcmp(old.data, buf.data, buf.len) == 0;
        wo_buf_free(&old);
    }
    bool ok = same || wo_write_file_atomic(path, buf.data, buf.len, error);
    if (ok && !same && written) {
        *written = true;
    }
    wo_buf_free(&buf);
    return ok;
}

/* --- Change detection ---------------------------------------------------------------- */

static bool handle_identity(HANDLE handle, const char *path, WoFileIdentity *out,
                            WoError *error) {
    memset(out, 0, sizeof(*out));
    FILE_BASIC_INFO basic;
    FILE_STANDARD_INFO standard;
    if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard))) {
        return wo_fail_win32(error, GetLastError(), "Cannot read the identity of \"%s\"", path);
    }
    if (standard.Directory) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "\"%s\" is a folder, not a file.", path);
    }
    out->size = (uint64_t)standard.EndOfFile.QuadPart;
    out->mtime = (uint64_t)basic.LastWriteTime.QuadPart;
    out->ctime = (uint64_t)basic.ChangeTime.QuadPart;
    FILE_ID_INFO id;
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) {
        out->volume = id.VolumeSerialNumber;
        memcpy(out->id, id.FileId.Identifier, sizeof(out->id));
        return true;
    }
    /* Older file systems (FAT) only offer the 64-bit index. */
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info)) {
        return wo_fail_win32(error, GetLastError(), "Cannot read the identity of \"%s\"", path);
    }
    out->volume = info.dwVolumeSerialNumber;
    uint64_t index = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    for (size_t i = 0; i < 8; i++) {
        out->id[i] = (uint8_t)(index >> (8 * i));
    }
    return true;
}

bool wo_file_identity(const char *path, WoFileIdentity *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    HANDLE handle;
    if (!open_handle(path, FILE_READ_ATTRIBUTES,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, &handle, error)) {
        return false;
    }
    bool ok = handle_identity(handle, path, out, error);
    CloseHandle(handle);
    return ok;
}

bool wo_file_identity_of(WoFile *file, WoFileIdentity *out, WoError *error) {
    return handle_identity(file->handle, file->path, out, error);
}

bool wo_file_identity_eq(const WoFileIdentity *a, const WoFileIdentity *b) {
    return a->size == b->size && a->mtime == b->mtime && a->ctime == b->ctime &&
           a->volume == b->volume && memcmp(a->id, b->id, sizeof(a->id)) == 0;
}

uint64_t wo_fs_now(void) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return filetime_ticks(now);
}

/* --- Changes ----------------------------------------------------------------------- */

bool wo_mkdirs(const char *path, WoError *error) {
    WoFileInfo info;
    if (!wo_stat(path, &info, error)) {
        return false;
    }
    if (info.exists) {
        return info.is_dir ||
               wo_fail(error, WO_ERR_IO, "\"%s\" exists but is not a folder.", path);
    }
    char *parent = wo_path_parent(path, error);
    if (parent) {
        bool ok = wo_mkdirs(parent, error);
        wo_free(parent);
        if (!ok) {
            return false;
        }
    }
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    bool created = CreateDirectoryW(wide, NULL) != 0;
    DWORD code = created ? 0 : GetLastError();
    wo_free(wide);
    if (!created && !(code == ERROR_ALREADY_EXISTS && wo_is_dir(path))) {
        return wo_fail_win32(error, code, "Cannot create the folder \"%s\"", path);
    }
    return true;
}

bool wo_mkparent(const char *path, WoError *error) {
    char *full = wo_path_full(path, error);
    char *parent = full ? wo_path_parent(full, error) : NULL;
    bool ok = full != NULL && (parent == NULL || wo_mkdirs(parent, error));
    wo_free(parent);
    wo_free(full);
    return ok;
}

static void clear_readonly(const wchar_t *wide) {
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(wide, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
}

bool wo_copy_file(const char *from, const char *to, WoError *error) {
    if (!wo_mkparent(to, error)) {
        return false;
    }
    wchar_t *source = wo_wide_path(from, error);
    wchar_t *target = source ? wo_wide_path(to, error) : NULL;
    if (!target) {
        wo_free(source);
        return false;
    }
    clear_readonly(target);
    BOOL copied = FALSE;
    DWORD code = 0;
    for (int attempt = 0; attempt < RETRY_ATTEMPTS; attempt++) {
        copied = CopyFileW(source, target, FALSE);
        if (copied) {
            break;
        }
        code = GetLastError();
        if (!is_transient(code)) {
            break;
        }
        Sleep(RETRY_DELAY_MS);
    }
    if (copied) {
        /* Files copied from discs arrive read-only; later repairs must be
         * able to replace them. */
        clear_readonly(target);
    }
    wo_free(source);
    wo_free(target);
    if (!copied) {
        return wo_fail_win32(error, code, "Cannot copy \"%s\" to \"%s\"", from, to);
    }
    return true;
}

static bool move_with_retry(const char *from, const char *to, DWORD flags, WoError *error) {
    wchar_t *source = wo_wide_path(from, error);
    wchar_t *target = source ? wo_wide_path(to, error) : NULL;
    if (!target) {
        wo_free(source);
        return false;
    }
    BOOL moved = FALSE;
    DWORD code = 0;
    for (int attempt = 0; attempt < RETRY_ATTEMPTS; attempt++) {
        moved = MoveFileExW(source, target, flags);
        if (moved) {
            break;
        }
        code = GetLastError();
        if (!is_transient(code)) {
            break;
        }
        Sleep(RETRY_DELAY_MS);
    }
    wo_free(source);
    wo_free(target);
    if (!moved) {
        return wo_fail_win32(error, code, "Cannot move \"%s\" to \"%s\"", from, to);
    }
    return true;
}

bool wo_move(const char *from, const char *to, WoError *error) {
    return move_with_retry(from, to, MOVEFILE_WRITE_THROUGH, error);
}

bool wo_replace_file(const char *from, const char *to, WoError *error) {
    wchar_t *target = wo_wide_path(to, error);
    if (!target) {
        return false;
    }
    clear_readonly(target);
    wo_free(target);
    return move_with_retry(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH, error);
}

bool wo_delete_file(const char *path, WoError *error) {
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    clear_readonly(wide);
    BOOL deleted = FALSE;
    DWORD code = 0;
    for (int attempt = 0; attempt < RETRY_ATTEMPTS; attempt++) {
        deleted = DeleteFileW(wide);
        if (deleted) {
            break;
        }
        code = GetLastError();
        if (wo_error_kind_from_win32(code) == WO_ERR_NOT_FOUND) {
            deleted = TRUE;
            break;
        }
        if (!is_transient(code)) {
            break;
        }
        Sleep(RETRY_DELAY_MS);
    }
    wo_free(wide);
    if (!deleted) {
        return wo_fail_win32(error, code, "Cannot delete \"%s\"", path);
    }
    return true;
}

bool wo_delete_empty_dir(const char *path, WoError *error) {
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    BOOL removed = FALSE;
    DWORD code = 0;
    for (int attempt = 0; attempt < RETRY_ATTEMPTS; attempt++) {
        removed = RemoveDirectoryW(wide);
        if (removed) {
            break;
        }
        code = GetLastError();
        if (wo_error_kind_from_win32(code) == WO_ERR_NOT_FOUND) {
            removed = TRUE;
            break;
        }
        if (!is_transient(code)) {
            break;
        }
        Sleep(RETRY_DELAY_MS);
    }
    wo_free(wide);
    if (!removed) {
        return wo_fail_win32(error, code, "Cannot remove the folder \"%s\"", path);
    }
    return true;
}

static bool delete_tree_inner(const char *path, int depth, WoError *error) {
    if (depth > 256) {
        return wo_fail(error, WO_ERR_LIMIT, "Folder nesting is too deep to delete: \"%s\".", path);
    }
    WoFileInfo info;
    if (!wo_stat(path, &info, error)) {
        return false;
    }
    if (!info.exists) {
        return true;
    }
    if (!info.is_dir) {
        return wo_delete_file(path, error);
    }
    if (info.is_link) {
        /* A directory link or junction: remove the link, never its target. */
        return wo_delete_empty_dir(path, error);
    }
    WoDirList list = {0};
    if (!wo_list_dir(path, &list, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < list.count; i++) {
        char *child = wo_path_join(path, list.items[i].name, error);
        ok = child && delete_tree_inner(child, depth + 1, error);
        wo_free(child);
    }
    wo_dirlist_free(&list);
    return ok && wo_delete_empty_dir(path, error);
}

bool wo_delete_tree(const char *guard_root, const char *path, WoError *error) {
    char *root = wo_path_full(guard_root, error);
    char *full = root ? wo_path_full(path, error) : NULL;
    bool ok = full != NULL;
    if (ok && !wo_path_is_within(root, full, false)) {
        ok = wo_fail(error, WO_ERR_UNSAFE, "Refusing to delete \"%s\" outside \"%s\".", full,
                     root);
    }
    ok = ok && delete_tree_inner(full, 0, error);
    wo_free(full);
    wo_free(root);
    return ok;
}

/* --- Listing ----------------------------------------------------------------------- */

static int compare_entries(const void *a, const void *b) {
    const WoDirEntry *left = a;
    const WoDirEntry *right = b;
    int order = wo_str_icmp(left->name, right->name);
    return order ? order : strcmp(left->name, right->name);
}

void wo_dirlist_free(WoDirList *list) {
    for (size_t i = 0; i < list->count; i++) {
        wo_free(list->items[i].name);
    }
    wo_free(list->items);
    memset(list, 0, sizeof(*list));
}

bool wo_list_dir(const char *dir, WoDirList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    char *pattern = wo_path_join(dir, "*", error);
    wchar_t *wide = pattern ? wo_wide_path(pattern, error) : NULL;
    wo_free(pattern);
    if (!wide) {
        return false;
    }
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(wide, FindExInfoBasic, &data, FindExSearchNameMatch, NULL,
                                   FIND_FIRST_EX_LARGE_FETCH);
    wo_free(wide);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        return wo_fail_win32(error, code, "Cannot list the folder \"%s\"", dir);
    }
    bool ok = true;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        if (out->count == out->cap) {
            size_t cap = out->cap ? out->cap * 2 : 32;
            WoDirEntry *grown = wo_realloc_array(out->items, cap, sizeof(*grown), error);
            if (!grown) {
                ok = false;
                break;
            }
            out->items = grown;
            out->cap = cap;
        }
        WoDirEntry *entry = &out->items[out->count];
        memset(entry, 0, sizeof(*entry));
        entry->name = wo_utf8(data.cFileName, error);
        if (!entry->name) {
            ok = false;
            break;
        }
        entry->attributes = data.dwFileAttributes;
        entry->is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry->is_link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                         IsReparseTagNameSurrogate(data.dwReserved0);
        entry->size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        entry->mtime = filetime_ticks(data.ftLastWriteTime);
        out->count++;
    } while (FindNextFileW(find, &data));
    DWORD code = GetLastError();
    FindClose(find);
    if (ok && code != ERROR_NO_MORE_FILES) {
        ok = wo_fail_win32(error, code, "Cannot list the folder \"%s\"", dir);
    }
    if (!ok) {
        wo_dirlist_free(out);
        return false;
    }
    qsort(out->items, out->count, sizeof(*out->items), compare_entries);
    return true;
}

static bool walk_inner(const char *dir, const char *relative, int depth, int max_depth,
                       WoWalkFn fn, void *context, WoError *error) {
    WoDirList list = {0};
    if (!wo_list_dir(dir, &list, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < list.count; i++) {
        const WoDirEntry *entry = &list.items[i];
        char *path = wo_path_join(dir, entry->name, error);
        char *rel = path ? (relative[0] ? wo_format(error, "%s/%s", relative, entry->name)
                                        : wo_strdup(entry->name, error))
                         : NULL;
        if (!rel) {
            wo_free(path);
            ok = false;
            break;
        }
        bool descend = entry->is_dir && !entry->is_link;
        ok = fn(context, path, rel, entry, &descend, error);
        if (ok && descend && entry->is_dir && !entry->is_link &&
            (max_depth < 0 || depth < max_depth)) {
            ok = walk_inner(path, rel, depth + 1, max_depth, fn, context, error);
        }
        wo_free(rel);
        wo_free(path);
    }
    wo_dirlist_free(&list);
    return ok;
}

bool wo_walk(const char *root, int max_depth, WoWalkFn fn, void *context, WoError *error) {
    return walk_inner(root, "", 0, max_depth, fn, context, error);
}
