#include <math.h>
#include <string.h>

#include "core/wo_audioclean.h"
#include "wo_test.h"

/* A 22050 Hz mono 16-bit WAV with a LIST chunk before the samples: a tone
 * with noise, then quiet noise. */
static void build_wav(WoBuf *b, size_t samples, uint16_t channels, uint32_t rate,
                      uint16_t bits) {
    uint8_t chunk[8];
    wo_buf_append(b, "RIFF\0\0\0\0WAVE", 12, NULL);
    memcpy(chunk, "fmt ", 4);
    wo_put_le32(chunk + 4, 16);
    wo_buf_append(b, chunk, 8, NULL);
    uint8_t fmt[16];
    wo_put_le16(fmt, 1);
    wo_put_le16(fmt + 2, channels);
    wo_put_le32(fmt + 4, rate);
    wo_put_le32(fmt + 8, rate * channels * bits / 8);
    wo_put_le16(fmt + 12, (uint16_t)(channels * bits / 8));
    wo_put_le16(fmt + 14, bits);
    wo_buf_append(b, fmt, 16, NULL);
    memcpy(chunk, "LIST", 4);
    wo_put_le32(chunk + 4, 3);
    wo_buf_append(b, chunk, 8, NULL);
    wo_buf_append(b, "abc\0", 4, NULL); /* odd chunk plus its pad byte */
    memcpy(chunk, "data", 4);
    wo_put_le32(chunk + 4, (uint32_t)(samples * 2));
    wo_buf_append(b, chunk, 8, NULL);
    uint32_t seed = 12345;
    for (size_t i = 0; i < samples; i++) {
        seed = seed * 1103515245u + 12345u;
        double noise = ((double)((seed >> 16) & 0x7FFF) / 32768.0 - 0.5) * 800.0;
        double tone = i < samples / 2 ? 9000.0 * sin((double)i * 0.07) : 0.0;
        int16_t v = (int16_t)(tone + noise);
        uint8_t s[2];
        wo_put_le16(s, (uint16_t)v);
        wo_buf_append(b, s, 2, NULL);
    }
    wo_put_le32(b->data + 4, (uint32_t)(b->len - 8));
}

static void test_clean(void) {
    WoBuf wav = {0};
    build_wav(&wav, 6000, 1, 22050, 16);
    WoError error = {0};
    uint8_t *out = NULL;
    CHECK_OK(wo_audio_clean(wav.data, wav.len, "C:\\x\\AUTOPLOT.WAV", &out, &error), &error);
    size_t data = wav.len - 6000 * 2;
    CHECK(out && memcmp(out, wav.data, data) == 0); /* every byte before the samples kept */
    CHECK(out && memcmp(out + data, wav.data + data, 6000 * 2) != 0);
    /* The quiet tail is attenuated. */
    double before = 0;
    double after = 0;
    for (size_t i = 4000; out && i < 6000; i++) {
        double a = (int16_t)wo_le16(wav.data + data + i * 2);
        double b = (int16_t)wo_le16(out + data + i * 2);
        before += a * a;
        after += b * b;
    }
    CHECK(after < before);
    /* Deterministic, and the gentler settings for some effects differ. */
    uint8_t *again = NULL;
    CHECK_OK(wo_audio_clean(wav.data, wav.len, "autoplot.wav", &again, &error), &error);
    CHECK(out && again && memcmp(out, again, wav.len) == 0);
    wo_free(again);
    CHECK_OK(wo_audio_clean(wav.data, wav.len, "audio/source/CROWD.WAV", &again, &error), &error);
    CHECK(out && again && memcmp(out, again, wav.len) != 0);
    wo_free(again);
    wo_free(out);
    wo_buf_free(&wav);
}

static void test_rejects(void) {
    WoError error = {0};
    uint8_t *out = NULL;
    WoBuf wav = {0};
    build_wav(&wav, 100, 2, 22050, 16);
    CHECK_FAILS(wo_audio_clean(wav.data, wav.len, "x.wav", &out, &error), &error,
                WO_ERR_UNSUPPORTED);
    wo_buf_free(&wav);
    build_wav(&wav, 100, 1, 44100, 16);
    CHECK_FAILS(wo_audio_clean(wav.data, wav.len, "x.wav", &out, &error), &error,
                WO_ERR_UNSUPPORTED);
    wo_buf_free(&wav);
    build_wav(&wav, 100, 1, 22050, 16);
    CHECK_FAILS(wo_audio_clean(wav.data, 43, "x.wav", &out, &error), &error, WO_ERR_FORMAT);
    /* A chunk that claims more bytes than the file has. */
    CHECK_FAILS(wo_audio_clean(wav.data, wav.len - 1, "x.wav", &out, &error), &error,
                WO_ERR_FORMAT);
    wav.data[8] = 'X';
    CHECK_FAILS(wo_audio_clean(wav.data, wav.len, "x.wav", &out, &error), &error, WO_ERR_FORMAT);
    wav.data[8] = 'W';
    /* An odd number of sample bytes. */
    size_t data_header = wav.len - 100 * 2 - 8;
    wo_put_le32(wav.data + data_header + 4, 199);
    CHECK_FAILS(wo_audio_clean(wav.data, wav.len, "x.wav", &out, &error), &error,
                WO_ERR_UNSUPPORTED);
    wo_buf_free(&wav);
    CHECK(out == NULL);
}

int main(void) {
    RUN(test_clean);
    RUN(test_rejects);
    return wo_test_finish();
}
