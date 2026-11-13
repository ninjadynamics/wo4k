/* High-level operations shared by Setup.exe and Play.exe: discovering and
 * probing game sources, installing, reconciling (check + repair + refresh)
 * and the user-facing explanation of a failure.
 *
 * Long operations report progress through WoProgress and track which stage
 * they are in, so a failure can be explained in plain words. */
#ifndef WO_OPS_H
#define WO_OPS_H

#include "core/wo_base.h"
#include "core/wo_mod.h"

typedef enum WoStage {
    WO_STAGE_GENERAL,
    WO_STAGE_GAME_SOURCE,
    WO_STAGE_PS1_SOURCE,
    WO_STAGE_SOUNDTRACK,
    WO_STAGE_SOUND_EFFECTS,
    WO_STAGE_TEXTURES,
    WO_STAGE_HD_ART,
    WO_STAGE_RUNTIME,
    WO_STAGE_UPDATE,
    WO_STAGE_LAUNCH,
    WO_STAGE_GAME,
    WO_STAGE_RESTART
} WoStage;

typedef struct WoProgress {
    void (*report)(void *context, int percent, const char *message);
    void *context;
    volatile long *cancel;
    WoStage stage; /* updated by the operation as it proceeds */
} WoProgress;

/* Record the stage and report progress (also logged). */
void wo_progress(WoProgress *progress, WoStage stage, int percent, const char *message);

/* The explanation shown to the user for a failure in a stage; the technical
 * message stays in the log. Returns a static or newly allocated string
 * (free with wo_free). */
char *wo_user_message(WoStage stage, const WoError *error);

/* --- Sources -------------------------------------------------------------------- */
typedef struct WoSourceInfo {
    bool game;         /* a supported Windows executable with retail game data */
    bool not_retail;   /* a supported executable, but texture sheets of an earlier
                          revision than the retail disc's (the "RIP" download) */
    bool pc_music;     /* PC CD audio or an extracted PC music folder */
    bool psx;          /* a supported PS1 disc or an extracted PS1 music folder */
    bool psx_music;    /* PS1 CD audio or music folder */
    bool psx_data;     /* PS1 data track (artwork) */
    char *pc_music_path;  /* where PC music was found (folder or image), or NULL */
    char *psx_path;       /* where the PS1 source was found, or NULL */
} WoSourceInfo;

/* Cheap check of one path (folder, ZIP or disc image). Never fails: anything
 * unreadable simply reports nothing found. */
void wo_source_probe(const WoMod *mod, const char *path, WoSourceInfo *info);
void wo_source_info_free(WoSourceInfo *info);

typedef struct WoDiscovery {
    char *game;
    char *psx;
} WoDiscovery;

/* Search the Desktop, Downloads, Documents and the mod's parent folder for
 * game sources, within a time and folder budget. */
void wo_discover_sources(const WoMod *mod, unsigned max_seconds, volatile long *cancel,
                         WoDiscovery *found);
void wo_discovery_free(WoDiscovery *found);

/* --- Installation ----------------------------------------------------------------- */
/* The mod is installed from two games: the PC version and a PlayStation
 * disc. Earlier releases needed only the first. */
#define WO_SOURCES_REQUIRED_MESSAGE                                                    \
    "WO4K now requires both the PC version of WipEout XL and a PlayStation ISO of the " \
    "game (the European version is recommended)."

/* True for an installation made by a release that did not yet require both:
 * it has to be installed again from the two, once. An installation made
 * since records that it was (the receipt's "playstation_required"), so
 * later updates never ask again. False where nothing is installed. */
bool wo_is_update(const WoMod *mod);

/* Fresh installation (or full rebuild) from config; saves config on success. */
bool wo_install(const WoMod *mod, const WoConfig *config, bool preserve_configuration,
                WoProgress *progress, WoError *error);

typedef struct WoReconcileResult {
    bool installed;
    bool repaired;
    int runtime_updated;
    int retired_removed;
} WoReconcileResult;

/* Check the configured installation, repair it when needed (or when
 * force_repair), synchronise runtime files and settings, import optional
 * PS1 artwork and music, then refresh artwork and sound effects. */
bool wo_reconcile(const WoMod *mod, bool force_repair, WoProgress *progress,
                  WoReconcileResult *result, WoError *error);

/* Refresh HD artwork, sound effects and videos before a launch. */
bool wo_refresh_assets(const WoMod *mod, WoProgress *progress, WoError *error);

#endif
