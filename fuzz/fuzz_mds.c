/* Alcohol 120% MDS descriptor parsing. */
#include "core/wo_disctracks.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoTrackList tracks;
    if (wo_mds_parse(data, size, "fuzz.mdf", &tracks, &error)) {
        FUZZ_ASSERT(tracks.count <= WO_TRACKS_MAX);
        for (size_t i = 0; i < tracks.count; i++) {
            FUZZ_ASSERT(tracks.items[i].number >= 1 && tracks.items[i].number <= 99);
        }
        FUZZ_ASSERT(wo_tracks_audio_count(&tracks) <= tracks.count);
        wo_tracks_free(&tracks);
    } else {
        FUZZ_ASSERT(tracks.items == NULL && tracks.count == 0);
    }
    return 0;
}
