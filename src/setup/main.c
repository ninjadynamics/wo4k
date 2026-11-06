/* Setup.exe entry point. */
#include "core/wo_win.h"

#include <shellapi.h>
#include <wchar.h>

#include "core/wo_fs.h"
#include "core/wo_log.h"
#include "core/wo_mod.h"
#include "core/wo_update.h"
#include "setup/app.h"
#include "setup/win32.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show);

static void parse_arguments(AppOptions *options) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (wcscmp(argv[i], L"--after-update") == 0) {
            options->after_update = true;
        } else if (wcscmp(argv[i], L"--wait-pid") == 0 && i + 1 < argc) {
            options->wait_pid = (uint32_t)wcstoul(argv[++i], NULL, 10);
        }
    }
    LocalFree(argv);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    AppOptions options = {0};
    parse_arguments(&options);
    char *exe = win32_exe_path();
    char *root = exe ? wo_path_parent(exe, NULL) : NULL;
    if (!root) {
        win32_message(NULL, "WO4K Setup", "Setup could not determine its own folder.", true);
        return 1;
    }
    /* After an update the previous Setup must exit before we take over. */
    if (options.wait_pid) {
        win32_wait_for_process(options.wait_pid, 30000);
    }
    if (!win32_claim_single_instance(root, APP_WINDOW_TITLE)) {
        wo_free(root);
        wo_free(exe);
        return 0;
    }
    wo_log_start("setup", root);
    if (options.after_update) {
        WoMod mod;
        if (wo_mod_init(&mod, root, NULL)) {
            wo_update_remove_replaced(&mod);
            wo_mod_free(&mod);
        }
    }
    win32_init();
    int code = app_run(root, exe, &options);
    win32_shutdown();
    wo_log_end();
    wo_free(root);
    wo_free(exe);
    return code;
}
