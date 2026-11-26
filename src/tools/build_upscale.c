/* wo4k-build upscale <debanded folder> <output folder> <2|4> [<authored folder>]
 *
 * A whole set of upscaled textures for assets/hd, made from the debanded
 * pictures Setup and Play keep in assets/sd: what Setup's "Upscale
 * textures" makes on the player's computer (core/wo_upscaled.h), here at
 * either scale and into any folder. Pictures already made from the same
 * source at the same scale are kept. */
#include <stdio.h>

#include "core/wo_upscaled.h"
#include "tools/build_tool.h"

bool build_upscale(const char *source, const char *output, const char *scale_text,
                   const char *authored, WoError *error) {
    unsigned scale = wo_str_eq(scale_text, "2") ? 2 : wo_str_eq(scale_text, "4") ? 4 : 0;
    if (!scale) {
        return wo_fail(error, WO_ERR_FORMAT, "The scale is 2 or 4.");
    }
    WoUpscaledResult result;
    if (!wo_upscaled_make(source, output, scale, authored, NULL, NULL, NULL, 0, 0, &result,
                          error)) {
        return false;
    }
    printf("%zu pictures at %ux in %s (%zu made, %zu already made); %zu left out, %zu removed\n",
           result.made + result.kept, scale, output, result.made, result.kept, result.left_out,
           result.removed);
    return true;
}
