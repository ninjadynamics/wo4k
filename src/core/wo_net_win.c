#include "core/wo_net.h"
/* windows.h must precede the crypto and WinHTTP headers. */
#include "core/wo_win.h"

#include <softpub.h>
#include <stdio.h>
#include <string.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <wintrust.h>

#include "core/wo_fs.h"
#include "core/wo_release.h"
#include "wo_version.h"

#define CHUNK_BYTES (64u * 1024u)

static const char *winhttp_reason(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
        return "the connection timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "the server name could not be resolved";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return "the server could not be reached";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return "the connection was interrupted";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "the server's security certificate was not accepted";
    case ERROR_WINHTTP_REDIRECT_FAILED:
        return "the server redirected to an unsafe address";
    case ERROR_WINHTTP_OPERATION_CANCELLED:
        return "the download was cancelled";
    default:
        return "a network error occurred";
    }
}

static bool net_fail(WoError *error, DWORD code, const char *url) {
    if (code >= WINHTTP_ERROR_BASE && code <= WINHTTP_ERROR_LAST) {
        return wo_fail_code(error, WO_ERR_NETWORK, code, "Download failed: %s (WinHTTP %lu) for %s.",
                            winhttp_reason(code), (unsigned long)code, url);
    }
    wo_fail_win32(error, code, "Download failed for %s", url);
    if (error && error->kind == WO_ERR_IO) {
        error->kind = WO_ERR_NETWORK;
    }
    return false;
}

typedef struct Request {
    HINTERNET session;
    HINTERNET connection;
    HINTERNET request;
    uint64_t length; /* Content-Length, 0 when unknown */
} Request;

static void request_close(Request *r) {
    if (r->request) {
        WinHttpCloseHandle(r->request);
    }
    if (r->connection) {
        WinHttpCloseHandle(r->connection);
    }
    if (r->session) {
        WinHttpCloseHandle(r->session);
    }
    memset(r, 0, sizeof(*r));
}

/* The user's manual proxy or proxy script when one is set, otherwise a direct
 * connection (or the machine-wide netsh proxy). Automatic proxy discovery
 * (WPAD) is only a fallback: on networks without it, it can stall the first
 * request of a session for about 20 seconds. */
static HINTERNET open_session(bool discover, bool *can_discover) {
    const wchar_t *agent = L"WO4K-Setup/" WO_VERSION_STRING_W;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG user;
    memset(&user, 0, sizeof(user));
    bool have = WinHttpGetIEProxyConfigForCurrentUser(&user) != FALSE;
    HINTERNET session;
    if (discover || (have && user.lpszAutoConfigUrl)) {
        session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    } else if (have && user.lpszProxy) {
        session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NAMED_PROXY, user.lpszProxy,
                              user.lpszProxyBypass ? user.lpszProxyBypass : WINHTTP_NO_PROXY_BYPASS,
                              0);
    } else {
        session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    *can_discover = !discover && have && user.fAutoDetect && !user.lpszAutoConfigUrl &&
                    !user.lpszProxy;
    DWORD code = session ? ERROR_SUCCESS : GetLastError();
    if (user.lpszAutoConfigUrl) {
        GlobalFree(user.lpszAutoConfigUrl);
    }
    if (user.lpszProxy) {
        GlobalFree(user.lpszProxy);
    }
    if (user.lpszProxyBypass) {
        GlobalFree(user.lpszProxyBypass);
    }
    SetLastError(code);
    return session;
}

static bool request_try(Request *r, const char *url, const char *accept, unsigned timeout_seconds,
                        bool discover, bool *can_discover, WoError *error) {
    memset(r, 0, sizeof(*r));
    *can_discover = false;
    WoUrl parsed;
    if (!wo_url_parse(url, &parsed)) {
        return wo_fail(error, WO_ERR_UNSAFE, "Refusing a non-HTTPS or malformed address: %s", url);
    }
    char *target = wo_format(error, "%s%s%s", parsed.path, parsed.query[0] ? "?" : "",
                             parsed.query);
    wchar_t *host = target ? wo_wide(parsed.host, error) : NULL;
    wchar_t *object = host ? wo_wide(target, error) : NULL;
    wo_free(target);
    wo_url_free(&parsed);
    if (!object) {
        wo_free(host);
        return false;
    }
    bool ok = false;
    r->session = open_session(discover, can_discover);
    if (!r->session) {
        net_fail(error, GetLastError(), url);
        goto done;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(r->session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                          sizeof(protocols))) {
        /* Older Windows 10 builds reject the TLS 1.3 flag. */
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(r->session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                         sizeof(protocols));
    }
    DWORD redirects = 5;
    WinHttpSetOption(r->session, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects,
                     sizeof(redirects));
    int step = (int)(timeout_seconds > 60 ? 60 : timeout_seconds) * 1000;
    WinHttpSetTimeouts(r->session, step, step, step, step);
    r->connection = WinHttpConnect(r->session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!r->connection) {
        net_fail(error, GetLastError(), url);
        goto done;
    }
    r->request = WinHttpOpenRequest(r->connection, L"GET", object, NULL, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r->request) {
        net_fail(error, GetLastError(), url);
        goto done;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(r->request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    wchar_t headers[256] = L"";
    if (accept) {
        wchar_t *wide_accept = wo_wide(accept, error);
        if (!wide_accept) {
            goto done;
        }
        _snwprintf(headers, WO_COUNT(headers) - 1,
                   L"Accept: %ls\r\nX-GitHub-Api-Version: 2022-11-28\r\n", wide_accept);
        wo_free(wide_accept);
    }
    if (!WinHttpSendRequest(r->request, headers[0] ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
                            headers[0] ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r->request, NULL)) {
        net_fail(error, GetLastError(), url);
        goto done;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(r->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        net_fail(error, GetLastError(), url);
        goto done;
    }
    if (status != 200) {
        wo_fail_code(error, WO_ERR_NETWORK, status, "Download failed: the server answered HTTP %lu for %s.",
                     (unsigned long)status, url);
        goto done;
    }
    wchar_t length_text[32];
    size = sizeof(length_text);
    if (WinHttpQueryHeaders(r->request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            length_text, &size, WINHTTP_NO_HEADER_INDEX)) {
        char narrow[32];
        WideCharToMultiByte(CP_UTF8, 0, length_text, -1, narrow, sizeof(narrow), NULL, NULL);
        uint64_t length = 0;
        if (wo_parse_u64(narrow, UINT64_MAX, &length)) {
            r->length = length;
        }
    }
    ok = true;
done:
    wo_free(host);
    wo_free(object);
    if (!ok) {
        request_close(r);
    }
    return ok;
}

/* A network that relies on automatic proxy discovery fails the direct
 * attempt at the connection stage; only then is discovery tried. */
static bool request_open(Request *r, const char *url, const char *accept, unsigned timeout_seconds,
                         WoError *error) {
    bool can_discover = false;
    WoError first = {0};
    if (request_try(r, url, accept, timeout_seconds, false, &can_discover, &first)) {
        return true;
    }
    bool unreachable = first.kind == WO_ERR_NETWORK && (first.code == ERROR_WINHTTP_CANNOT_CONNECT ||
                                                        first.code == ERROR_WINHTTP_TIMEOUT ||
                                                        first.code == ERROR_WINHTTP_NAME_NOT_RESOLVED);
    if (can_discover && unreachable) {
        return request_try(r, url, accept, timeout_seconds, true, &can_discover, error);
    }
    if (error) {
        *error = first;
    }
    return false;
}

typedef bool (*SinkFn)(void *context, const void *data, size_t len, WoError *error);

static bool request_read(Request *r, const char *url, uint64_t max_bytes, unsigned timeout_seconds,
                         const WoNetProgress *progress, SinkFn sink, void *context, WoError *error) {
    if (r->length > max_bytes) {
        return wo_fail(error, WO_ERR_LIMIT, "The download from %s is larger than expected.", url);
    }
    uint8_t *buffer = wo_alloc(CHUNK_BYTES, error);
    if (!buffer) {
        return false;
    }
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeout_seconds * 1000u;
    uint64_t received = 0;
    bool ok = true;
    for (;;) {
        if (progress && progress->cancel && *progress->cancel) {
            ok = wo_fail(error, WO_ERR_CANCELLED, "The download was cancelled.");
            break;
        }
        if (GetTickCount64() > deadline) {
            ok = wo_fail(error, WO_ERR_NETWORK, "Download failed: %s took too long.", url);
            break;
        }
        DWORD got = 0;
        if (!WinHttpReadData(r->request, buffer, CHUNK_BYTES, &got)) {
            ok = net_fail(error, GetLastError(), url);
            break;
        }
        if (got == 0) {
            break;
        }
        if (received + got > max_bytes) {
            ok = wo_fail(error, WO_ERR_LIMIT, "The download from %s is larger than expected.", url);
            break;
        }
        if (!sink(context, buffer, got, error)) {
            ok = false;
            break;
        }
        received += got;
        if (progress && progress->report) {
            progress->report(progress->context, received, r->length);
        }
    }
    if (ok && r->length && received != r->length) {
        ok = wo_fail(error, WO_ERR_NETWORK, "Download failed: %s ended early.", url);
    }
    wo_free(buffer);
    return ok;
}

static bool sink_memory(void *context, const void *data, size_t len, WoError *error) {
    return wo_buf_append(context, data, len, error);
}

static bool sink_file(void *context, const void *data, size_t len, WoError *error) {
    return wo_file_write(context, data, len, error);
}

bool wo_http_get(const char *url, const char *accept, size_t max_bytes, unsigned timeout_seconds,
                 const WoNetProgress *progress, WoBuf *out, WoError *error) {
    Request r;
    if (!request_open(&r, url, accept, timeout_seconds, error)) {
        return false;
    }
    wo_buf_clear(out);
    bool ok = request_read(&r, url, max_bytes, timeout_seconds, progress, sink_memory, out, error) &&
              wo_buf_terminate(out, error);
    request_close(&r);
    return ok;
}

bool wo_http_download(const char *url, const char *path, uint64_t max_bytes,
                      unsigned timeout_seconds, const WoNetProgress *progress, WoError *error) {
    char *partial = wo_format(error, "%s.part", path);
    if (!partial) {
        return false;
    }
    Request r;
    WoFile *file = NULL;
    bool ok = request_open(&r, url, NULL, timeout_seconds, error);
    if (ok) {
        ok = wo_file_create(partial, &file, error) &&
             request_read(&r, url, max_bytes, timeout_seconds, progress, sink_file, file, error);
        request_close(&r);
    }
    if (file) {
        bool closed = wo_file_close(file, ok ? error : NULL);
        ok = ok && closed;
    }
    ok = ok && wo_replace_file(partial, path, error);
    if (!ok) {
        wo_delete_file(partial, NULL);
    }
    wo_free(partial);
    return ok;
}

/* --- Authenticode ---------------------------------------------------------------- */

bool wo_signature_check(const char *path, WoSignature *signature, WoError *error) {
    memset(signature, 0, sizeof(*signature));
    wchar_t *wide = wo_wide_path(path, error);
    if (!wide) {
        return false;
    }
    if (GetFileAttributesW(wide) == INVALID_FILE_ATTRIBUTES) {
        DWORD code = GetLastError();
        wo_free(wide);
        return wo_fail_win32(error, code, "Cannot check the signature of \"%s\"", path);
    }
    WINTRUST_FILE_INFO file;
    memset(&file, 0, sizeof(file));
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = wide;
    WINTRUST_DATA data;
    memset(&data, 0, sizeof(data));
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    /* Integrity and chain only: revocation checks would fail offline. */
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
    if (status == ERROR_SUCCESS) {
        signature->trusted = true;
        CRYPT_PROVIDER_DATA *provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR *signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)
                                               : NULL;
        PCCERT_CONTEXT certificate = (signer && signer->csCertChain > 0)
                                         ? signer->pasCertChain[0].pCert
                                         : NULL;
        if (certificate) {
            wchar_t name[256];
            if (CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name,
                                   (DWORD)WO_COUNT(name)) > 1) {
                WideCharToMultiByte(CP_UTF8, 0, name, -1, signature->subject,
                                    (int)sizeof(signature->subject), NULL, NULL);
            }
            BYTE hash[20];
            DWORD hash_size = sizeof(hash);
            if (CertGetCertificateContextProperty(certificate, CERT_SHA1_HASH_PROP_ID, hash,
                                                  &hash_size) &&
                hash_size == sizeof(hash)) {
                for (size_t i = 0; i < sizeof(hash); i++) {
                    snprintf(signature->thumbprint + i * 2, 3, "%02x", hash[i]);
                }
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
    wo_free(wide);
    return true;
}
