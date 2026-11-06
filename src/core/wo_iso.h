/* ISO9660 data-track reader (portable, over WoSource).
 *
 * Handles plain 2048-byte images and raw 2352/2448-byte sectors (with 16 or
 * 24 bytes of sync/header before each 2048-byte payload). The layout is
 * found by looking for the Primary Volume Descriptor. Only the primary
 * directory tree is read (no Joliet or Rock Ridge names). */
#ifndef WO_ISO_H
#define WO_ISO_H

#include "core/wo_base.h"
#include "core/wo_io.h"

#define WO_ISO_MAX_DEPTH 16
#define WO_ISO_MAX_ENTRIES 50000u
/* Largest directory or file read into memory in one piece. */
#define WO_ISO_MAX_RECORD (64u * 1024u * 1024u)

typedef struct WoIsoEntry {
    char *path; /* '/'-separated, without the ";1" version suffix */
    uint32_t lba;
    uint32_t length;
    bool is_dir;
} WoIsoEntry;

typedef struct WoIso {
    const WoSource *source; /* not owned */
    uint64_t origin;        /* byte offset of the data track's first sector */
    uint32_t stride;        /* 2048, 2352 or 2448 */
    uint32_t payload;       /* 0, 16 or 24 */
    WoIsoEntry *entries;    /* in directory order, parents before children */
    size_t count;
    size_t cap;
} WoIso;

/* Locate the volume descriptor at origin and read the whole directory tree.
 * Names containing '/', '\', ':' or control characters, and the names "."
 * and "..", are rejected as unsafe. */
bool wo_iso_open(WoIso *iso, const WoSource *source, uint64_t origin, WoError *error);
void wo_iso_close(WoIso *iso);

/* Case-insensitive lookup of a '/'-separated path; NULL when absent. */
const WoIsoEntry *wo_iso_find(const WoIso *iso, const char *path);

/* Read size bytes starting at a logical sector, refusing more than max_bytes. */
bool wo_iso_read(const WoIso *iso, uint32_t lba, uint64_t size, size_t max_bytes, uint8_t **out,
                 WoError *error);
/* Read a whole file entry into memory (max_bytes caps it). */
bool wo_iso_read_entry(const WoIso *iso, const WoIsoEntry *entry, size_t max_bytes,
                       uint8_t **out, WoError *error);

/* Deliver a file's bytes in order through sink, in chunks of at most 256 KiB. */
bool wo_iso_stream_entry(const WoIso *iso, const WoIsoEntry *entry, WoSinkFn sink, void *context,
                         WoError *error);

#endif
