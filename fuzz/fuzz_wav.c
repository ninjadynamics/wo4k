/* Sound-effect cleaning: RIFF parsing and the bounded DSP. */
#include <string.h>

#include "core/wo_audioclean.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > (256u << 10)) {
        return 0; /* the DSP is linear in size; keep runs fast */
    }
    WoError error = {0};
    uint8_t *out = NULL;
    if (wo_audio_clean(data, size, "FUZZ.WAV", &out, &error)) {
        /* Only PCM samples may change; the first 12 bytes are the RIFF header. */
        FUZZ_ASSERT(memcmp(out, data, 12) == 0);
        wo_free(out);
    } else {
        FUZZ_ASSERT(out == NULL);
    }
    return 0;
}
