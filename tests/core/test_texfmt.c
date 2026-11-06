#include <string.h>

#include "core/wo_texfmt.h"
#include "wo_test.h"

/* --- Builders ------------------------------------------------------------------ */

static void put32(WoBuf *b, uint32_t v) {
    uint8_t bytes[4];
    wo_put_le32(bytes, v);
    wo_buf_append(b, bytes, 4, NULL);
}

static void put16(WoBuf *b, uint16_t v) {
    uint8_t bytes[2];
    wo_put_le16(bytes, v);
    wo_buf_append(b, bytes, 2, NULL);
}

/* TIM with an optional CLUT of `colours` entries (value = index * 3 + 1). */
static void build_tim(WoBuf *b, uint32_t flags, size_t colours, uint16_t words, uint16_t height,
                      const uint8_t *pixels, size_t pixel_len) {
    put32(b, 16);
    put32(b, flags);
    if (flags & 8) {
        put32(b, (uint32_t)(12 + colours * 2));
        put32(b, 0);
        put16(b, (uint16_t)colours);
        put16(b, 1);
        for (size_t i = 0; i < colours; i++) {
            put16(b, (uint16_t)(i * 3 + 1));
        }
    }
    put32(b, (uint32_t)(12 + pixel_len));
    put32(b, 0);
    put16(b, words);
    put16(b, height);
    wo_buf_append(b, pixels, pixel_len, NULL);
}

static void test_tim_modes(void) {
    WoError error = {0};
    WoNativeImage image = {0};
    /* 4-bit: one word = four pixels; low nibble first. */
    WoBuf b = {0};
    uint8_t four[] = {0x21, 0x43, 0x65, 0x87};
    build_tim(&b, 8, 16, 1, 2, four, sizeof(four));
    CHECK_OK(wo_tim_decode(b.data, b.len, &image, &error), &error);
    CHECK(image.width == 4 && image.height == 2);
    CHECK(image.pixels[0] == 1 * 3 + 1 && image.pixels[1] == 2 * 3 + 1);
    CHECK(image.pixels[7] == 8 * 3 + 1);
    wo_native_image_free(&image);
    wo_buf_free(&b);

    /* 8-bit with a 256-colour CLUT. */
    uint8_t eight[] = {0, 255, 7, 9};
    build_tim(&b, 9, 256, 1, 2, eight, sizeof(eight));
    CHECK_OK(wo_tim_decode(b.data, b.len, &image, &error), &error);
    CHECK(image.width == 2 && image.height == 2 && image.pixels[1] == 255 * 3 + 1);
    wo_native_image_free(&image);
    wo_buf_free(&b);

    /* 16-bit direct colour. */
    uint8_t direct[] = {0x34, 0x12, 0xFF, 0xFF};
    build_tim(&b, 2, 0, 2, 1, direct, sizeof(direct));
    CHECK_OK(wo_tim_decode(b.data, b.len, &image, &error), &error);
    CHECK(image.width == 2 && image.pixels[0] == 0x1234 && image.pixels[1] == 0xFFFF);
    wo_native_image_free(&image);

    /* The export decoder gives BGRA with colour 0 transparent. */
    WoImage bgra = {0};
    CHECK_OK(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error);
    CHECK(bgra.width == 2 && bgra.pixels[3] == 255 && bgra.pixels[4] == 255);
    wo_image_free(&bgra);
    wo_buf_free(&b);

    /* 24-bit: only the export decoder accepts it. */
    uint8_t rgb[] = {10, 20, 30, 40, 50, 60};
    build_tim(&b, 3, 0, 3, 1, rgb, sizeof(rgb));
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_UNSUPPORTED);
    CHECK_OK(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error);
    CHECK(bgra.width == 2 && bgra.pixels[0] == 30 && bgra.pixels[1] == 20 && bgra.pixels[2] == 10);
    CHECK(bgra.pixels[4] == 60 && bgra.pixels[7] == 255);
    wo_image_free(&bgra);
    wo_buf_free(&b);
}

static void test_tim_rejects(void) {
    WoError error = {0};
    WoNativeImage image = {0};
    WoImage bgra = {0};
    WoBuf b = {0};
    uint8_t pixels[8] = {0};
    /* Bad magic. */
    build_tim(&b, 2, 0, 1, 1, pixels, 2);
    b.data[0] = 17;
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
    /* Paletted image without a palette. */
    build_tim(&b, 1, 0, 1, 1, pixels, 2);
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
    /* 4-bit image with a CLUT too short for 16 colours. */
    build_tim(&b, 8, 4, 1, 1, pixels, 2);
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
    /* Pixel block declares more rows than it holds. */
    build_tim(&b, 2, 0, 1, 4, pixels, 4);
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
    /* Block size larger than the file. */
    build_tim(&b, 2, 0, 1, 1, pixels, 2);
    wo_put_le32(b.data + 8, 0x7FFFFFF0u);
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
    /* Wider than 4096 pixels. */
    build_tim(&b, 2, 0, 4097, 1, pixels, 2);
    CHECK_FAILS(wo_tim_decode(b.data, b.len, &image, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
    /* Truncated header. */
    uint8_t tiny[6] = {16, 0, 0, 0, 2, 0};
    CHECK_FAILS(wo_tim_decode(tiny, sizeof(tiny), &image, &error), &error, WO_ERR_FORMAT);
    /* An 8-bit index past a short export palette. */
    uint8_t index[] = {5, 0};
    build_tim(&b, 9, 4, 1, 1, index, sizeof(index));
    CHECK_FAILS(wo_tim_decode_bgra(b.data, b.len, &bgra, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
}

static void build_tga(WoBuf *b, uint16_t w, uint16_t h, uint8_t id_len) {
    uint8_t header[18] = {0};
    header[0] = id_len;
    header[2] = 2;
    wo_put_le16(header + 12, w);
    wo_put_le16(header + 14, h);
    header[16] = 16;
    wo_buf_append(b, header, sizeof(header), NULL);
    for (uint8_t i = 0; i < id_len; i++) {
        wo_buf_append_byte(b, 0xEE, NULL);
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        put16(b, 0);
    }
}

static void test_tga(void) {
    WoError error = {0};
    WoBuf b = {0};
    build_tga(&b, 4, 3, 2);
    WoTgaInfo info;
    CHECK_OK(wo_tga_info(b.data, b.len, &info, &error), &error);
    CHECK(info.width == 4 && info.height == 3 && info.pixel_offset == 20);
    uint16_t px[4] = {0x001F, 0x7C00, 0x83E0, 0x0000}; /* red, blue, green+flag, black */
    WoNativeImage image = {2, 2, px};
    CHECK_OK(wo_tga_patch(b.data, b.len, &image, 1, 1, 2, 2, &error), &error);
    /* PlayStation red (bits 0-4) becomes PC red (bits 10-14). */
    CHECK(wo_le16(b.data + 20 + (1 * 4 + 1) * 2) == 0x7C00);
    CHECK(wo_le16(b.data + 20 + (1 * 4 + 2) * 2) == 0x001F);
    CHECK(wo_le16(b.data + 20 + (2 * 4 + 1) * 2) == 0x83E0);
    WoImage bgra = {0};
    CHECK_OK(wo_tga_decode_bgra(b.data, b.len, &bgra, &error), &error);
    const uint8_t *red = bgra.pixels + (1 * 4 + 1) * 4;
    CHECK(red[0] == 0 && red[1] == 0 && red[2] == 255 && red[3] == 255);
    CHECK(bgra.pixels[3] == 0); /* black is transparent */
    CHECK(wo_image_has_transparency(&bgra));
    wo_image_free(&bgra);

    /* Placement outside the sheet, or a size that differs from the image. */
    CHECK_FAILS(wo_tga_patch(b.data, b.len, &image, 3, 0, 2, 2, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tga_patch(b.data, b.len, &image, -1, 0, 2, 2, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tga_patch(b.data, b.len, &image, 0, 2, 2, 2, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tga_patch(b.data, b.len, &image, 0, 0, 2, 1, &error), &error, WO_ERR_FORMAT);
    /* Truncated pixel area. */
    CHECK_FAILS(wo_tga_patch(b.data, b.len - 1, &image, 0, 0, 2, 2, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tga_info(b.data, b.len - 1, &info, &error), &error, WO_ERR_FORMAT);
    /* Not an uncompressed 16-bit image. */
    b.data[2] = 10;
    CHECK_FAILS(wo_tga_info(b.data, b.len, &info, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_tga_patch(b.data, b.len, &image, 0, 0, 2, 2, &error), &error, WO_ERR_FORMAT);
    b.data[2] = 2;
    b.data[16] = 24;
    CHECK_FAILS(wo_tga_info(b.data, b.len, &info, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);
}

/* --- CMP ---------------------------------------------------------------------- */

typedef struct BitWriter {
    WoBuf buf;
    unsigned bits;
    unsigned used;
} BitWriter;

static void put_bits(BitWriter *w, unsigned value, unsigned count) {
    for (unsigned i = count; i-- > 0;) {
        w->bits = (w->bits << 1) | ((value >> i) & 1u);
        if (++w->used == 8) {
            wo_buf_append_byte(&w->buf, (uint8_t)w->bits, NULL);
            w->bits = 0;
            w->used = 0;
        }
    }
}

static void flush_bits(BitWriter *w) {
    while (w->used) {
        put_bits(w, 0, 1);
    }
}

static void cmp_header(BitWriter *w, const uint32_t *sizes, size_t count) {
    put32(&w->buf, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        put32(&w->buf, sizes[i]);
    }
}

static void test_cmp(void) {
    WoError error = {0};
    WoCmp cmp = {0};
    /* "ab" "abab": literals a, b, then a match of 4 from window position 1. */
    BitWriter w = {{0}, 0, 0};
    uint32_t sizes[] = {2, 4};
    cmp_header(&w, sizes, 2);
    put_bits(&w, 1, 1);
    put_bits(&w, 'a', 8);
    put_bits(&w, 1, 1);
    put_bits(&w, 'b', 8);
    put_bits(&w, 0, 1);
    put_bits(&w, 1, 13);
    put_bits(&w, 1, 4); /* 1 + 3 = 4 bytes */
    put_bits(&w, 0, 1);
    put_bits(&w, 0, 13);
    flush_bits(&w);
    CHECK_OK(wo_cmp_decode(w.buf.data, w.buf.len, &cmp, &error), &error);
    CHECK(cmp.count == 2 && cmp.total == 6);
    CHECK(memcmp(cmp.data, "ababab", 6) == 0);
    CHECK(cmp.offsets[1] == 2 && cmp.sizes[1] == 4);
    wo_cmp_free(&cmp);

    /* The stream must produce exactly the declared total. */
    size_t len = w.buf.len;
    wo_put_le32(w.buf.data + 8, 5);
    CHECK_FAILS(wo_cmp_decode(w.buf.data, len, &cmp, &error), &error, WO_ERR_FORMAT);
    wo_put_le32(w.buf.data + 8, 3);
    CHECK_FAILS(wo_cmp_decode(w.buf.data, len, &cmp, &error), &error, WO_ERR_FORMAT);
    wo_put_le32(w.buf.data + 8, 4);
    /* Truncated stream. */
    CHECK_FAILS(wo_cmp_decode(w.buf.data, len - 2, &cmp, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&w.buf);

    /* Oversized declaration and bad counts. */
    uint8_t oversized[] = {1, 0, 0, 0, 255, 255, 255, 127};
    CHECK_FAILS(wo_cmp_decode(oversized, sizeof(oversized), &cmp, &error), &error, WO_ERR_FORMAT);
    uint8_t none[] = {0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_FAILS(wo_cmp_decode(none, sizeof(none), &cmp, &error), &error, WO_ERR_FORMAT);
    uint8_t many[] = {1, 16, 0, 0, 0, 0, 0, 0};
    CHECK_FAILS(wo_cmp_decode(many, sizeof(many), &cmp, &error), &error, WO_ERR_FORMAT);
    uint8_t short_dir[] = {2, 0, 0, 0, 1, 0, 0, 0};
    CHECK_FAILS(wo_cmp_decode(short_dir, sizeof(short_dir), &cmp, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_cmp_decode(none, 3, &cmp, &error), &error, WO_ERR_FORMAT);
    /* Sum of entries past the 64 MiB limit. */
    WoBuf big = {0};
    put32(&big, 5);
    for (int i = 0; i < 5; i++) {
        put32(&big, WO_CMP_MAX_ENTRY_BYTES);
    }
    CHECK_FAILS(wo_cmp_decode(big.data, big.len, &cmp, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&big);

    /* A literal past the end, and a match past the end. */
    BitWriter o = {{0}, 0, 0};
    uint32_t one[] = {1};
    cmp_header(&o, one, 1);
    put_bits(&o, 1, 1);
    put_bits(&o, 'x', 8);
    put_bits(&o, 1, 1);
    put_bits(&o, 'y', 8);
    flush_bits(&o);
    CHECK_FAILS(wo_cmp_decode(o.buf.data, o.buf.len, &cmp, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&o.buf);
    BitWriter m = {{0}, 0, 0};
    cmp_header(&m, one, 1);
    put_bits(&m, 0, 1);
    put_bits(&m, 5, 13);
    put_bits(&m, 0, 4);
    flush_bits(&m);
    CHECK_FAILS(wo_cmp_decode(m.buf.data, m.buf.len, &cmp, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&m.buf);
}

/* --- WAD ---------------------------------------------------------------------- */

static void wad_entry(WoBuf *b, const char *name, size_t pad, uint32_t size) {
    wo_buf_append(b, name, strlen(name) + 1, NULL);
    for (size_t i = 0; i < pad; i++) {
        wo_buf_append_byte(b, 0xAB, NULL);
    }
    put32(b, size);
    put32(b, size);
    wo_buf_append_byte(b, 0, NULL);
}

static void test_wad(void) {
    WoError error = {0};
    WoWad wad = {0};
    WoBuf b = {0};
    put16(&b, 2);
    wad_entry(&b, "library.ttf", 4, 3);
    wad_entry(&b, "Sky.cmp", 9, 2);
    wo_buf_append(&b, "abcXY", 5, NULL);
    CHECK_OK(wo_wad_parse(b.data, b.len, &wad, &error), &error);
    CHECK(wad.count == 2);
    const WoWadEntry *sky = wo_wad_find(&wad, "sky.CMP");
    CHECK(sky && sky->size == 2 && memcmp(b.data + sky->offset, "XY", 2) == 0);
    const WoWadEntry *lib = wo_wad_find(&wad, "LIBRARY.TTF");
    CHECK(lib && memcmp(b.data + lib->offset, "abc", 3) == 0);
    CHECK(wo_wad_find(&wad, "missing") == NULL);
    wo_wad_free(&wad);
    /* Payload size must match the directory exactly. */
    CHECK_FAILS(wo_wad_parse(b.data, b.len - 1, &wad, &error), &error, WO_ERR_FORMAT);
    wo_buf_append_byte(&b, 0, NULL);
    CHECK_FAILS(wo_wad_parse(b.data, b.len, &wad, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);

    /* Duplicate names (ignoring case). */
    put16(&b, 2);
    wad_entry(&b, "a.tim", 4, 1);
    wad_entry(&b, "A.TIM", 4, 1);
    wo_buf_append(&b, "xy", 2, NULL);
    CHECK_FAILS(wo_wad_parse(b.data, b.len, &wad, &error), &error, WO_ERR_FORMAT);
    /* The message is formatted before the directory is freed (fuzzer find). */
    CHECK(strstr(error.message, "\"a.tim\"") != NULL);
    wo_buf_free(&b);

    /* Size pair not found within 4..12 bytes after the name. */
    put16(&b, 1);
    wad_entry(&b, "far.tim", 13, 1);
    wo_buf_append_byte(&b, 'z', NULL);
    CHECK_FAILS(wo_wad_parse(b.data, b.len, &wad, &error), &error, WO_ERR_FORMAT);
    CHECK(strstr(error.message, "\"far.tim\"") != NULL);
    wo_buf_free(&b);

    /* Names must end within 64 bytes. */
    put16(&b, 1);
    for (int i = 0; i < 70; i++) {
        wo_buf_append_byte(&b, 'n', NULL);
    }
    wad_entry(&b, "", 4, 1);
    wo_buf_append_byte(&b, 'z', NULL);
    CHECK_FAILS(wo_wad_parse(b.data, b.len, &wad, &error), &error, WO_ERR_FORMAT);
    wo_buf_free(&b);

    uint8_t zero[] = {0, 0};
    CHECK_FAILS(wo_wad_parse(zero, sizeof(zero), &wad, &error), &error, WO_ERR_FORMAT);
    uint8_t too_many[] = {1, 1};
    CHECK_FAILS(wo_wad_parse(too_many, sizeof(too_many), &wad, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_wad_parse(zero, 1, &wad, &error), &error, WO_ERR_FORMAT);
    uint8_t truncated[] = {1, 0, 'a', 0, 1};
    CHECK_FAILS(wo_wad_parse(truncated, sizeof(truncated), &wad, &error), &error, WO_ERR_FORMAT);
}

int main(void) {
    RUN(test_tim_modes);
    RUN(test_tim_rejects);
    RUN(test_tga);
    RUN(test_cmp);
    RUN(test_wad);
    return wo_test_finish();
}
