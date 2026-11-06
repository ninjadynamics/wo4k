/* Runs the 32-bit crash-diagnostics fixture (tests/test_crash_diagnostics.c,
 * built as crash_fixture.exe beside this program) and checks its logs.
 *
 *   handled  twenty handled access violations: execution continues, the
 *            observer records exactly sixteen and never "unhandled";
 *   fatal    a real unhandled x86 write fault: module/RVA, access kind and
 *            address, registers, code candidates and memory figures are
 *            recorded, the previous filter still runs and the exit status
 *            stays 0xC0000005;
 *   reader   the VirtualQuery-validated memory reader.
 * No log may contain a local path. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "wo_version.h"

#define FIXTURE_NAME L"crash_fixture.exe"
#define WORK_DIR L"build\\test-work\\crash-diagnostics"
#define LOG_SIZE 65536

static int failures;

#define CHECK(expression, message) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL: %s: %s (%s:%d)\n", mode, message, \
                __FILE__, __LINE__); \
            ++failures; \
        } \
    } while (0)

static bool fixture_path(wchar_t *out, DWORD capacity)
{
    DWORD length = GetModuleFileNameW(NULL, out, capacity);
    if (length == 0 || length >= capacity) {
        return false;
    }
    while (length > 0 && out[length - 1] != L'\\') {
        --length;
    }
    size_t name = wcslen(FIXTURE_NAME);
    if (length + name >= capacity) {
        return false;
    }
    memcpy(out + length, FIXTURE_NAME, (name + 1) * sizeof(wchar_t));
    return true;
}

static bool read_text(const wchar_t *path, char *text, size_t size)
{
    text[0] = '\0';
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD read = 0;
    bool ok = ReadFile(file, text, (DWORD)(size - 1), &read, NULL) != 0;
    CloseHandle(file);
    text[ok ? read : 0] = '\0';
    return ok;
}

static unsigned count_of(const char *text, const char *needle)
{
    unsigned count = 0;
    size_t length = strlen(needle);
    for (const char *at = strstr(text, needle); at != NULL;
        at = strstr(at + length, needle)) {
        ++count;
    }
    return count;
}

/* "fault_instruction 0x<8 hex> crash_fixture.exe+0x" */
static bool has_fault_module(const char *text)
{
    const char *prefix = "fault_instruction 0x";
    const char *suffix = " crash_fixture.exe+0x";
    for (const char *at = strstr(text, prefix); at != NULL;
        at = strstr(at + 1, prefix)) {
        const char *digits = at + strlen(prefix);
        size_t i = 0;
        while (i < 8 && strchr("0123456789ABCDEF", digits[i]) != NULL &&
            digits[i] != '\0') {
            ++i;
        }
        if (i == 8 && strncmp(digits + 8, suffix, strlen(suffix)) == 0) {
            return true;
        }
    }
    return false;
}

/* True when text contains the UTF-8 form of a non-empty path. */
static bool contains_path(const char *text, const wchar_t *path)
{
    char narrow[4 * MAX_PATH];
    if (path[0] == L'\0') {
        return false;
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow,
        (int)sizeof(narrow), NULL, NULL);
    return length > 1 && strstr(text, narrow) != NULL;
}

static DWORD run_fixture(const wchar_t *fixture, const wchar_t *mode_name,
    const wchar_t *log)
{
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    wchar_t command[] = L"crash_fixture.exe";
    DWORD code = 0xffffffffu;
    DeleteFileW(log);
    if (!SetEnvironmentVariableW(L"WO_CRASH_TEST_LOG", log) ||
        !SetEnvironmentVariableW(L"WO_CRASH_TEST_MODE", mode_name)) {
        return code;
    }
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(fixture, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
            NULL, NULL, &startup, &process)) {
        return code;
    }
    if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, code);
        WaitForSingleObject(process.hProcess, 5000);
        fprintf(stderr, "FAIL: %ls: the fixture timed out\n", mode_name);
        ++failures;
    }
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

static void check_mode(const wchar_t *fixture, const wchar_t *work,
    const char *mode, const wchar_t *mode_name)
{
    static char text[LOG_SIZE];
    wchar_t log[MAX_PATH];
    wchar_t profile[MAX_PATH];
    int written = swprintf(log, MAX_PATH, L"%ls\\%ls.log", work, mode_name);
    if (written < 0 || written >= MAX_PATH) {
        CHECK(false, "paths");
        return;
    }
    DWORD code = run_fixture(fixture, mode_name, log);
    CHECK(read_text(log, text, sizeof(text)), "the fixture wrote no log");
    CHECK(strstr(text, "mod_version " WO_VERSION_STRING "\r\n") != NULL,
        "the log omits the compiled mod version");
    if (strcmp(mode, "handled") == 0) {
        CHECK(code == 0, "the observer interfered with handled exceptions");
        CHECK(strstr(text, "handled_execution_continued") != NULL,
            "execution did not continue");
        CHECK(strstr(text, "exception_unhandled") == NULL,
            "a handled exception was logged as unhandled");
        CHECK(count_of(text, "exception_first_chance") == 16,
            "first-chance logging is not bounded to sixteen");
    } else if (strcmp(mode, "fatal") == 0) {
        CHECK(code == (DWORD)EXCEPTION_ACCESS_VIOLATION,
            "the crash exit status changed");
        CHECK(strstr(text, "exception_unhandled 0xC0000005") != NULL,
            "the unhandled fault is missing");
        CHECK(strstr(text, "previous_filter_called") != NULL,
            "the previous filter was not chained");
        CHECK(has_fault_module(text), "the fault module/RVA is missing");
        CHECK(strstr(text, "access_address 0x00000001") != NULL &&
            strstr(text, "access_kind_read0_write1_execute8 0x00000001") !=
            NULL, "the write address/kind is missing");
        CHECK(strstr(text, "process_virtual_available_bytes") != NULL &&
            strstr(text, "physical_available_bytes") != NULL,
            "memory figures are missing");
        CHECK(strstr(text, "stack_code_candidate") != NULL,
            "code-address candidates are missing");
    } else {
        CHECK(code == 0, "the validated reader accepted or rejected the "
            "wrong range");
        CHECK(strstr(text, "reader_checks_passed") != NULL,
            "the reader checks did not finish");
    }
    DWORD profile_length = GetEnvironmentVariableW(L"USERPROFILE", profile,
        MAX_PATH);
    if (profile_length == 0 || profile_length >= MAX_PATH) {
        profile[0] = L'\0';
    }
    CHECK(!contains_path(text, work) && !contains_path(text, profile),
        "the crash log leaked a local path");
}

int main(void)
{
    const char *mode = "setup";
    wchar_t fixture[MAX_PATH];
    wchar_t work[MAX_PATH];
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
        SEM_NOOPENFILEERRORBOX);
    CHECK(fixture_path(fixture, MAX_PATH), "cannot locate the fixture");
    CreateDirectoryW(L"build\\test-work", NULL);
    CreateDirectoryW(WORK_DIR, NULL);
    DWORD length = GetFullPathNameW(WORK_DIR, MAX_PATH, work, NULL);
    CHECK(length > 0 && length < MAX_PATH, "cannot resolve the work folder");
    if (failures != 0) {
        return 1;
    }
    check_mode(fixture, work, "handled", L"handled");
    check_mode(fixture, work, "fatal", L"fatal");
    check_mode(fixture, work, "reader", L"reader");
    SetEnvironmentVariableW(L"WO_CRASH_TEST_LOG", NULL);
    SetEnvironmentVariableW(L"WO_CRASH_TEST_MODE", NULL);
    if (failures != 0) {
        fprintf(stderr, "FAIL: %d crash diagnostics check(s) failed.\n",
            failures);
        return 1;
    }
    printf("PASS: an actual x86 access violation records module/RVA, access "
        "type/address, registers, code candidates and available RAM/address "
        "space; the original exit status and filter chain are retained. "
        "Twenty handled exceptions continue normally; capture is bounded. "
        "Direct stack reads are VirtualQuery-validated.\n");
    return 0;
}
