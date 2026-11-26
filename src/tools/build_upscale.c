/* wo4k-build upscale <debanded folder> <output folder> <2|4> [<authored folder>]
 *
 * A whole set of upscaled textures for assets/hd, made from the debanded
 * pictures Setup and Play keep in assets/sd (core/wo_upscale.h). The names
 * are kept: the game uses a PNG of assets/hd wherever assets/auth has none
 * of that name. Left out are the pictures the authored folder has, which
 * would never be shown, and Valparaiso's waterfall, which the game paints
 * from its palette.
 *
 * Each picture is debanded, upscaled and debanded again. The first is the
 * installer's work, by the choices made for each texture: assets/sd has a
 * picture only where a strength was chosen, and it is that picture which is
 * upscaled. But what debanding leaves at one size shows again at four times
 * it: a gradient's steps of a single level are four pixels wide, and the
 * grain that hid them is four-pixel blots. So the upscaled picture goes
 * through the four-sample filter and the dither once more, reaching as far
 * across the texture as before, which is `scale` times as many pixels. The
 * band ramps are not made again: they are for the PlayStation's flat bands
 * of one exact colour, and in an upscaled picture every small patch of one
 * colour would count as a band. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../tools/deband/filter.h"
#include "core/wo_fs.h"
#include "core/wo_png.h"
#include "core/wo_upscale.h"
#include "tools/build_tool.h"

#define WATERFALL "track13-scene-cmp-16.png"
#define FILTER_REACH_LIMIT 128 /* the filter's own */

static bool deband_again(const float *noise, WoImage *large, unsigned scale, const char *name,
                         WoError *error) {
    DebandOptions options;
    deband_defaults(&options);
    options.band = 0;
    options.radius *= (int)scale;
    if (options.radius > FILTER_REACH_LIMIT) {
        options.radius = FILTER_REACH_LIMIT;
    }
    uint8_t *smooth = deband_smooth(large->pixels, (int)large->width, (int)large->height, &options,
                                    noise, deband_seed(name));
    if (!smooth) {
        return wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    memcpy(large->pixels, smooth, (size_t)large->width * large->height * 4);
    free(smooth);
    return true;
}

bool build_upscale(const char *source, const char *output, const char *scale_text,
                   const char *authored, WoError *error) {
    unsigned scale = wo_str_eq(scale_text, "2") ? 2 : wo_str_eq(scale_text, "4") ? 4 : 0;
    if (!scale) {
        return wo_fail(error, WO_ERR_FORMAT, "The scale is 2 or 4.");
    }
    WoDirList list = {0};
    float *noise = wo_alloc_array(DEBAND_NOISE_COUNT, sizeof(float), error);
    if (!noise || !wo_list_dir(source, &list, error) || !wo_mkdirs(output, error)) {
        wo_dirlist_free(&list);
        wo_free(noise);
        return false;
    }
    bool ok = deband_noise(noise) || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    size_t made = 0;
    size_t left_out = 0;
    for (size_t i = 0; ok && i < list.count; i++) {
        const char *name = list.items[i].name;
        if (list.items[i].is_dir || !wo_str_iends_with(name, ".png")) {
            continue;
        }
        char *theirs = authored ? wo_path_join(authored, name, error) : NULL;
        bool skip = wo_str_ieq(name, WATERFALL) || (theirs && wo_is_file(theirs));
        ok = !authored || theirs;
        wo_free(theirs);
        if (!ok || skip) {
            left_out += skip;
            continue;
        }
        char *from = wo_path_join(source, name, error);
        char *to = from ? wo_path_join(output, name, error) : NULL;
        WoImage texture = {0};
        WoImage large = {0};
        ok = to && wo_png_read(from, WO_PIXELS_RGBA, &texture, error) &&
             wo_upscale_texture(texture.pixels, texture.width, texture.height, scale, &large,
                                error) &&
             deband_again(noise, &large, scale, name, error) &&
             wo_png_write(to, &large, WO_PIXELS_RGBA, error);
        if (!ok) {
            wo_error_context(error, "%s", name);
        }
        wo_image_free(&large);
        wo_image_free(&texture);
        wo_free(to);
        wo_free(from);
        made += ok;
        if (ok && made % 50 == 0) {
            printf("  %zu done\n", made);
            fflush(stdout);
        }
    }
    wo_dirlist_free(&list);
    wo_free(noise);
    if (ok) {
        printf("%zu pictures at %ux in %s; %zu left out\n", made, scale, output, left_out);
    }
    return ok;
}
