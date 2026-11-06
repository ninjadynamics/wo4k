/* Play.exe: check and refresh the installation, start the game, wait for it
 * and keep a diagnostic log. Failures are explained in a dialog that names
 * the log; crash details come from the runtime's own logs and the Windows
 * Application Error event for the game process.
 *
 *   Play.exe [--refresh-only] [--no-error-dialog] [--root <mod folder>]
 *
 * --root runs against another mod folder (for testing and measurements). */
#include "core/wo_win.h"

#include <psapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <winevt.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hashcache.h"
#include "core/wo_log.h"
#include "core/wo_mod.h"
#include "core/wo_net.h"
#include "core/wo_ops.h"
#include "core/wo_settings.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show);

typedef struct PlayOptions {
    bool refresh_only;
    bool no_dialog;
    bool purge; /* --purge: delete every log, crash logs included, and exit */
    char *root; /* --root <dir> (testing), else NULL for the EXE's folder */
} PlayOptions;

static void parse_arguments(PlayOptions *options) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (wcscmp(argv[i], L"--refresh-only") == 0) {
            options->refresh_only = true;
        } else if (wcscmp(argv[i], L"--no-error-dialog") == 0) {
            options->no_dialog = true;
        } else if (wcscmp(argv[i], L"--purge") == 0) {
            options->purge = true;
        } else if (wcscmp(argv[i], L"--root") == 0 && i + 1 < argc && !options->root) {
            i++;
            char *given = wo_utf8(argv[i], NULL);
            options->root = given ? wo_path_full(given, NULL) : NULL;
            wo_free(given);
        }
    }
    LocalFree(argv);
}

static void show_failure(const PlayOptions *options, const char *message) {
    if (options->no_dialog) {
        return;
    }
    const char *log = wo_log_path();
    char *text = wo_format(NULL,
                           "%s\r\n\r\nDiagnostic log:\r\n%s\r\n\r\nShare this log with the WO4K "
                           "developer. It stays on your PC until you choose to share it.",
                           message, log ? log : "(unavailable)");
    wchar_t *wide = wo_wide(text ? text : message, NULL);
    MessageBoxW(NULL, wide ? wide : L"WO4K could not start.", L"WO4K - operation failed",
                MB_OK | MB_ICONERROR);
    wo_free(wide);
    wo_free(text);
}

static char *exit_description(DWORD code) {
    const char *hint;
    switch (code) {
    case 0xC0000135:
        hint = "A required DLL could not be loaded.";
        break;
    case 0xC000007B:
        hint = "A required executable or DLL has an invalid format.";
        break;
    case 0xC0000428:
        hint = "Windows could not verify a file signature; check Windows Security and Code "
               "Integrity events.";
        break;
    case 0xC0000022:
        hint = "Access was denied; check Windows Security for a blocked file.";
        break;
    case 0xC0000005:
        hint = "The game crashed with an access violation.";
        break;
    default:
        hint = "Check this session log and Windows Security / Event Viewer for details.";
        break;
    }
    return wo_format(NULL, "The game exited with code 0x%08lX (%ld). %s", (unsigned long)code,
                     (long)(LONG)code, hint);
}

/* Signature verdict for a digest: remembered in the integrity cache, so
 * WinVerifyTrust only runs for a binary it has not judged before. */
static const char *signature_status(WoHashCache *cache, const char *path, const char *digest) {
    const char *known = wo_json_str(wo_hash_cache_facts(cache, digest, "signature"), "status",
                                    NULL);
    if (known && (wo_str_eq(known, "valid") || wo_str_eq(known, "none"))) {
        return known;
    }
    WoSignature signature;
    if (!wo_signature_check(path, &signature, NULL)) {
        return "unknown";
    }
    const char *status = signature.trusted ? "valid" : "none";
    WoJson *facts = wo_json_new_object();
    if (facts && wo_json_put_str(facts, "status", status)) {
        wo_hash_cache_set_facts(cache, digest, "signature", facts);
    } else {
        wo_json_free(facts);
    }
    return status;
}

static void log_dependencies(const WoMod *mod, WoError *error) {
    static const char *const names[] = {"Wipeout2097.exe", "wohr.dll", "ddraw.dll",
                                        "wo2097-controller.exe", "wo4k-video.exe"};
    /* Reconcile has just checked these files, so their digests are cached. */
    char *cache_path = wo_path_child(mod->state, "integrity.json", NULL);
    const char *note = NULL;
    WoHashCache *cache = cache_path && !wo_chain_has_link(mod->root, cache_path)
                             ? wo_hash_cache_open(cache_path, mod->root, false, &note, NULL)
                             : NULL;
    wo_free(cache_path);
    for (size_t i = 0; i < WO_COUNT(names); i++) {
        char *path = wo_mod_game_path(mod, names[i], error);
        WoFileInfo info;
        if (!path || !wo_stat(path, &info, NULL) || !info.exists) {
            if (i < 4) {
                wo_fail(error, WO_ERR_NOT_FOUND,
                        "Missing %s. Run Install / repair. Check Windows Security if a file was "
                        "removed.",
                        names[i]);
            }
            wo_free(path);
            if (i < 4) {
                break;
            }
            continue;
        }
        char digest[WO_SHA256_HEX_SIZE] = "";
        bool hashed = wo_hash_cache_file(cache, path, digest, NULL);
        WoJson *data = wo_json_new_object();
        wo_json_put_str(data, "file", names[i]);
        wo_json_put_int(data, "bytes", (int64_t)info.size);
        wo_json_put_str(data, "sha256", digest);
        wo_json_put_str(data, "signature",
                        hashed ? signature_status(cache, path, digest) : "unknown");
        wo_log_event("dependency", data);
        wo_free(path);
    }
    wo_hash_cache_save(cache, false, NULL);
    wo_hash_cache_free(cache);
}

/* Find the Application Error (event 1000) record for the game's process and
 * log its named fields only (no paths, report ids or account names). */
static bool xml_field(const wchar_t *xml, const wchar_t *name, wchar_t *out, size_t cap) {
    wchar_t pattern[64];
    _snwprintf(pattern, WO_COUNT(pattern) - 1, L"<Data Name='%ls'>", name);
    pattern[WO_COUNT(pattern) - 1] = 0;
    const wchar_t *start = wcsstr(xml, pattern);
    if (!start) {
        return false;
    }
    start += wcslen(pattern);
    const wchar_t *end = wcsstr(start, L"</Data>");
    if (!end || (size_t)(end - start) >= cap) {
        return false;
    }
    wmemcpy(out, start, (size_t)(end - start));
    out[end - start] = 0;
    return true;
}

static void log_crash_event(DWORD pid, FILETIME started) {
    SYSTEMTIME utc;
    FileTimeToSystemTime(&started, &utc);
    wchar_t query[256];
    _snwprintf(query, WO_COUNT(query) - 1,
               L"*[System[Provider[@Name='Application Error'] and (EventID=1000) and "
               L"TimeCreated[@SystemTime>='%04u-%02u-%02uT%02u:%02u:%02u.000Z']]]",
               utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    query[WO_COUNT(query) - 1] = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        EVT_HANDLE results = EvtQuery(NULL, L"Application", query,
                                      EvtQueryChannelPath | EvtQueryReverseDirection);
        if (!results) {
            wo_log_text("windows_application_error_unavailable", "The event log could not be queried.");
            return;
        }
        EVT_HANDLE events[16];
        DWORD returned = 0;
        bool found = false;
        if (EvtNext(results, WO_COUNT(events), events, 1000, 0, &returned)) {
            for (DWORD i = 0; i < returned; i++) {
                DWORD used = 0;
                DWORD count = 0;
                wchar_t *xml = NULL;
                EvtRender(NULL, events[i], EvtRenderEventXml, 0, NULL, &used, &count);
                if (used > 0 && used < (1u << 20)) {
                    xml = wo_alloc(used + sizeof(wchar_t), NULL);
                }
                if (xml && EvtRender(NULL, events[i], EvtRenderEventXml, used, xml, &used, &count)) {
                    wchar_t field[256];
                    wchar_t app[256] = L"";
                    DWORD event_pid = 0;
                    if (xml_field(xml, L"ProcessId", field, WO_COUNT(field))) {
                        event_pid = (DWORD)wcstoul(field, NULL, 0);
                    }
                    xml_field(xml, L"AppName", app, WO_COUNT(app));
                    if (!found && event_pid == pid && _wcsicmp(app, L"Wipeout2097.exe") == 0) {
                        found = true;
                        static const wchar_t *const keys[] = {L"AppName",       L"AppVersion",
                                                              L"ModuleName",    L"ModuleVersion",
                                                              L"ExceptionCode", L"FaultingOffset"};
                        WoJson *data = wo_json_new_object();
                        for (size_t k = 0; k < WO_COUNT(keys); k++) {
                            if (xml_field(xml, keys[k], field, WO_COUNT(field))) {
                                char *key = wo_utf8(keys[k], NULL);
                                char *value = wo_utf8(field, NULL);
                                if (key && value) {
                                    wo_json_put_str(data, key, value);
                                }
                                wo_free(key);
                                wo_free(value);
                            }
                        }
                        wo_log_event("windows_application_error", data);
                    }
                }
                wo_free(xml);
                EvtClose(events[i]);
            }
        }
        EvtClose(results);
        if (found) {
            return;
        }
        Sleep(1000);
    }
    wo_log_text("windows_application_error_unavailable",
                "No matching Application Error event was available yet; native exception "
                "breadcrumbs may still identify the fault.");
}

/* The game's own logs go into the session log: each one's start in full,
 * then a share of the rolling window for its most recent lines. game.log
 * comes last, so it gets whatever the small logs leave of the window, and
 * its start runs until the player first leaves the title screen. */
static void save_game_logs(const WoMod *mod) {
    static const struct {
        const char *name;
        bool in_game_folder;
        const char *start_marker;
    } logs[] = {{"game-crash.log", false, NULL},  {"game-debug.txt", false, NULL},
                {"Debug.txt", true, NULL},        {"DDrawCompat-Wipeout2097.log", true, NULL},
                {"video-intro.log", false, NULL}, {"video-xtro1.log", false, NULL},
                {"video-xtro2.log", false, NULL}, {"video-xtro3.log", false, NULL},
                {"video-xtro4.log", false, NULL}, {"video-error.log", false, NULL},
                {"game.log", false, "title_left "}};
    char *paths[WO_COUNT(logs)] = {0};
    size_t present = 0;
    for (size_t i = 0; i < WO_COUNT(logs); i++) {
        char *relative = logs[i].in_game_folder ? NULL : wo_format(NULL, "logs/%s", logs[i].name);
        paths[i] = logs[i].in_game_folder ? wo_mod_game_path(mod, logs[i].name, NULL)
                   : relative             ? wo_mod_path(mod, relative, NULL)
                                          : NULL;
        wo_free(relative);
        if (paths[i] && wo_is_file(paths[i])) {
            present++;
        } else {
            wo_free(paths[i]);
            paths[i] = NULL;
        }
    }
    size_t left = WO_LOG_MAX_BYTES;
    for (size_t i = 0; i < WO_COUNT(logs); i++) {
        if (!paths[i]) {
            continue;
        }
        size_t wrote = wo_log_attach(logs[i].name, paths[i], true, left / present,
                                     logs[i].start_marker);
        left -= wrote < left ? wrote : left;
        present--;
        wo_free(paths[i]);
    }
}

static bool prepare_environment(const WoMod *mod, WoError *error) {
    WoJson *receipt = NULL;
    if (!wo_receipt_load(mod, &receipt, error)) {
        return false;
    }
    if (!receipt) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "Run Setup / Install / repair before playing.");
    }
    WoJson *data = wo_json_new_object();
    wo_json_put_str(data, "version", wo_json_str(receipt, "version", ""));
    wo_json_put_bool(data, "hd_art", wo_json_bool(receipt, "hd_textures", false));
    wo_json_put_bool(data, "improved_audio", wo_json_bool(receipt, "clean_audio", false));
    wo_log_event("installation", data);
    /* Runtime switches read by the game DLL. */
    SetEnvironmentVariableW(L"WO2097_HD_ART", wo_json_bool(receipt, "hd_textures", false) ? L"1" : L"0");
    SetEnvironmentVariableW(L"WO2097_RENDER_TEST", L"1");
    SetEnvironmentVariableW(L"WO2097_DISTANCE_TEST", L"1");
    SetEnvironmentVariableW(L"WO2097_SIM60_TEST", L"0");
    wo_json_free(receipt);
    char *ddraw = wo_mod_game_path(mod, "DDrawCompat-Wipeout2097.ini", error);
    bool ok = ddraw && wo_settings_set_log_level(ddraw, "info", error);
    wo_free(ddraw);
    if (ok) {
        wo_log_event("game_configuration", wo_settings_snapshot(mod->game));
    }
    return ok;
}

/* Milliseconds from this process's creation (as Windows recorded it) until
 * now: how long Windows took to start Play.exe before its code ran. */
static double since_process_created_ms(void) {
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return -1.0;
    }
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    uint64_t from = ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
    uint64_t to = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    /* FILETIME counts 100 ns units; 1000 of them are 0.1 ms. */
    return to > from ? (double)((to - from) / 1000u) / 10.0 : 0.0;
}

static int run_game(const WoMod *mod, WoLaps *laps, WoError *error) {
    char *exe = wo_mod_game_path(mod, "Wipeout2097.exe", error);
    wchar_t *wide_exe = exe ? wo_wide_path(exe, error) : NULL;
    wchar_t *wide_dir = wide_exe ? wo_wide_path(mod->game, error) : NULL;
    wo_free(exe);
    if (!wide_dir) {
        wo_free(wide_exe);
        return -1;
    }
    STARTUPINFOW startup;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process;
    memset(&process, 0, sizeof(process));
    FILETIME started;
    GetSystemTimeAsFileTime(&started);
    ULONGLONG begin = GetTickCount64();
    BOOL launched = CreateProcessW(wide_exe, NULL, NULL, NULL, FALSE, 0, NULL, wide_dir, &startup,
                                   &process);
    DWORD launch_error = GetLastError();
    wo_free(wide_exe);
    wo_free(wide_dir);
    if (!launched) {
        wo_fail_win32(error, launch_error, "Windows could not start the game");
        return -1;
    }
    CloseHandle(process.hThread);
    wo_laps_mark(laps, "create_process_ms");
    WoJson *data = wo_json_new_object();
    wo_json_put_int(data, "pid", process.dwProcessId);
    wo_json_put_int(data, "process_bits", 32);
    wo_json_put_str(data, "message", "Waiting for the game to exit.");
    wo_log_event("game_started", data);
    wo_laps_log(laps, "launch_timing");
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    PROCESS_MEMORY_COUNTERS memory;
    memory.cb = sizeof(memory);
    WoJson *exit_data = wo_json_new_object();
    char hex[16];
    snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)code);
    wo_json_put_int(exit_data, "code", (int64_t)(LONG)code);
    wo_json_put_str(exit_data, "hex", hex);
    wo_json_put(exit_data, "seconds", wo_json_new_number((double)(GetTickCount64() - begin) / 1000.0));
    if (GetProcessMemoryInfo(process.hProcess, &memory, sizeof(memory))) {
        wo_json_put_int(exit_data, "peak_working_set_bytes", (int64_t)memory.PeakWorkingSetSize);
    }
    wo_log_event("game_exit", exit_data);
    if (code != 0) {
        /* Renamed before the failure message shows the log's path. */
        wo_log_mark_crash();
        log_crash_event(process.dwProcessId, started);
        char *description = exit_description(code);
        wo_fail(error, WO_ERR_INTERNAL, "%s", description ? description : "The game exited with an error.");
        wo_free(description);
    }
    CloseHandle(process.hProcess);
    return (int)code;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    double process_start_ms = since_process_created_ms();
    WoLaps laps;
    wo_laps_begin(&laps);
    PlayOptions options = {0};
    parse_arguments(&options);
    char *root = options.root;
    if (!root) {
        wchar_t module[32768];
        DWORD len = GetModuleFileNameW(NULL, module, (DWORD)WO_COUNT(module));
        char *exe = (len > 0 && len < WO_COUNT(module)) ? wo_utf8(module, NULL) : NULL;
        root = exe ? wo_path_parent(exe, NULL) : NULL;
        wo_free(exe);
    }
    WoMod mod;
    if (!root || !wo_mod_init(&mod, root, NULL)) {
        MessageBoxW(NULL, L"Play could not determine the WO4K folder.", L"WO4K", MB_OK | MB_ICONERROR);
        wo_free(root);
        return 1;
    }
    if (options.purge) {
        size_t removed = wo_log_purge(root);
        if (!options.no_dialog) {
            wchar_t text[96];
            _snwprintf(text, WO_COUNT(text) - 1, L"Deleted %lu WO4K log file%ls.",
                       (unsigned long)removed, removed == 1 ? L"" : L"s");
            text[WO_COUNT(text) - 1] = 0;
            MessageBoxW(NULL, text, L"WO4K", MB_OK | MB_ICONINFORMATION);
        }
        wo_mod_free(&mod);
        wo_free(root);
        return 0;
    }
    wo_log_start("play", root);
    wo_json_put(laps.fields, "process_start_ms", wo_json_new_number(process_start_ms));
    wo_laps_mark(&laps, "log_start_ms");
    WoError error = {0};
    WoProgress progress = {NULL, NULL, NULL, WO_STAGE_GENERAL};
    WoReconcileResult result;
    wo_log_stage("Checking the installation");
    bool ok = wo_reconcile(&mod, false, &progress, &result, &error);
    wo_laps_mark(&laps, "reconcile_ms");
    if (ok && !result.installed) {
        ok = wo_fail(&error, WO_ERR_NOT_FOUND, "Run Setup / Install / repair before playing.");
    }
    WoStage stage = progress.stage;
    int code = 0;
    if (ok && !options.refresh_only) {
        wo_log_stage("Checking game dependencies");
        stage = WO_STAGE_RUNTIME;
        log_dependencies(&mod, &error);
        wo_laps_mark(&laps, "dependencies_ms");
        ok = !wo_error_is_set(&error) && prepare_environment(&mod, &error);
        wo_laps_mark(&laps, "environment_ms");
        if (ok) {
            wo_log_stage("Starting the game");
            stage = WO_STAGE_LAUNCH;
            code = run_game(&mod, &laps, &error);
            ok = code == 0;
            if (code > 0 || code < -1) {
                stage = WO_STAGE_GAME;
            }
        }
    }
    if (laps.fields) {
        /* Refresh only, or the game was never started. */
        wo_laps_log(&laps, "launch_timing");
    }
    save_game_logs(&mod);
    if (ok) {
        wo_log_text("complete", options.refresh_only ? "Assets refreshed without starting the game."
                                                     : "The game exited normally.");
    } else {
        wo_log_failure("Play", &error);
        char *message = wo_user_message(stage, &error);
        show_failure(&options, message ? message : error.message);
        wo_free(message);
    }
    wo_log_end();
    wo_mod_free(&mod);
    wo_free(root);
    return ok ? 0 : 1;
}
