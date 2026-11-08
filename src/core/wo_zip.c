#include "core/wo_zip.h"

#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_miniz.h"

#define ZIP_NAME_MAX 1024

struct WoZip {
    mz_zip_archive archive;
    WoSource source;
    WoZipEntry *entries;
    size_t count;
    char path[260]; /* display name */
};

struct WoZipWriter {
    mz_zip_archive archive;
    time_t when;
};

/* IBM code page 437, bytes 0x80-0xFF. */
static const uint16_t cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8,
    0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2,
    0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1,
    0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD,
    0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562,
    0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534,
    0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560,
    0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393,
    0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6,
    0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0,
    0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static bool valid_utf8(const unsigned char *s, bool *has_multibyte) {
    *has_multibyte = false;
    while (*s) {
        if (*s < 0x80) {
            s++;
            continue;
        }
        size_t n;
        uint32_t cp;
        if (*s >= 0xC2 && *s <= 0xDF) {
            n = 2;
            cp = *s & 0x1F;
        } else if (*s >= 0xE0 && *s <= 0xEF) {
            n = 3;
            cp = *s & 0x0F;
        } else if (*s >= 0xF0 && *s <= 0xF4) {
            n = 4;
            cp = *s & 0x07;
        } else {
            return false;
        }
        for (size_t i = 1; i < n; i++) {
            if ((s[i] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (s[i] & 0x3F);
        }
        if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        *has_multibyte = true;
        s += n;
    }
    return true;
}

static bool append_code_point(WoBuf *buf, uint32_t cp, WoError *error) {
    uint8_t bytes[3];
    size_t n;
    if (cp < 0x80) {
        bytes[0] = (uint8_t)cp;
        n = 1;
    } else if (cp < 0x800) {
        bytes[0] = (uint8_t)(0xC0 | (cp >> 6));
        bytes[1] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 2;
    } else {
        bytes[0] = (uint8_t)(0xE0 | (cp >> 12));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 3;
    }
    return wo_buf_append(buf, bytes, n, error);
}

static bool reserved_device_name(const char *component, size_t len) {
    static const char *names[] = {"CON", "PRN", "AUX", "NUL"};
    size_t stem = 0;
    while (stem < len && component[stem] != '.') {
        stem++;
    }
    for (size_t i = 0; i < WO_COUNT(names); i++) {
        if (stem == 3 && wo_str_istarts_with(component, names[i])) {
            return true;
        }
    }
    if (stem == 4 && (wo_str_istarts_with(component, "COM") || wo_str_istarts_with(component, "LPT")) &&
        component[3] >= '1' && component[3] <= '9') {
        return true;
    }
    return false;
}

static bool component_ok(const char *component, size_t len) {
    if (len == 0 || (len == 1 && component[0] == '.') ||
        (len == 2 && component[0] == '.' && component[1] == '.')) {
        return false;
    }
    if (component[len - 1] == '.' || component[len - 1] == ' ') {
        return false;
    }
    return !reserved_device_name(component, len);
}

char *wo_zip_normalise_name(const char *raw, bool is_utf8, bool *safe, WoError *error) {
    *safe = false;
    bool multibyte = false;
    bool utf8 = valid_utf8((const unsigned char *)raw, &multibyte);
    WoBuf buf = {0};
    bool ok = true;
    if (is_utf8 ? utf8 : (utf8 && multibyte)) {
        ok = wo_buf_append_str(&buf, raw, error);
    } else if (!is_utf8) {
        for (const unsigned char *s = (const unsigned char *)raw; ok && *s; s++) {
            uint32_t cp = *s < 0x80 ? *s : cp437_high[*s - 0x80];
            ok = append_code_point(&buf, cp, error);
        }
    } else {
        ok = wo_buf_append_str(&buf, "(invalid name)", error);
        utf8 = false;
    }
    if (!ok || !wo_buf_terminate(&buf, error)) {
        wo_buf_free(&buf);
        return NULL;
    }
    char *name = wo_buf_detach(&buf);
    for (char *p = name; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    size_t len = strlen(name);
    while (len > 0 && name[len - 1] == '/') {
        name[--len] = 0;
    }
    bool good = (is_utf8 ? utf8 : true) && len > 0 && len <= ZIP_NAME_MAX && name[0] != '/';
    for (size_t i = 0; good && i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F || strchr(":<>\"|?*", c)) {
            good = false;
        }
    }
    const char *start = name;
    while (good) {
        const char *slash = strchr(start, '/');
        size_t part = slash ? (size_t)(slash - start) : strlen(start);
        good = component_ok(start, part);
        if (!slash) {
            break;
        }
        start = slash + 1;
    }
    *safe = good;
    return name;
}

static size_t zip_read(void *opaque, mz_uint64 offset, void *buffer, size_t n) {
    WoSource *source = opaque;
    if (offset >= source->size) {
        return 0;
    }
    if (n > source->size - offset) {
        n = (size_t)(source->size - offset);
    }
    return wo_source_read(source, offset, buffer, n, NULL) ? n : 0;
}

static bool zip_fail(WoZip *zip, WoError *error, const char *what) {
    mz_zip_error code = mz_zip_get_last_error(&zip->archive);
    return wo_fail(error, code == MZ_ZIP_FILE_READ_FAILED ? WO_ERR_IO : WO_ERR_FORMAT,
                   "%s: %s (%s).", zip->path, what, mz_zip_get_error_string(code));
}

void wo_zip_close(WoZip *zip) {
    if (!zip) {
        return;
    }
    mz_zip_reader_end(&zip->archive);
    for (size_t i = 0; i < zip->count; i++) {
        wo_free(zip->entries[i].name);
    }
    wo_free(zip->entries);
    wo_source_close(&zip->source);
    wo_free(zip);
}

bool wo_zip_open(const char *path, WoZip **out, WoError *error) {
    *out = NULL;
    WoZip *zip = wo_alloc(sizeof(*zip), error);
    if (!zip) {
        return false;
    }
    wo_copy_str(zip->path, sizeof(zip->path), wo_path_basename(path));
    if (!wo_source_open_file(&zip->source, path, error)) {
        wo_free(zip);
        return false;
    }
    zip->archive.m_pRead = zip_read;
    zip->archive.m_pIO_opaque = &zip->source;
    if (!mz_zip_reader_init(&zip->archive, zip->source.size, 0)) {
        zip_fail(zip, error, "not a readable ZIP archive");
        wo_source_close(&zip->source);
        wo_free(zip);
        return false;
    }
    mz_uint total = mz_zip_reader_get_num_files(&zip->archive);
    zip->entries = wo_alloc_array(total ? total : 1, sizeof(WoZipEntry), error);
    if (!zip->entries) {
        wo_zip_close(zip);
        return false;
    }
    for (mz_uint i = 0; i < total; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip->archive, i, &stat)) {
            zip_fail(zip, error, "damaged central directory");
            wo_zip_close(zip);
            return false;
        }
        bool safe = false;
        char *name = wo_zip_normalise_name(stat.m_filename, (stat.m_bit_flag & (1u << 11)) != 0,
                                           &safe, error);
        if (!name) {
            wo_zip_close(zip);
            return false;
        }
        unsigned host = stat.m_version_made_by >> 8;
        unsigned mode = (unsigned)(stat.m_external_attr >> 16);
        bool symlink = host == 3 && (mode & 0170000u) == 0120000u;
        WoZipEntry *entry = &zip->entries[zip->count++];
        entry->name = name;
        entry->size = stat.m_uncomp_size;
        entry->compressed = stat.m_comp_size;
        entry->is_dir = stat.m_is_directory != 0;
        entry->index = i;
        entry->safe = safe && !symlink && !stat.m_is_encrypted && stat.m_is_supported;
    }
    *out = zip;
    return true;
}

size_t wo_zip_count(const WoZip *zip) {
    return zip->count;
}

const WoZipEntry *wo_zip_entry(const WoZip *zip, size_t index) {
    return index < zip->count ? &zip->entries[index] : NULL;
}

size_t wo_zip_find(const WoZip *zip, const char *name) {
    for (size_t i = 0; i < zip->count; i++) {
        if (wo_str_ieq(zip->entries[i].name, name)) {
            return i;
        }
    }
    return SIZE_MAX;
}

typedef struct MemorySink {
    WoBuf *buf;
    uint64_t limit;
    bool overflow;
} MemorySink;

static size_t memory_write(void *opaque, mz_uint64 offset, const void *data, size_t n) {
    MemorySink *sink = opaque;
    if (offset != sink->buf->len || offset + n > sink->limit) {
        sink->overflow = true;
        return 0;
    }
    return wo_buf_append(sink->buf, data, n, NULL) ? n : 0;
}

bool wo_zip_read(WoZip *zip, size_t index, size_t max_bytes, WoBuf *out, WoError *error) {
    const WoZipEntry *entry = wo_zip_entry(zip, index);
    if (!entry || entry->is_dir) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "%s: no such file entry.", zip->path);
    }
    if (entry->size > max_bytes) {
        return wo_fail(error, WO_ERR_LIMIT, "%s: %s is larger than expected.", zip->path,
                       entry->name);
    }
    wo_buf_clear(out);
    MemorySink sink = {out, entry->size, false};
    if (!mz_zip_reader_extract_to_callback(&zip->archive, entry->index, memory_write, &sink, 0)) {
        if (sink.overflow) {
            return wo_fail(error, WO_ERR_FORMAT, "%s: %s is larger than its declared size.",
                           zip->path, entry->name);
        }
        return zip_fail(zip, error, "cannot decompress an entry");
    }
    if (out->len != entry->size) {
        return wo_fail(error, WO_ERR_FORMAT, "%s: %s is truncated.", zip->path, entry->name);
    }
    return wo_buf_terminate(out, error);
}

typedef struct FileSink {
    WoFile *file;
    uint64_t written;
    uint64_t limit;
    bool overflow;
    bool failed;
    WoError *error;
} FileSink;

static size_t file_write(void *opaque, mz_uint64 offset, const void *data, size_t n) {
    FileSink *sink = opaque;
    if (offset != sink->written || offset + n > sink->limit) {
        sink->overflow = true;
        return 0;
    }
    if (!wo_file_write(sink->file, data, n, sink->error)) {
        sink->failed = true;
        return 0;
    }
    sink->written += n;
    return n;
}

bool wo_zip_extract_entry(WoZip *zip, size_t index, const char *dest_path, WoError *error) {
    const WoZipEntry *entry = wo_zip_entry(zip, index);
    if (!entry || entry->is_dir) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "%s: no such file entry.", zip->path);
    }
    if (!entry->safe) {
        return wo_fail(error, WO_ERR_UNSAFE, "%s: refusing to extract \"%s\".", zip->path,
                       entry->name);
    }
    WoFile *file;
    if (!wo_file_create(dest_path, &file, error)) {
        return false;
    }
    FileSink sink = {file, 0, entry->size, false, false, error};
    bool ok = mz_zip_reader_extract_to_callback(&zip->archive, entry->index, file_write, &sink, 0);
    if (!ok && !sink.failed) {
        if (sink.overflow) {
            wo_fail(error, WO_ERR_FORMAT, "%s: %s is larger than its declared size.", zip->path,
                    entry->name);
        } else {
            zip_fail(zip, error, "cannot decompress an entry");
        }
    }
    if (ok && sink.written != entry->size) {
        ok = wo_fail(error, WO_ERR_FORMAT, "%s: %s is truncated.", zip->path, entry->name);
    }
    bool closed = wo_file_close(file, ok ? error : NULL);
    ok = ok && closed;
    if (!ok) {
        wo_delete_file(dest_path, NULL);
    }
    return ok;
}

bool wo_zip_extract_all(WoZip *zip, const char *dest_dir, const WoZipLimits *limits,
                        WoError *error) {
    if (limits->max_entries && zip->count > limits->max_entries) {
        return wo_fail(error, WO_ERR_LIMIT, "%s has too many entries (%zu).", zip->path,
                       zip->count);
    }
    uint64_t total = 0;
    for (size_t i = 0; i < zip->count; i++) {
        const WoZipEntry *entry = &zip->entries[i];
        if (!entry->safe && limits->skip_unsafe) {
            continue;
        }
        if (!entry->safe) {
            return wo_fail(error, WO_ERR_UNSAFE,
                           "%s contains an entry that cannot be extracted safely: \"%s\".",
                           zip->path, entry->name);
        }
        if (!wo_add_u64(total, entry->size, &total) ||
            (limits->max_total_bytes && total > limits->max_total_bytes)) {
            return wo_fail(error, WO_ERR_LIMIT, "%s exceeds the extraction size limit.",
                           zip->path);
        }
        if (limits->reject_duplicates) {
            for (size_t j = 0; j < i; j++) {
                if (wo_str_ieq(zip->entries[j].name, entry->name)) {
                    return wo_fail(error, WO_ERR_FORMAT, "%s contains \"%s\" twice.", zip->path,
                                   entry->name);
                }
            }
        }
    }
    for (size_t i = 0; i < zip->count; i++) {
        const WoZipEntry *entry = &zip->entries[i];
        if (!entry->safe) {
            continue; /* skip_unsafe: checked above */
        }
        char *target = wo_path_child(dest_dir, entry->name, error);
        if (!target) {
            return false;
        }
        bool ok = entry->is_dir ? wo_mkdirs(target, error)
                                : wo_zip_extract_entry(zip, i, target, error);
        wo_free(target);
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* --- Writer ---------------------------------------------------------------- */

/* ZIP entry times are a wall-clock date and time without a zone, which miniz
 * fills in with localtime(). Hand it the instant whose local time reads as
 * when's UTC time, so every machine writes the same bytes. */
static time_t local_for_utc(time_t when) {
    struct tm *utc = gmtime(&when);
    if (!utc) {
        return when;
    }
    struct tm fields = *utc;
    fields.tm_isdst = -1;
    time_t local = mktime(&fields);
    return local == (time_t)-1 ? when : local;
}

bool wo_zip_writer_begin(time_t when, WoZipWriter **out, WoError *error) {
    *out = NULL;
    WoZipWriter *writer = wo_alloc(sizeof(*writer), error);
    if (!writer) {
        return false;
    }
    writer->when = local_for_utc(when);
    if (!mz_zip_writer_init_heap(&writer->archive, 0, 1u << 20)) {
        wo_free(writer);
        return wo_fail(error, WO_ERR_INTERNAL, "Cannot start a ZIP archive.");
    }
    *out = writer;
    return true;
}

bool wo_zip_writer_add(WoZipWriter *writer, const char *name, const void *data, size_t len,
                       WoError *error) {
    bool safe = false;
    char *normal = wo_zip_normalise_name(name, true, &safe, error);
    if (!normal) {
        return false;
    }
    bool same = strcmp(normal, name) == 0;
    wo_free(normal);
    if (!safe || !same) {
        return wo_fail(error, WO_ERR_UNSAFE, "Invalid archive path \"%s\".", name);
    }
    if (!mz_zip_writer_add_mem_ex_v2(&writer->archive, name, data, len, NULL, 0,
                                     MZ_BEST_COMPRESSION, 0, 0, &writer->when, NULL, 0, NULL, 0)) {
        return wo_fail(error, WO_ERR_INTERNAL, "Cannot add \"%s\" to the archive (%s).", name,
                       mz_zip_get_error_string(mz_zip_get_last_error(&writer->archive)));
    }
    return true;
}

bool wo_zip_writer_add_file(WoZipWriter *writer, const char *name, const char *source_path,
                            WoError *error) {
    WoBuf data = {0};
    bool ok = wo_read_file(source_path, (size_t)1 << 30, &data, error) &&
              wo_zip_writer_add(writer, name, data.data, data.len, error);
    wo_buf_free(&data);
    return ok;
}

void wo_zip_writer_abort(WoZipWriter *writer) {
    if (writer) {
        mz_zip_writer_end(&writer->archive);
        wo_free(writer);
    }
}

bool wo_zip_writer_finish(WoZipWriter *writer, const char *path, WoError *error) {
    void *buffer = NULL;
    size_t size = 0;
    bool ok = mz_zip_writer_finalize_heap_archive(&writer->archive, &buffer, &size) != 0;
    if (!ok) {
        wo_fail(error, WO_ERR_INTERNAL, "Cannot finish the ZIP archive.");
    }
    ok = ok && wo_write_file_atomic(path, buffer, size, error);
    /* finalize_heap_archive transfers the buffer to us; release it with the
     * archive's allocator. */
    if (buffer) {
        writer->archive.m_pFree(writer->archive.m_pAlloc_opaque, buffer);
    }
    wo_zip_writer_abort(writer);
    return ok;
}
