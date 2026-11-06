/* Updating the mod from its configured release URL, and removing files
 * that earlier releases installed but the current one no longer ships.
 *
 * The updater replaces mod files only. Afterwards Setup restarts itself and
 * the new version runs Install / repair, which brings the installed game
 * folder (runtime files, receipt) up to date with its own code. */
#ifndef WO_UPDATE_H
#define WO_UPDATE_H

#include "core/wo_base.h"
#include "core/wo_mod.h"

typedef struct WoUpdateCallbacks {
    void (*status)(void *context, int percent, const char *message);
    void *context;
    volatile long *cancel;
} WoUpdateCallbacks;

typedef enum WoUpdateOutcome {
    WO_UPDATE_INSTALLED, /* files replaced; restart Setup and repair */
    WO_UPDATE_NOT_NEWER  /* the configured release is older; nothing changed */
} WoUpdateOutcome;

typedef struct WoUpdateResult {
    WoUpdateOutcome outcome;
    char version[64];
    size_t files_changed;
} WoUpdateResult;

/* running_exe: path of the Setup.exe performing the update. When it is
 * Authenticode-signed, every WO4K binary in the update must be signed by
 * the same publisher. */
bool wo_update_run(const WoMod *mod, const char *running_exe, const WoUpdateCallbacks *callbacks,
                   WoUpdateResult *result, WoError *error);
/* The same verification and installation for a release ZIP already on disk. */
bool wo_update_from_zip(const WoMod *mod, const char *zip_path, const char *running_exe,
                        const WoUpdateCallbacks *callbacks, WoUpdateResult *result,
                        WoError *error);

/* Delete the "<name>.wo4k-old" copies left when running programs were
 * replaced (call from the restarted Setup). */
void wo_update_remove_replaced(const WoMod *mod);

/* Remove unchanged files listed by the previous release inventory but not
 * by the current one (including the old PowerShell scripts), then record
 * the current inventory as the new baseline. Modified files are kept. */
bool wo_cleanup_retired(const WoMod *mod, int *removed, int *kept, WoError *error);

#endif
