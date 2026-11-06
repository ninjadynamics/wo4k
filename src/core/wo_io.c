#include "core/wo_io.h"

#include <string.h>

static bool memory_read_at(const WoSource *source, uint64_t offset, void *dst, size_t len,
                           WoError *error) {
    (void)error;
    memcpy(dst, source->memory + offset, len);
    return true;
}

void wo_source_from_memory(WoSource *source, const void *data, size_t len, const char *name) {
    memset(source, 0, sizeof(*source));
    source->size = len;
    source->memory = data;
    source->read_at = memory_read_at;
    wo_copy_str(source->name, sizeof(source->name), name ? name : "memory");
}

bool wo_source_read(const WoSource *source, uint64_t offset, void *dst, size_t len,
                    WoError *error) {
    if (offset > source->size || len > source->size - offset) {
        return wo_fail(error, WO_ERR_FORMAT, "%s is truncated (needed %zu bytes at offset %llu).",
                       source->name, len, (unsigned long long)offset);
    }
    if (len == 0) {
        return true;
    }
    return source->read_at(source, offset, dst, len, error);
}

bool wo_source_read_alloc(const WoSource *source, uint64_t offset, size_t len, size_t max_len,
                          uint8_t **out, WoError *error) {
    *out = NULL;
    if (len > max_len) {
        return wo_fail(error, WO_ERR_LIMIT, "%s: a %zu-byte record exceeds the %zu-byte limit.",
                       source->name, len, max_len);
    }
    uint8_t *data = wo_alloc(len, error);
    if (!data) {
        return false;
    }
    if (!wo_source_read(source, offset, data, len, error)) {
        wo_free(data);
        return false;
    }
    *out = data;
    return true;
}

void wo_source_close(WoSource *source) {
    if (source->close) {
        source->close(source);
    }
    memset(source, 0, sizeof(*source));
}
