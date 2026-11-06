/* PNG (and other image) files through the Windows Imaging Component,
 * implemented in wo_png_win.c. Decoding accepts any format WIC can read,
 * as the game's runtime loader does; encoding always writes PNG. */
#ifndef WO_PNG_H
#define WO_PNG_H

#include "core/wo_base.h"
#include "core/wo_texfmt.h"

/* Limits shared with the runtime loader. */
#define WO_PNG_MAX_SIDE 16384u
#define WO_PNG_MAX_PIXEL_BYTES (256u * 1024u * 1024u)
/* Largest encoded file read. */
#define WO_PNG_MAX_FILE_BYTES (256u * 1024u * 1024u)

/* Decode the first frame to 32-bit pixels with straight alpha. */
bool wo_png_read(const char *path, WoPixelOrder order, WoImage *out, WoError *error);
/* Read only the dimensions (the image is not decoded). */
bool wo_png_read_size(const char *path, uint32_t *width, uint32_t *height, WoError *error);
/* Encode image as a 32-bit PNG and replace path atomically. */
bool wo_png_write(const char *path, const WoImage *image, WoPixelOrder order, WoError *error);
/* Decode path and report whether any pixel is not fully opaque. */
bool wo_png_has_transparency(const char *path, bool *transparent, WoError *error);

#endif
