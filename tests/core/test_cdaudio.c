#include <math.h>
#include <string.h>

#include "core/wo_cdaudio.h"
#include "wo_test.h"

#define SECTORS 40u

/* A slow stereo sine wave, stored little- or big-endian, with optional
 * subchannel bytes after each sector. */
static uint8_t *make_track(uint32_t stride, bool big_endian, size_t lead, size_t *len) {
    *len = lead + (size_t)SECTORS * stride;
    uint8_t *data = wo_alloc(*len, NULL);
    memset(data, 0x77, *len);
    size_t frame = 0;
    for (size_t s = 0; s < SECTORS; s++) {
        uint8_t *sector = data + lead + s * stride;
        for (size_t i = 0; i < WO_CD_AUDIO_SECTOR; i += 4, frame++) {
            int16_t left = (int16_t)(12000.0 * sin((double)frame / 40.0));
            int16_t right = (int16_t)(9000.0 * cos((double)frame / 55.0));
            uint16_t l = (uint16_t)left;
            uint16_t r = (uint16_t)right;
            sector[i] = (uint8_t)(big_endian ? l >> 8 : l);
            sector[i + 1] = (uint8_t)(big_endian ? l : l >> 8);
            sector[i + 2] = (uint8_t)(big_endian ? r >> 8 : r);
            sector[i + 3] = (uint8_t)(big_endian ? r : r >> 8);
        }
    }
    return data;
}

static bool collect(void *context, const void *data, size_t len, WoError *error) {
    return wo_buf_append(context, data, len, error);
}

static void check_conversion(uint32_t stride, bool big_endian) {
    size_t len = 0;
    size_t lead = 2352 * 3;
    uint8_t *track = make_track(stride, big_endian, lead, &len);
    WoSource source;
    wo_source_from_memory(&source, track, len, "track.bin");
    WoError error = {0};
    bool swap = !big_endian;
    CHECK_OK(wo_cdaudio_detect_swap(&source, lead, SECTORS, stride, &swap, &error), &error);
    CHECK(swap == big_endian);
    WoBuf wav = {0};
    CHECK_OK(wo_cdaudio_to_wav(&source, lead, SECTORS, stride, collect, &wav, &error), &error);
    size_t pcm = (size_t)SECTORS * WO_CD_AUDIO_SECTOR;
    CHECK(wav.len == WO_WAV_HEADER_BYTES + pcm);
    if (wav.len == WO_WAV_HEADER_BYTES + pcm) {
        CHECK(memcmp(wav.data, "RIFF", 4) == 0 && wo_le32(wav.data + 4) == pcm + 36);
        CHECK(memcmp(wav.data + 8, "WAVEfmt ", 8) == 0 && wo_le32(wav.data + 40) == pcm);
        CHECK(wo_le16(wav.data + 22) == 2 && wo_le32(wav.data + 24) == 44100);
        /* The output is little-endian and drops the subchannel bytes. */
        size_t reference_len = 0;
        uint8_t *reference = make_track(WO_CD_AUDIO_SECTOR, false, 0, &reference_len);
        CHECK(memcmp(wav.data + WO_WAV_HEADER_BYTES, reference, pcm) == 0);
        wo_free(reference);
    }
    wo_buf_free(&wav);
    wo_free(track);
}

static void test_conversions(void) {
    check_conversion(2352, false);
    check_conversion(2352, true);
    check_conversion(2448, false);
    check_conversion(2448, true);
}

static void test_header(void) {
    uint8_t header[WO_WAV_HEADER_BYTES];
    wo_wav_header(header, 2352);
    static const uint8_t expected[WO_WAV_HEADER_BYTES] = {
        'R', 'I', 'F', 'F', 0x54, 0x09, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
        16,  0,   0,   0,   1,    0,    2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0,
        4,   0,   16,  0,   'd',  'a',  't', 'a', 0x30, 0x09, 0, 0};
    CHECK(memcmp(header, expected, sizeof(expected)) == 0);
}

static void test_rejects(void) {
    size_t len = 0;
    uint8_t *track = make_track(2352, false, 0, &len);
    WoSource source;
    wo_source_from_memory(&source, track, len, "track.bin");
    WoError error = {0};
    bool swap = false;
    CHECK_FAILS(wo_cdaudio_detect_swap(&source, 0, 0, 2352, &swap, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_cdaudio_detect_swap(&source, 0, 10, 2048, &swap, &error), &error,
                WO_ERR_FORMAT);
    CHECK_FAILS(wo_cdaudio_detect_swap(&source, 0, 10, 9000, &swap, &error), &error,
                WO_ERR_UNSUPPORTED);
    CHECK_FAILS(wo_cdaudio_detect_swap(&source, 0, SECTORS + 1, 2352, &swap, &error), &error,
                WO_ERR_FORMAT);
    CHECK_FAILS(wo_cdaudio_detect_swap(&source, 1, SECTORS, 2352, &swap, &error), &error,
                WO_ERR_FORMAT);
    CHECK_FAILS(wo_cdaudio_check(UINT64_MAX, 0, UINT32_MAX, 2352, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_cdaudio_check(100, UINT64_MAX, 1, 2352, &error), &error, WO_ERR_FORMAT);
    /* Bytes that read the same either way give no usable byte order. */
    for (size_t i = 0; i < len; i += 2) {
        uint8_t v = (uint8_t)((i / 2) * 37);
        track[i] = v;
        track[i + 1] = v;
    }
    CHECK_FAILS(wo_cdaudio_detect_swap(&source, 0, SECTORS, 2352, &swap, &error), &error,
                WO_ERR_FORMAT);
    /* Silence is not ambiguous: it converts unchanged. */
    memset(track, 0, len);
    CHECK_OK(wo_cdaudio_detect_swap(&source, 0, SECTORS, 2352, &swap, &error), &error);
    CHECK(!swap);
    /* A one-sector track has no probe and is kept as stored. */
    CHECK_OK(wo_cdaudio_detect_swap(&source, 0, 1, 2352, &swap, &error), &error);
    wo_free(track);
}

int main(void) {
    RUN(test_conversions);
    RUN(test_header);
    RUN(test_rejects);
    return wo_test_finish();
}
