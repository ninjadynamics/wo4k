/* Windows boundary helpers: UTF-8/UTF-16 conversion and Win32 error
 * reporting. Only *_win.c files and the GUI programs include this. */
#ifndef WO_WIN_H
#define WO_WIN_H

#include <windows.h>

#include "core/wo_base.h"

/* Returned strings are heap-allocated; release them with wo_free. */
wchar_t *wo_wide(const char *utf8, WoError *error);
char *wo_utf8(const wchar_t *wide, WoError *error);
char *wo_utf8_n(const wchar_t *wide, size_t len, WoError *error);

/* Fail with a message followed by Windows' description of code, choosing
 * the error kind from the code (access denied, sharing violation, ...). */
bool wo_fail_win32(WoError *error, DWORD code, const char *format, ...) WO_PRINTF(3, 4);
WoErrorKind wo_error_kind_from_win32(DWORD code);

/* Absolute, normalised UTF-16 path suitable for any length: adds the \\?\
 * (or \\?\UNC\) prefix when the path would exceed MAX_PATH. */
wchar_t *wo_wide_path(const char *path, WoError *error);

#endif
