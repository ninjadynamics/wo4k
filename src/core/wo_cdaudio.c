#include "core/wo_cdaudio.h"

#include <stdlib.h>
#include <string.h>

/* The probe starts 10 seconds (750 sectors) in, or half-way through a short
 * track, and covers one second (75 sectors). */
#define PROBE_SKIP_SECTORS 750u
#define PROBE_SECTORS 75u
#define CHUNK_SECTORS 256u

bool wo_cdaudio_check(uint64_t source_size, uint64_t offset, uint64_t sectors, uint32_t stride,
                      WoError *error) {
    if (sectors == 0 || stride < WO_CD_AUDIO_SECTOR || sectors > UINT32_MAX / WO_CD_AUDIO_SECTOR) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid CD audio track extent.");
    }
    if (stride > WO_CD_MAX_STRIDE) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "Unsupported CD sector size (%u bytes).",
                       (unsigned)stride);
    }
    uint64_t bytes = sectors * stride;
    if (offset > source_size || bytes > source_size - offset) {
        return wo_fail(error, WO_ERR_FORMAT, "The CD audio track extends past the end of the image.");
    }
    return true;
}

static int16_t le_sample(const uint8_t *p) {
    return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}

static int16_t be_sample(const uint8_t *p) {
    return (int16_t)(uint16_t)((p[0] << 8) | p[1]);
}

bool wo_cdaudio_detect_swap(const WoSource *source, uint64_t offset, uint64_t sectors,
                            uint32_t stride, bool *swap, WoError *error) {
    *swap = false;
    if (!wo_cdaudio_check(source->size, offset, sectors, stride, error)) {
        return false;
    }
    uint64_t half = sectors / 2;
    uint64_t skip = half < PROBE_SKIP_SECTORS ? half : PROBE_SKIP_SECTORS;
    uint64_t count = half < PROBE_SECTORS ? half : PROBE_SECTORS;
    size_t len = (size_t)(count * stride);
    uint8_t *probe = wo_alloc(len, error);
    if (!probe) {
        return false;
    }
    if (!wo_source_read(source, offset + skip * stride, probe, len, error)) {
        wo_free(probe);
        return false;
    }
    /* Compare each sample with the same channel's previous sample. The raw
     * bytes are used as stored, subchannel data included. */
    int64_t little = 0;
    int64_t big = 0;
    for (size_t i = 4; i + 1 < len; i += 2) {
        little += llabs((long long)le_sample(probe + i) - le_sample(probe + i - 4));
        big += llabs((long long)be_sample(probe + i) - be_sample(probe + i - 4));
    }
    wo_free(probe);
    int64_t high = little > big ? little : big;
    int64_t low = little > big ? big : little;
    if ((double)high < (double)low * 1.5) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "The CD audio byte order is ambiguous; the track may not contain music.");
    }
    *swap = big < little;
    return true;
}

void wo_wav_header(uint8_t header[WO_WAV_HEADER_BYTES], uint32_t data_bytes) {
    memcpy(header, "RIFF", 4);
    wo_put_le32(header + 4, data_bytes + 36u);
    memcpy(header + 8, "WAVEfmt ", 8);
    wo_put_le32(header + 16, 16);
    wo_put_le16(header + 20, 1);      /* PCM */
    wo_put_le16(header + 22, 2);      /* channels */
    wo_put_le32(header + 24, 44100);  /* sample rate */
    wo_put_le32(header + 28, 176400); /* bytes per second */
    wo_put_le16(header + 32, 4);      /* block align */
    wo_put_le16(header + 34, 16);     /* bits per sample */
    memcpy(header + 36, "data", 4);
    wo_put_le32(header + 40, data_bytes);
}

bool wo_cdaudio_to_wav(const WoSource *source, uint64_t offset, uint64_t sectors,
                       uint32_t stride, WoSinkFn sink, void *context, WoError *error) {
    bool swap = false;
    if (!wo_cdaudio_detect_swap(source, offset, sectors, stride, &swap, error)) {
        return false;
    }
    uint8_t header[WO_WAV_HEADER_BYTES];
    wo_wav_header(header, (uint32_t)(sectors * WO_CD_AUDIO_SECTOR));
    if (!sink(context, header, sizeof(header), error)) {
        return false;
    }
    size_t raw_len = (size_t)CHUNK_SECTORS * stride;
    uint8_t *raw = wo_alloc(raw_len, error);
    uint8_t *pcm = raw ? wo_alloc((size_t)CHUNK_SECTORS * WO_CD_AUDIO_SECTOR, error) : NULL;
    bool ok = pcm != NULL;
    uint64_t at = offset;
    uint64_t left = sectors;
    while (ok && left > 0) {
        size_t take = left > CHUNK_SECTORS ? CHUNK_SECTORS : (size_t)left;
        ok = wo_source_read(source, at, raw, take * stride, error);
        for (size_t s = 0; ok && s < take; s++) {
            const uint8_t *from = raw + s * stride;
            uint8_t *to = pcm + s * WO_CD_AUDIO_SECTOR;
            if (swap) {
                for (size_t i = 0; i < WO_CD_AUDIO_SECTOR; i += 2) {
                    to[i] = from[i + 1];
                    to[i + 1] = from[i];
                }
            } else {
                memcpy(to, from, WO_CD_AUDIO_SECTOR);
            }
        }
        ok = ok && sink(context, pcm, take * WO_CD_AUDIO_SECTOR, error);
        at += (uint64_t)take * stride;
        left -= take;
    }
    wo_free(pcm);
    wo_free(raw);
    return ok;
}
