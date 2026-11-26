/* The upscaled textures (assets/hd), made on the player's computer.
 *
 * Setup's "Upscale textures" makes, from the pictures as the game has them
 * (wobble/textures/originals, which the deband step keeps whatever its
 * choices: upscaling does not depend on debanding), a picture four times the
 * size of each with the neural upscaler (core/wo_upscale.h), under the same
 * name: the HD artwork step uses a PNG of assets/hd wherever authored
 * artwork has none of that name (or is switched off). Left out are the
 * pictures the authored folder has, which would never be shown, and
 * Valparaiso's waterfall, which the game paints from its palette.
 *
 * Each picture is upscaled, then debanded: at four times the size a
 * gradient's steps of a single level are four pixels wide. So the upscaled
 * picture goes through the four-sample filter and the dither, reaching as
 * far across the texture as at its own size, which is `scale` times as many
 * pixels. The band ramps are not made: they are for the PlayStation's flat
 * bands of one exact colour, and in an upscaled picture every small patch of
 * one colour would count as a band.
 *
 * The folder keeps a record (upscaled.json) of what it made from which
 * picture, so a picture is made again only when its source or the scale
 * changed, and what it made can be taken away again. PNGs it did not make
 * are left alone. */
#ifndef WO_UPSCALED_H
#define WO_UPSCALED_H

#include "core/wo_base.h"
#include "core/wo_hashcache.h"
#include "core/wo_json.h"
#include "core/wo_ops.h"

#define WO_UPSCALED_SCALE 4

typedef struct WoUpscaledResult {
    size_t made;     /* pictures upscaled now */
    size_t kept;     /* already made from the same source */
    size_t removed;  /* made before from a source now gone or left out */
    size_t left_out; /* authored, the waterfall, or unknown to the catalogue */
} WoUpscaledResult;

/* Bring `output` up to date with the pictures of `source` at `scale` (2 or
 * 4). `authored` and `cache` are optional. With `catalog` (the parsed
 * manifests/hd-textures.json), only pictures it names are made: the HD
 * artwork step refuses a PNG it does not know. `progress` (optional) is told
 * about the pictures being made, between percent `from` and `to`. */
bool wo_upscaled_make(const char *source, const char *output, unsigned scale, const char *authored,
                      const WoJson *catalog, WoHashCache *cache, WoProgress *progress, int from,
                      int to, WoUpscaledResult *result, WoError *error);

/* Take away what wo_upscaled_make made in `output`, and its record. */
bool wo_upscaled_remove(const char *output, size_t *removed, WoError *error);

#endif
