#include "core/wo_base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void error_vset(WoError *error, WoErrorKind kind, uint32_t code, const char *format,
                       va_list args) {
    if (!error) {
        return;
    }
    error->kind = kind;
    error->code = code;
    int written = vsnprintf(error->message, sizeof(error->message), format, args);
    if (written < 0) {
        wo_copy_str(error->message, sizeof(error->message), "Unknown error.");
    }
}

bool wo_fail(WoError *error, WoErrorKind kind, const char *format, ...) {
    va_list args;
    va_start(args, format);
    error_vset(error, kind, 0, format, args);
    va_end(args);
    return false;
}

bool wo_fail_code(WoError *error, WoErrorKind kind, uint32_t code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    error_vset(error, kind, code, format, args);
    va_end(args);
    return false;
}

void wo_error_context(WoError *error, const char *format, ...) {
    if (!error) {
        return;
    }
    char prefix[WO_ERROR_MESSAGE_MAX];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(prefix, sizeof(prefix), format, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    char combined[WO_ERROR_MESSAGE_MAX];
    snprintf(combined, sizeof(combined), "%s%s", prefix, error->message);
    memcpy(error->message, combined, sizeof(error->message));
    if (error->kind == WO_ERR_NONE) {
        error->kind = WO_ERR_INTERNAL;
    }
}

void wo_error_clear(WoError *error) {
    if (error) {
        memset(error, 0, sizeof(*error));
    }
}

bool wo_error_is_set(const WoError *error) {
    return error && error->kind != WO_ERR_NONE;
}

bool wo_add_size(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

bool wo_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

bool wo_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a > UINT64_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

bool wo_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0 && b > UINT64_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

void *wo_alloc(size_t size, WoError *error) {
    void *block = calloc(1, size ? size : 1);
    if (!block) {
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory (%zu bytes).", size);
    }
    return block;
}

void *wo_alloc_array(size_t count, size_t size, WoError *error) {
    size_t total;
    if (!wo_mul_size(count, size, &total)) {
        wo_fail(error, WO_ERR_LIMIT, "Allocation size overflow.");
        return NULL;
    }
    return wo_alloc(total, error);
}

void *wo_realloc_array(void *block, size_t count, size_t size, WoError *error) {
    size_t total;
    if (!wo_mul_size(count, size, &total)) {
        wo_fail(error, WO_ERR_LIMIT, "Allocation size overflow.");
        return NULL;
    }
    void *grown = realloc(block, total ? total : 1);
    if (!grown) {
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory (%zu bytes).", total);
    }
    return grown;
}

void wo_free(void *block) {
    free(block);
}

uint16_t wo_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t wo_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint64_t wo_le64(const uint8_t *p) {
    return (uint64_t)wo_le32(p) | ((uint64_t)wo_le32(p + 4) << 32);
}

void wo_put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void wo_put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

bool wo_range_ok(size_t len, size_t offset, size_t count) {
    return offset <= len && count <= len - offset;
}

bool wo_read_le16(const uint8_t *data, size_t len, size_t offset, uint16_t *out) {
    if (!wo_range_ok(len, offset, 2)) {
        return false;
    }
    *out = wo_le16(data + offset);
    return true;
}

bool wo_read_le32(const uint8_t *data, size_t len, size_t offset, uint32_t *out) {
    if (!wo_range_ok(len, offset, 4)) {
        return false;
    }
    *out = wo_le32(data + offset);
    return true;
}

bool wo_buf_reserve(WoBuf *buf, size_t extra, WoError *error) {
    size_t needed;
    /* One spare byte keeps room for wo_buf_terminate. */
    if (!wo_add_size(buf->len, extra, &needed) || !wo_add_size(needed, 1, &needed)) {
        return wo_fail(error, WO_ERR_LIMIT, "Buffer size overflow.");
    }
    if (needed <= buf->cap) {
        return true;
    }
    size_t cap = buf->cap ? buf->cap : 64;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }
    uint8_t *grown = wo_realloc_array(buf->data, cap, 1, error);
    if (!grown) {
        return false;
    }
    buf->data = grown;
    buf->cap = cap;
    return true;
}

bool wo_buf_append(WoBuf *buf, const void *data, size_t len, WoError *error) {
    if (len == 0) {
        return true;
    }
    if (!wo_buf_reserve(buf, len, error)) {
        return false;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return true;
}

bool wo_buf_append_str(WoBuf *buf, const char *text, WoError *error) {
    return wo_buf_append(buf, text, strlen(text), error);
}

bool wo_buf_append_byte(WoBuf *buf, uint8_t byte, WoError *error) {
    return wo_buf_append(buf, &byte, 1, error);
}

bool wo_buf_printf(WoBuf *buf, WoError *error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    bool ok = needed >= 0 && wo_buf_reserve(buf, (size_t)needed, error);
    if (ok) {
        vsnprintf((char *)buf->data + buf->len, (size_t)needed + 1, format, args);
        buf->len += (size_t)needed;
    } else if (needed < 0) {
        wo_fail(error, WO_ERR_INTERNAL, "Text formatting failed.");
    }
    va_end(args);
    return ok;
}

bool wo_buf_terminate(WoBuf *buf, WoError *error) {
    if (!wo_buf_reserve(buf, 0, error)) {
        return false;
    }
    buf->data[buf->len] = 0;
    return true;
}

char *wo_buf_detach(WoBuf *buf) {
    if (!wo_buf_terminate(buf, NULL)) {
        return NULL;
    }
    char *text = (char *)buf->data;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return text;
}

void wo_buf_clear(WoBuf *buf) {
    buf->len = 0;
}

void wo_buf_free(WoBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

char *wo_strdup(const char *text, WoError *error) {
    return wo_strndup(text, strlen(text), error);
}

char *wo_strndup(const char *text, size_t len, WoError *error) {
    size_t size;
    if (!wo_add_size(len, 1, &size)) {
        wo_fail(error, WO_ERR_LIMIT, "String too long.");
        return NULL;
    }
    char *copy = wo_alloc(size, error);
    if (copy) {
        memcpy(copy, text, len);
        copy[len] = 0;
    }
    return copy;
}

char *wo_vformat(WoError *error, const char *format, va_list args) {
    WoBuf buf = {0};
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0) {
        wo_fail(error, WO_ERR_INTERNAL, "Text formatting failed.");
        return NULL;
    }
    if (!wo_buf_reserve(&buf, (size_t)needed, error)) {
        return NULL;
    }
    vsnprintf((char *)buf.data, (size_t)needed + 1, format, args);
    buf.len = (size_t)needed;
    return wo_buf_detach(&buf);
}

char *wo_format(WoError *error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *text = wo_vformat(error, format, args);
    va_end(args);
    return text;
}

bool wo_copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) {
        return false;
    }
    size_t len = strlen(src);
    if (len >= cap) {
        memcpy(dst, src, cap - 1);
        dst[cap - 1] = 0;
        return false;
    }
    memcpy(dst, src, len + 1);
    return true;
}

bool wo_str_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

char wo_ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int wo_str_icmp(const char *a, const char *b) {
    for (;; a++, b++) {
        unsigned char ca = (unsigned char)wo_ascii_lower(*a);
        unsigned char cb = (unsigned char)wo_ascii_lower(*b);
        if (ca != cb || ca == 0) {
            return (int)ca - (int)cb;
        }
    }
}

bool wo_str_ieq(const char *a, const char *b) {
    return wo_str_icmp(a, b) == 0;
}

bool wo_str_istarts_with(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        if (wo_ascii_lower(*text) != wo_ascii_lower(*prefix)) {
            return false;
        }
    }
    return true;
}

bool wo_str_starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

bool wo_str_iends_with(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return suffix_len <= text_len && wo_str_ieq(text + text_len - suffix_len, suffix);
}

bool wo_is_hex_digest(const char *text) {
    size_t i = 0;
    for (; text[i]; i++) {
        char c = text[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex || i >= 64) {
            return false;
        }
    }
    return i == 64;
}

bool wo_parse_u64(const char *text, uint64_t max, uint64_t *out) {
    if (!text || !*text) {
        return false;
    }
    uint64_t value = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        uint64_t digit = (uint64_t)(*p - '0');
        if (value > (max - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

bool wo_strlist_push_owned(WoStrList *list, char *text, WoError *error) {
    if (!text) {
        return false;
    }
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        char **grown = wo_realloc_array(list->items, cap, sizeof(char *), error);
        if (!grown) {
            free(text);
            return false;
        }
        list->items = grown;
        list->cap = cap;
    }
    list->items[list->count++] = text;
    return true;
}

bool wo_strlist_push(WoStrList *list, const char *text, WoError *error) {
    char *copy = wo_strdup(text, error);
    return copy && wo_strlist_push_owned(list, copy, error);
}

bool wo_strlist_contains_i(const WoStrList *list, const char *text) {
    for (size_t i = 0; i < list->count; i++) {
        if (wo_str_ieq(list->items[i], text)) {
            return true;
        }
    }
    return false;
}

static int compare_icase(const void *a, const void *b) {
    const char *const *left = a;
    const char *const *right = b;
    int order = wo_str_icmp(*left, *right);
    return order ? order : strcmp(*left, *right);
}

void wo_strlist_sort_i(WoStrList *list) {
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(char *), compare_icase);
    }
}

void wo_strlist_free(WoStrList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}
