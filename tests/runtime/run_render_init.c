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

/* The fixture's movie bridge check (WO_TEST_BRIDGE_ROOT) runs the real
 * launch code in src/runtime/movies_patch.c. Point it at a folder whose
 * wo4k-video.exe is the windowless stand-in, never the player. */
static bool prepare_bridge(const wchar_t *work, wchar_t *bridge)
{
    wchar_t standin[PATH_SIZE];
    wchar_t player[PATH_SIZE];
    wchar_t movies[PATH_SIZE];
    wchar_t movie[PATH_SIZE];
    wchar_t result[PATH_SIZE];
    wchar_t root[PATH_SIZE];
    if (!beside_self(standin, L"video_bridge_standin.exe") ||
        !join(bridge, work, L"bridge") || !join(player, bridge,
            L"wo4k-video.exe") || !join(movies, bridge, L"movies") ||
        !join(movie, movies, L"WXLINTR.MPG") ||
        !join(result, bridge, L"bridge-result.txt")) {
        return false;
    }
    CreateDirectoryW(bridge, NULL);
    CreateDirectoryW(movies, NULL);
    DeleteFileW(result);
    /* movies_patch.c only checks that a movie file exists. */
    if (!CopyFileW(standin, player, FALSE) || !write_ascii(movie, "")) {
        return false;
    }
    int written = swprintf(root, PATH_SIZE, L"%ls\\", bridge);
    return written > 0 && written < PATH_SIZE &&
        SetEnvironmentVariableW(L"WO_TEST_BRIDGE_ROOT", root);
}

static bool check_bridge(const wchar_t *bridge)
{
    static const char *const expected[] = {
        "arguments_parsed 1", "game_root_matches 1", "movie_is_intro 1",
        "game_pid_is_game 1", "not_probe 1", "skip_event_inherited 1",
        "repeat_ignored 1", "skip_signaled 1"
    };
    wchar_t path[PATH_SIZE];
    char text[1024];
    DWORD read = 0;
    if (!join(path, bridge, L"bridge-result.txt")) {
        return false;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "FAIL: the runtime never started the movie player\n");
        return false;
    }
    bool ok = ReadFile(file, text, sizeof(text) - 1, &read, NULL) != 0;
    CloseHandle(file);
    text[ok ? read : 0] = '\0';
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        if (strstr(text, expected[i]) == NULL) {
            fprintf(stderr, "FAIL: movie launch check failed: %s\n%s",
                expected[i], text);
            ok = false;
        }
    }
    return ok;
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
            L"assets\\auth") ||
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
        SetEnvironmentVariableW(L"WO2097_SIM60_TEST", L"0");
    /* Never an inherited bridge root: it could start the real, full-screen
     * movie player. The stand-in is set up below, for the final run only. */
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
    wchar_t bridge[PATH_SIZE];
    if (!prepare_bridge(work, bridge)) {
        fprintf(stderr, "FAIL: cannot prepare the movie launch stand-in\n");
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
    if (!check_bridge(bridge)) {
        return 1;
    }
    printf("PASS: the runtime starts wo4k-video.exe from the game folder with "
        "a command line the player parses and a skip event inherited through "
        "a handle list; a fresh Esc in the game's message loop signals it, "
        "an auto-repeat does not.\n");
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
