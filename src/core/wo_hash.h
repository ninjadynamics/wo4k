/* SHA-256 using Windows' built-in CNG (bcrypt.dll). Digests are 64
 * lowercase hex characters, matching the manifests and release inventories. */
#ifndef WO_HASH_H
#define WO_HASH_H

#include "core/wo_base.h"

#define WO_SHA256_HEX_SIZE 65 /* 64 digits + NUL */

typedef struct WoSha256 WoSha256;

WoSha256 *wo_sha256_begin(WoError *error);
bool wo_sha256_update(WoSha256 *hash, const void *data, size_t len, WoError *error);
/* Finish and free the context. */
bool wo_sha256_end(WoSha256 *hash, char hex[WO_SHA256_HEX_SIZE], WoError *error);
void wo_sha256_abort(WoSha256 *hash);

bool wo_sha256_mem(const void *data, size_t len, char hex[WO_SHA256_HEX_SIZE], WoError *error);
bool wo_sha256_file(const char *path, char hex[WO_SHA256_HEX_SIZE], WoError *error);
/* Case-insensitive digest comparison; false unless both are valid digests. */
bool wo_digest_eq(const char *a, const char *b);

/* Fill buf with cryptographically random bytes. */
bool wo_random_bytes(void *buf, size_t len, WoError *error);

#endif
