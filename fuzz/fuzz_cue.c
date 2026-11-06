/* CUE sheet parsing and multi-file track layout, then tracks.ini output. */
#include <string.h>

#include "core/wo_disctracks.h"
#include "fuzz.h"

/* Every FILE name exists; its size depends on the name so layouts vary. */
static bool resolve(void *context, const char *name, char **path, uint64_t *size,
                    WoError *error) {
    (void)context;
    size_t len = strlen(name);
    if (len > 200) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "Missing file.");
    }
    *size = (uint64_t)((unsigned char)name[0]) * 2352u * 7u + len * 1000u;
    *path = wo_strdup(name, error);
    return *path != NULL;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoTrackList tracks;
    if (wo_cue_parse((const char *)data, size, resolve, NULL, &tracks, &error)) {
        FUZZ_ASSERT(tracks.count <= WO_TRACKS_MAX);
        for (size_t i = 0; i < tracks.count; i++) {
            FUZZ_ASSERT(tracks.items[i].frames > 0 && tracks.items[i].image);
            FUZZ_ASSERT(tracks.items[i].stride == 2048 || tracks.items[i].stride == 2352 ||
                        tracks.items[i].stride == 2448);
        }
        WoBuf ini = {0};
        FUZZ_ASSERT(wo_tracks_write_ini(&tracks, &ini, &error));
        wo_buf_free(&ini);
        wo_tracks_free(&tracks);
    } else {
        FUZZ_ASSERT(tracks.items == NULL && tracks.count == 0);
    }
    return 0;
}
