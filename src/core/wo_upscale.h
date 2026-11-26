/* Neural upscaling of the game's textures, 2x or 4x.
 *
 * The network is Rich Geldreich's neural_upsampler (public domain; its
 * weights are in third_party/neural_upsampler): for every pixel it reads the
 * 7x7 pixels around it and gives the 2x2 pixels that replace it, as
 * corrections to a bilinear interpolation. It was trained on photographs,
 * each to be recovered from its own half-size copy, and that copy was made
 * in one particular way: a Mitchell filter, in linear light. The network
 * undoes exactly that softening. A PlayStation texture has none of it: its
 * texels are as sharp as texels can be, and given them as they are the
 * network sharpens what is already sharp, into beaded edges and a woven
 * pattern over anything noisy.
 *
 * So a texture is first made into what the network expects. Taking every
 * texel for a 2x2 block of the picture twice its size, the trainer's
 * half-size copy of that picture is the texture itself filtered by five
 * weights each way, in linear light (wo_upscale_prefilter). The network
 * then gives its picture of twice the size: edges straight where the texels
 * stepped, nothing invented. For 4x the two steps are done again.
 *
 * A texture's see-through texels are black underneath. Before anything
 * else they take the colours of the nearest texels that show, so that no
 * black is drawn into an outline; the cut-out itself is upscaled as a
 * picture of its own, by the same two steps, and its faint skirt cut off.
 *
 * All pixels here are four bytes, red first, rows top-down. */
#ifndef WO_UPSCALE_H
#define WO_UPSCALE_H

#include "core/wo_base.h"
#include "core/wo_texfmt.h"

/* One pass of the network as its author's library runs it
 * (jpegc_up2x_rgba, the BEST model at the strength it was trained for): out
 * holds 2 * height rows of 2 * width pixels and must not overlap rgba.
 * Alpha is interpolated bilinearly. The rows are shared among the
 * processor's threads. */
void wo_upscale_double(const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t *out);

/* Filter a picture's colours, in place, into what the network expects of
 * one (see above). Alpha is left as it is. */
bool wo_upscale_prefilter(uint8_t *rgba, uint32_t width, uint32_t height, WoError *error);

/* A texture `scale` times as large; scale is 2 or 4. out->pixels is
 * allocated here (wo_image_free). A texture with no see-through texel
 * comes out with none. */
bool wo_upscale_texture(const uint8_t *rgba, uint32_t width, uint32_t height, unsigned scale,
                        WoImage *out, WoError *error);

#endif
