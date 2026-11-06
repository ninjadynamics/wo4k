/* Builds small ISO9660 images in memory for the tests. Header-only so each
 * test program can include it. */
#ifndef ISO_BUILDER_H
#define ISO_BUILDER_H

#include <string.h>

#include "core/wo_base.h"

#define ISO_SECTOR 2048u

typedef struct IsoImage {
    uint8_t *data; /* 2048-byte logical sectors */
    size_t sectors;
} IsoImage;

static inline void iso_init(IsoImage *iso, size_t sectors) {
    iso->sectors = sectors;
    iso->data = wo_alloc(sectors * ISO_SECTOR, NULL);
}

static inline uint8_t *iso_sector(IsoImage *iso, size_t lba) {
    return iso->data + lba * ISO_SECTOR;
}

/* Directory record; returns its length. raw_name is copied verbatim. */
static inline size_t iso_record(uint8_t *at, const char *raw_name, size_t name_len, uint32_t lba,
                                uint32_t size, bool is_dir) {
    size_t len = 33 + name_len;
    if (len & 1u) {
        len++;
    }
    memset(at, 0, len);
    at[0] = (uint8_t)len;
    wo_put_le32(at + 2, lba);
    wo_put_le32(at + 10, size);
    at[25] = is_dir ? 2 : 0;
    at[32] = (uint8_t)name_len;
    memcpy(at + 33, raw_name, name_len);
    return len;
}

/* Start a directory sector with its "." and ".." records; returns the
 * write position. */
static inline size_t iso_dir_begin(IsoImage *iso, size_t lba, uint32_t self_size) {
    uint8_t *dir = iso_sector(iso, lba);
    size_t at = iso_record(dir, "\0", 1, (uint32_t)lba, self_size, true);
    at += iso_record(dir + at, "\1", 1, (uint32_t)lba, self_size, true);
    return at;
}

static inline void iso_pvd(IsoImage *iso, uint32_t root_lba, uint32_t root_size) {
    uint8_t *pvd = iso_sector(iso, 16);
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    iso_record(pvd + 156, "\0", 1, root_lba, root_size, true);
}

static inline void iso_file(IsoImage *iso, size_t lba, const void *data, size_t len) {
    memcpy(iso_sector(iso, lba), data, len);
}

/* Convert to raw sectors of stride bytes with the payload at offset
 * payload, preceded by `origin` bytes of padding. */
static inline uint8_t *iso_raw(const IsoImage *iso, uint32_t stride, uint32_t payload,
                               size_t origin, size_t *len) {
    *len = origin + iso->sectors * stride;
    uint8_t *raw = wo_alloc(*len, NULL);
    memset(raw, 0x5A, *len);
    for (size_t s = 0; s < iso->sectors; s++) {
        memcpy(raw + origin + s * stride + payload, iso->data + s * ISO_SECTOR, ISO_SECTOR);
    }
    return raw;
}

/* A small PlayStation-like disc:
 *   SYSTEM.CNF                   (sector 20)
 *   WIPEOUT2/                    (sector 19)
 *   WIPEOUT2/TEXTURES/           (sector 21)
 *   WIPEOUT2/TEXTURES/EXPLODE.CMP (sectors 22-24, 5000 bytes)
 *   WIPEOUT2/COMMON/             (sector 25)
 *   WIPEOUT2/COMMON/MINE.CMP     (sector 26)
 *   WIPEOUT2/COMMON/ALCOL.CMP    (sector 27)
 *   README.TXT                   (sector 28) */
static inline void iso_build_sample(IsoImage *iso, const char *system_cnf) {
    iso_init(iso, 32);
    iso_pvd(iso, 18, ISO_SECTOR);
    size_t at = iso_dir_begin(iso, 18, ISO_SECTOR);
    uint8_t *root = iso_sector(iso, 18);
    at += iso_record(root + at, "README.TXT;1", 12, 28, 5, false);
    at += iso_record(root + at, "SYSTEM.CNF;1", 12, 20, (uint32_t)strlen(system_cnf), false);
    at += iso_record(root + at, "WIPEOUT2", 8, 19, ISO_SECTOR, true);
    (void)at;
    iso_file(iso, 20, system_cnf, strlen(system_cnf));
    iso_file(iso, 28, "hello", 5);

    at = iso_dir_begin(iso, 19, ISO_SECTOR);
    uint8_t *game = iso_sector(iso, 19);
    at += iso_record(game + at, "COMMON", 6, 25, ISO_SECTOR, true);
    at += iso_record(game + at, "TEXTURES", 8, 21, ISO_SECTOR, true);
    (void)at;

    at = iso_dir_begin(iso, 21, ISO_SECTOR);
    iso_record(iso_sector(iso, 21) + at, "EXPLODE.CMP;1", 13, 22, 5000, false);
    for (size_t i = 0; i < 5000; i++) {
        iso->data[22 * ISO_SECTOR + i] = (uint8_t)(i * 7 + 3);
    }

    at = iso_dir_begin(iso, 25, ISO_SECTOR);
    uint8_t *common = iso_sector(iso, 25);
    at += iso_record(common + at, "MINE.CMP;1", 10, 26, 4, false);
    at += iso_record(common + at, "ALCOL.CMP;1", 11, 27, 4, false);
    (void)at;
    iso_file(iso, 26, "MINE", 4);
    iso_file(iso, 27, "ALCO", 4);
}

#endif
