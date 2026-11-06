#include "core/wo_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    const unsigned char *at;
    const unsigned char *end;
    const unsigned char *start;
    WoError *error;
} Parser;

static WoJson *new_value(WoJsonType type) {
    WoJson *value = calloc(1, sizeof(*value));
    if (value) {
        value->type = type;
    }
    return value;
}

void wo_json_free(WoJson *value) {
    if (!value) {
        return;
    }
    for (size_t i = 0; i < value->count; i++) {
        wo_json_free(value->items[i]);
        if (value->keys) {
            free(value->keys[i]);
        }
    }
    free(value->items);
    free(value->keys);
    free(value->string);
    free(value);
}

static bool parse_fail(Parser *p, const char *what) {
    return wo_fail(p->error, WO_ERR_FORMAT, "Invalid JSON at byte %zu: %s.",
                   (size_t)(p->at - p->start), what);
}

static void skip_space(Parser *p) {
    while (p->at < p->end &&
           (*p->at == ' ' || *p->at == '\t' || *p->at == '\n' || *p->at == '\r')) {
        p->at++;
    }
}

static bool literal(Parser *p, const char *word) {
    size_t len = strlen(word);
    if ((size_t)(p->end - p->at) < len || memcmp(p->at, word, len) != 0) {
        return false;
    }
    p->at += len;
    return true;
}

static bool grow_children(WoJson *value, bool with_keys) {
    if (value->count < value->cap) {
        return true;
    }
    size_t cap = value->cap ? value->cap * 2 : 8;
    WoJson **items = wo_realloc_array(value->items, cap, sizeof(*items), NULL);
    if (!items) {
        return false;
    }
    value->items = items;
    if (with_keys) {
        char **keys = wo_realloc_array(value->keys, cap, sizeof(*keys), NULL);
        if (!keys) {
            return false;
        }
        value->keys = keys;
    }
    value->cap = cap;
    return true;
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool read_hex4(Parser *p, uint32_t *out) {
    if (p->end - p->at < 4) {
        return parse_fail(p, "truncated \\u escape");
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_value(p->at[i]);
        if (digit < 0) {
            return parse_fail(p, "bad \\u escape");
        }
        value = (value << 4) | (uint32_t)digit;
    }
    p->at += 4;
    *out = value;
    return true;
}

static bool append_utf8(WoBuf *buf, uint32_t cp) {
    uint8_t bytes[4];
    size_t n;
    if (cp < 0x80) {
        bytes[0] = (uint8_t)cp;
        n = 1;
    } else if (cp < 0x800) {
        bytes[0] = (uint8_t)(0xC0 | (cp >> 6));
        bytes[1] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (uint8_t)(0xE0 | (cp >> 12));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        bytes[0] = (uint8_t)(0xF0 | (cp >> 18));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 4;
    }
    return wo_buf_append(buf, bytes, n, NULL);
}

/* Length of the valid UTF-8 sequence at s (rejecting overlong forms,
 * surrogates and code points above U+10FFFF), or 0 if invalid. */
static size_t utf8_sequence(const unsigned char *s, const unsigned char *end) {
    unsigned char c = s[0];
    if (c < 0x80) {
        return 1;
    }
    size_t n;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
        cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        cp = c & 0x07;
    } else {
        return 0;
    }
    if ((size_t)(end - s) < n) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    return n;
}

static bool parse_string_raw(Parser *p, char **out) {
    /* Caller has checked the opening quote. */
    p->at++;
    WoBuf buf = {0};
    for (;;) {
        if (p->at >= p->end) {
            wo_buf_free(&buf);
            return parse_fail(p, "unterminated string");
        }
        unsigned char c = *p->at;
        if (c == '"') {
            p->at++;
            break;
        }
        if (c < 0x20) {
            wo_buf_free(&buf);
            return parse_fail(p, "control character in string");
        }
        if (c == '\\') {
            p->at++;
            if (p->at >= p->end) {
                wo_buf_free(&buf);
                return parse_fail(p, "truncated escape");
            }
            unsigned char e = *p->at++;
            char plain = 0;
            switch (e) {
            case '"': plain = '"'; break;
            case '\\': plain = '\\'; break;
            case '/': plain = '/'; break;
            case 'b': plain = '\b'; break;
            case 'f': plain = '\f'; break;
            case 'n': plain = '\n'; break;
            case 'r': plain = '\r'; break;
            case 't': plain = '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!read_hex4(p, &cp)) {
                    wo_buf_free(&buf);
                    return false;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low;
                    if (p->end - p->at < 2 || p->at[0] != '\\' || p->at[1] != 'u') {
                        wo_buf_free(&buf);
                        return parse_fail(p, "unpaired surrogate");
                    }
                    p->at += 2;
                    if (!read_hex4(p, &low)) {
                        wo_buf_free(&buf);
                        return false;
                    }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        wo_buf_free(&buf);
                        return parse_fail(p, "unpaired surrogate");
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    wo_buf_free(&buf);
                    return parse_fail(p, "unpaired surrogate");
                }
                if (cp == 0) {
                    wo_buf_free(&buf);
                    return parse_fail(p, "\\u0000 is not allowed");
                }
                if (!append_utf8(&buf, cp)) {
                    wo_buf_free(&buf);
                    return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
                }
                continue;
            }
            default:
                wo_buf_free(&buf);
                return parse_fail(p, "unknown escape");
            }
            if (!wo_buf_append_byte(&buf, (uint8_t)plain, NULL)) {
                wo_buf_free(&buf);
                return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
            }
            continue;
        }
        size_t n = utf8_sequence(p->at, p->end);
        if (n == 0) {
            wo_buf_free(&buf);
            return parse_fail(p, "invalid UTF-8");
        }
        if (!wo_buf_append(&buf, p->at, n, NULL)) {
            wo_buf_free(&buf);
            return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
        }
        p->at += n;
    }
    if (!wo_buf_terminate(&buf, NULL)) {
        wo_buf_free(&buf);
        return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
    }
    *out = wo_buf_detach(&buf);
    return true;
}

static bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static bool parse_number(Parser *p, WoJson **out) {
    const unsigned char *begin = p->at;
    bool integral = true;
    if (p->at < p->end && *p->at == '-') {
        p->at++;
    }
    if (p->at >= p->end || !is_digit(*p->at)) {
        return parse_fail(p, "bad number");
    }
    if (*p->at == '0') {
        p->at++;
    } else {
        while (p->at < p->end && is_digit(*p->at)) {
            p->at++;
        }
    }
    if (p->at < p->end && *p->at == '.') {
        integral = false;
        p->at++;
        if (p->at >= p->end || !is_digit(*p->at)) {
            return parse_fail(p, "bad fraction");
        }
        while (p->at < p->end && is_digit(*p->at)) {
            p->at++;
        }
    }
    if (p->at < p->end && (*p->at == 'e' || *p->at == 'E')) {
        integral = false;
        p->at++;
        if (p->at < p->end && (*p->at == '+' || *p->at == '-')) {
            p->at++;
        }
        if (p->at >= p->end || !is_digit(*p->at)) {
            return parse_fail(p, "bad exponent");
        }
        while (p->at < p->end && is_digit(*p->at)) {
            p->at++;
        }
    }
    size_t len = (size_t)(p->at - begin);
    char text[64];
    if (len >= sizeof(text)) {
        return parse_fail(p, "number too long");
    }
    memcpy(text, begin, len);
    text[len] = 0;
    WoJson *value = new_value(WO_JSON_NUMBER);
    if (!value) {
        return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
    }
    value->number = strtod(text, NULL);
    if (integral) {
        /* Accumulate negatively so INT64_MIN is representable. */
        bool negative = text[0] == '-';
        int64_t accumulated = 0;
        bool fits = true;
        for (const char *d = text + (negative ? 1 : 0); *d; d++) {
            int digit = *d - '0';
            if (accumulated < (INT64_MIN + digit) / 10) {
                fits = false;
                break;
            }
            accumulated = accumulated * 10 - digit;
        }
        if (fits && !negative && accumulated == INT64_MIN) {
            fits = false;
        }
        if (fits) {
            value->is_integer = true;
            value->integer = negative ? accumulated : -accumulated;
        }
    }
    *out = value;
    return true;
}

static bool parse_value(Parser *p, WoJson **out, int depth);

static bool parse_array(Parser *p, WoJson **out, int depth) {
    WoJson *array = new_value(WO_JSON_ARRAY);
    if (!array) {
        return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
    }
    p->at++;
    skip_space(p);
    if (p->at < p->end && *p->at == ']') {
        p->at++;
        *out = array;
        return true;
    }
    for (;;) {
        WoJson *item = NULL;
        if (!parse_value(p, &item, depth + 1)) {
            wo_json_free(array);
            return false;
        }
        if (!grow_children(array, false)) {
            wo_json_free(item);
            wo_json_free(array);
            return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
        }
        array->items[array->count++] = item;
        skip_space(p);
        if (p->at < p->end && *p->at == ',') {
            p->at++;
            continue;
        }
        if (p->at < p->end && *p->at == ']') {
            p->at++;
            *out = array;
            return true;
        }
        wo_json_free(array);
        return parse_fail(p, "expected ',' or ']'");
    }
}

static bool parse_object(Parser *p, WoJson **out, int depth) {
    WoJson *object = new_value(WO_JSON_OBJECT);
    if (!object) {
        return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
    }
    p->at++;
    skip_space(p);
    if (p->at < p->end && *p->at == '}') {
        p->at++;
        *out = object;
        return true;
    }
    for (;;) {
        skip_space(p);
        if (p->at >= p->end || *p->at != '"') {
            wo_json_free(object);
            return parse_fail(p, "expected member name");
        }
        char *key = NULL;
        if (!parse_string_raw(p, &key)) {
            wo_json_free(object);
            return false;
        }
        for (size_t i = 0; i < object->count; i++) {
            if (strcmp(object->keys[i], key) == 0) {
                free(key);
                wo_json_free(object);
                return parse_fail(p, "duplicate member name");
            }
        }
        skip_space(p);
        if (p->at >= p->end || *p->at != ':') {
            free(key);
            wo_json_free(object);
            return parse_fail(p, "expected ':'");
        }
        p->at++;
        WoJson *member = NULL;
        if (!parse_value(p, &member, depth + 1)) {
            free(key);
            wo_json_free(object);
            return false;
        }
        if (!grow_children(object, true)) {
            free(key);
            wo_json_free(member);
            wo_json_free(object);
            return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
        }
        object->keys[object->count] = key;
        object->items[object->count] = member;
        object->count++;
        skip_space(p);
        if (p->at < p->end && *p->at == ',') {
            p->at++;
            continue;
        }
        if (p->at < p->end && *p->at == '}') {
            p->at++;
            *out = object;
            return true;
        }
        wo_json_free(object);
        return parse_fail(p, "expected ',' or '}'");
    }
}

static bool parse_value(Parser *p, WoJson **out, int depth) {
    if (depth > WO_JSON_MAX_DEPTH) {
        return wo_fail(p->error, WO_ERR_LIMIT, "JSON nesting is too deep.");
    }
    skip_space(p);
    if (p->at >= p->end) {
        return parse_fail(p, "unexpected end");
    }
    switch (*p->at) {
    case '{':
        return parse_object(p, out, depth);
    case '[':
        return parse_array(p, out, depth);
    case '"': {
        char *text = NULL;
        if (!parse_string_raw(p, &text)) {
            return false;
        }
        WoJson *value = new_value(WO_JSON_STRING);
        if (!value) {
            free(text);
            return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
        }
        value->string = text;
        *out = value;
        return true;
    }
    case 't':
    case 'f':
    case 'n': {
        WoJsonType type;
        bool truth = false;
        if (literal(p, "true")) {
            type = WO_JSON_BOOL;
            truth = true;
        } else if (literal(p, "false")) {
            type = WO_JSON_BOOL;
        } else if (literal(p, "null")) {
            type = WO_JSON_NULL;
        } else {
            return parse_fail(p, "unknown literal");
        }
        WoJson *value = new_value(type);
        if (!value) {
            return wo_fail(p->error, WO_ERR_INTERNAL, "Out of memory.");
        }
        value->boolean = truth;
        *out = value;
        return true;
    }
    default:
        return parse_number(p, out);
    }
}

bool wo_json_parse(const char *text, size_t len, WoJson **out, WoError *error) {
    *out = NULL;
    Parser p;
    p.start = (const unsigned char *)text;
    p.at = p.start;
    p.end = p.start + len;
    p.error = error;
    if (len >= 3 && p.at[0] == 0xEF && p.at[1] == 0xBB && p.at[2] == 0xBF) {
        p.at += 3;
    }
    WoJson *value = NULL;
    if (!parse_value(&p, &value, 0)) {
        return false;
    }
    skip_space(&p);
    if (p.at != p.end) {
        wo_json_free(value);
        return parse_fail(&p, "trailing data");
    }
    *out = value;
    return true;
}

/* --- Writing ----------------------------------------------------------------- */

static bool write_string(WoBuf *out, const char *text, WoError *error) {
    if (!wo_buf_append_byte(out, '"', error)) {
        return false;
    }
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        bool ok;
        switch (*s) {
        case '"': ok = wo_buf_append_str(out, "\\\"", error); break;
        case '\\': ok = wo_buf_append_str(out, "\\\\", error); break;
        case '\n': ok = wo_buf_append_str(out, "\\n", error); break;
        case '\r': ok = wo_buf_append_str(out, "\\r", error); break;
        case '\t': ok = wo_buf_append_str(out, "\\t", error); break;
        default:
            if (*s < 0x20) {
                ok = wo_buf_printf(out, error, "\\u%04x", *s);
            } else {
                ok = wo_buf_append_byte(out, *s, error);
            }
            break;
        }
        if (!ok) {
            return false;
        }
    }
    return wo_buf_append_byte(out, '"', error);
}

static bool write_indent(WoBuf *out, bool pretty, int depth, WoError *error) {
    if (!pretty) {
        return true;
    }
    if (!wo_buf_append_byte(out, '\n', error)) {
        return false;
    }
    for (int i = 0; i < depth; i++) {
        if (!wo_buf_append_str(out, "  ", error)) {
            return false;
        }
    }
    return true;
}

static bool write_value(const WoJson *v, bool pretty, int depth, WoBuf *out, WoError *error) {
    if (depth > WO_JSON_MAX_DEPTH) {
        return wo_fail(error, WO_ERR_LIMIT, "JSON nesting is too deep.");
    }
    switch (v->type) {
    case WO_JSON_NULL:
        return wo_buf_append_str(out, "null", error);
    case WO_JSON_BOOL:
        return wo_buf_append_str(out, v->boolean ? "true" : "false", error);
    case WO_JSON_NUMBER:
        if (v->is_integer) {
            return wo_buf_printf(out, error, "%lld", (long long)v->integer);
        }
        if (!isfinite(v->number)) {
            return wo_buf_append_str(out, "null", error);
        }
        /* Shortest precision that reads back exactly (63.65, not 63.649999…). */
        for (int precision = 15; precision <= 17; precision++) {
            char text[40];
            snprintf(text, sizeof(text), "%.*g", precision, v->number);
            if (precision == 17 || strtod(text, NULL) == v->number) {
                return wo_buf_append_str(out, text, error);
            }
        }
        return wo_fail(error, WO_ERR_INTERNAL, "Number formatting failed.");
    case WO_JSON_STRING:
        return write_string(out, v->string ? v->string : "", error);
    case WO_JSON_ARRAY:
    case WO_JSON_OBJECT: {
        bool object = v->type == WO_JSON_OBJECT;
        if (!wo_buf_append_byte(out, object ? '{' : '[', error)) {
            return false;
        }
        for (size_t i = 0; i < v->count; i++) {
            if (i > 0 && !wo_buf_append_byte(out, ',', error)) {
                return false;
            }
            if (!write_indent(out, pretty, depth + 1, error)) {
                return false;
            }
            if (object) {
                if (!write_string(out, v->keys[i], error) ||
                    !wo_buf_append_str(out, pretty ? ": " : ":", error)) {
                    return false;
                }
            }
            if (!write_value(v->items[i], pretty, depth + 1, out, error)) {
                return false;
            }
        }
        if (v->count > 0 && !write_indent(out, pretty, depth, error)) {
            return false;
        }
        return wo_buf_append_byte(out, object ? '}' : ']', error);
    }
    }
    return wo_fail(error, WO_ERR_INTERNAL, "Unknown JSON value type.");
}

bool wo_json_write(const WoJson *value, bool pretty, WoBuf *out, WoError *error) {
    if (!write_value(value, pretty, 0, out, error)) {
        return false;
    }
    if (pretty && !wo_buf_append_byte(out, '\n', error)) {
        return false;
    }
    return wo_buf_terminate(out, error);
}

/* --- Reading helpers ----------------------------------------------------------- */

WoJson *wo_json_get(const WoJson *object, const char *key) {
    if (!object || object->type != WO_JSON_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) {
            return object->items[i];
        }
    }
    for (size_t i = 0; i < object->count; i++) {
        if (wo_str_ieq(object->keys[i], key)) {
            return object->items[i];
        }
    }
    return NULL;
}

WoJson *wo_json_at(const WoJson *array, size_t index) {
    if (!array || array->type != WO_JSON_ARRAY || index >= array->count) {
        return NULL;
    }
    return array->items[index];
}

size_t wo_json_length(const WoJson *value) {
    if (!value || (value->type != WO_JSON_ARRAY && value->type != WO_JSON_OBJECT)) {
        return 0;
    }
    return value->count;
}

bool wo_json_is(const WoJson *value, WoJsonType type) {
    return value && value->type == type;
}

const char *wo_json_as_str(const WoJson *value, const char *fallback) {
    return (value && value->type == WO_JSON_STRING) ? value->string : fallback;
}

bool wo_json_as_bool(const WoJson *value, bool fallback) {
    return (value && value->type == WO_JSON_BOOL) ? value->boolean : fallback;
}

int64_t wo_json_as_int(const WoJson *value, int64_t fallback) {
    return (value && value->type == WO_JSON_NUMBER && value->is_integer) ? value->integer
                                                                         : fallback;
}

double wo_json_as_number(const WoJson *value, double fallback) {
    return (value && value->type == WO_JSON_NUMBER) ? value->number : fallback;
}

const char *wo_json_str(const WoJson *object, const char *key, const char *fallback) {
    return wo_json_as_str(wo_json_get(object, key), fallback);
}

bool wo_json_bool(const WoJson *object, const char *key, bool fallback) {
    return wo_json_as_bool(wo_json_get(object, key), fallback);
}

int64_t wo_json_int(const WoJson *object, const char *key, int64_t fallback) {
    return wo_json_as_int(wo_json_get(object, key), fallback);
}

bool wo_json_has(const WoJson *object, const char *key) {
    return wo_json_get(object, key) != NULL;
}

/* --- Building helpers ---------------------------------------------------------- */

WoJson *wo_json_new_object(void) {
    return new_value(WO_JSON_OBJECT);
}

WoJson *wo_json_new_array(void) {
    return new_value(WO_JSON_ARRAY);
}

WoJson *wo_json_new_string(const char *text) {
    WoJson *value = new_value(WO_JSON_STRING);
    if (value) {
        value->string = wo_strdup(text ? text : "", NULL);
        if (!value->string) {
            free(value);
            return NULL;
        }
    }
    return value;
}

WoJson *wo_json_new_int(int64_t integer) {
    WoJson *value = new_value(WO_JSON_NUMBER);
    if (value) {
        value->is_integer = true;
        value->integer = integer;
        value->number = (double)integer;
    }
    return value;
}

WoJson *wo_json_new_number(double number) {
    WoJson *value = new_value(WO_JSON_NUMBER);
    if (value) {
        value->number = number;
    }
    return value;
}

WoJson *wo_json_new_bool(bool truth) {
    WoJson *value = new_value(WO_JSON_BOOL);
    if (value) {
        value->boolean = truth;
    }
    return value;
}

WoJson *wo_json_new_null(void) {
    return new_value(WO_JSON_NULL);
}

bool wo_json_put(WoJson *object, const char *key, WoJson *value) {
    if (!value) {
        return false;
    }
    if (!object || object->type != WO_JSON_OBJECT) {
        wo_json_free(value);
        return false;
    }
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) {
            wo_json_free(object->items[i]);
            object->items[i] = value;
            return true;
        }
    }
    char *name = wo_strdup(key, NULL);
    if (!name || !grow_children(object, true)) {
        free(name);
        wo_json_free(value);
        return false;
    }
    object->keys[object->count] = name;
    object->items[object->count] = value;
    object->count++;
    return true;
}

bool wo_json_put_str(WoJson *object, const char *key, const char *text) {
    return wo_json_put(object, key, wo_json_new_string(text));
}

bool wo_json_put_int(WoJson *object, const char *key, int64_t value) {
    return wo_json_put(object, key, wo_json_new_int(value));
}

bool wo_json_put_bool(WoJson *object, const char *key, bool value) {
    return wo_json_put(object, key, wo_json_new_bool(value));
}

bool wo_json_remove(WoJson *object, const char *key) {
    if (!object || object->type != WO_JSON_OBJECT) {
        return false;
    }
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) {
            wo_json_free(object->items[i]);
            free(object->keys[i]);
            memmove(&object->items[i], &object->items[i + 1],
                    (object->count - i - 1) * sizeof(*object->items));
            memmove(&object->keys[i], &object->keys[i + 1],
                    (object->count - i - 1) * sizeof(*object->keys));
            object->count--;
            return true;
        }
    }
    return false;
}

bool wo_json_push(WoJson *array, WoJson *value) {
    if (!value) {
        return false;
    }
    if (!array || array->type != WO_JSON_ARRAY || !grow_children(array, false)) {
        wo_json_free(value);
        return false;
    }
    array->items[array->count++] = value;
    return true;
}

bool wo_json_push_str(WoJson *array, const char *text) {
    return wo_json_push(array, wo_json_new_string(text));
}

WoJson *wo_json_clone(const WoJson *value) {
    if (!value) {
        return NULL;
    }
    WoJson *copy = new_value(value->type);
    if (!copy) {
        return NULL;
    }
    copy->boolean = value->boolean;
    copy->is_integer = value->is_integer;
    copy->integer = value->integer;
    copy->number = value->number;
    if (value->string) {
        copy->string = wo_strdup(value->string, NULL);
        if (!copy->string) {
            wo_json_free(copy);
            return NULL;
        }
    }
    for (size_t i = 0; i < value->count; i++) {
        WoJson *child = wo_json_clone(value->items[i]);
        bool ok = value->type == WO_JSON_OBJECT ? wo_json_put(copy, value->keys[i], child)
                                                : wo_json_push(copy, child);
        if (!ok) {
            wo_json_free(copy);
            return NULL;
        }
    }
    return copy;
}
