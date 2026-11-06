/* Road composition and frame decoding from a fuzzed WAD archive. */
#include "core/wo_texsrc.h"
#include "fuzz.h"

typedef struct Input {
    const uint8_t *data;
    size_t size;
} Input;

static bool load(void *context, WoTexOrigin origin, const char *relative, size_t max_bytes,
                 WoBuf *out, WoError *error) {
    (void)origin;
    (void)relative;
    const Input *input = context;
    if (input->size > max_bytes) {
        return wo_fail(error, WO_ERR_LIMIT, "Too large.");
    }
    return wo_buf_append(out, input->data, input->size, error);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    Input input = {data, size};
    WoError error = {0};
    WoTexSources *sources = wo_texsrc_new(load, &input, &error);
    if (!sources) {
        return 0;
    }
    static const int64_t levels[] = {128, 64, 32};
    for (int64_t material = 0; material < 3; material++) {
        for (size_t l = 0; l < 3; l++) {
            WoNativeImage road;
            if (wo_texsrc_road(sources, "T.WAD", "library.cmp", "library.ttf", material,
                               levels[l], &road, &error)) {
                FUZZ_ASSERT(road.width == (uint32_t)levels[l] && road.height == road.width);
                wo_native_image_free(&road);
            }
        }
    }
    static const char *const entries[] = {"sky.cmp", "a.tim"};
    for (size_t e = 0; e < 2; e++) {
        for (int64_t frame = -1; frame < 3; frame++) {
            WoNativeImage image;
            if (wo_texsrc_frame(sources, NULL, "T.WAD", entries[e], false, frame, &image,
                                &error)) {
                wo_native_image_free(&image);
            }
        }
    }
    WoNativeImage file;
    if (wo_texsrc_frame(sources, "X.CMP", NULL, NULL, true, 0, &file, &error)) {
        wo_native_image_free(&file);
    }
    wo_texsrc_free(sources);
    return 0;
}
