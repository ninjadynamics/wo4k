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

/* A record's bytes; false when the placement is not a valid one. */
static bool make_record(uint8_t record[WO_HD_REGION_RECORD_BYTES], const char *png,
                        const char *native, int64_t page, int64_t x, int64_t y, int64_t width,
                        int64_t height, bool alpha) {
    bool valid = name_matches(png, ".png", true, 128) && name_matches(native, ".tga", false, 32) &&
                 page >= 0 && page <= 255 && x >= 0 && y >= 0 && width >= 1 && height >= 1 &&
                 x <= PAGE_SIDE - width && y <= PAGE_SIDE - height;
    if (!valid) {
        return false;
    }
    memset(record, 0, WO_HD_REGION_RECORD_BYTES);
    for (size_t i = 0; png[i]; i++) {
        record[i * 2] = (uint8_t)png[i]; /* ASCII, so UTF-16LE is one byte and a zero */
    }
    memcpy(record + PNG_FIELD, native, strlen(native));
    uint32_t values[6] = {(uint32_t)page,  (uint32_t)x,      (uint32_t)y,
                          (uint32_t)width, (uint32_t)height, alpha ? 1u : 0u};
    for (size_t i = 0; i < WO_COUNT(values); i++) {
        wo_put_le32(record + PNG_FIELD + NATIVE_FIELD + i * 4, values[i]);
    }
    return true;
}

/* Two records' sheets and rectangles are the same (names without regard to case). */
static bool same_place(const uint8_t *a, const uint8_t *b) {
    for (size_t i = PNG_FIELD; i < PNG_FIELD + NATIVE_FIELD; i++) {
        if (wo_ascii_lower((char)a[i]) != wo_ascii_lower((char)b[i])) {
            return false;
        }
    }
    return memcmp(a + PNG_FIELD + NATIVE_FIELD, b + PNG_FIELD + NATIVE_FIELD, 5 * 4) == 0;
}

static bool same_picture(const uint8_t *a, const uint8_t *b) {
    for (size_t i = 0; i < PNG_FIELD; i++) {
        if (wo_ascii_lower((char)a[i]) != wo_ascii_lower((char)b[i])) {
            return false;
        }
    }
    return true;
}

bool wo_hd_regions_add(WoHdRegionIndex *index, const char *png, const char *native, int64_t page,
                       int64_t x, int64_t y, int64_t width, int64_t height, bool alpha,
                       WoError *error) {
    uint8_t record[WO_HD_REGION_RECORD_BYTES];
    if (index->count >= WO_HD_REGION_MAX_RECORDS ||
        !make_record(record, png, native, page, x, y, width, height, alpha)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid HD texture mapping: %s", png);
    }
    if (!wo_buf_append(&index->records, record, sizeof(record), error)) {
        return false;
    }
    index->count++;
    return true;
}

size_t wo_hd_regions_find(const WoHdRegionIndex *index, const char *native, int64_t page, int64_t x,
                          int64_t y, int64_t width, int64_t height) {
    uint8_t wanted[WO_HD_REGION_RECORD_BYTES];
    if (!make_record(wanted, "a.png", native, page, x, y, width, height, false)) {
        return WO_HD_REGION_NONE;
    }
    for (size_t i = 0; i < index->count; i++) {
        if (same_place(index->records.data + i * WO_HD_REGION_RECORD_BYTES, wanted)) {
            return i;
        }
    }
    return WO_HD_REGION_NONE;
}

bool wo_hd_regions_replace(WoHdRegionIndex *index, size_t at, const char *png, bool alpha,
                           WoError *error) {
    if (at >= index->count || !name_matches(png, ".png", true, 128)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid HD texture mapping: %s", png);
    }
    uint8_t *record = index->records.data + at * WO_HD_REGION_RECORD_BYTES;
    memset(record, 0, PNG_FIELD);
    for (size_t i = 0; png[i]; i++) {
        record[i * 2] = (uint8_t)png[i];
    }
    wo_put_le32(record + PNG_FIELD + NATIVE_FIELD + 5 * 4, alpha ? 1u : 0u);
    return true;
}

bool wo_hd_regions_file_has(const uint8_t *file, size_t len, const char *png, const char *native,
                            int64_t page, int64_t x, int64_t y, int64_t width, int64_t height) {
    uint8_t wanted[WO_HD_REGION_RECORD_BYTES];
    if (!file || len < 12 || memcmp(file, "WOHDREG1", 8) != 0 ||
        !make_record(wanted, png, native, page, x, y, width, height, false)) {
        return false;
    }
    size_t count = wo_le32(file + 8);
    if (count > WO_HD_REGION_MAX_RECORDS || len != 12 + count * WO_HD_REGION_RECORD_BYTES) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        const uint8_t *record = file + 12 + i * WO_HD_REGION_RECORD_BYTES;
        if (same_place(record, wanted) && same_picture(record, wanted)) {
            return true;
        }
    }
    return false;
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
