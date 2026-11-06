/* Soundtrack banks: finding an extracted music folder, extracting CD audio
 * from a disc image, installing a bank into the game folder and checking an
 * installed bank.
 *
 * A bank is described by tracks.ini ("Tracks=10" for the PC disc: one data
 * track and nine songs; "Tracks=12" for the PlayStation disc: one data
 * track and eleven songs) plus track02..trackNN as .wav or .mp3. */
#ifndef WO_MUSIC_H
#define WO_MUSIC_H

#include "core/wo_base.h"

typedef enum WoMusicBank {
    WO_MUSIC_PC,
    WO_MUSIC_PSX
} WoMusicBank;

/* Tracks listed in a complete tracks.ini: 10 (PC) or 12 (PlayStation). */
unsigned wo_music_bank_tracks(WoMusicBank bank);
/* "Pc" or "Psx", as used in messages. */
const char *wo_music_bank_label(WoMusicBank bank);
/* "music/pc" or "music/psx" below the game folder. */
const char *wo_music_bank_folder(WoMusicBank bank);

/* True when some line of text reads "Tracks = <expected>" (any case, any
 * surrounding whitespace). */
bool wo_music_toc_matches(const char *text, size_t len, unsigned expected);

/* Look for a folder with a matching tracks.ini in source itself, then in
 * music/pc, music/psx, Music/PS1 and Music. *folder is NULL (and the call
 * succeeds) when there is none or source is not a folder. */
bool wo_music_find_folder(const char *source, WoMusicBank bank, char **folder, WoError *error);

/* Prepare a bank in destination from source, which is a folder or a disc
 * descriptor path (.mds/.mdf/.cue/.bin; ZIPs must already be unpacked):
 *   - an existing music folder is copied (track files and tracks.ini);
 *   - otherwise the first descriptor with exactly 9 (PC) or 11 (PlayStation)
 *     audio tracks is converted to trackNN.wav files plus tracks.ini.
 * *extracted is false only when allow_missing is set and no CD audio was
 * found; without allow_missing that is an error. tracks.ini is written last. */
bool wo_music_extract(const char *source, const char *destination, WoMusicBank bank,
                      bool allow_missing, bool *extracted, WoError *error);

/* Install the bank found in source_folder (see wo_music_find_folder) into
 * game_root/music/<bank>: track02..trackNN (WAV preferred over MP3) and
 * tracks.ini. Unchanged files are not rewritten. A superseded encoding of a
 * track is removed only after its replacement is in place. songs (optional)
 * receives the number of songs installed. */
bool wo_music_install(const char *source_folder, const char *game_root, WoMusicBank bank,
                      unsigned *songs, WoError *error);

/* The installed bank's files (tracks.ini first, then each song) when it is
 * complete and every song has a WAV (RIFF/WAVE) or MP3 (ID3 or MPEG frame
 * sync) header; an empty list otherwise. Errors are reserved for failures
 * such as running out of memory. */
bool wo_music_files(const char *game_root, WoMusicBank bank, WoStrList *files, WoError *error);

#endif
