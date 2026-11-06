/* Local diagnostic logs: one JSON object per line in <mod>/logs/, falling
 * back to %TEMP%\WO4K\logs when the mod folder is not writable. Logs never
 * leave the PC. Common personal paths are replaced by placeholders, and
 * hardware details are limited to non-identifying fields collected only
 * when an operation fails. Safe to call from several threads. */
#ifndef WO_LOG_H
#define WO_LOG_H

#include "core/wo_base.h"
#include "core/wo_json.h"

/* Like a dashcam, a session log holds the start of the session in full and
 * then a rolling window: the most recent WO_LOG_MAX_BYTES of the logs
 * attached to it (start-up itself is capped at WO_LOG_START_MAX_BYTES).
 * What happens to it at the end depends on how the session went:
 *   - a real crash (wo_log_mark_crash): <name>.crash.log, never deleted;
 *   - another failure (wo_log_failure): <name>.log, removed after 30 days;
 *   - nothing went wrong: <name>.ok.log, and only the newest two of each
 *     operation are kept. */
#define WO_LOG_MAX_BYTES (256u << 10)
#define WO_LOG_START_MAX_BYTES (256u << 10)
#define WO_LOG_CRASH_SUFFIX ".crash.log"
#define WO_LOG_CLEAN_SUFFIX ".ok.log"

/* Start a session log named <operation>-<timestamp>-<pid>.log. Returns
 * false only when no log could be created anywhere; logging calls are then
 * silently ignored. */
bool wo_log_start(const char *operation, const char *mod_root);
/* The session log's current path (it changes when it is renamed). */
const char *wo_log_path(void);
/* The session ended in a crash: keep its log for good, renamed now (before
 * any message shows the path) to <name>.crash.log. */
void wo_log_mark_crash(void);
/* Bytes written to the session log so far. */
uint64_t wo_log_size(void);
/* Delete every file in <mod_root>\logs and %TEMP%\WO4K\logs, crash logs
 * included (Play --purge). Call outside a session. Returns the count. */
size_t wo_log_purge(const char *mod_root);
/* Close the session log and file it as described above. */
void wo_log_end(void);

/* Append {"time":..,"event":..,"data":..}. Takes ownership of data (may be
 * NULL). Text inside data is redacted. */
void wo_log_event(const char *event, WoJson *data);
void wo_log_text(const char *event, const char *text);
void wo_log_stage(const char *stage);
/* Log a failure with its kind and code, plus system information the first
 * time a failure is logged in this session. */
void wo_log_failure(const char *stage, const WoError *error);
/* OS edition/build/architecture, CPU model and core counts, RAM size and
 * GPU model/driver. No names, serial numbers or device identifiers. */
WoJson *wo_log_system_info(void);

/* Replace the mod root, %USERPROFILE% and %TEMP% with placeholders. */
char *wo_log_redact(const char *text);

/* Append a game/runtime log to the session log, then delete the original:
 * a session is always a single file to share. Its start is kept in full,
 * through the first line beginning with start_marker (all of it when
 * start_marker is NULL or absent, up to WO_LOG_START_MAX_BYTES), followed
 * by its most recent lines that fit in rolling_bytes. One untimed record
 * per line, named after the log, with device identifiers and personal paths
 * redacted. Files older than the session start are skipped when
 * current_session_only is set. Returns the rolling bytes used. */
size_t wo_log_attach(const char *name, const char *path, bool current_session_only,
                     size_t rolling_bytes, const char *start_marker);

/* Current UTC time as ISO 8601 with 100 ns precision ("o" format). */
void wo_log_timestamp(char out[40]);

/* Monotonic clock in microseconds, for elapsed-time fields in log events. */
uint64_t wo_clock_us(void);
/* Milliseconds (0.1 ms resolution) elapsed since a wo_clock_us() reading. */
double wo_elapsed_ms(uint64_t since_us);

/* Phase timing breadcrumbs: each wo_laps_mark records the milliseconds
 * since the previous mark under its name; wo_laps_log adds total_ms and
 * logs the set as one event. A NULL laps is ignored. */
typedef struct WoLaps {
    WoJson *fields;
    uint64_t started;
    uint64_t mark;
} WoLaps;

void wo_laps_begin(WoLaps *laps);
void wo_laps_mark(WoLaps *laps, const char *name);
void wo_laps_log(WoLaps *laps, const char *event);

#endif
