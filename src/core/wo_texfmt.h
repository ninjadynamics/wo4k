/* Native WipEout texture formats (portable): PlayStation TIM images, the
 * PC's RGB555 TGA sheets, CMP (LZ-compressed TIM sequences) and WAD
 * archives. Every offset and length read from a file is checked before use.
 *
 * Two pixel representations are used:
 *   - WoNativeImage keeps 16-bit PlayStation colours (bits 0-4 red, 5-9
 *     green, 10-14 blue, bit 15 the semi-transparency flag). This is what
 *     the installer patches into the PC's RGB555 sheets.
 *   - WoImage holds 8-bit-per-channel pixels in the order given by
 *     WoPixelOrder, used for PNG import and export. */
#ifndef WO_TEXFMT_H
#define WO_TEXFMT_H

#include "core/wo_base.h"

/* Largest image any decoder accepts (4096 x 4096). */
#define WO_TEX_MAX_PIXELS 16777216u
/* CMP limits, as in the original loader. */
#define WO_CMP_MAX_ENTRIES 4096u
#define WO_CMP_MAX_ENTRY_BYTES 16777216u
#define WO_CMP_MAX_TOTAL_BYTES 67108864u
#define WO_WAD_MAX_ENTRIES 256u
#define WO_WAD_NAME_MAX 64u

typedef struct WoNativeImage {
    uint32_t width;
    uint32_t height;
    uint16_t *pixels; /* width * height PlayStation colours, row-major */
} WoNativeImage;

typedef enum WoPixelOrder {
    WO_PIXELS_BGRA,
    WO_PIXELS_RGBA
} WoPixelOrder;

typedef struct WoImage {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels; /* width * height * 4 bytes, rows top-down, no padding */
} WoImage;

void wo_native_image_free(WoNativeImage *image);
void wo_image_free(WoImage *image);
/* True when any pixel's alpha is not 255 (WoImage alpha is byte 3 in both orders). */
bool wo_image_has_transparency(const WoImage *image);

/* --- TIM -------------------------------------------------------------------- */
/* Decode a 4-, 8- or 16-bit TIM into PlayStation colours (the installer's
 * decoder). 24-bit and mixed images are rejected as unsupported. */
bool wo_tim_decode(const uint8_t *data, size_t len, WoNativeImage *out, WoError *error);
/* Where a TIM is loaded in PlayStation video memory: x in 16-bit words from
 * the left edge, y in lines from the top, and how many of the image's
 * texels one word holds (4, 2 or 1). */
bool wo_tim_origin(const uint8_t *data, size_t len, uint32_t *x_words, uint32_t *y,
                   uint32_t *texels_per_word, WoError *error);
/* Give a TIM another place in video memory. */
bool wo_tim_move(uint8_t *data, size_t len, uint32_t x_words, uint32_t y, WoError *error);
/* Decode any TIM (4/8/16/24-bit) to BGRA for PNG export. Colour 0 is fully
 * transparent, as in the maintainer exporter. */
bool wo_tim_decode_bgra(const uint8_t *data, size_t len, WoImage *out, WoError *error);

/* --- TGA (the PC's uncompressed RGB555 sheets) -------------------------------- */
typedef struct WoTgaInfo {
    uint32_t width;
    uint32_t height;
    size_t pixel_offset; /* 18 + image ID length */
} WoTgaInfo;

/* Validate the header of an uncompressed 16-bit TGA and require the whole
 * pixel area to be present. */
bool wo_tga_info(const uint8_t *data, size_t len, WoTgaInfo *out, WoError *error);
/* Decode to BGRA, keeping the file's raw row order (the PC loader ignores
 * the TGA origin bits). Black (ignoring bit 15) is transparent. */
bool wo_tga_decode_bgra(const uint8_t *data, size_t len, WoImage *out, WoError *error);
/* Write image into the sheet at (x, y), converting PlayStation colours to
 * the PC's RGB555 layout. width/height must equal the image's size.
 *
 * A PlayStation texel of black with the semi-transparency flag (0x8000) is
 * solid black on a solid polygon and adds nothing on a blended one; only
 * 0x0000 is see-through everywhere. The PC game keys every black texel.
 * With opaque_black such texels are stored as the darkest grey, which the
 * PC game draws: right for scenery, which is drawn solid, and wrong for
 * explosions, flares and other blended textures, where the grey would show
 * as a faint square. */
bool wo_tga_patch(uint8_t *tga, size_t len, const WoNativeImage *image, int64_t x, int64_t y,
                  int64_t width, int64_t height, bool opaque_black, WoError *error);

/* --- CMP ---------------------------------------------------------------------- */
typedef struct WoCmp {
    uint8_t *data;   /* all decoded entries back to back */
    size_t total;
    size_t count;
    size_t *offsets; /* entry i is data[offsets[i] .. offsets[i] + sizes[i]) */
    size_t *sizes;
} WoCmp;

/* Decode a CMP archive. Empty entries are allowed (the loader skips them). */
bool wo_cmp_decode(const uint8_t *data, size_t len, WoCmp *out, WoError *error);
/* Write entries as a CMP archive the game and wo_cmp_decode read back. The
 * entries are stored without compression (nine bits to the byte). */
bool wo_cmp_encode(const uint8_t *const *entries, const size_t *sizes, size_t count, WoBuf *out,
                   WoError *error);
void wo_cmp_free(WoCmp *cmp);

/* --- WAD ----------------------------------------------------------------------- */
typedef struct WoWadEntry {
    char name[WO_WAD_NAME_MAX]; /* ASCII; bytes >= 0x80 read as '?' */
    size_t offset;              /* into the WAD's bytes */
    size_t size;
    size_t record;              /* where the directory holds the size, twice */
} WoWadEntry;

typedef struct WoWad {
    WoWadEntry *entries;
    size_t count;
} WoWad;

/* Parse a WAD directory. Entries point into data, which must outlive any
 * use of the offsets. Names must be unique ignoring ASCII case. */
bool wo_wad_parse(const uint8_t *data, size_t len, WoWad *out, WoError *error);
/* Case-insensitive lookup; NULL when absent. */
const WoWadEntry *wo_wad_find(const WoWad *wad, const char *name);
void wo_wad_free(WoWad *wad);

#endif
