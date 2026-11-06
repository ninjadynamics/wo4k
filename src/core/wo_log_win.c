#include "core/wo_log.h"

#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_win.h"
#include "wo_version.h"

#define ATTACHMENT_MAX_BYTES (8u << 20)
#define TAIL_LINES 80

static SRWLOCK log_lock = SRWLOCK_INIT;
static HANDLE log_file = INVALID_HANDLE_VALUE;
static char *log_path;
static char *log_root;
static char *log_profile;
static char *log_temp;
static uint64_t log_started; /* FILETIME ticks, UTC */
static bool log_system_written;

static uint64_t now_ticks(void) {
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    return ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
}

void wo_log_timestamp(char out[40]) {
    uint64_t ticks = now_ticks();
    FILETIME time;
    time.dwLowDateTime = (DWORD)ticks;
    time.dwHighDateTime = (DWORD)(ticks >> 32);
    SYSTEMTIME utc;
    FileTimeToSystemTime(&time, &utc);
    snprintf(out, 40, "%04u-%02u-%02uT%02u:%02u:%02u.%07uZ", utc.wYear, utc.wMonth, utc.wDay,
             utc.wHour, utc.wMinute, utc.wSecond, (unsigned)(ticks % 10000000u));
}

static char *env_path(const wchar_t *name) {
    wchar_t buffer[MAX_PATH * 2];
    DWORD len = GetEnvironmentVariableW(name, buffer, (DWORD)WO_COUNT(buffer));
    if (len == 0 || len >= WO_COUNT(buffer)) {
        return NULL;
    }
    while (len > 3 && (buffer[len - 1] == L'\\' || buffer[len - 1] == L'/')) {
        buffer[--len] = 0;
    }
    return wo_utf8(buffer, NULL);
}

static HANDLE open_append(const char *path) {
    if (!wo_mkparent(path, NULL)) {
        return INVALID_HANDLE_VALUE;
    }
    wchar_t *wide = wo_wide_path(path, NULL);
    if (!wide) {
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle = CreateFileW(wide, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    wo_free(wide);
    return handle;
}

bool wo_log_start(const char *operation, const char *mod_root) {
    wo_log_end();
    AcquireSRWLockExclusive(&log_lock);
    log_started = now_ticks();
    log_system_written = false;
    log_root = wo_path_full(mod_root, NULL);
    log_profile = env_path(L"USERPROFILE");
    log_temp = env_path(L"TEMP");

    SYSTEMTIME local;
    GetLocalTime(&local);
    char name[160];
    snprintf(name, sizeof(name), "%s-%04u%02u%02u-%02u%02u%02u-%03u-%lu.log", operation,
             local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
             local.wMilliseconds, (unsigned long)GetCurrentProcessId());
    char *candidates[2] = {NULL, NULL};
    if (log_root) {
        candidates[0] = wo_format(NULL, "%s\\logs\\%s", log_root, name);
    }
    if (log_temp) {
        candidates[1] = wo_format(NULL, "%s\\WO4K\\logs\\%s", log_temp, name);
    }
    for (size_t i = 0; i < 2 && log_file == INVALID_HANDLE_VALUE; i++) {
        if (candidates[i]) {
            log_file = open_append(candidates[i]);
            if (log_file != INVALID_HANDLE_VALUE) {
                log_path = candidates[i];
                candidates[i] = NULL;
            }
        }
    }
    wo_free(candidates[0]);
    wo_free(candidates[1]);
    bool ok = log_file != INVALID_HANDLE_VALUE;
    ReleaseSRWLockExclusive(&log_lock);
    if (ok) {
        WoJson *data = wo_json_new_object();
        wo_json_put_str(data, "operation", operation);
        wo_json_put_str(data, "mod_version", WO_VERSION_STRING);
        wo_json_put_str(data, "privacy",
                        "Local only; no identifiers, network addresses, account names or "
                        "automatic uploads.");
        wo_log_event("session", data);
    }
    return ok;
}

const char *wo_log_path(void) {
    return log_path;
}

void wo_log_end(void) {
    AcquireSRWLockExclusive(&log_lock);
    if (log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }
    wo_free(log_path);
    wo_free(log_root);
    wo_free(log_profile);
    wo_free(log_temp);
    log_path = log_root = log_profile = log_temp = NULL;
    ReleaseSRWLockExclusive(&log_lock);
}

/* Case-insensitive (ASCII) replacement of every occurrence of needle. */
static bool replace_all(WoBuf *out, const char *text, const char *needle, const char *with) {
    size_t needle_len = strlen(needle);
    const char *at = text;
    while (*at) {
        if (needle_len > 0 && wo_str_istarts_with(at, needle)) {
            if (!wo_buf_append_str(out, with, NULL)) {
                return false;
            }
            at += needle_len;
        } else if (!wo_buf_append_byte(out, (uint8_t)*at++, NULL)) {
            return false;
        }
    }
    return wo_buf_terminate(out, NULL);
}

static char *replace_path(char *text, const char *path, const char *token) {
    if (!text || !path || strlen(path) < 4) {
        return text;
    }
    /* Both the plain form and the JSON-escaped form (doubled backslashes). */
    WoBuf escaped_path = {0};
    for (const char *p = path; *p; p++) {
        wo_buf_append_str(&escaped_path, *p == '\\' ? "\\\\" : (char[2]){*p, 0}, NULL);
    }
    wo_buf_terminate(&escaped_path, NULL);
    const char *variants[2] = {(const char *)escaped_path.data, path};
    for (size_t i = 0; i < 2 && text; i++) {
        if (!variants[i]) {
            continue;
        }
        WoBuf out = {0};
        if (replace_all(&out, text, variants[i], token)) {
            wo_free(text);
            text = wo_buf_detach(&out);
        } else {
            wo_buf_free(&out);
        }
    }
    wo_buf_free(&escaped_path);
    return text;
}

char *wo_log_redact(const char *text) {
    char *copy = wo_strdup(text, NULL);
    /* Most specific first: the mod folder usually lives inside the profile. */
    copy = replace_path(copy, log_root, "%MODROOT%");
    copy = replace_path(copy, log_temp, "%TEMP%");
    copy = replace_path(copy, log_profile, "%USERPROFILE%");
    return copy;
}

static void write_line(const char *line) {
    AcquireSRWLockExclusive(&log_lock);
    if (log_file != INVALID_HANDLE_VALUE) {
        size_t len = strlen(line);
        DWORD wrote = 0;
        if (len < UINT32_MAX) {
            WriteFile(log_file, line, (DWORD)len, &wrote, NULL);
        }
    }
    ReleaseSRWLockExclusive(&log_lock);
}

void wo_log_event(const char *event, WoJson *data) {
    if (log_file == INVALID_HANDLE_VALUE) {
        wo_json_free(data);
        return;
    }
    char stamp[40];
    wo_log_timestamp(stamp);
    WoJson *entry = wo_json_new_object();
    wo_json_put_str(entry, "time", stamp);
    wo_json_put_str(entry, "event", event);
    wo_json_put(entry, "data", data ? data : wo_json_new_null());
    WoBuf line = {0};
    if (entry && wo_json_write(entry, false, &line, NULL) &&
        wo_buf_append_str(&line, "\r\n", NULL) && wo_buf_terminate(&line, NULL)) {
        char *safe = wo_log_redact((const char *)line.data);
        if (safe) {
            write_line(safe);
            wo_free(safe);
        }
    }
    wo_buf_free(&line);
    wo_json_free(entry);
}

void wo_log_text(const char *event, const char *text) {
    wo_log_event(event, wo_json_new_string(text));
}

void wo_log_stage(const char *stage) {
    wo_log_text("stage", stage);
}

static const char *kind_name(WoErrorKind kind) {
    static const char *names[] = {"none",     "internal", "io",          "not_found", "access",
                                  "sharing",  "format",   "unsupported", "checksum",  "limit",
                                  "unsafe",   "network",  "busy",        "cancelled"};
    return (size_t)kind < WO_COUNT(names) ? names[kind] : "unknown";
}

void wo_log_failure(const char *stage, const WoError *error) {
    WoJson *data = wo_json_new_object();
    wo_json_put_str(data, "stage", stage);
    wo_json_put_str(data, "message", error ? error->message : "");
    wo_json_put_str(data, "kind", kind_name(error ? error->kind : WO_ERR_INTERNAL));
    if (error && error->code) {
        char code[16];
        snprintf(code, sizeof(code), "0x%08lX", (unsigned long)error->code);
        wo_json_put_str(data, "code", code);
    }
    wo_log_event("failure", data);
    if (!log_system_written) {
        log_system_written = true;
        wo_log_event("system", wo_log_system_info());
    }
}

/* --- System information ------------------------------------------------------- */

static char *registry_string(HKEY root, const wchar_t *key, const wchar_t *value) {
    wchar_t buffer[256];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, NULL, buffer, &size) != ERROR_SUCCESS) {
        return NULL;
    }
    buffer[WO_COUNT(buffer) - 1] = 0;
    return wo_utf8(buffer, NULL);
}

static DWORD registry_dword(HKEY root, const wchar_t *key, const wchar_t *value) {
    DWORD data = 0;
    DWORD size = sizeof(data);
    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, NULL, &data, &size) != ERROR_SUCCESS) {
        return 0;
    }
    return data;
}

static void put_owned_str(WoJson *object, const char *key, char *text) {
    if (text) {
        /* Trim the padding some CPU names carry. */
        char *start = text;
        while (*start == ' ') {
            start++;
        }
        size_t len = strlen(start);
        while (len > 0 && start[len - 1] == ' ') {
            start[--len] = 0;
        }
        wo_json_put_str(object, key, start);
        wo_free(text);
    }
}

static unsigned physical_cores(void) {
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &size);
    uint8_t *buffer = size ? wo_alloc(size, NULL) : NULL;
    unsigned cores = 0;
    if (buffer && GetLogicalProcessorInformationEx(
                      RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer,
                      &size)) {
        for (DWORD at = 0; at < size;) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX item =
                (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer + at);
            if (item->Size == 0) {
                break;
            }
            cores++;
            at += item->Size;
        }
    }
    wo_free(buffer);
    return cores;
}

static const IID iid_dxgi_factory1 = {
    0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
static const IID iid_dxgi_device = {
    0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};

static WoJson *gpu_list(void) {
    WoJson *list = wo_json_new_array();
    IDXGIFactory1 *factory = NULL;
    if (FAILED(CreateDXGIFactory1(&iid_dxgi_factory1, (void **)&factory))) {
        return list;
    }
    for (UINT i = 0; i < 8; i++) {
        IDXGIAdapter1 *adapter = NULL;
        if (IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != S_OK) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc)) &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            WoJson *gpu = wo_json_new_object();
            put_owned_str(gpu, "model", wo_utf8(desc.Description, NULL));
            LARGE_INTEGER version;
            if (SUCCEEDED(IDXGIAdapter1_CheckInterfaceSupport(adapter, &iid_dxgi_device,
                                                              &version))) {
                char text[48];
                snprintf(text, sizeof(text), "%u.%u.%u.%u",
                         (unsigned)HIWORD(version.HighPart), (unsigned)LOWORD(version.HighPart),
                         (unsigned)HIWORD(version.LowPart), (unsigned)LOWORD(version.LowPart));
                wo_json_put_str(gpu, "driver", text);
            }
            wo_json_push(list, gpu);
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    return list;
}

WoJson *wo_log_system_info(void) {
    static const wchar_t *nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    WoJson *info = wo_json_new_object();
    char *product = registry_string(HKEY_LOCAL_MACHINE, nt, L"ProductName");
    char *build = registry_string(HKEY_LOCAL_MACHINE, nt, L"CurrentBuildNumber");
    DWORD revision = registry_dword(HKEY_LOCAL_MACHINE, nt, L"UBR");
    uint64_t build_number = 0;
    if (build) {
        wo_parse_u64(build, 1000000, &build_number);
    }
    /* Windows 11 still reports "Windows 10" as its product name. */
    if (product && build_number >= 22000 && wo_str_istarts_with(product, "Windows 10")) {
        product[9] = '1';
    }
    put_owned_str(info, "windows", product);
    put_owned_str(info, "version", registry_string(HKEY_LOCAL_MACHINE, nt, L"DisplayVersion"));
    if (build) {
        char text[48];
        snprintf(text, sizeof(text), "%s.%lu", build, (unsigned long)revision);
        wo_json_put_str(info, "build", text);
        wo_free(build);
    }
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    const char *architecture = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64"
                               : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64
                                   ? "ARM64"
                                   : "other";
    wo_json_put_str(info, "architecture", architecture);

    WoJson *cpu = wo_json_new_object();
    put_owned_str(cpu, "model",
                  registry_string(HKEY_LOCAL_MACHINE,
                                  L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                  L"ProcessorNameString"));
    wo_json_put_int(cpu, "cores", physical_cores());
    wo_json_put_int(cpu, "logical_processors", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    wo_json_put(info, "cpu", cpu);

    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        double gib = (double)memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        wo_json_put(info, "ram_gib", wo_json_new_number((double)(int64_t)(gib * 100 + 0.5) / 100));
    }
    wo_json_put(info, "gpu", gpu_list());
    return info;
}

/* --- Attachments ----------------------------------------------------------------- */

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool guid_at(const char *s) {
    static const int groups[] = {8, 4, 4, 4, 12};
    for (size_t g = 0; g < WO_COUNT(groups); g++) {
        for (int i = 0; i < groups[g]; i++) {
            if (!is_hex(*s++)) {
                return false;
            }
        }
        if (g + 1 < WO_COUNT(groups) && *s++ != '-') {
            return false;
        }
    }
    return true;
}

static bool sensitive_line(const char *line, size_t len) {
    static const char *markers[] = {"DeviceIdentifier", "PNPDevice", "AdapterLuid", "SerialNumber"};
    for (size_t m = 0; m < WO_COUNT(markers); m++) {
        size_t marker_len = strlen(markers[m]);
        for (size_t i = 0; i + marker_len <= len; i++) {
            if (wo_str_istarts_with(line + i, markers[m])) {
                return true;
            }
        }
    }
    return false;
}

void wo_log_attach(const char *name, const char *path, bool current_session_only) {
    if (!log_path) {
        return;
    }
    WoFileInfo info;
    if (!wo_stat(path, &info, NULL) || !info.exists || info.is_dir) {
        return;
    }
    /* File times come from a coarse clock; allow two seconds of slack. */
    uint64_t slack = 2ull * 10000000ull;
    if (current_session_only && info.mtime + slack < log_started) {
        return;
    }
    char *text = NULL;
    WoError error = {0};
    if (!wo_read_text(path, ATTACHMENT_MAX_BYTES, &text, &error)) {
        WoJson *data = wo_json_new_object();
        wo_json_put_str(data, "file", name);
        wo_json_put_str(data, "message", error.message);
        wo_log_event("game_log_unavailable", data);
        return;
    }
    WoBuf clean = {0};
    wo_buf_printf(&clean, NULL, "# WO4K mod_version=%s\r\n", WO_VERSION_STRING);
    WoJson *tail = wo_json_new_array();
    const char *line = text;
    while (*line) {
        const char *end = line + strcspn(line, "\n");
        size_t len = (size_t)(end - line);
        if (!sensitive_line(line, len)) {
            for (size_t i = 0; i < len; i++) {
                if (i + 36 <= len && guid_at(line + i)) {
                    wo_buf_append_str(&clean, "[identifier omitted]", NULL);
                    i += 35;
                } else {
                    wo_buf_append_byte(&clean, (uint8_t)line[i], NULL);
                }
            }
            wo_buf_append_byte(&clean, '\n', NULL);
            size_t trimmed = len;
            while (trimmed > 0 && (line[trimmed - 1] == '\r' || line[trimmed - 1] == ' ')) {
                trimmed--;
            }
            if (trimmed > 0) {
                char *copy = wo_strndup(line, trimmed, NULL);
                if (copy) {
                    if (wo_json_length(tail) == TAIL_LINES) {
                        wo_json_free(tail->items[0]);
                        memmove(tail->items, tail->items + 1,
                                (tail->count - 1) * sizeof(*tail->items));
                        tail->count--;
                    }
                    wo_json_push_str(tail, copy);
                    wo_free(copy);
                }
            }
        }
        line = *end ? end + 1 : end;
    }
    wo_free(text);
    wo_buf_terminate(&clean, NULL);
    char *safe = wo_log_redact(clean.data ? (const char *)clean.data : "");
    wo_buf_free(&clean);
    char *target = wo_format(NULL, "%.*s.%s", (int)(strlen(log_path) - strlen(".log")), log_path,
                             name);
    bool saved = safe && target && wo_write_file(target, safe, strlen(safe), NULL);
    if (saved) {
        wo_delete_file(path, NULL);
        WoJson *data = wo_json_new_object();
        wo_json_put_str(data, "file", wo_path_basename(target));
        wo_log_event("game_log_saved", data);
        WoJson *tail_data = wo_json_new_object();
        wo_json_put_str(tail_data, "file", wo_path_basename(target));
        wo_json_put(tail_data, "lines", tail);
        tail = NULL;
        wo_log_event("game_log_tail", tail_data);
    }
    wo_json_free(tail);
    wo_free(target);
    wo_free(safe);
}
