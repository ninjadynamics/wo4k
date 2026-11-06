/* JSON-lines breadcrumbs in <mod root>\logs\video-<movie>.log. The launcher
 * attaches these files to its diagnostics, so the event names and fields
 * match the PowerShell player that came before. */
#ifndef WO_VIDEO_LOG_H
#define WO_VIDEO_LOG_H

#include <stdbool.h>

#include "video/video_core.h"

typedef struct VideoLog {
    wchar_t path[VIDEO_PATH_CAPACITY];
    const wchar_t *movie;
    bool enabled;
} VideoLog;

/* Creates <mod root>\logs if needed. A log that cannot be prepared is
 * disabled; playback never depends on it. */
void video_log_open(VideoLog *log, const wchar_t *mod_root, VideoMovie movie);

/* Appends one event; truncate starts a fresh file first. */
void video_log_event(VideoLog *log, const char *event, const char *data_json,
    bool truncate);

/* Convenience for an event whose data is a single JSON string or null. */
void video_log_text(VideoLog *log, const char *event, const char *text);

/* <mod root>\logs\video-error.log, for failures before a movie is known. */
void video_log_startup_error(const wchar_t *mod_root, const char *message);

#endif
