/* Win32 services for Setup. raylib.h and windows.h cannot share a
 * translation unit (their names clash), so everything that needs Windows
 * headers lives behind this plain C interface. Window handles are passed as
 * void * (raylib's GetWindowHandle()). */
#ifndef SETUP_WIN32_H
#define SETUP_WIN32_H

#include "core/wo_base.h"

bool win32_init(void);
void win32_shutdown(void);

/* Modal pickers; a UTF-8 path, or NULL when cancelled. */
char *win32_pick_file(void *owner, const char *initial_path);
char *win32_pick_folder(void *owner, const char *initial_path);

void win32_message(void *owner, const char *title, const char *text, bool is_error);

/* Open an https:// URL in the default browser. */
bool win32_open_url(const char *url);

/* Start a program directly (no shell) and do not wait for it. arguments may
 * be NULL. */
bool win32_start_program(const char *exe, const char *arguments, const char *directory,
                         WoError *error);

char *win32_exe_path(void);
uint32_t win32_current_pid(void);
void win32_wait_for_process(uint32_t pid, uint32_t timeout_ms);

/* One Setup per mod folder: false when another instance already runs (it is
 * brought to the front). */
bool win32_claim_single_instance(const char *mod_root, const char *window_title);

/* Work area of the monitor showing the window (falls back to primary). */
void win32_work_area(void *window, int *x, int *y, int *width, int *height);
/* Path of a font in the Windows fonts folder, or NULL if absent. */
char *win32_font_path(const char *file_name);

#endif
