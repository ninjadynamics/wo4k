/* Disc images on disk: track tables for .mds/.mdf and .cue/.bin sets,
 * locating and importing a data track's files, and writing CD audio tracks
 * as WAV files. Built on the portable parsers in wo_iso.h, wo_disctracks.h
 * and wo_cdaudio.h. */
#ifndef WO_DISC_H
#define WO_DISC_H

#include "core/wo_base.h"
#include "core/wo_disctracks.h"
#include "core/wo_iso.h"

/* Track table of a disc descriptor. A .mdf path reads its .mds, a .bin
 * path reads its .cue. Any other extension yields an empty list (not an
 * error), as for a plain .iso. */
bool wo_disc_tracks(const char *path, WoTrackList *out, WoError *error);
bool wo_mds_tracks(const char *mds_path, WoTrackList *out, WoError *error);
/* FILE names are resolved relative to the sheet's folder and must stay
 * inside it. */
bool wo_cue_tracks(const char *cue_path, WoTrackList *out, WoError *error);

/* Where a disc image's ISO9660 data starts: the whole file for .iso and
 * other plain images, or the first data track of a .mds/.mdf/.cue/.bin set.
 * Fails when a descriptor lists only audio tracks. */
bool wo_disc_data_track(const char *path, char **image, uint64_t *offset, WoError *error);

/* Write one disc file to path (parents created). A partial file is removed
 * on failure. */
bool wo_disc_extract_entry(const WoIso *iso, const WoIsoEntry *entry, const char *path,
                           WoError *error);

/* Decides whether the disc file at relative ('/'-separated) is imported. */
typedef bool (*WoDiscFilterFn)(void *context, const char *relative);

/* Open the ISO9660 data at (image, offset) and extract every file the
 * filter accepts below destination, keeping its relative path. imported
 * (optional) receives the number of files written. */
bool wo_disc_import(const char *image, uint64_t offset, const char *destination,
                    WoDiscFilterFn filter, void *context, size_t *imported, WoError *error);

/* Convenience: wo_disc_data_track followed by wo_disc_import. */
bool wo_disc_import_image(const char *path, const char *destination, WoDiscFilterFn filter,
                          void *context, size_t *imported, WoError *error);

/* Convert a CD audio track to a 16-bit stereo 44.1 kHz WAV file. */
bool wo_cdaudio_extract(const char *image, uint64_t offset, uint64_t sectors, uint32_t stride,
                        const char *output, WoError *error);

#endif
