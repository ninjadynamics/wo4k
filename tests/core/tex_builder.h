/* Builders for native texture fixtures (TIM, CMP, WAD, TGA). Header-only so
 * each test program can include it. */
#ifndef TEX_BUILDER_H
#define TEX_BUILDER_H

#include <string.h>

#include "core/wo_base.h"

static inline void tb_put32(WoBuf *b, uint32_t v) {
    uint8_t bytes[4];
    wo_put_le32(bytes, v);
    wo_buf_append(b, bytes, 4, NULL);
}

static inline void tb_put16(WoBuf *b, uint16_t v) {
    uint8_t bytes[2];
    wo_put_le16(bytes, v);
    wo_buf_append(b, bytes, 2, NULL);
}

/* A 16-bit direct-colour TIM where pixel (x, y) = base + y * width + x. */
static inline void tb_tim16(WoBuf *b, uint16_t width, uint16_t height, uint16_t base) {
    tb_put32(b, 16);
    tb_put32(b, 2);
    tb_put32(b, (uint32_t)(12 + (size_t)width * height * 2));
    tb_put32(b, 0);
    tb_put16(b, width);
    tb_put16(b, height);
    for (uint32_t i = 0; i < (uint32_t)width * height; i++) {
        tb_put16(b, (uint16_t)(base + i));
    }
}

typedef struct TbBits {
    WoBuf *out;
    unsigned bits;
    unsigned used;
} TbBits;

static inline void tb_bits(TbBits *w, unsigned value, unsigned count) {
    for (unsigned i = count; i-- > 0;) {
        w->bits = (w->bits << 1) | ((value >> i) & 1u);
        if (++w->used == 8) {
            wo_buf_append_byte(w->out, (uint8_t)w->bits, NULL);
            w->bits = 0;
            w->used = 0;
        }
    }
}

/* A CMP holding the given entries, encoded with literals only. */
static inline void tb_cmp(WoBuf *b, const WoBuf *entries, size_t count) {
    tb_put32(b, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        tb_put32(b, (uint32_t)entries[i].len);
    }
    TbBits w = {b, 0, 0};
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < entries[i].len; j++) {
            tb_bits(&w, 1, 1);
            tb_bits(&w, entries[i].data[j], 8);
        }
    }
    tb_bits(&w, 0, 1);
    tb_bits(&w, 0, 13);
    while (w.used) {
        tb_bits(&w, 0, 1);
    }
}

/* A WAD with the given names and contents. */
static inline void tb_wad(WoBuf *b, const char *const *names, const WoBuf *contents,
                          size_t count) {
    tb_put16(b, (uint16_t)count);
    for (size_t i = 0; i < count; i++) {
        wo_buf_append(b, names[i], strlen(names[i]) + 1, NULL);
        tb_put32(b, 0x11111111u); /* the four bytes the loader skips */
        tb_put32(b, (uint32_t)contents[i].len);
        tb_put32(b, (uint32_t)contents[i].len);
        wo_buf_append_byte(b, 0, NULL);
    }
    for (size_t i = 0; i < count; i++) {
        wo_buf_append(b, contents[i].data, contents[i].len, NULL);
    }
}

/* An uncompressed RGB555 TGA filled with colour. */
static inline void tb_tga(WoBuf *b, uint16_t width, uint16_t height, uint16_t colour) {
    uint8_t header[18] = {0};
    header[2] = 2;
    wo_put_le16(header + 12, width);
    wo_put_le16(header + 14, height);
    header[16] = 16;
    wo_buf_append(b, header, sizeof(header), NULL);
    for (uint32_t i = 0; i < (uint32_t)width * height; i++) {
        tb_put16(b, colour);
    }
}

#endif
