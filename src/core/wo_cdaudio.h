/* CD-DA track to WAV conversion (portable, over WoSource).
 *
 * Audio sectors hold 2352 bytes of 16-bit stereo 44.1 kHz PCM; images may
 * store extra subchannel bytes per sector (stride 2448), which are dropped.
 * Some images store the samples big-endian. The byte order is detected from
 * a probe of the track: the interpretation with the smoother waveform wins,
 * and a track where neither is clearly smoother is rejected. */
#ifndef WO_CDAUDIO_H
#define WO_CDAUDIO_H

#include "core/wo_base.h"
#include "core/wo_io.h"

#define WO_CD_AUDIO_SECTOR 2352u
#define WO_WAV_HEADER_BYTES 44u
/* Largest sector stride accepted (raw sector plus subchannel data). */
#define WO_CD_MAX_STRIDE 8192u

/* Validate a track's extent inside a source of the given size. */
bool wo_cdaudio_check(uint64_t source_size, uint64_t offset, uint64_t sectors, uint32_t stride,
                      WoError *error);
/* Decide whether the samples must be byte-swapped to become little-endian. */
bool wo_cdaudio_detect_swap(const WoSource *source, uint64_t offset, uint64_t sectors,
                            uint32_t stride, bool *swap, WoError *error);
/* The canonical 44-byte header for 16-bit stereo 44.1 kHz PCM. */
void wo_wav_header(uint8_t header[WO_WAV_HEADER_BYTES], uint32_t data_bytes);
/* Produce the complete WAV file (header and samples) through sink. */
bool wo_cdaudio_to_wav(const WoSource *source, uint64_t offset, uint64_t sectors,
                       uint32_t stride, WoSinkFn sink, void *context, WoError *error);

#endif
