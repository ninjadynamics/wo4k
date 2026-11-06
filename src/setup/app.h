#ifndef SETUP_APP_H
#define SETUP_APP_H

#include "core/wo_base.h"

typedef struct AppOptions {
    bool after_update; /* started by the previous Setup after an update */
    uint32_t wait_pid; /* wait for this process to exit first */
} AppOptions;

#define APP_WINDOW_TITLE "WO4K Setup - WipEout 2097 / XL"

int app_run(const char *mod_root, const char *exe_path, const AppOptions *options);

#endif
