/* Sound-effect cleaning (portable): bounded spectral noise reduction and
 * active-passage loudness matching for the game's 22050 Hz mono 16-bit PCM
 * WAVs. Every RIFF byte outside the PCM samples, the sample count and the
 * clock are kept. The result is bit-identical to the former
 * runtime/audio-clean.exe built with the same compiler (gcc -std=c11 -O2). */
#ifndef WO_AUDIOCLEAN_H
#define WO_AUDIOCLEAN_H

#include "core/wo_base.h"

/* Recorded in audio/install-state.json; change it whenever the algorithm
 * or its parameters change so installed effects are reprocessed. */
#define WO_AUDIO_PROCESSOR_ID "wo4k-audio-clean-1"
#define WO_AUDIO_MIN_BYTES 44u
#define WO_AUDIO_MAX_BYTES (32u * 1024u * 1024u)

/* Clean a whole WAV file held in memory. name is the effect's file name
 * (a path is fine): some effects use gentler settings chosen by name.
 * *output receives a new buffer of len bytes. */
bool wo_audio_clean(const uint8_t *input, size_t len, const char *name, uint8_t **output,
                    WoError *error);

#endif
