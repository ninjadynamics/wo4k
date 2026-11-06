/* Local diagnostic logs: one JSON object per line in <mod>/logs/, falling
 * back to %TEMP%\WO4K\logs when the mod folder is not writable. Logs never
 * leave the PC. Common personal paths are replaced by placeholders, and
 * hardware details are limited to non-identifying fields collected only
 * when an operation fails. Safe to call from several threads. */
#ifndef WO_LOG_H
#define WO_LOG_H

#include "core/wo_base.h"
#include "core/wo_json.h"

/* Start a session log named <operation>-<timestamp>-<pid>.log. Returns
 * false only when no log could be created anywhere; logging calls are then
 * silently ignored. */
bool wo_log_start(const char *operation, const char *mod_root);
const char *wo_log_path(void);
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

/* Copy a game/runtime log next to the session log as
 * <session>.<name>, redacting device identifiers and personal paths, then
 * delete the original and record its last lines. Files older than the
 * session start are skipped when current_session_only is set. */
void wo_log_attach(const char *name, const char *path, bool current_session_only);

/* Current UTC time as ISO 8601 with 100 ns precision ("o" format). */
void wo_log_timestamp(char out[40]);

#endif
