/* HTTPS downloads (WinHTTP) and Authenticode signature checks (WinTrust).
 * Only HTTPS is used, including for redirects; TLS 1.2 or newer; the
 * Windows certificate store validates servers. */
#ifndef WO_NET_H
#define WO_NET_H

#include "core/wo_base.h"

typedef struct WoNetProgress {
    /* Called with bytes received so far and the expected total (0 when
     * unknown). May be NULL. */
    void (*report)(void *context, uint64_t received, uint64_t total);
    void *context;
    /* Polled during transfers; non-zero aborts with WO_ERR_CANCELLED. */
    volatile long *cancel;
} WoNetProgress;

/* GET an HTTPS URL into memory (refusing more than max_bytes). accept is an
 * Accept header value or NULL. */
bool wo_http_get(const char *url, const char *accept, size_t max_bytes, unsigned timeout_seconds,
                 const WoNetProgress *progress, WoBuf *out, WoError *error);
/* GET an HTTPS URL into a file (written to path.part, then renamed). accept
 * is an Accept header value or NULL. */
bool wo_http_download(const char *url, const char *accept, const char *path, uint64_t max_bytes,
                      unsigned timeout_seconds, const WoNetProgress *progress, WoError *error);

typedef struct WoSignature {
    bool trusted;         /* valid Authenticode signature chaining to a trusted root */
    char subject[256];    /* signer's display name (UTF-8) */
    char thumbprint[41];  /* SHA-1 of the signing certificate, hex */
} WoSignature;

/* Never fails for an unsigned or untrusted file: that is reported through
 * signature->trusted. Errors are only for files that cannot be checked. */
bool wo_signature_check(const char *path, WoSignature *signature, WoError *error);

#endif
