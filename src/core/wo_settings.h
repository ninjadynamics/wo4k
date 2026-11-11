/* Line-preserving INI editing for the game's Wipeout2097.ini and
 * DDrawCompat-Wipeout2097.ini. Unknown lines, comments and the order of
 * existing settings are kept; files are written with CRLF line ends. Key and
 * section names match case-insensitively. */
#ifndef WO_SETTINGS_H
#define WO_SETTINGS_H

#include "core/wo_base.h"
#include "core/wo_json.h"

typedef struct WoIni {
    WoStrList lines;
} WoIni;

bool wo_ini_parse(const char *text, WoIni *ini, WoError *error);
/* A missing file loads as empty. */
bool wo_ini_load(const char *path, WoIni *ini, WoError *error);
bool wo_ini_save(const char *path, const WoIni *ini, WoError *error);
void wo_ini_free(WoIni *ini);

/* Value of the first matching key (trimmed, newly allocated), or NULL.
 * section NULL searches the whole file; "" means before any section. */
char *wo_ini_get(const WoIni *ini, const char *section, const char *key, WoError *error);
/* Set key in section: the last existing occurrence is rewritten as
 * key<separator>value, other occurrences in that section are removed, and a
 * missing key is added at the end of the section (created if needed). With
 * section NULL the file is treated as one section. */
bool wo_ini_set(WoIni *ini, const char *section, const char *key, const char *value,
                const char *separator, WoError *error);
/* Rewrite every occurrence of key in any section; if there is none, add it
 * to fallback_section (NULL: end of file). */
bool wo_ini_set_everywhere(WoIni *ini, const char *key, const char *value,
                           const char *separator, const char *fallback_section, WoError *error);
/* Rewrite existing occurrences only; returns whether any existed. */
bool wo_ini_replace_existing(WoIni *ini, const char *key, const char *value,
                             const char *separator, WoError *error);
/* Add settings present in defaults but missing from ini, never changing
 * existing values. sectioned: compare per section (Wipeout2097.ini);
 * otherwise anywhere in the file (DDrawCompat). Returns the number added
 * through *added. */
bool wo_ini_merge_defaults(WoIni *ini, const WoIni *defaults, bool sectioned, int *added,
                           WoError *error);

/* --- Game settings ------------------------------------------------------------- */
typedef enum WoFullscreen { WO_FULLSCREEN_BORDERLESS, WO_FULLSCREEN_EXCLUSIVE } WoFullscreen;
/* Exclusive fullscreen is switched off, not removed: with variable refresh
 * rate displays it showed black frames, and it cannot be screenshotted.
 * While 0, Setup does not offer it and a saved "exclusive" is read as
 * borderless, which the next launch writes back to the game's settings. */
#define WO_EXCLUSIVE_FULLSCREEN_OFFERED 0
typedef enum WoSoundtrack { WO_SOUNDTRACK_PC, WO_SOUNDTRACK_PSX, WO_SOUNDTRACK_NONE } WoSoundtrack;

const char *wo_fullscreen_name(WoFullscreen mode); /* "borderless" / "exclusive" */
bool wo_fullscreen_parse(const char *name, WoFullscreen *mode);
const char *wo_soundtrack_name(WoSoundtrack soundtrack); /* "Pc" / "Psx" / "None" */
bool wo_soundtrack_parse(const char *name, WoSoundtrack *soundtrack);
bool wo_sound_mode_valid(int mode); /* 1 mono, 2 stereo, 5 Dolby Surround */

/* Missing files and unrecognised values give the defaults (borderless, 2). */
WoFullscreen wo_settings_get_fullscreen(const char *ddraw_ini);
bool wo_settings_set_fullscreen(const char *ddraw_ini, WoFullscreen mode, WoError *error);
int wo_settings_get_sound_mode(const char *game_ini);
bool wo_settings_set_sound_mode(const char *game_ini, int mode, WoError *error);
/* Select the soundtrack bank; None also mutes music, and leaving None
 * restores MusicVolume=100. */
bool wo_settings_apply_soundtrack(const char *game_ini, WoSoundtrack soundtrack, bool was_none,
                                  WoError *error);
bool wo_settings_set_log_level(const char *ddraw_ini, const char *level, WoError *error);
/* Allowlisted settings from the game's INI files for diagnostics. */
WoJson *wo_settings_snapshot(const char *game_root);

#endif
