/* The mod folder, its saved Setup configuration and installation receipt.
 *
 * Layout (all inside the mod root):
 *   wobble/                    installed game (installation.json receipt)
 *   .state/setup.json          saved sources and preferences
 *   .state/<name>.json         other bookkeeping (music, PS1 import, inventory)
 *   .work/                     per-operation scratch folders
 *   backups/                   previous installations and replaced files
 *   logs/                      diagnostic logs */
#ifndef WO_MOD_H
#define WO_MOD_H

#include "core/wo_base.h"
#include "core/wo_json.h"
#include "core/wo_settings.h"

typedef struct WoMod {
    char *root;
    char *game;  /* root\wobble */
    char *state; /* root\.state */
    char *work;  /* root\.work */
} WoMod;

bool wo_mod_init(WoMod *mod, const char *root, WoError *error);
void wo_mod_free(WoMod *mod);
/* root\relative, refusing anything that leaves the mod folder. */
char *wo_mod_path(const WoMod *mod, const char *relative, WoError *error);
char *wo_mod_game_path(const WoMod *mod, const char *relative, WoError *error);
/* Create a fresh scratch folder .work\<prefix><16 hex digits>. */
char *wo_mod_new_work_dir(const WoMod *mod, const char *prefix, WoError *error);
/* Recursively delete a scratch folder created by wo_mod_new_work_dir;
 * refuses anything else. Failures are logged, not returned. */
void wo_mod_remove_work_dir(const WoMod *mod, const char *path);
/* True when the game is running from this mod's game folder (a process whose
 * path cannot be read also counts, to stay on the safe side). */
bool wo_mod_game_running(const WoMod *mod);

/* --- Saved configuration --------------------------------------------------------- */
typedef struct WoConfig {
    int64_t revision;
    char *game_source;     /* absolute paths, "" when unset */
    char *pc_music_source;
    char *psx_source;
    WoSoundtrack soundtrack;
    bool hd_textures;
    bool clean_audio;
    int sound_mode;
    WoFullscreen fullscreen;
} WoConfig;

/* Effective configuration: .state/setup.json, else the receipt's copy, else
 * older fallbacks, with defaults from the installed INI files. */
bool wo_config_load(const WoMod *mod, WoConfig *config, WoError *error);
/* Store config (sources as mod-relative paths when inside the mod folder).
 * An unchanged configuration is not rewritten; otherwise the revision is
 * incremented. *saved receives the reloaded result (may be NULL). */
bool wo_config_save(const WoMod *mod, const WoConfig *config, WoConfig *saved, WoError *error);
bool wo_config_copy(const WoConfig *from, WoConfig *to, WoError *error);
void wo_config_free(WoConfig *config);

/* Absolute form of a stored path ("" stays ""); relative paths are taken
 * from the mod root and must stay inside it. */
char *wo_config_resolve_path(const WoMod *mod, const char *stored, WoError *error);
/* Mod-relative ('/' separators) when inside the mod root, else absolute. */
char *wo_config_portable_path(const WoMod *mod, const char *path, WoError *error);

/* --- Installation receipt (wobble/installation.json) ---------------------------- */
/* NULL (without error) when there is no receipt. */
bool wo_receipt_load(const WoMod *mod, WoJson **out, WoError *error);
bool wo_receipt_save(const WoMod *mod, const WoJson *receipt, WoError *error);

/* State files under .state/; missing files load as NULL. */
bool wo_state_load(const WoMod *mod, const char *name, WoJson **out, WoError *error);
bool wo_state_save(const WoMod *mod, const char *name, const WoJson *value, WoError *error);

#endif
