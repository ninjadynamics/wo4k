/*
 * filter.h - the debanding filter itself: band ramps, the four-sample
 * filter and the dither, over RGBA pixels in memory. No files, no threads;
 * C99 and libm only.
 *
 * Used by deband.c (its native mode, and the dither of its FFmpeg mode) and
 * by the WO4K installer (src/core/wo_deband.c), which debands the game's
 * textures on the player's own machine.
 */
#ifndef DEBAND_FILTER_H
#define DEBAND_FILTER_H

#include <stdint.h>

#define DEBAND_NOISE_SIDE 512
#define DEBAND_NOISE_COUNT (DEBAND_NOISE_SIDE * DEBAND_NOISE_SIDE)

typedef struct DebandOptions {
    float threshold; /* four-sample filter: largest change it makes, 0.00003..0.5 of full scale */
    int radius;      /* how far its samples lie, 1..128 pixels */
    float dither;    /* amplitude in 8-bit levels, 0..4 */
    float band;      /* band ramps: largest step that counts as a band, 0..0.5 (0: no ramps) */
    int reach;       /* band ramps: a wide flat area changes within this many pixels of a band, 1..128 */
    float keep;      /* band ramps: 1 leaves light and dark details alone, 0 ramps them like bands */
} DebandOptions;

/* threshold 0.020, radius 24, dither 0.8, band 0.160, reach 12, keep 1. */
void deband_defaults(DebandOptions *options);

/* The dither pattern: DEBAND_NOISE_COUNT values, the same on every call.
 * Returns 0 when out of memory. */
int deband_noise(float *out);
/* Where a picture starts in the pattern: a number made from its name. */
uint32_t deband_seed(const char *name);
/* The pattern's value for a pixel, before scaling by the dither amplitude. */
float deband_noise_at(const float *noise, int x, int y, uint32_t seed);
/* A value in 8-bit levels to the nearest level. */
uint8_t deband_quantize(float value);

/* Each of these returns a new w*h*4 buffer for free(), or NULL when out of
 * memory. Alpha, and every byte of a fully transparent pixel, are copied
 * unchanged, and transparent pixels take no part. */

/* Band ramps alone (see filter.c). */
uint8_t *deband_band_ramps(const uint8_t *rgba, int w, int h, float band, int reach, float keep);
/* The four-sample filter and the dither. */
uint8_t *deband_smooth(const uint8_t *rgba, int w, int h, const DebandOptions *options,
                       const float *noise, uint32_t seed);
/* Band ramps (when options->band is not 0), then the filter and the dither. */
uint8_t *deband_picture(const uint8_t *rgba, int w, int h, const DebandOptions *options,
                        const float *noise, uint32_t seed);

#endif
