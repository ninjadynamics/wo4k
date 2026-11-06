/* WAD directory parsing and lookups. */
#include <string.h>

#include "core/wo_texfmt.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoWad wad;
    if (!wo_wad_parse(data, size, &wad, &error)) {
        FUZZ_ASSERT(wad.entries == NULL && wad.count == 0);
        return 0;
    }
    FUZZ_ASSERT(wad.count >= 1 && wad.count <= WO_WAD_MAX_ENTRIES);
    size_t end = 0;
    for (size_t i = 0; i < wad.count; i++) {
        const WoWadEntry *entry = &wad.entries[i];
        FUZZ_ASSERT(entry->offset >= end && entry->size > 0);
        FUZZ_ASSERT(entry->offset <= size && entry->size <= size - entry->offset);
        FUZZ_ASSERT(strlen(entry->name) < WO_WAD_NAME_MAX);
        FUZZ_ASSERT(wo_wad_find(&wad, entry->name) == entry);
        end = entry->offset + entry->size;
    }
    FUZZ_ASSERT(end == size);
    wo_wad_free(&wad);
    return 0;
}
