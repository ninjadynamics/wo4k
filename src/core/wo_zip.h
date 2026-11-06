/* ZIP archives (miniz) read through WoSource and written for packaging.
 *
 * Entry names are normalised to UTF-8 with '/' separators (CP437 names are
 * converted). An entry is "safe" when its name is relative, has no "." or
 * ".." component, no ':' or control characters, and it is neither encrypted
 * nor a symbolic link. Extraction refuses archives with unsafe entries. */
#ifndef WO_ZIP_H
#define WO_ZIP_H

#include <time.h>

#include "core/wo_base.h"
#include "core/wo_io.h"

typedef struct WoZipEntry {
    char *name;
    uint64_t size;       /* uncompressed */
    uint64_t compressed;
    bool is_dir;
    bool safe;
    uint32_t index;      /* miniz file index */
} WoZipEntry;

typedef struct WoZip WoZip;

bool wo_zip_open(const char *path, WoZip **out, WoError *error);
size_t wo_zip_count(const WoZip *zip);
const WoZipEntry *wo_zip_entry(const WoZip *zip, size_t index);
/* Index of the entry with this exact name (case-insensitive), or SIZE_MAX. */
size_t wo_zip_find(const WoZip *zip, const char *name);
/* Decompress one entry into memory, refusing entries larger than max_bytes. */
bool wo_zip_read(WoZip *zip, size_t index, size_t max_bytes, WoBuf *out, WoError *error);
/* Decompress one safe file entry to dest_path (parents are created). */
bool wo_zip_extract_entry(WoZip *zip, size_t index, const char *dest_path, WoError *error);

typedef struct WoZipLimits {
    uint64_t max_total_bytes;
    size_t max_entries;
    bool reject_duplicates; /* case-insensitive duplicate names */
} WoZipLimits;

/* Extract every entry beneath dest_dir after checking all names and the
 * limits up front, so nothing is written for a rejected archive. */
bool wo_zip_extract_all(WoZip *zip, const char *dest_dir, const WoZipLimits *limits,
                        WoError *error);
void wo_zip_close(WoZip *zip);

/* --- Writing (in memory, then saved; suitable for release packages) ------ */
typedef struct WoZipWriter WoZipWriter;

/* All entries get the timestamp `when`, making packages reproducible. */
bool wo_zip_writer_begin(time_t when, WoZipWriter **out, WoError *error);
bool wo_zip_writer_add(WoZipWriter *writer, const char *name, const void *data, size_t len,
                       WoError *error);
bool wo_zip_writer_add_file(WoZipWriter *writer, const char *name, const char *source_path,
                            WoError *error);
/* Write the archive atomically to path and free the writer. */
bool wo_zip_writer_finish(WoZipWriter *writer, const char *path, WoError *error);
void wo_zip_writer_abort(WoZipWriter *writer);

/* Validate and normalise a relative archive/inventory path (used for ZIP
 * entries and release inventories). Returns a new string or NULL. */
char *wo_zip_normalise_name(const char *raw, bool is_utf8, bool *safe, WoError *error);

#endif
