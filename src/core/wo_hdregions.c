#include "core/wo_hdregions.h"

#include <string.h>

#define PNG_FIELD 256u
#define NATIVE_FIELD 32u
#define PAGE_SIDE 256

/* [a-z0-9<extra>]+ followed by suffix, ignoring case, shorter than limit. */
static bool name_matches(const char *name, const char *suffix, bool allow_hyphen, size_t limit) {
    size_t len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (len >= limit || len <= suffix_len || !wo_str_iends_with(name, suffix)) {
        return false;
    }
    for (size_t i = 0; i < len - suffix_len; i++) {
        char c = wo_ascii_lower(name[i]);
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (allow_hyphen && c == '-');
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool wo_hd_regions_add(WoHdRegionIndex *index, const char *png, const char *native, int64_t page,
                       int64_t x, int64_t y, int64_t width, int64_t height, bool alpha,
                       WoError *error) {
    bool valid = name_matches(png, ".png", true, 128) && name_matches(native, ".tga", false, 32) &&
                 page >= 0 && page <= 255 && x >= 0 && y >= 0 && width >= 1 && height >= 1 &&
                 x <= PAGE_SIDE - width && y <= PAGE_SIDE - height &&
                 index->count < WO_HD_REGION_MAX_RECORDS;
    if (!valid) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid HD texture mapping: %s", png);
    }
    uint8_t record[WO_HD_REGION_RECORD_BYTES];
    memset(record, 0, sizeof(record));
    for (size_t i = 0; png[i]; i++) {
        record[i * 2] = (uint8_t)png[i]; /* ASCII, so UTF-16LE is one byte and a zero */
    }
    memcpy(record + PNG_FIELD, native, strlen(native));
    uint32_t values[6] = {(uint32_t)page,  (uint32_t)x,      (uint32_t)y,
                          (uint32_t)width, (uint32_t)height, alpha ? 1u : 0u};
    for (size_t i = 0; i < WO_COUNT(values); i++) {
        wo_put_le32(record + PNG_FIELD + NATIVE_FIELD + i * 4, values[i]);
    }
    if (!wo_buf_append(&index->records, record, sizeof(record), error)) {
        return false;
    }
    index->count++;
    return true;
}

bool wo_hd_regions_serialize(const WoHdRegionIndex *index, WoBuf *out, WoError *error) {
    uint8_t header[12];
    memcpy(header, "WOHDREG1", 8);
    wo_put_le32(header + 8, index->count);
    return wo_buf_append(out, header, sizeof(header), error) &&
           wo_buf_append(out, index->records.data, index->records.len, error);
}

void wo_hd_regions_free(WoHdRegionIndex *index) {
    if (index) {
        wo_buf_free(&index->records);
        index->count = 0;
    }
}
