/*
 * filter.c - the debanding filter (see filter.h).
 */
#include "filter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void deband_defaults(DebandOptions *options) {
    options->threshold = 0.020f;
    options->radius = 24;
    options->dither = 0.8f;
    options->band = 0.160f;
    options->reach = 12;
    options->keep = 1.0f;
}

uint32_t deband_seed(const char *name) {
    uint32_t h = 2166136261u;
    while (*name) { h ^= (uint8_t)*name++; h *= 16777619u; }
    return h;
}

static uint32_t mix32(uint32_t n) {
    n ^= n >> 16; n *= 0x7feb352du;
    n ^= n >> 15; n *= 0x846ca68bu;
    n ^= n >> 16; return n;
}

/* White noise with its low frequencies taken out, scaled to unit power. */
int deband_noise(float *out) {
    float *white = (float *)malloc(DEBAND_NOISE_COUNT * sizeof(float));
    double sq = 0.0;
    if (!white) return 0;
    for (uint32_t i = 0; i < DEBAND_NOISE_COUNT; i++) {
        float a = (float)(mix32(i ^ 0xb4d531d5u) >> 8) * (1.0f / 16777216.0f);
        float b = (float)(mix32(i ^ 0x06d22f8bu) >> 8) * (1.0f / 16777216.0f);
        white[i] = a - b;
    }
    for (int y = 0; y < DEBAND_NOISE_SIDE; y++) {
        int ym = ((y - 1) & (DEBAND_NOISE_SIDE - 1)) * DEBAND_NOISE_SIDE;
        int yp = ((y + 1) & (DEBAND_NOISE_SIDE - 1)) * DEBAND_NOISE_SIDE;
        int yy = y * DEBAND_NOISE_SIDE;
        for (int x = 0; x < DEBAND_NOISE_SIDE; x++) {
            float n = white[yy + x] - 0.25f * (
                white[yy + ((x - 1) & (DEBAND_NOISE_SIDE - 1))] +
                white[yy + ((x + 1) & (DEBAND_NOISE_SIDE - 1))] +
                white[ym + x] + white[yp + x]);
            out[yy + x] = n;
            sq += (double)n * n;
        }
    }
    float norm = (float)(1.0 / sqrt(sq / DEBAND_NOISE_COUNT));
    for (int i = 0; i < DEBAND_NOISE_COUNT; i++) out[i] *= norm;
    free(white);
    return 1;
}

float deband_noise_at(const float *noise, int x, int y, uint32_t seed) {
    int nx = (x + (int)(seed & 511u)) & 511;
    int ny = (y + (int)((seed >> 9) & 511u)) & 511;
    return noise[ny * DEBAND_NOISE_SIDE + nx];
}

uint8_t deband_quantize(float value) {
    int i = (int)floorf(value + 0.5f);
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

/* Band ramps: coarse steps between flat areas, which the threshold filter
 * leaves alone.
 *
 * Artwork reduced to a few colours (a 16-colour PlayStation texture, a GIF)
 * shows a gradient as flat bands whose steps are far larger than any deband
 * threshold. Each band stood for every value between the midpoints to its
 * neighbours, so a band is redrawn as a ramp between those midpoints: a
 * pixel moves towards a neighbouring band by at most half the step, and by
 * less the further it is from that band. Nothing moves across a step larger
 * than the limit, so outlines and lettering stay; a wide flat area changes
 * only within `reach` pixels of a band beside it. A band one pixel wide is
 * already one sample of the gradient and stays as it is.
 *
 * Only a step of a staircase is a band: an area with a lighter neighbour on
 * one side and a darker one on another. An area that is lighter, or darker,
 * than everything around it is a detail (a rivet, a seam, a stripe's shade,
 * the peak of a highlight) and keeps its colour; ramping it towards its
 * surroundings would only wash it out. `keep` scales that: 1 leaves details
 * alone, 0 ramps them like any band (smoother, softer). */
#define BAND_FAR 1.0e9f
static void band_distances(const uint8_t *src, const int32_t *label, int w, int h, int channel,
                           int sign, int limit, float *dist, uint8_t *gap, uint8_t *stepped) {
    size_t pixels = (size_t)w * (size_t)h;
    static const int nx4[4] = {1, -1, 0, 0}, ny4[4] = {0, 0, 1, -1};
    for (size_t i = 0; i < pixels; i++) { dist[i] = BAND_FAR; gap[i] = 0; }
    /* Seeds: pixels beside another band that is this much higher (or lower). */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * (size_t)w + (size_t)x;
            if (label[i] < 0) continue;
            const uint8_t *p = src + i * 4u;
            int best = 0;
            for (int k = 0; k < 4; k++) {
                int qx = x + nx4[k], qy = y + ny4[k];
                if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                size_t n = (size_t)qy * (size_t)w + (size_t)qx;
                if (label[n] < 0 || label[n] == label[i]) continue;
                const uint8_t *q = src + n * 4u;
                int step = sign * ((int)q[channel] - (int)p[channel]);
                if (step <= 0 || abs(q[0] - p[0]) > limit || abs(q[1] - p[1]) > limit ||
                    abs(q[2] - p[2]) > limit) continue;
                if (!best || step < best) best = step;
            }
            if (best) { dist[i] = 0.5f; gap[i] = (uint8_t)best; stepped[label[i]] |= (uint8_t)(sign > 0 ? 1 : 2); }
        }
    }
    /* Distance to the nearest seed through the pixel's own band (chamfer
     * sweeps, repeated until nothing changes); the seed's step travels along. */
    for (int pass = 0, changed = 1; changed && pass < 64; pass++) {
        changed = 0;
        for (int dir = 0; dir < 2; dir++) {
            for (int yy = 0; yy < h; yy++) {
                int y = dir ? h - 1 - yy : yy;
                for (int xx = 0; xx < w; xx++) {
                    int x = dir ? w - 1 - xx : xx;
                    size_t i = (size_t)y * (size_t)w + (size_t)x;
                    if (label[i] < 0) continue;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            int qx = x + dx, qy = y + dy;
                            if ((!dx && !dy) || qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                            size_t n = (size_t)qy * (size_t)w + (size_t)qx;
                            if (label[n] != label[i]) continue;
                            float d = dist[n] + ((dx && dy) ? 1.41421356f : 1.0f);
                            if (d + 1.0e-4f < dist[i]) { dist[i] = d; gap[i] = gap[n]; changed = 1; }
                        }
                    }
                }
            }
        }
    }
}

uint8_t *deband_band_ramps(const uint8_t *src, int w, int h, float band, int reach, float keep) {
    size_t pixels = (size_t)w * (size_t)h;
    int limit = (int)floorf(band * 255.0f + 0.5f);
    uint8_t *out = (uint8_t *)malloc(pixels * 4u);
    int32_t *label = (int32_t *)malloc(pixels * sizeof(int32_t));
    uint32_t *stack = (uint32_t *)malloc(pixels * sizeof(uint32_t));
    float *up = (float *)malloc(pixels * sizeof(float));
    float *down = (float *)malloc(pixels * sizeof(float));
    uint8_t *gap_up = (uint8_t *)malloc(pixels);
    uint8_t *gap_down = (uint8_t *)malloc(pixels);
    uint8_t *stepped = (uint8_t *)malloc(pixels); /* per band: 1 lighter, 2 darker neighbour */
    if (!out || !label || !stack || !up || !down || !gap_up || !gap_down || !stepped ||
        pixels > 0x7fffffffu) {
        free(out); out = NULL; goto done;
    }
    memcpy(out, src, pixels * 4u);
    /* Bands: visible pixels of one exact colour, joined along edges. */
    for (size_t i = 0; i < pixels; i++) label[i] = -1;
    int32_t bands = 0;
    for (size_t start = 0; start < pixels; start++) {
        if (label[start] >= 0 || src[start * 4u + 3u] == 0) continue;
        size_t top = 0;
        stack[top++] = (uint32_t)start; label[start] = bands;
        while (top) {
            size_t i = stack[--top];
            int x = (int)(i % (size_t)w), y = (int)(i / (size_t)w);
            static const int nx4[4] = {1, -1, 0, 0}, ny4[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int qx = x + nx4[k], qy = y + ny4[k];
                if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                size_t n = (size_t)qy * (size_t)w + (size_t)qx;
                if (label[n] >= 0 || src[n * 4u + 3u] == 0 || memcmp(src + n * 4u, src + i * 4u, 3)) continue;
                label[n] = bands; stack[top++] = (uint32_t)n;
            }
        }
        bands++;
    }
    for (int channel = 0; channel < 3; channel++) {
        memset(stepped, 0, (size_t)bands);
        band_distances(src, label, w, h, channel, 1, limit, up, gap_up, stepped);
        band_distances(src, label, w, h, channel, -1, limit, down, gap_down, stepped);
        for (size_t i = 0; i < pixels; i++) {
            if (label[i] < 0) continue;
            float strength = stepped[label[i]] == 3 ? 1.0f : 1.0f - keep;
            if (strength <= 0.0f) continue;
            int has_up = up[i] < BAND_FAR, has_down = down[i] < BAND_FAR;
            if (!has_up && !has_down) continue;
            float width = has_up && has_down ? up[i] + down[i] : (float)reach;
            if (width > (float)reach) width = (float)reach;
            float value = (float)src[i * 4u + (size_t)channel];
            if (has_up && up[i] < width) value += strength * 0.5f * (float)gap_up[i] * (1.0f - up[i] / width);
            if (has_down && down[i] < width) value -= strength * 0.5f * (float)gap_down[i] * (1.0f - down[i] / width);
            out[i * 4u + (size_t)channel] = deband_quantize(value);
        }
    }
done:
    free(label); free(stack); free(up); free(down); free(gap_up); free(gap_down); free(stepped);
    return out;
}

/* The four-sample filter, as FFmpeg's `deband` with blur on does it, so
 * that the native mode gives what the FFmpeg mode gives.
 *
 * Every pixel has a direction and a distance of its own, both taken from
 * one number that depends only on where the pixel is. Four samples lie at
 * that offset mirrored about the pixel. Where their average differs from
 * the pixel by less than the threshold, the average replaces it.
 *
 * FFmpeg works on 16-bit planes, in which an 8-bit level v arrives as about
 * 256 v, and the result goes back to 8 bits as value * 255 / 65535: the
 * same arithmetic is kept here, including the 0.4% it takes off every
 * level. Unlike FFmpeg, fully transparent samples are left out of the
 * average, so nothing hidden under a cut-out bleeds into its edge. */
static float pixel_number(int x, int y) {
    float r = sinf((float)x * 12.9898f + (float)y * 78.233f) * 43758.545f;
    return r - floorf(r);
}

uint8_t *deband_smooth(const uint8_t *src, int w, int h, const DebandOptions *options,
                       const float *noise, uint32_t seed) {
    size_t pixels = (size_t)w * (size_t)h;
    uint8_t *out = (uint8_t *)malloc(pixels * 4u);
    if (!out) return NULL;
    int limit = (int)(options->threshold * 65535.0f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t pos = (size_t)y * (size_t)w + (size_t)x;
            const uint8_t *p = src + pos * 4u;
            uint8_t *q = out + pos * 4u;
            /* Preserve every channel, including hidden RGB, under zero alpha. */
            if (p[3] == 0) {
                memcpy(q, p, 4);
                continue;
            }
            float r = pixel_number(x, y);
            float direction = r * 6.2831855f;
            int distance = (int)(r * (float)options->radius);
            int dx = (int)(cosf(direction) * (float)distance);
            int dy = (int)(sinf(direction) * (float)distance);
            int xs[2] = {x + dx, x - dx}, ys[2] = {y + dy, y - dy};
            for (int i = 0; i < 2; i++) {
                if (xs[i] < 0) xs[i] = 0;
                if (xs[i] >= w) xs[i] = w - 1;
                if (ys[i] < 0) ys[i] = 0;
                if (ys[i] >= h) ys[i] = h - 1;
            }
            const uint8_t *samples[4] = {
                src + ((size_t)ys[0] * (size_t)w + (size_t)xs[0]) * 4u,
                src + ((size_t)ys[1] * (size_t)w + (size_t)xs[0]) * 4u,
                src + ((size_t)ys[1] * (size_t)w + (size_t)xs[1]) * 4u,
                src + ((size_t)ys[0] * (size_t)w + (size_t)xs[1]) * 4u};
            int count = 0;
            for (int i = 0; i < 4; i++) count += samples[i][3] != 0;
            float grain = deband_noise_at(noise, x, y, seed) * options->dither;
            for (int k = 0; k < 3; k++) {
                int own = p[k] * 256, value = own;
                if (count) {
                    int sum = 0;
                    for (int i = 0; i < 4; i++) if (samples[i][3]) sum += samples[i][k];
                    int average = sum * 256 / count;
                    if (abs(own - average) < limit) value = average;
                }
                q[k] = deband_quantize((float)value * (255.0f / 65535.0f) + grain);
            }
            q[3] = p[3];
        }
    }
    return out;
}

uint8_t *deband_picture(const uint8_t *rgba, int w, int h, const DebandOptions *options,
                        const float *noise, uint32_t seed) {
    if (options->band <= 0.0f) return deband_smooth(rgba, w, h, options, noise, seed);
    uint8_t *ramped = deband_band_ramps(rgba, w, h, options->band, options->reach, options->keep);
    uint8_t *out = ramped ? deband_smooth(ramped, w, h, options, noise, seed) : NULL;
    free(ramped);
    return out;
}
