/* RGB555 TGA header checks, decoding and region patching. */
#include <string.h>

#include "core/wo_texfmt.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoTgaInfo info;
    bool valid = wo_tga_info(data, size, &info, &error);
    WoImage bgra;
    if (wo_tga_decode_bgra(data, size, &bgra, &error)) {
        FUZZ_ASSERT(valid && bgra.width == info.width && bgra.height == info.height);
        wo_image_free(&bgra);
    }
    if (size < 4) {
        return 0;
    }
    /* Patch a small image at a position taken from the input's last bytes. */
    uint8_t *copy = wo_alloc(size, &error);
    if (!copy) {
        return 0;
    }
    memcpy(copy, data, size);
    uint16_t pixels[16];
    for (size_t i = 0; i < 16; i++) {
        pixels[i] = (uint16_t)(i * 0x1111u);
    }
    WoNativeImage image = {4, 4, pixels};
    int64_t x = (int8_t)data[size - 1];
    int64_t y = (int8_t)data[size - 2];
    int64_t w = data[size - 3] & 7u;
    int64_t h = data[size - 4] & 7u;
    bool opaque_black = (data[size - 3] & 8u) != 0;
    bool patched = wo_tga_patch(copy, size, &image, x, y, w, h, opaque_black, &error);
    FUZZ_ASSERT(!patched || (valid && w == 4 && h == 4 && x >= 0 && y >= 0));
    wo_free(copy);
    return 0;
}
