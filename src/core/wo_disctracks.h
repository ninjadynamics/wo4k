/* CD track tables from Alcohol 120% MDS descriptors and CUE sheets
 * (portable). File access is left to the caller: MDS parsing works on the
 * descriptor's bytes, CUE parsing asks a callback to resolve each FILE name. */
#ifndef WO_DISCTRACKS_H
#define WO_DISCTRACKS_H

#include "core/wo_base.h"

/* A CD holds at most 99 tracks. */
#define WO_TRACKS_MAX 99u
/* Largest MDS or CUE descriptor read. */
#define WO_DESCRIPTOR_MAX_BYTES (1024u * 1024u)

typedef struct WoTrack {
    int number;           /* 0..99 */
    bool audio;
    char *image;          /* path of the file holding the sectors */
    uint32_t stride;      /* bytes per sector in that file */
    uint64_t offset;      /* byte offset of the track's first sector (INDEX 01) */
    uint64_t frames;      /* sectors */
    uint64_t start_frame; /* absolute position on the disc, including the 150-frame lead-in */
} WoTrack;

typedef struct WoTrackList {
    WoTrack *items;
    size_t count;
    size_t cap;
} WoTrackList;

void wo_tracks_free(WoTrackList *list);
size_t wo_tracks_audio_count(const WoTrackList *list);
/* First non-audio track, or NULL. */
const WoTrack *wo_tracks_first_data(const WoTrackList *list);

/* Parse an MDS version 1 descriptor. image is the matching .mdf path,
 * recorded in every track. */
bool wo_mds_parse(const uint8_t *data, size_t len, const char *image, WoTrackList *out,
                  WoError *error);

/* Resolve a CUE FILE name to a path and that file's size. Called once per
 * FILE line, in order. Must fail (with a message) when the file is missing
 * or the name is unsafe. *path is heap-allocated and owned by the caller. */
typedef bool (*WoCueFileFn)(void *context, const char *name, char **path, uint64_t *size,
                            WoError *error);

/* Parse a CUE sheet (UTF-8, optional BOM). Only quoted BINARY files are
 * supported; INDEX 00 and INDEX 01 positions are used, other indexes are
 * ignored. Multi-file sheets are laid out back to back. */
bool wo_cue_parse(const char *text, size_t len, WoCueFileFn resolve, void *context,
                  WoTrackList *out, WoError *error);

/* The tracks.ini table of contents the game's music loader reads:
 * "[CD]", "Tracks=N", then per track "[TrackNN]", "StartFrame=", "Frames="
 * and a blank line, all CRLF-terminated. */
bool wo_tracks_write_ini(const WoTrackList *list, WoBuf *out, WoError *error);

#endif
