/* CD audio extent checks, byte-order detection and WAV production. The
 * first 8 bytes choose the offset, sector count and stride. */
#include "core/wo_cdaudio.h"
#include "fuzz.h"

static bool count_bytes(void *context, const void *data, size_t len, WoError *error) {
    (void)data;
    (void)error;
    *(uint64_t *)context += len;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) {
        return 0;
    }
    WoSource source;
    wo_source_from_memory(&source, data, size, "fuzz.bin");
    uint64_t offset = wo_le16(data);
    uint64_t sectors = wo_le16(data + 2);
    uint32_t stride = 2352u + (wo_le16(data + 4) % 200u);
    if (data[6] & 1u) {
        sectors |= (uint64_t)wo_le32(data + 4) << 16; /* sometimes absurdly large */
    }
    WoError error = {0};
    uint64_t produced = 0;
    if (wo_cdaudio_to_wav(&source, offset, sectors, stride, count_bytes, &produced, &error)) {
        FUZZ_ASSERT(produced == WO_WAV_HEADER_BYTES + sectors * WO_CD_AUDIO_SECTOR);
    }
    return 0;
}
