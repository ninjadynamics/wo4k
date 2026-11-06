/* ISO9660 reader: layout detection, directory walk and file reads. */
#include <string.h>

#include "core/wo_iso.h"
#include "fuzz.h"

/* No empty, "." or ".." component and no separator other than '/'. */
static bool safe_path(const char *path) {
    if (strchr(path, '\\') || strchr(path, ':')) {
        return false;
    }
    const char *part = path;
    for (;;) {
        const char *end = strchr(part, '/');
        size_t len = end ? (size_t)(end - part) : strlen(part);
        if (len == 0 || (len == 1 && part[0] == '.') ||
            (len == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        if (!end) {
            return true;
        }
        part = end + 1;
    }
}

static bool count_bytes(void *context, const void *data, size_t len, WoError *error) {
    (void)data;
    (void)error;
    *(size_t *)context += len;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoSource source;
    wo_source_from_memory(&source, data, size, "fuzz.iso");
    WoError error = {0};
    /* Try the image as a whole and as a data track after a small lead-in. */
    static const uint64_t origins[] = {0, 2352};
    for (size_t o = 0; o < sizeof(origins) / sizeof(origins[0]); o++) {
        WoIso iso;
        if (!wo_iso_open(&iso, &source, origins[o], &error)) {
            FUZZ_ASSERT(iso.entries == NULL && iso.count == 0);
            continue;
        }
        FUZZ_ASSERT(iso.count <= WO_ISO_MAX_ENTRIES);
        for (size_t i = 0; i < iso.count; i++) {
            const WoIsoEntry *entry = &iso.entries[i];
            FUZZ_ASSERT(safe_path(entry->path));
            FUZZ_ASSERT(wo_iso_find(&iso, entry->path) != NULL);
            if (entry->is_dir || entry->length > (1u << 20)) {
                continue;
            }
            uint8_t *bytes = NULL;
            if (wo_iso_read_entry(&iso, entry, 1u << 20, &bytes, &error)) {
                wo_free(bytes);
            }
            size_t streamed = 0;
            if (wo_iso_stream_entry(&iso, entry, count_bytes, &streamed, &error)) {
                FUZZ_ASSERT(streamed == entry->length);
            }
        }
        wo_iso_close(&iso);
    }
    return 0;
}
