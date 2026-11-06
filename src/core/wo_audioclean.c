/* Ported from src/audio-clean.c (MIT). The floating-point operations are
 * kept in exactly the original order so the output stays bit-identical;
 * only file handling, bounds checks and error reporting changed. */
#include "core/wo_audioclean.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define N 512
#define H 128
#define PI 3.14159265358979323846
#define BLOCK 256

typedef struct Complex {
    double r;
    double i;
} Complex;

static void fft(Complex *x, int inverse) {
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            Complex t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }
    for (int len = 2; len <= N; len <<= 1) {
        double angle = (inverse ? 2 : -2) * PI / len;
        double wr0 = cos(angle);
        double wi0 = sin(angle);
        for (int base = 0; base < N; base += len) {
            double wr = 1;
            double wi = 0;
            for (int j = 0; j < len / 2; j++) {
                Complex a = x[base + j];
                Complex b = x[base + j + len / 2];
                double r = b.r * wr - b.i * wi;
                double i = b.r * wi + b.i * wr;
                x[base + j] = (Complex){a.r + r, a.i + i};
                x[base + j + len / 2] = (Complex){a.r - r, a.i - i};
                double next = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0;
                wr = next;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < N; i++) {
            x[i].r /= N;
            x[i].i /= N;
        }
    }
}

static int compare_doubles(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* True when the file's stem (upper-cased, up to the first '.') followed by
 * '|' occurs in list. */
static bool named(const char *path, const char *list) {
    const char *p = path;
    for (const char *q = path; *q; q++) {
        if (*q == '\\' || *q == '/') {
            p = q + 1;
        }
    }
    char name[80];
    int n = 0;
    while (p[n] && p[n] != '.' && n < 77) {
        char c = p[n];
        name[n] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        n++;
    }
    name[n++] = '|';
    name[n] = 0;
    return strstr(list, name) != NULL;
}

static double shape(double x, double gain) {
    x *= gain;
    double a = fabs(x);
    return copysign(a <= 32000 ? a : 32000 + 766 * tanh((a - 32000) / 766), x);
}

typedef struct WavLayout {
    size_t data;  /* offset of the PCM samples */
    size_t bytes; /* PCM byte count */
} WavLayout;

static bool parse_wav(const uint8_t *blob, size_t length, WavLayout *out, WoError *error) {
    if (length < WO_AUDIO_MIN_BYTES || length > WO_AUDIO_MAX_BYTES) {
        return wo_fail(error, WO_ERR_FORMAT, "The sound effect is not a WAV file of a supported size.");
    }
    if (memcmp(blob, "RIFF", 4) != 0 || memcmp(blob + 8, "WAVE", 4) != 0) {
        return wo_fail(error, WO_ERR_FORMAT, "The sound effect is not a RIFF WAVE file.");
    }
    size_t data = 0;
    size_t bytes = 0;
    bool format = false;
    for (size_t at = 12; at + 8 <= length;) {
        uint32_t n = wo_le32(blob + at + 4);
        if (n > length - at - 8) {
            return wo_fail(error, WO_ERR_FORMAT, "The sound effect has a damaged RIFF chunk.");
        }
        if (memcmp(blob + at, "fmt ", 4) == 0 && n >= 16) {
            format = wo_le16(blob + at + 8) == 1 && wo_le16(blob + at + 10) == 1 &&
                     wo_le32(blob + at + 12) == 22050 && wo_le16(blob + at + 22) == 16;
        }
        if (memcmp(blob + at, "data", 4) == 0) {
            data = at + 8;
            bytes = n;
        }
        at += 8 + (size_t)n + (n & 1u);
    }
    if (!format || !data || bytes < 2 || (bytes & 1u)) {
        return wo_fail(error, WO_ERR_UNSUPPORTED,
                       "The sound effect must be 22050 Hz mono 16-bit PCM.");
    }
    out->data = data;
    out->bytes = bytes;
    return true;
}

typedef struct Work {
    double *dry;
    double *wet;
    double *weight;
    double *powers;
    Complex *spectra;
} Work;

static void work_free(Work *w) {
    wo_free(w->dry);
    wo_free(w->wet);
    wo_free(w->weight);
    wo_free(w->powers);
    wo_free(w->spectra);
}

static void denoise(Work *w, int count, int frames, const char *name) {
    double window[N];
    double noise[N / 2 + 1];
    double smooth[N / 2 + 1];
    for (int i = 0; i < N; i++) {
        window[i] = sqrt(0.5 - 0.5 * cos(2 * PI * (i + 0.5) / N));
    }
    for (int frame = 0; frame < frames; frame++) {
        int start = frame * H - N;
        Complex *s = w->spectra + (size_t)frame * N;
        for (int i = 0; i < N; i++) {
            int sample = start + i;
            if (sample >= 0 && sample < count) {
                s[i].r = w->dry[sample] * window[i];
            }
        }
        fft(s, 0);
    }
    for (int bin = 0; bin <= N / 2; bin++) {
        int used = 0;
        for (int frame = 0; frame < frames; frame++) {
            Complex a = w->spectra[(size_t)frame * N + bin];
            double p = a.r * a.r + a.i * a.i;
            if (p > 0.001) {
                w->powers[used++] = p;
            }
        }
        if (used) {
            qsort(w->powers, (size_t)used, sizeof(double), compare_doubles);
            noise[bin] = w->powers[(used - 1) / 8] * 0.5;
        } else {
            noise[bin] = 0;
        }
        smooth[bin] = 1;
    }
    double reduction = 12;
    double mix = 1;
    if (named(name, "CROWD|DROID|ENGINES|SCRAPE|TURBLNCE|")) {
        reduction = 5;
        mix = .55;
    } else if (named(name, "HITWALL2|SHP2SHP|SHPSTALL|THUNDER|WEAPNHIT|WEPNWALL|")) {
        reduction = 7;
        mix = .8;
    } else if (named(name, "MENUMOVE|MENUSEL|MENUSLID|PICKUP|RECHARGE|")) {
        reduction = 5;
        mix = .7;
    }
    double floor_gain = pow(10, -reduction / 20);
    for (int frame = 0; frame < frames; frame++) {
        Complex *s = w->spectra + (size_t)frame * N;
        for (int bin = 0; bin <= N / 2; bin++) {
            double p = s[bin].r * s[bin].r + s[bin].i * s[bin].i;
            double average = 0;
            int terms = 0;
            for (int j = bin - 2; j <= bin + 2; j++) {
                if (j >= 0 && j <= N / 2) {
                    average += noise[j];
                    terms++;
                }
            }
            average /= terms;
            double gain = sqrt(fmax(0, 1 - average / fmax(p, 0.000001)));
            gain = fmax(floor_gain, gain);
            double memory = gain < smooth[bin] ? .65 : .2;
            smooth[bin] = smooth[bin] * memory + gain * (1 - memory);
            gain = smooth[bin];
            s[bin].r *= gain;
            s[bin].i *= gain;
            if (bin > 0 && bin < N / 2) {
                s[N - bin].r *= gain;
                s[N - bin].i *= gain;
            }
        }
        fft(s, 1);
        int start = frame * H - N;
        for (int i = 0; i < N; i++) {
            int sample = start + i;
            if (sample >= 0 && sample < count) {
                w->wet[sample] += s[i].r * window[i];
                w->weight[sample] += window[i] * window[i];
            }
        }
    }
    for (int i = 0; i < count; i++) {
        double cleaned = w->weight[i] > 1e-12 ? w->wet[i] / w->weight[i] : w->dry[i];
        w->wet[i] = w->dry[i] + mix * (cleaned - w->dry[i]);
    }
}

/* Gain that matches the cleaned signal's active-passage energy to the
 * original's, by bisection. */
static double loudness_gain(const Work *w, int count) {
    int blocks = count / BLOCK;
    double maximum = 0;
    double target = 0;
    for (int block = 0; block < blocks; block++) {
        double e = 0;
        for (int j = 0; j < BLOCK; j++) {
            e += w->dry[block * BLOCK + j] * w->dry[block * BLOCK + j];
        }
        w->powers[block] = e;
        if (e > maximum) {
            maximum = e;
        }
    }
    for (int block = 0; block < blocks; block++) {
        if (w->powers[block] >= maximum * .1) {
            target += w->powers[block];
        }
    }
    double lo = .5;
    double hi = 2;
    double gain = 1;
    if (target > 0) {
        for (int iteration = 0; iteration < 45; iteration++) {
            gain = (lo + hi) * .5;
            double energy = 0;
            for (int block = 0; block < blocks; block++) {
                if (w->powers[block] >= maximum * .1) {
                    for (int j = 0; j < BLOCK; j++) {
                        double v = shape(w->wet[block * BLOCK + j], gain);
                        energy += v * v;
                    }
                }
            }
            if (energy < target) {
                lo = gain;
            } else {
                hi = gain;
            }
        }
    }
    return gain;
}

bool wo_audio_clean(const uint8_t *input, size_t len, const char *name, uint8_t **output,
                    WoError *error) {
    *output = NULL;
    WavLayout layout = {0, 0};
    if (!parse_wav(input, len, &layout, error)) {
        return false;
    }
    int count = (int)(layout.bytes / 2);
    int frames = (count + N + H - 1) / H;
    Work w = {0};
    w.dry = wo_alloc_array((size_t)count, sizeof(double), error);
    w.wet = w.dry ? wo_alloc_array((size_t)count, sizeof(double), error) : NULL;
    w.weight = w.wet ? wo_alloc_array((size_t)count, sizeof(double), error) : NULL;
    w.powers = w.weight ? wo_alloc_array((size_t)frames, sizeof(double), error) : NULL;
    w.spectra = w.powers ? wo_alloc_array((size_t)frames * N, sizeof(Complex), error) : NULL;
    uint8_t *blob = w.spectra ? wo_alloc(len, error) : NULL;
    if (!blob) {
        work_free(&w);
        return false;
    }
    memcpy(blob, input, len);
    for (int i = 0; i < count; i++) {
        w.dry[i] = (int16_t)wo_le16(blob + layout.data + (size_t)i * 2);
    }
    denoise(&w, count, frames, name ? name : "");
    double gain = loudness_gain(&w, count);
    for (int i = 0; i < count; i++) {
        long v = lround(shape(w.wet[i], gain));
        if (v > 32766) {
            v = 32766;
        }
        if (v < -32766) {
            v = -32766;
        }
        uint16_t sample = (uint16_t)(int16_t)v;
        blob[layout.data + (size_t)i * 2] = (uint8_t)sample;
        blob[layout.data + (size_t)i * 2 + 1] = (uint8_t)(sample >> 8);
    }
    work_free(&w);
    *output = blob;
    return true;
}
