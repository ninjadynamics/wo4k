/* Portable foundations shared by every WO4K component: error reporting,
 * checked arithmetic, growable buffers and small string helpers.
 *
 * Conventions used across src/core:
 *   - Text is UTF-8 everywhere, including paths. Only the *_win.c files talk
 *     to Windows, converting to UTF-16 at that boundary.
 *   - Functions that can fail return bool and take a trailing WoError *.
 *     On failure they return false and describe the problem in the error.
 *   - Portable headers (everything without _win in its name) never include
 *     windows.h, so the parsers can be fuzzed on any platform. */
#ifndef WO_BASE_H
#define WO_BASE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__MINGW32__)
/* Built with __USE_MINGW_ANSI_STDIO=1, so the C99 printf rules apply. */
#define WO_PRINTF(fmt, args) __attribute__((format(gnu_printf, fmt, args)))
#define WO_NODISCARD __attribute__((warn_unused_result))
#elif defined(__GNUC__)
#define WO_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#define WO_NODISCARD __attribute__((warn_unused_result))
#else
#define WO_PRINTF(fmt, args)
#define WO_NODISCARD
#endif

#define WO_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* Broad failure classes. The user-facing explanation is chosen from the
 * kind plus the operation stage, never by pattern-matching message text. */
typedef enum WoErrorKind {
    WO_ERR_NONE = 0,
    WO_ERR_INTERNAL,    /* bug or out of memory */
    WO_ERR_IO,          /* the operating system refused a file operation */
    WO_ERR_NOT_FOUND,   /* a required file, folder or entry is missing */
    WO_ERR_ACCESS,      /* permission denied / blocked by security software */
    WO_ERR_SHARING,     /* a file is open in another program */
    WO_ERR_FORMAT,      /* malformed or truncated data */
    WO_ERR_UNSUPPORTED, /* valid data we deliberately do not handle */
    WO_ERR_CHECKSUM,    /* content does not match its expected hash */
    WO_ERR_LIMIT,       /* a size or count limit was exceeded */
    WO_ERR_UNSAFE,      /* a path escapes its root or crosses a link */
    WO_ERR_NETWORK,     /* download or connection failure */
    WO_ERR_BUSY,        /* the game or another WO4K process is running */
    WO_ERR_CANCELLED
} WoErrorKind;

#define WO_ERROR_MESSAGE_MAX 1024

typedef struct WoError {
    WoErrorKind kind;
    uint32_t code; /* Win32 error code or other numeric detail; 0 if none */
    char message[WO_ERROR_MESSAGE_MAX];
} WoError;

/* Record a failure and return false, so callers can write
 *     return wo_fail(error, WO_ERR_FORMAT, "Bad header in %s", name);
 * A NULL error is allowed and simply discards the details. */
bool wo_fail(WoError *error, WoErrorKind kind, const char *format, ...) WO_PRINTF(3, 4);
bool wo_fail_code(WoError *error, WoErrorKind kind, uint32_t code, const char *format, ...)
    WO_PRINTF(4, 5);
/* Prepend context to an existing failure: "Installing music: <message>". */
void wo_error_context(WoError *error, const char *format, ...) WO_PRINTF(2, 3);
void wo_error_clear(WoError *error);
bool wo_error_is_set(const WoError *error);

/* --- Checked arithmetic ---------------------------------------------------- */
WO_NODISCARD bool wo_add_size(size_t a, size_t b, size_t *out);
WO_NODISCARD bool wo_mul_size(size_t a, size_t b, size_t *out);
WO_NODISCARD bool wo_add_u64(uint64_t a, uint64_t b, uint64_t *out);
WO_NODISCARD bool wo_mul_u64(uint64_t a, uint64_t b, uint64_t *out);

/* --- Memory ---------------------------------------------------------------- */
/* Zero-initialised allocation; reports WO_ERR_INTERNAL on exhaustion. */
void *wo_alloc(size_t size, WoError *error);
void *wo_alloc_array(size_t count, size_t size, WoError *error);
void *wo_realloc_array(void *block, size_t count, size_t size, WoError *error);
void wo_free(void *block);

/* --- Little-endian access with bounds checks -------------------------------- */
uint16_t wo_le16(const uint8_t *p);
uint32_t wo_le32(const uint8_t *p);
uint64_t wo_le64(const uint8_t *p);
void wo_put_le16(uint8_t *p, uint16_t value);
void wo_put_le32(uint8_t *p, uint32_t value);
/* True when [offset, offset + count) lies inside a buffer of length len. */
bool wo_range_ok(size_t len, size_t offset, size_t count);
bool wo_read_le16(const uint8_t *data, size_t len, size_t offset, uint16_t *out);
bool wo_read_le32(const uint8_t *data, size_t len, size_t offset, uint32_t *out);

/* --- Growable byte buffer ---------------------------------------------------- */
typedef struct WoBuf {
    uint8_t *data;
    size_t len;
    size_t cap;
} WoBuf;

bool wo_buf_reserve(WoBuf *buf, size_t extra, WoError *error);
bool wo_buf_append(WoBuf *buf, const void *data, size_t len, WoError *error);
bool wo_buf_append_str(WoBuf *buf, const char *text, WoError *error);
bool wo_buf_append_byte(WoBuf *buf, uint8_t byte, WoError *error);
bool wo_buf_printf(WoBuf *buf, WoError *error, const char *format, ...) WO_PRINTF(3, 4);
/* Ensure data[len] == 0 without counting it, so data can be used as a C string. */
bool wo_buf_terminate(WoBuf *buf, WoError *error);
/* Hand the storage to the caller (NUL-terminated); the buffer becomes empty. */
char *wo_buf_detach(WoBuf *buf);
void wo_buf_clear(WoBuf *buf);
void wo_buf_free(WoBuf *buf);

/* --- Strings (UTF-8) ---------------------------------------------------------- */
char *wo_strdup(const char *text, WoError *error);
char *wo_strndup(const char *text, size_t len, WoError *error);
char *wo_format(WoError *error, const char *format, ...) WO_PRINTF(2, 3);
char *wo_vformat(WoError *error, const char *format, va_list args);
/* Copy with truncation detection: false (and an empty-safe, terminated dst)
 * when src does not fit. */
bool wo_copy_str(char *dst, size_t cap, const char *src);
bool wo_str_eq(const char *a, const char *b);
/* ASCII case-insensitive comparisons, matching Windows file-name semantics
 * for the names WO4K deals with. */
bool wo_str_ieq(const char *a, const char *b);
int wo_str_icmp(const char *a, const char *b);
bool wo_str_istarts_with(const char *text, const char *prefix);
bool wo_str_iends_with(const char *text, const char *suffix);
bool wo_str_starts_with(const char *text, const char *prefix);
char wo_ascii_lower(char c);
bool wo_is_hex_digest(const char *text); /* 64 hex digits */
/* Parse a whole decimal string into an unsigned value within [0, max]. */
bool wo_parse_u64(const char *text, uint64_t max, uint64_t *out);

/* --- String list ------------------------------------------------------------ */
typedef struct WoStrList {
    char **items;
    size_t count;
    size_t cap;
} WoStrList;

bool wo_strlist_push(WoStrList *list, const char *text, WoError *error);
/* Takes ownership of text on success and on failure. */
bool wo_strlist_push_owned(WoStrList *list, char *text, WoError *error);
bool wo_strlist_contains_i(const WoStrList *list, const char *text);
void wo_strlist_sort_i(WoStrList *list);
void wo_strlist_free(WoStrList *list);

#endif
