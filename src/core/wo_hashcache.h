/* A persistent cache of SHA-256 digests, so checking an unchanged
 * installation costs one metadata query per file instead of reading it.
 *
 * Like git's index, each entry records the file's identity (size, last
 * write and change times, volume and file id; see wo_file_identity) next to
 * its digest. A file is hashed again whenever any part of its identity
 * differs, so a changed, replaced or truncated file is always read. Two
 * further rules keep a stale digest from ever being trusted:
 *   - a file written within WO_HASH_CACHE_RACY_SECONDS of being hashed is
 *     not remembered (a later write in the same time-stamp tick could
 *     otherwise go unnoticed on file systems with coarse times);
 *   - an identity that changed while the file was being read is not
 *     remembered either.
 * A missing, unreadable or malformed cache file is ignored: every file is
 * then verified in full, and the cache is rebuilt.
 *
 * The cache can also remember facts derived from a file's contents (for
 * example the size of a decoded image), keyed by digest, so work that only
 * depends on the bytes is not repeated while they stay the same.
 *
 * Paths inside the cache's root folder are stored relative to it, so the
 * mod folder can be moved or renamed. */
#ifndef WO_HASHCACHE_H
#define WO_HASHCACHE_H

#include "core/wo_base.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"

#define WO_HASH_CACHE_FORMAT 1
#define WO_HASH_CACHE_RACY_SECONDS 3
#define WO_HASH_CACHE_MAX_BYTES (16u << 20)

typedef struct WoHashCache WoHashCache;

typedef struct WoHashCacheStats {
    bool loaded;           /* entries were read from the cache file */
    size_t entries;        /* entries now held */
    size_t hits;           /* digests taken from the cache */
    size_t hashed;         /* files read and hashed */
    size_t racy;           /* hashed files too recent (or changing) to remember */
    uint64_t hashed_bytes;
} WoHashCacheStats;

/* A cache stored in file (NULL: kept in memory only) for paths below root
 * (NULL: every key absolute). With fresh, the file's entries are ignored
 * and every file is verified again (after Install / repair). Fails only
 * when out of memory; a bad cache file is reported through *note (static
 * text, NULL when the file was fine or absent) and treated as empty. */
WoHashCache *wo_hash_cache_open(const char *file, const char *root, bool fresh,
                                const char **note, WoError *error);
void wo_hash_cache_free(WoHashCache *cache);

/* Digest of the file at path, reusing the cached one while its identity is
 * unchanged. cache may be NULL: the file is then always hashed. */
bool wo_hash_cache_file(WoHashCache *cache, const char *path, char hex[WO_SHA256_HEX_SIZE],
                        WoError *error);

/* Facts of the given kind remembered for a digest, or NULL. */
const WoJson *wo_hash_cache_facts(const WoHashCache *cache, const char *digest,
                                  const char *kind);
/* Remember facts (takes ownership; NULL cache just frees them). Facts of
 * digests no longer held by any entry are dropped when saving. */
void wo_hash_cache_set_facts(WoHashCache *cache, const char *digest, const char *kind,
                             WoJson *facts);

/* Write the cache file when anything changed. With prune, entries not used
 * since the cache was opened are dropped, so after a complete check it only
 * holds the files still checked. */
bool wo_hash_cache_save(WoHashCache *cache, bool prune, WoError *error);

void wo_hash_cache_stats(const WoHashCache *cache, WoHashCacheStats *out);

#endif
