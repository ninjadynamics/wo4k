/* TIM decoding, both the installer's and the PNG exporter's decoder. */
#include "core/wo_texfmt.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoNativeImage image;
    if (wo_tim_decode(data, size, &image, &error)) {
        FUZZ_ASSERT(image.width >= 1 && image.width <= 4096 && image.height >= 1 &&
                    image.height <= 4096 && image.pixels);
        wo_native_image_free(&image);
    }
    WoImage bgra;
    if (wo_tim_decode_bgra(data, size, &bgra, &error)) {
        FUZZ_ASSERT((uint64_t)bgra.width * bgra.height <= WO_TEX_MAX_PIXELS);
        (void)wo_image_has_transparency(&bgra);
        wo_image_free(&bgra);
    }
    return 0;
}
