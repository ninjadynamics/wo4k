/* CMP (LZ-compressed TIM sequence) decoding. */
#include "core/wo_texfmt.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoCmp cmp;
    if (!wo_cmp_decode(data, size, &cmp, &error)) {
        FUZZ_ASSERT(cmp.data == NULL && cmp.count == 0);
        return 0;
    }
    FUZZ_ASSERT(cmp.count >= 1 && cmp.count <= WO_CMP_MAX_ENTRIES);
    size_t sum = 0;
    for (size_t i = 0; i < cmp.count; i++) {
        FUZZ_ASSERT(cmp.offsets[i] == sum && cmp.sizes[i] <= WO_CMP_MAX_ENTRY_BYTES);
        sum += cmp.sizes[i];
        WoNativeImage image;
        if (cmp.sizes[i] && wo_tim_decode(cmp.data + cmp.offsets[i], cmp.sizes[i], &image,
                                          &error)) {
            wo_native_image_free(&image);
        }
    }
    FUZZ_ASSERT(sum == cmp.total && cmp.total <= WO_CMP_MAX_TOTAL_BYTES);
    wo_cmp_free(&cmp);
    return 0;
}
