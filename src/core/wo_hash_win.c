#include "core/wo_hash.h"
#include "core/wo_fs.h"
#include "core/wo_win.h"

#include <bcrypt.h>
#include <string.h>

#define SHA256_BYTES 32

struct WoSha256 {
    BCRYPT_HASH_HANDLE handle;
};

static INIT_ONCE provider_once = INIT_ONCE_STATIC_INIT;
static BCRYPT_ALG_HANDLE provider;
static NTSTATUS provider_status;

static BOOL CALLBACK open_provider(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    provider_status = BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    return TRUE;
}

static bool status_ok(NTSTATUS status) {
    return status >= 0;
}

WoSha256 *wo_sha256_begin(WoError *error) {
    InitOnceExecuteOnce(&provider_once, open_provider, NULL, NULL);
    if (!status_ok(provider_status)) {
        wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)provider_status,
                     "Windows SHA-256 is unavailable (status 0x%08lx).",
                     (unsigned long)provider_status);
        return NULL;
    }
    WoSha256 *hash = wo_alloc(sizeof(*hash), error);
    if (!hash) {
        return NULL;
    }
    NTSTATUS status = BCryptCreateHash(provider, &hash->handle, NULL, 0, NULL, 0, 0);
    if (!status_ok(status)) {
        wo_free(hash);
        wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)status,
                     "Cannot start a SHA-256 hash (status 0x%08lx).", (unsigned long)status);
        return NULL;
    }
    return hash;
}

bool wo_sha256_update(WoSha256 *hash, const void *data, size_t len, WoError *error) {
    const uint8_t *bytes = data;
    while (len > 0) {
        ULONG chunk = len > (1u << 30) ? (1u << 30) : (ULONG)len;
        NTSTATUS status = BCryptHashData(hash->handle, (PUCHAR)(uintptr_t)bytes, chunk, 0);
        if (!status_ok(status)) {
            return wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)status,
                                "SHA-256 hashing failed (status 0x%08lx).",
                                (unsigned long)status);
        }
        bytes += chunk;
        len -= chunk;
    }
    return true;
}

void wo_sha256_abort(WoSha256 *hash) {
    if (hash) {
        BCryptDestroyHash(hash->handle);
        wo_free(hash);
    }
}

bool wo_sha256_end(WoSha256 *hash, char hex[WO_SHA256_HEX_SIZE], WoError *error) {
    UCHAR digest[SHA256_BYTES];
    NTSTATUS status = BCryptFinishHash(hash->handle, digest, sizeof(digest), 0);
    wo_sha256_abort(hash);
    if (!status_ok(status)) {
        return wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)status,
                            "SHA-256 hashing failed (status 0x%08lx).", (unsigned long)status);
    }
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_BYTES; i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[SHA256_BYTES * 2] = 0;
    return true;
}

bool wo_sha256_mem(const void *data, size_t len, char hex[WO_SHA256_HEX_SIZE], WoError *error) {
    WoSha256 *hash = wo_sha256_begin(error);
    if (!hash) {
        return false;
    }
    if (!wo_sha256_update(hash, data, len, error)) {
        wo_sha256_abort(hash);
        return false;
    }
    return wo_sha256_end(hash, hex, error);
}

bool wo_sha256_file(const char *path, char hex[WO_SHA256_HEX_SIZE], WoError *error) {
    WoFile *file;
    if (!wo_file_open_read(path, &file, error)) {
        return false;
    }
    WoSha256 *hash = wo_sha256_begin(error);
    enum { CHUNK = 1 << 20 };
    uint8_t *buffer = hash ? wo_alloc(CHUNK, error) : NULL;
    bool ok = buffer != NULL;
    while (ok) {
        size_t got = 0;
        ok = wo_file_read(file, buffer, CHUNK, &got, error);
        if (!ok || got == 0) {
            break;
        }
        ok = wo_sha256_update(hash, buffer, got, error);
    }
    wo_free(buffer);
    wo_file_close(file, NULL);
    if (!ok) {
        wo_sha256_abort(hash);
        return false;
    }
    return wo_sha256_end(hash, hex, error);
}

bool wo_digest_eq(const char *a, const char *b) {
    return a && b && wo_is_hex_digest(a) && wo_is_hex_digest(b) && wo_str_ieq(a, b);
}

bool wo_random_bytes(void *buf, size_t len, WoError *error) {
    if (len > ULONG_MAX) {
        return wo_fail(error, WO_ERR_LIMIT, "Too many random bytes requested.");
    }
    NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!status_ok(status)) {
        return wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)status,
                            "Random number generation failed (status 0x%08lx).",
                            (unsigned long)status);
    }
    return true;
}
