/* Game sources: which files belong to the PC game, unpacking ZIP inputs,
 * locating the PC game folder inside a folder/ZIP/disc image, copying its
 * data, and source "stamps" that tell when a configured source changed. */
#ifndef WO_SOURCES_H
#define WO_SOURCES_H

#include "core/wo_base.h"
#include "core/wo_json.h"
#include "core/wo_mod.h"

/* True for a '/'-separated path that is part of the PC game: game data
 * folders, the executables, saves/settings and the five videos. */
bool wo_game_file(const char *relative);

/* A folder for source: ZIP archives are extracted into a new folder under
 * work (12 GB limit); anything else must exist and is returned unchanged
 * (as a full path). */
char *wo_resolve_input(const char *source, const char *work, WoError *error);

/* The folder holding a supported PC executable next to Common and Track01,
 * found in source (folder, ZIP or disc image). Disc contents are imported
 * into work. manifest is manifests/patches.json. */
char *wo_native_root(const char *source, const char *work, const WoJson *manifest,
                     WoError *error);

/* Copy game data (Common, TrackNN except texture containers), the videos and
 * saves/settings from a PC game folder into destination. */
bool wo_copy_game_data(const char *native_root, const char *destination, WoError *error);

/* Find the sound-effect folder of a PC game folder (audio/source, source,
 * Sound or audio) and copy its .wav files into destination. */
bool wo_copy_sound_effects(const char *native_root, const char *destination, WoError *error);

/* Identity of a source path and its relevant files' sizes and times, in the
 * same format as earlier releases, so unchanged sources are recognised after
 * an upgrade. "<path>|unavailable" when it does not exist; "" for "". */
char *wo_source_stamp(const char *path, WoError *error);
bool wo_stamp_unavailable(const char *stamp);

/* Supported executable hashes from manifests/patches.json (sources and the
 * patched output). */
bool wo_supported_executable(const WoJson *manifest, const char *sha256);

/* Folders searched by the background discovery: Desktop, Downloads,
 * Documents and the mod folder's parent. */
bool wo_discovery_roots(const WoMod *mod, WoStrList *roots, WoError *error);

#endif
