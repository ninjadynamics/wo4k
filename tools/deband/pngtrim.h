/*
 * pngtrim - lossless PNG recompression (C99, zlib).
 *
 * The image stays exactly as encoded: the same IHDR (size, colour type, bit
 * depth, interlacing), palette, transparency and raw samples, including
 * colour hidden under zero alpha. Only the row filters and the deflate stream
 * are chosen again, keeping the smallest of several strategies. Colour-space
 * chunks (cHRM, gAMA, iCCP, sBIT, sRGB, cICP, mDCv, cLLi) are kept verbatim;
 * other ancillary chunks (text, EXIF, XMP, Photoshop data, timestamps,
 * resolution) are dropped. The result is decoded again and compared with the
 * original samples before it is returned. Animated PNGs are left alone.
 */
#ifndef PNGTRIM_H
#define PNGTRIM_H

#include <stddef.h>

typedef enum {
    PNGTRIM_SMALLER,     /* *out holds a smaller, verified PNG (free() it) */
    PNGTRIM_NOT_SMALLER, /* no strategy beat the input */
    PNGTRIM_SKIPPED,     /* valid, but deliberately left alone */
    PNGTRIM_FAILED       /* malformed or unsupported input, or out of memory */
} PngTrimResult;

/* why (optional) receives the reason for SKIPPED and FAILED. */
PngTrimResult pngtrim(const unsigned char *in, size_t in_len, unsigned char **out,
                      size_t *out_len, char *why, size_t why_size);

#endif
