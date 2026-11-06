/* Strict JSON (RFC 8259) document model used for manifests, state files,
 * logs and GitHub release metadata. Object member order is preserved.
 *
 * Parsing rejects: invalid UTF-8, unpaired surrogates, \u0000 (strings are
 * always safe C strings), duplicate keys, trailing data and nesting deeper
 * than WO_JSON_MAX_DEPTH. A leading UTF-8 byte-order mark is accepted
 * because Windows tools sometimes write one. */
#ifndef WO_JSON_H
#define WO_JSON_H

#include "core/wo_base.h"

#define WO_JSON_MAX_DEPTH 64

typedef enum WoJsonType {
    WO_JSON_NULL,
    WO_JSON_BOOL,
    WO_JSON_NUMBER,
    WO_JSON_STRING,
    WO_JSON_ARRAY,
    WO_JSON_OBJECT
} WoJsonType;

typedef struct WoJson WoJson;
struct WoJson {
    WoJsonType type;
    bool boolean;
    bool is_integer;  /* number written without fraction/exponent and fits int64 */
    int64_t integer;
    double number;
    char *string;     /* WO_JSON_STRING: NUL-terminated UTF-8 */
    size_t count;     /* array items / object members */
    size_t cap;
    WoJson **items;   /* array items, or object member values */
    char **keys;      /* object member names */
};

bool wo_json_parse(const char *text, size_t len, WoJson **out, WoError *error);
void wo_json_free(WoJson *value);
WoJson *wo_json_clone(const WoJson *value);

/* Serialise. pretty uses two-space indentation and a trailing newline;
 * otherwise the output is a single compact line. */
bool wo_json_write(const WoJson *value, bool pretty, WoBuf *out, WoError *error);

/* --- Reading ---------------------------------------------------------------- */
/* Member lookup: exact name first, then ASCII case-insensitive (matching how
 * the previous PowerShell implementation read these files). NULL if value is
 * not an object or has no such member. */
WoJson *wo_json_get(const WoJson *object, const char *key);
WoJson *wo_json_at(const WoJson *array, size_t index);
size_t wo_json_length(const WoJson *array_or_object);
bool wo_json_is(const WoJson *value, WoJsonType type);

/* Value accessors returning fallback when the value is missing or of a
 * different type. */
const char *wo_json_as_str(const WoJson *value, const char *fallback);
bool wo_json_as_bool(const WoJson *value, bool fallback);
int64_t wo_json_as_int(const WoJson *value, int64_t fallback);
double wo_json_as_number(const WoJson *value, double fallback);

/* Member shortcuts: wo_json_str(obj, "name", "") etc. */
const char *wo_json_str(const WoJson *object, const char *key, const char *fallback);
bool wo_json_bool(const WoJson *object, const char *key, bool fallback);
int64_t wo_json_int(const WoJson *object, const char *key, int64_t fallback);
bool wo_json_has(const WoJson *object, const char *key);

/* --- Building --------------------------------------------------------------- */
/* Constructors return NULL when out of memory. */
WoJson *wo_json_new_object(void);
WoJson *wo_json_new_array(void);
WoJson *wo_json_new_string(const char *text);
WoJson *wo_json_new_int(int64_t value);
WoJson *wo_json_new_number(double value);
WoJson *wo_json_new_bool(bool value);
WoJson *wo_json_new_null(void);

/* Insert or replace a member (an existing exact-name member keeps its
 * position). Always takes ownership of value; returns false when out of
 * memory, when value is NULL or when object is not an object. */
bool wo_json_put(WoJson *object, const char *key, WoJson *value);
bool wo_json_put_str(WoJson *object, const char *key, const char *text);
bool wo_json_put_int(WoJson *object, const char *key, int64_t value);
bool wo_json_put_bool(WoJson *object, const char *key, bool value);
bool wo_json_remove(WoJson *object, const char *key);
/* Append to an array; same ownership rules as wo_json_put. */
bool wo_json_push(WoJson *array, WoJson *value);
bool wo_json_push_str(WoJson *array, const char *text);

#endif
