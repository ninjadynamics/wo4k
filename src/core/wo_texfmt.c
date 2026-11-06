#include "core/wo_texfmt.h"

#include <string.h>

#define TIM_MAGIC 16u
#define TIM_HAS_CLUT 8u
#define TIM_MAX_SIDE 4096u
#define CMP_WINDOW 8192u

void wo_native_image_free(WoNativeImage *image) {
    if (image) {
        wo_free(image->pixels);
        memset(image, 0, sizeof(*image));
    }
}

void wo_image_free(WoImage *image) {
    if (image) {
        wo_free(image->pixels);
        memset(image, 0, sizeof(*image));
    }
}

bool wo_image_has_transparency(const WoImage *image) {
    size_t count = (size_t)image->width * image->height;
    for (size_t i = 0; i < count; i++) {
        if (image->pixels[i * 4 + 3] != 255) {
            return true;
        }
    }
    return false;
}

/* --- TIM -------------------------------------------------------------------- */

bool wo_tim_decode(const uint8_t *data, size_t len, WoNativeImage *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    uint32_t magic = 0;
    uint32_t flags = 0;
    if (!wo_read_le32(data, len, 0, &magic) || !wo_read_le32(data, len, 4, &flags)) {
        return wo_fail(error, WO_ERR_FORMAT, "The TIM image is truncated.");
    }
    if (magic != TIM_MAGIC) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid TIM signature.");
    }
    uint32_t mode = flags & 7u;
    if (mode > 2) {
        return wo_fail(error, WO_ERR_UNSUPPORTED,
                       "Unsupported TIM pixel format %u; only 4-, 8- and 16-bit images are used.",
                       (unsigned)mode);
    }
    size_t at = 8;
    uint16_t palette[256];
    bool has_palette = false;
    if (flags & TIM_HAS_CLUT) {
        uint32_t size = 0;
        if (!wo_read_le32(data, len, at, &size)) {
            return wo_fail(error, WO_ERR_FORMAT, "The TIM palette is truncated.");
        }
        if (size < 12 || size > len - at || (size & 1u)) {
            return wo_fail(error, WO_ERR_FORMAT, "Invalid TIM palette extent.");
        }
        size_t colours = mode == 0 ? 16 : 256;
        if (size < 12 + colours * 2) {
            return wo_fail(error, WO_ERR_FORMAT, "The TIM palette is truncated.");
        }
        for (size_t i = 0; i < colours; i++) {
            palette[i] = wo_le16(data + at + 12 + i * 2);
        }
        has_palette = true;
        at += size;
    }
    uint32_t block = 0;
    uint16_t words = 0;
    uint16_t height = 0;
    if (!wo_read_le32(data, len, at, &block) || !wo_read_le16(data, len, at + 8, &words) ||
        !wo_read_le16(data, len, at + 10, &height)) {
        return wo_fail(error, WO_ERR_FORMAT, "The TIM image is truncated.");
    }
    uint32_t per_word = mode == 0 ? 4u : (mode == 1 ? 2u : 1u);
    uint32_t width = (uint32_t)words * per_word;
    uint64_t pixel_bytes = (uint64_t)words * height * 2u;
    if (width < 1 || height < 1 || width > TIM_MAX_SIDE || height > TIM_MAX_SIDE || block < 12 ||
        block > len - at || pixel_bytes > (uint64_t)block - 12u) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid TIM image extent.");
    }
    if (mode < 2 && !has_palette) {
        return wo_fail(error, WO_ERR_FORMAT, "The TIM image needs a palette but has none.");
    }
    uint16_t *pixels = wo_alloc_array((size_t)width * height, sizeof(uint16_t), error);
    if (!pixels) {
        return false;
    }
    size_t start = at + 12;
    size_t stride = (size_t)words * 2u;
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = data + start + (size_t)y * stride;
        uint16_t *dst = pixels + (size_t)y * width;
        for (uint32_t x = 0; x < width; x++) {
            uint16_t colour;
            if (mode == 2) {
                colour = wo_le16(row + (size_t)x * 2);
            } else if (mode == 0) {
                unsigned index = (row[x / 2] >> ((x & 1u) * 4u)) & 15u;
                colour = palette[index];
            } else {
                colour = palette[row[x]];
            }
            dst[x] = colour;
        }
    }
    out->width = width;
    out->height = height;
    out->pixels = pixels;
    return true;
}

static uint8_t expand5(unsigned value) {
    return (uint8_t)((value & 31u) * 255u / 31u);
}

/* PlayStation colour to BGRA; colour 0 is transparent. */
static void put_ps_colour(uint8_t *p, uint16_t c) {
    p[0] = expand5(c >> 10);
    p[1] = expand5(c >> 5);
    p[2] = expand5(c);
    p[3] = c != 0 ? 255 : 0;
}

bool wo_tim_decode_bgra(const uint8_t *data, size_t len, WoImage *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    if (len < 20 || wo_le32(data) != TIM_MAGIC) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid TIM image.");
    }
    uint32_t mode = wo_le32(data + 4);
    size_t at = 8;
    uint16_t *palette = NULL;
    size_t colours = 0;
    if (mode & TIM_HAS_CLUT) {
        uint32_t size = wo_le32(data + at);
        if (size < 12 || size > INT32_MAX || size > len - at) {
            return wo_fail(error, WO_ERR_FORMAT, "Invalid TIM palette.");
        }
        colours = (size - 12) / 2;
        palette = wo_alloc_array(colours ? colours : 1, sizeof(uint16_t), error);
        if (!palette) {
            return false;
        }
        for (size_t i = 0; i < colours; i++) {
            palette[i] = wo_le16(data + at + 12 + i * 2);
        }
        at += size;
    }
    uint32_t block = 0;
    uint16_t words = 0;
    uint16_t height = 0;
    bool ok = wo_read_le32(data, len, at, &block) && wo_read_le16(data, len, at + 8, &words) &&
              wo_read_le16(data, len, at + 10, &height);
    mode &= 7u;
    uint64_t width = mode == 0   ? (uint64_t)words * 4u
                     : mode == 1 ? (uint64_t)words * 2u
                     : mode == 2 ? (uint64_t)words
                                 : (uint64_t)words * 2u / 3u;
    uint64_t pixel_bytes = (uint64_t)words * height * 2u;
    if (!ok || block < 12 || block > INT32_MAX || block > len - at || width < 1 || height < 1 ||
        width * height > WO_TEX_MAX_PIXELS || pixel_bytes > (uint64_t)block - 12u) {
        wo_free(palette);
        return wo_fail(error, WO_ERR_FORMAT, "Invalid TIM pixel data.");
    }
    if (mode != 2 && mode != 3 && !palette) {
        return wo_fail(error, WO_ERR_FORMAT, "The TIM image needs a palette but has none.");
    }
    uint8_t *pixels = wo_alloc_array((size_t)(width * height), 4, error);
    if (!pixels) {
        wo_free(palette);
        return false;
    }
    size_t start = at + 12;
    size_t stride = (size_t)words * 2u;
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = data + start + (size_t)y * stride;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *p = pixels + ((size_t)y * width + x) * 4;
            if (mode == 3) {
                p[0] = row[(size_t)x * 3 + 2];
                p[1] = row[(size_t)x * 3 + 1];
                p[2] = row[(size_t)x * 3];
                p[3] = 255;
                continue;
            }
            uint16_t colour;
            if (mode == 2) {
                colour = wo_le16(row + (size_t)x * 2);
            } else {
                size_t index = mode == 0 ? ((row[x / 2] >> ((x & 1u) * 4u)) & 15u) : row[x];
                if (index >= colours) {
                    wo_free(pixels);
                    wo_free(palette);
                    return wo_fail(error, WO_ERR_FORMAT,
                                   "A TIM pixel uses colour %zu of a %zu-colour palette.", index,
                                   colours);
                }
                colour = palette[index];
            }
            put_ps_colour(p, colour);
        }
    }
    wo_free(palette);
    out->width = (uint32_t)width;
    out->height = height;
    out->pixels = pixels;
    return true;
}

/* --- TGA -------------------------------------------------------------------- */

bool wo_tga_info(const uint8_t *data, size_t len, WoTgaInfo *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    if (len < 18 || data[2] != 2 || data[16] != 16) {
        return wo_fail(error, WO_ERR_FORMAT, "Expected a native RGB555 TGA image.");
    }
    out->width = wo_le16(data + 12);
    out->height = wo_le16(data + 14);
    out->pixel_offset = 18u + data[0];
    uint64_t bytes = (uint64_t)out->width * out->height * 2u;
    if (out->width < 1 || out->height < 1 || out->pixel_offset > len ||
        bytes > len - out->pixel_offset) {
        return wo_fail(error, WO_ERR_FORMAT, "The TGA image's pixel data is incomplete.");
    }
    return true;
}

bool wo_tga_decode_bgra(const uint8_t *data, size_t len, WoImage *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    WoTgaInfo info;
    if (!wo_tga_info(data, len, &info, error)) {
        return false;
    }
    size_t count = (size_t)info.width * info.height;
    if (count > WO_TEX_MAX_PIXELS) {
        return wo_fail(error, WO_ERR_LIMIT, "The TGA image is too large (%ux%u).", info.width,
                       info.height);
    }
    uint8_t *pixels = wo_alloc_array(count, 4, error);
    if (!pixels) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        uint16_t c = wo_le16(data + info.pixel_offset + i * 2);
        uint8_t *p = pixels + i * 4;
        p[0] = expand5(c);
        p[1] = expand5(c >> 5);
        p[2] = expand5(c >> 10);
        p[3] = (c & 0x7FFFu) != 0 ? 255 : 0;
    }
    out->width = info.width;
    out->height = info.height;
    out->pixels = pixels;
    return true;
}

bool wo_tga_patch(uint8_t *tga, size_t len, const WoNativeImage *image, int64_t x, int64_t y,
                  int64_t width, int64_t height, WoError *error) {
    if (len < 18 || tga[2] != 2 || tga[16] != 16) {
        return wo_fail(error, WO_ERR_FORMAT, "Expected a native RGB555 TGA image.");
    }
    int64_t sheet_w = wo_le16(tga + 12);
    int64_t sheet_h = wo_le16(tga + 14);
    size_t offset = 18u + tga[0];
    bool fits = width == (int64_t)image->width && height == (int64_t)image->height && x >= 0 &&
                y >= 0 && width <= sheet_w - x && height <= sheet_h - y && offset <= len &&
                (uint64_t)(sheet_w * sheet_h * 2) <= len - offset;
    if (!fits) {
        return wo_fail(error, WO_ERR_FORMAT, "Native texture placement is out of bounds.");
    }
    for (int64_t row = 0; row < height; row++) {
        for (int64_t col = 0; col < width; col++) {
            uint16_t ps = image->pixels[row * width + col];
            uint16_t c = (uint16_t)(((ps & 31u) << 10) | (ps & 0x83E0u) | ((ps >> 10) & 31u));
            size_t at = offset + (size_t)(((y + row) * sheet_w + x + col) * 2);
            wo_put_le16(tga + at, c);
        }
    }
    return true;
}

/* --- CMP -------------------------------------------------------------------- */

typedef struct BitReader {
    const uint8_t *data;
    size_t len;
    uint64_t bit;
} BitReader;

static bool read_bits(BitReader *reader, unsigned count, unsigned *out, WoError *error) {
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++) {
        uint64_t byte = reader->bit / 8u;
        if (byte >= reader->len) {
            return wo_fail(error, WO_ERR_FORMAT, "The CMP stream is truncated.");
        }
        unsigned shift = 7u - (unsigned)(reader->bit & 7u);
        value = (value << 1) | ((reader->data[byte] >> shift) & 1u);
        reader->bit++;
    }
    *out = value;
    return true;
}

void wo_cmp_free(WoCmp *cmp) {
    if (cmp) {
        wo_free(cmp->data);
        wo_free(cmp->offsets);
        wo_free(cmp->sizes);
        memset(cmp, 0, sizeof(*cmp));
    }
}

static bool cmp_unpack(BitReader *bits, uint8_t *output, size_t total, WoError *error) {
    uint8_t window[CMP_WINDOW];
    memset(window, 0, sizeof(window));
    size_t at = 0;
    unsigned pos = 1;
    for (;;) {
        unsigned flag = 0;
        if (!read_bits(bits, 1, &flag, error)) {
            return false;
        }
        if (flag) {
            unsigned value = 0;
            if (at >= total) {
                return wo_fail(error, WO_ERR_FORMAT, "The CMP stream decodes past its size.");
            }
            if (!read_bits(bits, 8, &value, error)) {
                return false;
            }
            output[at++] = (uint8_t)value;
            window[pos] = (uint8_t)value;
            pos = (pos + 1) & (CMP_WINDOW - 1);
            continue;
        }
        unsigned start = 0;
        unsigned length = 0;
        if (!read_bits(bits, 13, &start, error)) {
            return false;
        }
        if (start == 0) {
            break;
        }
        if (!read_bits(bits, 4, &length, error)) {
            return false;
        }
        length += 3;
        if (length > total - at) {
            return wo_fail(error, WO_ERR_FORMAT, "A CMP match decodes past its size.");
        }
        for (unsigned i = 0; i < length; i++) {
            uint8_t value = window[(start + i) & (CMP_WINDOW - 1)];
            output[at++] = value;
            window[pos] = value;
            pos = (pos + 1) & (CMP_WINDOW - 1);
        }
    }
    if (at != total) {
        return wo_fail(error, WO_ERR_FORMAT, "The CMP stream ends before its declared size.");
    }
    return true;
}

bool wo_cmp_decode(const uint8_t *data, size_t len, WoCmp *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    uint32_t count = 0;
    if (!wo_read_le32(data, len, 0, &count)) {
        return wo_fail(error, WO_ERR_FORMAT, "The CMP archive is truncated.");
    }
    if (count < 1 || count > WO_CMP_MAX_ENTRIES || count > (len - 4) / 4) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid CMP directory.");
    }
    size_t *sizes = wo_alloc_array(count, sizeof(size_t), error);
    size_t *offsets = sizes ? wo_alloc_array(count, sizeof(size_t), error) : NULL;
    if (!offsets) {
        wo_free(sizes);
        return false;
    }
    size_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size = wo_le32(data + 4 + (size_t)i * 4);
        if (size > WO_CMP_MAX_ENTRY_BYTES || total > WO_CMP_MAX_TOTAL_BYTES - size) {
            wo_free(sizes);
            wo_free(offsets);
            return wo_fail(error, WO_ERR_FORMAT, "Invalid CMP entry size.");
        }
        offsets[i] = total;
        sizes[i] = size;
        total += size;
    }
    /* Every decoded byte costs at least one bit (a literal 9 bits, a match
     * of up to 18 bytes 18 bits), so a larger declaration can never decode. */
    size_t stream_bytes = len - 4 - (size_t)count * 4;
    if (total / 8 > stream_bytes) {
        wo_free(sizes);
        wo_free(offsets);
        return wo_fail(error, WO_ERR_FORMAT, "The CMP archive declares more data than it holds.");
    }
    uint8_t *output = wo_alloc(total, error);
    if (!output) {
        wo_free(sizes);
        wo_free(offsets);
        return false;
    }
    BitReader bits = {data, len, (uint64_t)(4u + (size_t)count * 4u) * 8u};
    if (!cmp_unpack(&bits, output, total, error)) {
        wo_free(output);
        wo_free(sizes);
        wo_free(offsets);
        return false;
    }
    out->data = output;
    out->total = total;
    out->count = count;
    out->offsets = offsets;
    out->sizes = sizes;
    return true;
}

/* --- WAD -------------------------------------------------------------------- */

void wo_wad_free(WoWad *wad) {
    if (wad) {
        wo_free(wad->entries);
        memset(wad, 0, sizeof(*wad));
    }
}

const WoWadEntry *wo_wad_find(const WoWad *wad, const char *name) {
    for (size_t i = 0; i < wad->count; i++) {
        if (wo_str_ieq(wad->entries[i].name, name)) {
            return &wad->entries[i];
        }
    }
    return NULL;
}

/* Directory records are "name\0", 4 to 12 bytes the loader skips, then the
 * size twice and a zero byte. The size pair is located the same way the
 * original importer did. */
static bool wad_record_size(const uint8_t *data, size_t len, size_t at, size_t *size_out,
                            size_t *next) {
    for (size_t p = at + 4; p <= at + 12 && len >= 9 && p <= len - 9; p++) {
        uint32_t size = wo_le32(data + p);
        uint32_t again = wo_le32(data + p + 4);
        if (size > 0 && size <= INT32_MAX && size <= len && size == again && data[p + 8] == 0) {
            *size_out = size;
            *next = p + 9;
            return true;
        }
    }
    return false;
}

bool wo_wad_parse(const uint8_t *data, size_t len, WoWad *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    uint16_t count = 0;
    if (!wo_read_le16(data, len, 0, &count)) {
        return wo_fail(error, WO_ERR_FORMAT, "The WAD archive is truncated.");
    }
    if (count < 1 || count > WO_WAD_MAX_ENTRIES) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid WAD directory count (%u).", (unsigned)count);
    }
    WoWadEntry *entries = wo_alloc_array(count, sizeof(WoWadEntry), error);
    if (!entries) {
        return false;
    }
    size_t at = 2;
    uint64_t total = 0;
    for (size_t i = 0; i < count; i++) {
        size_t limit = at < len ? len - at : 0;
        if (limit > WO_WAD_NAME_MAX) {
            limit = WO_WAD_NAME_MAX;
        }
        const uint8_t *end = limit ? memchr(data + at, 0, limit) : NULL;
        if (!end) {
            wo_free(entries);
            return wo_fail(error, WO_ERR_FORMAT, "Invalid WAD entry name.");
        }
        size_t name_len = (size_t)(end - (data + at));
        for (size_t c = 0; c < name_len; c++) {
            uint8_t byte = data[at + c];
            entries[i].name[c] = byte < 0x80 ? (char)byte : '?';
        }
        entries[i].name[name_len] = 0;
        at += name_len + 1;
        size_t size = 0;
        if (!wad_record_size(data, len, at, &size, &at)) {
            wo_fail(error, WO_ERR_FORMAT, "Invalid WAD directory record for \"%s\".",
                    entries[i].name);
            wo_free(entries);
            return false;
        }
        for (size_t j = 0; j < i; j++) {
            if (wo_str_ieq(entries[j].name, entries[i].name)) {
                wo_fail(error, WO_ERR_FORMAT, "The WAD archive lists \"%s\" twice.",
                        entries[j].name);
                wo_free(entries);
                return false;
            }
        }
        entries[i].size = size;
        total += size;
    }
    if (total != len - at) {
        wo_free(entries);
        return wo_fail(error, WO_ERR_FORMAT, "The WAD archive's contents do not match its directory.");
    }
    for (size_t i = 0; i < count; i++) {
        entries[i].offset = at;
        at += entries[i].size;
    }
    out->entries = entries;
    out->count = count;
    return true;
}
