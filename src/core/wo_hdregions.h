/* hd-regions.bin, the installer-owned index the game's HD texture loader
 * reads (portable). Format, all integers little-endian:
 *
 *   "WOHDREG1"            8 bytes
 *   count                 uint32
 *   count records of 312 bytes:
 *     png name            256 bytes, UTF-16LE, zero-padded ([a-z0-9-]+.png, < 128 chars)
 *     native sheet        32 bytes, ASCII, zero-padded ([a-z0-9]+.tga, < 32 chars)
 *     page, x, y, width, height, alpha   6 x uint32
 *
 * A placement must fit inside a 256x256 page. */
#ifndef WO_HDREGIONS_H
#define WO_HDREGIONS_H

#include "core/wo_base.h"

#define WO_HD_REGION_RECORD_BYTES 312u
#define WO_HD_REGION_MAX_RECORDS 16384u

typedef struct WoHdRegionIndex {
    WoBuf records;
    uint32_t count;
} WoHdRegionIndex;

bool wo_hd_regions_add(WoHdRegionIndex *index, const char *png, const char *native, int64_t page,
                       int64_t x, int64_t y, int64_t width, int64_t height, bool alpha,
                       WoError *error);
/* The complete file contents. */
bool wo_hd_regions_serialize(const WoHdRegionIndex *index, WoBuf *out, WoError *error);
void wo_hd_regions_free(WoHdRegionIndex *index);

#endif
