/* Runs the 32-bit renderer integration fixture (tests/test_render_init.c,
 * built as render_init_fixture.exe beside this program). The fixture maps a
 * private copy of the game executable and runs the production hook
 * initialisation against it; it never starts the game, and no process, GPU
 * or user setting changes.
 *
 * The game executable is not part of the repository. The test reads it from
 * WO4K_TEST_GAME_ROOT, else wobble, and skips when Wipeout2097.exe is absent.
 * WO_TEST_GPU_MATERIALS=1 adds the offscreen DDrawCompat material checks
 * (they need runtime\ddraw.dll). Run from the repository root. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define FIXTURE_NAME L"render_init_fixture.exe"
#define WORK_DIR L"build\\test-work\\render-init"
#define PATH_SIZE 1024

static bool join(wchar_t *out, const wchar_t *directory, const wchar_t *name)
{
    int written = swprintf(out, PATH_SIZE, L"%ls\\%ls", directory, name);
    return written > 0 && written < PATH_SIZE;
}

static bool is_file(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool full_path(const wchar_t *path, wchar_t *out)
{
    DWORD length = GetFullPathNameW(path, PATH_SIZE, out, NULL);
    return length > 0 && length < PATH_SIZE;
}

/* A file beside this program (the build's test folder). */
static bool beside_self(wchar_t *out, const wchar_t *name)
{
    DWORD length = GetModuleFileNameW(NULL, out, PATH_SIZE);
    if (length == 0 || length >= PATH_SIZE) {
        return false;
    }
    while (length > 0 && out[length - 1] != L'\\') {
        --length;
    }
    out[length] = L'\0';
    size_t name_length = wcslen(name);
    if (length + name_length >= PATH_SIZE) {
        return false;
    }
    memcpy(out + length, name, (name_length + 1) * sizeof(wchar_t));
    return true;
}

static bool set_path(const wchar_t *name, const wchar_t *directory,
    const wchar_t *file)
{
    wchar_t path[PATH_SIZE];
    return join(path, directory, file) && SetEnvironmentVariableW(name, path);
}

static bool write_ascii(const wchar_t *path, const char *text)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    DWORD length = (DWORD)strlen(text);
    bool ok = WriteFile(file, text, length, &written, NULL) && written == length;
    CloseHandle(file);
    return ok;
}

static DWORD run_fixture(const wchar_t *fixture, const wchar_t *directory)
{
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    wchar_t command[] = L"render_init_fixture.exe";
    DWORD code = 0xffffffffu;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(fixture, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
            NULL, directory, &startup, &process)) {
        return code;
    }
    if (WaitForSingleObject(process.hProcess, 300000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, code);
        WaitForSingleObject(process.hProcess, 5000);
        fprintf(stderr, "FAIL: the renderer fixture timed out\n");
    }
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

static const char ddraw_ini_original[] = "LogLevel = none\r\n"
    "ResolutionScale = off\r\nTextureFilter = bilinear\r\n"
    "ColorKeyMethod = alphatest(1)\r\n";
static const char ddraw_ini_cutoff[] = "LogLevel = none\r\n"
    "ResolutionScale = off\r\nTextureFilter = bilinear\r\n"
    "ColorKeyMethod = alphatest(128)\r\n";

/* The optional GPU pass first records the original sprite-edge baseline. */
static bool prepare_gpu(const wchar_t *fixture, const wchar_t *work,
    const wchar_t *repository)
{
    wchar_t ini[PATH_SIZE];
    wchar_t report[PATH_SIZE];
    wchar_t baseline[PATH_SIZE];
    wchar_t runtime[PATH_SIZE];
    if (!join(runtime, repository, L"runtime") ||
        !set_path(L"WO_TEST_DDRAW_WRAPPER", runtime, L"ddraw.dll") ||
        !set_path(L"WO_TEST_MATERIAL_REPORT", work,
            L"world-material-gpu.bin") ||
        !join(ini, work, L"DDrawCompat.ini") ||
        !join(report, work, L"world-material-gpu.bin") ||
        !join(baseline, work, L"world-material-edge-original.bin") ||
        !write_ascii(ini, ddraw_ini_original) ||
        !SetEnvironmentVariableW(L"WO_TEST_EDGE_REF", L"1")) {
        fprintf(stderr, "FAIL: cannot prepare the GPU material pass\n");
        return false;
    }
    DWORD code = run_fixture(fixture, work);
    if (code != 0) {
        fprintf(stderr, "FAIL: original sprite-edge baseline failed: %lu\n",
            (unsigned long)code);
        return false;
    }
    if (!CopyFileW(report, baseline, FALSE) ||
        !write_ascii(ini, ddraw_ini_cutoff) ||
        !SetEnvironmentVariableW(L"WO_TEST_EDGE_REF", L"128")) {
        fprintf(stderr, "FAIL: cannot prepare the cutoff pass\n");
        return false;
    }
    return true;
}

int main(void)
{
    wchar_t repository[PATH_SIZE];
    wchar_t configured[PATH_SIZE];
    wchar_t game[PATH_SIZE];
    wchar_t executable[PATH_SIZE];
    wchar_t work[PATH_SIZE];
    wchar_t fixture[PATH_SIZE];
    wchar_t assets[PATH_SIZE];
    wchar_t race_art[PATH_SIZE];
    wchar_t regions[PATH_SIZE];
    wchar_t flag[8];
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
        SEM_NOOPENFILEERRORBOX);
    DWORD length = GetEnvironmentVariableW(L"WO4K_TEST_GAME_ROOT", configured,
        PATH_SIZE);
    const wchar_t *root = length > 0 && length < PATH_SIZE ? configured :
        L"wobble";
    if (!full_path(L".", repository) || !full_path(root, game) ||
        !join(executable, game, L"Wipeout2097.exe") ||
        !beside_self(fixture, FIXTURE_NAME)) {
        fprintf(stderr, "FAIL: invalid paths\n");
        return 1;
    }
    if (!is_file(executable)) {
        printf("SKIP: no Wipeout2097.exe in %ls (set WO4K_TEST_GAME_ROOT); "
            "renderer integration skipped.\n", game);
        return 0;
    }
    CreateDirectoryW(L"build\\test-work", NULL);
    CreateDirectoryW(WORK_DIR, NULL);
    if (!full_path(WORK_DIR, work) || !join(assets, repository,
            L"assets\\hd") ||
        !join(race_art, game, L"textures\\sd\\Pics\\Hi-res\\NRACE01.TGA") ||
        !join(regions, game, L"textures\\hd-regions.bin")) {
        fprintf(stderr, "FAIL: invalid work paths\n");
        return 1;
    }
    /* Outputs of an earlier run must not be mistaken for this one's. */
    const wchar_t *const stale[] = {
        L"test-settings.ini", L"test-settings.ini.fault", L"title-bounds.bin",
        L"world-material-gpu.bin", L"world-material-edge-original.bin"
    };
    for (size_t i = 0; i < sizeof(stale) / sizeof(stale[0]); ++i) {
        wchar_t path[PATH_SIZE];
        if (join(path, work, stale[i])) {
            DeleteFileW(path);
        }
    }
    bool hd_regions = is_file(race_art) && is_file(regions);
    bool ready = SetEnvironmentVariableW(L"WO_TEST_GAME_ROOT", game) &&
        set_path(L"WO_TEST_SETTINGS", work, L"test-settings.ini") &&
        set_path(L"WO_TEST_BOUNDS", work, L"title-bounds.bin") &&
        SetEnvironmentVariableW(L"WO_TEST_FRAME_ART_ROOT", assets) &&
        SetEnvironmentVariableW(L"WO_TEST_HD_REGION_ASSETS",
            hd_regions ? L"1" : NULL) &&
        SetEnvironmentVariableW(L"WO2097_RENDER_TEST", L"1") &&
        SetEnvironmentVariableW(L"WO2097_DISTANCE_TEST", L"1") &&
        SetEnvironmentVariableW(L"WO2097_REFRESH_TEST", L"1") &&
        SetEnvironmentVariableW(L"WO2097_SIM60_TEST", L"0");
    /* Never the bridge smoke test: it would start the real, full-screen
     * movie player. */
    SetEnvironmentVariableW(L"WO_TEST_BRIDGE_ROOT", NULL);
    SetEnvironmentVariableW(L"WO_TEST_DDRAW_WRAPPER", NULL);
    SetEnvironmentVariableW(L"WO_TEST_MATERIAL_REPORT", NULL);
    if (!ready) {
        fprintf(stderr, "FAIL: cannot set the fixture environment\n");
        return 1;
    }
    length = GetEnvironmentVariableW(L"WO_TEST_GPU_MATERIALS", flag, 8);
    bool gpu = length == 1 && flag[0] == L'1';
    if (gpu && !prepare_gpu(fixture, work, repository)) {
        return 1;
    }
    DWORD code = run_fixture(fixture, gpu ? work : NULL);
    if (code == 10) {
        fprintf(stderr, "FAIL: integration failure reproduced: full renderer "
            "installed, but race UI hooks never activated.\n");
        return 1;
    }
    if (code != 0) {
        fprintf(stderr, "FAIL: renderer initialization integration test "
            "failed: %lu\n", (unsigned long)code);
        return 1;
    }
    if (gpu) {
        printf("PASS: offscreen GPU material, sprite-edge and effect blend "
            "checks.\n");
    }
    if (hd_regions) {
        printf("PASS: all authored HD region placements decode and compose "
            "at their exact native locations.\n");
    }
    printf("PASS: production renderer hooks initialize against the original "
        "x86 image: world packets, shadows, team stats, explosions, native "
        "transform finish, scene lighting keys, title presentation, startup, "
        "HD headers, menu frames, HUD layout, all four ending-movie triggers "
        "and the intro-before-title order.\n");
    return 0;
}
