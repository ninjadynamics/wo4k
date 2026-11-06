#include "core/wo_iso.h"

#include <string.h>

#define SECTOR 2048u
#define PVD_SECTOR 16u
/* Payload bytes delivered per streamed chunk (128 sectors). */
#define STREAM_SECTORS 128u
#define STREAM_CHUNK (SECTOR * STREAM_SECTORS)

static const uint32_t STRIDES[] = {2048, 2352, 2448};
static const uint32_t PAYLOADS[] = {0, 16, 24};

/* Read size payload bytes starting at logical sector lba into dst. Only the
 * bytes actually needed are read, so a final sector that lacks its trailing
 * error-correction or subchannel bytes is still usable. */
static bool read_payload(const WoIso *iso, uint64_t lba, uint64_t size, uint8_t *dst,
                         WoError *error) {
    if (iso->stride == SECTOR && iso->payload == 0) {
        uint64_t at = iso->origin + lba * SECTOR;
        return wo_source_read(iso->source, at, dst, (size_t)size, error);
    }
    uint8_t *raw = NULL;
    size_t raw_cap = 0;
    bool ok = true;
    while (ok && size > 0) {
        uint64_t sectors = (size + SECTOR - 1) / SECTOR;
        if (sectors > STREAM_SECTORS) {
            sectors = STREAM_SECTORS;
        }
        uint64_t take = sectors * SECTOR;
        if (take > size) {
            take = size;
        }
        size_t last = (size_t)(take - (sectors - 1) * SECTOR);
        size_t span = (size_t)((sectors - 1) * iso->stride) + last;
        if (span > raw_cap) {
            wo_free(raw);
            raw = wo_alloc(span, error);
            raw_cap = raw ? span : 0;
            if (!raw) {
                return false;
            }
        }
        uint64_t at = iso->origin + lba * iso->stride + iso->payload;
        ok = wo_source_read(iso->source, at, raw, span, error);
        for (uint64_t i = 0; ok && i < sectors; i++) {
            size_t n = i + 1 < sectors ? SECTOR : last;
            memcpy(dst + i * SECTOR, raw + i * iso->stride, n);
        }
        dst += take;
        size -= take;
        lba += sectors;
    }
    wo_free(raw);
    return ok;
}

bool wo_iso_read(const WoIso *iso, uint32_t lba, uint64_t size, size_t max_bytes, uint8_t **out,
                 WoError *error) {
    *out = NULL;
    if (size > max_bytes || size > WO_ISO_MAX_RECORD) {
        return wo_fail(error, WO_ERR_LIMIT, "%s: a %llu-byte disc record exceeds the size limit.",
                       iso->source->name, (unsigned long long)size);
    }
    uint8_t *data = wo_alloc((size_t)size, error);
    if (!data) {
        return false;
    }
    if (!read_payload(iso, lba, size, data, error)) {
        wo_free(data);
        return false;
    }
    *out = data;
    return true;
}

bool wo_iso_read_entry(const WoIso *iso, const WoIsoEntry *entry, size_t max_bytes,
                       uint8_t **out, WoError *error) {
    if (entry->is_dir) {
        *out = NULL;
        return wo_fail(error, WO_ERR_FORMAT, "%s is a folder on the disc, not a file.",
                       entry->path);
    }
    return wo_iso_read(iso, entry->lba, entry->length, max_bytes, out, error);
}

bool wo_iso_stream_entry(const WoIso *iso, const WoIsoEntry *entry, WoSinkFn sink, void *context,
                         WoError *error) {
    if (entry->is_dir) {
        return wo_fail(error, WO_ERR_FORMAT, "%s is a folder on the disc, not a file.",
                       entry->path);
    }
    uint8_t *chunk = wo_alloc(STREAM_CHUNK, error);
    if (!chunk) {
        return false;
    }
    uint64_t left = entry->length;
    uint64_t lba = entry->lba;
    bool ok = true;
    while (ok && left > 0) {
        size_t n = left > STREAM_CHUNK ? STREAM_CHUNK : (size_t)left;
        ok = read_payload(iso, lba, n, chunk, error) && sink(context, chunk, n, error);
        left -= n;
        lba += (n + SECTOR - 1) / SECTOR;
    }
    wo_free(chunk);
    return ok;
}

static bool add_entry(WoIso *iso, char *path, uint32_t lba, uint32_t length, bool is_dir,
                      WoError *error) {
    if (iso->count >= WO_ISO_MAX_ENTRIES) {
        wo_free(path);
        return wo_fail(error, WO_ERR_LIMIT, "%s: the disc lists more than %u files.",
                       iso->source->name, WO_ISO_MAX_ENTRIES);
    }
    if (iso->count == iso->cap) {
        size_t cap = iso->cap ? iso->cap * 2 : 256;
        WoIsoEntry *grown = wo_realloc_array(iso->entries, cap, sizeof(*grown), error);
        if (!grown) {
            wo_free(path);
            return false;
        }
        iso->entries = grown;
        iso->cap = cap;
    }
    WoIsoEntry *entry = &iso->entries[iso->count++];
    entry->path = path;
    entry->lba = lba;
    entry->length = length;
    entry->is_dir = is_dir;
    return true;
}

/* Copy a directory-record name, dropping the ";1" version. Returns false for
 * names that could escape the destination or are not printable. */
static bool clean_name(const uint8_t *raw, size_t len, char *out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < len && raw[i] != ';'; i++) {
        uint8_t c = raw[i];
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || n + 1 >= cap) {
            return false;
        }
        out[n++] = c < 0x80 ? (char)c : '?';
    }
    out[n] = 0;
    return n > 0 && strcmp(out, ".") != 0 && strcmp(out, "..") != 0;
}

static bool walk(WoIso *iso, uint32_t lba, uint32_t size, const char *prefix, int depth,
                 WoError *error) {
    if (depth > WO_ISO_MAX_DEPTH) {
        return wo_fail(error, WO_ERR_LIMIT, "%s: the disc's folders are nested too deeply.",
                       iso->source->name);
    }
    uint8_t *dir = NULL;
    if (!wo_iso_read(iso, lba, size, WO_ISO_MAX_RECORD, &dir, error)) {
        return false;
    }
    bool ok = true;
    size_t at = 0;
    while (ok && at < size) {
        size_t n = dir[at];
        if (n == 0) {
            /* Records never span sectors; the rest of this sector is padding. */
            at = (at / SECTOR + 1) * SECTOR;
            continue;
        }
        if (n < 34 || n > size - at || dir[at + 32] > n - 33) {
            ok = wo_fail(error, WO_ERR_FORMAT, "%s: the disc has a damaged directory.",
                         iso->source->name);
            break;
        }
        const uint8_t *record = dir + at;
        size_t name_len = record[32];
        at += n;
        if (name_len == 1 && (record[33] == 0 || record[33] == 1)) {
            continue; /* "." and ".." */
        }
        char name[256];
        if (!clean_name(record + 33, name_len, name, sizeof(name))) {
            ok = wo_fail(error, WO_ERR_UNSAFE, "%s: the disc contains an unsafe file name.",
                         iso->source->name);
            break;
        }
        uint32_t child_lba = wo_le32(record + 2);
        uint32_t child_len = wo_le32(record + 10);
        bool is_dir = (record[25] & 2) != 0;
        char *path = wo_format(error, "%s%s", prefix, name);
        if (!path) {
            ok = false;
            break;
        }
        ok = add_entry(iso, path, child_lba, child_len, is_dir, error);
        if (ok && is_dir) {
            char *child_prefix = wo_format(error, "%s/", path);
            ok = child_prefix && walk(iso, child_lba, child_len, child_prefix, depth + 1, error);
            wo_free(child_prefix);
        }
    }
    wo_free(dir);
    return ok;
}

void wo_iso_close(WoIso *iso) {
    if (!iso) {
        return;
    }
    for (size_t i = 0; i < iso->count; i++) {
        wo_free(iso->entries[i].path);
    }
    wo_free(iso->entries);
    memset(iso, 0, sizeof(*iso));
}

static bool find_layout(WoIso *iso, WoError *error) {
    uint8_t block[SECTOR];
    for (size_t s = 0; s < WO_COUNT(STRIDES); s++) {
        for (size_t p = 0; p < WO_COUNT(PAYLOADS); p++) {
            uint64_t at = iso->origin + (uint64_t)PVD_SECTOR * STRIDES[s] + PAYLOADS[p];
            if (at > iso->source->size || SECTOR > iso->source->size - at) {
                continue;
            }
            if (!wo_source_read(iso->source, at, block, SECTOR, error)) {
                return false;
            }
            if (block[0] == 1 && memcmp(block + 1, "CD001", 5) == 0) {
                iso->stride = STRIDES[s];
                iso->payload = PAYLOADS[p];
                return true;
            }
        }
    }
    return wo_fail(error, WO_ERR_FORMAT, "No supported ISO9660 data track: %s",
                   iso->source->name);
}

bool wo_iso_open(WoIso *iso, const WoSource *source, uint64_t origin, WoError *error) {
    memset(iso, 0, sizeof(*iso));
    iso->source = source;
    iso->origin = origin;
    if (origin > source->size) {
        return wo_fail(error, WO_ERR_FORMAT, "No supported ISO9660 data track: %s", source->name);
    }
    if (!find_layout(iso, error)) {
        return false;
    }
    uint8_t pvd[SECTOR];
    if (!read_payload(iso, PVD_SECTOR, SECTOR, pvd, error)) {
        wo_iso_close(iso);
        return false;
    }
    uint32_t root_lba = wo_le32(pvd + 158);
    uint32_t root_len = wo_le32(pvd + 166);
    if (!walk(iso, root_lba, root_len, "", 0, error)) {
        wo_iso_close(iso);
        return false;
    }
    return true;
}

const WoIsoEntry *wo_iso_find(const WoIso *iso, const char *path) {
    for (size_t i = 0; i < iso->count; i++) {
        if (wo_str_ieq(iso->entries[i].path, path)) {
            return &iso->entries[i];
        }
    }
    return NULL;
}
