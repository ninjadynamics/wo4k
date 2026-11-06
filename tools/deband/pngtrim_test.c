/*
 * pngtrim tests. Every trimmed result is decoded with libpng, independently
 * of pngtrim's own decoder, and compared with the original sample by sample.
 *
 *   pngtrim_test                 synthetic images of every PNG format
 *   pngtrim_test --compare A B   check that B is a faithful trim of A
 */
#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "pngtrim.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* --- libpng decoding ----------------------------------------------------------- */

typedef struct {
    png_uint_32 width, height;
    int depth, colour, interlace;
    size_t row_bytes;
    unsigned char *pixels;
    int palette_count, trans_count, has_trans_colour, has_gamma, srgb, text_count;
    png_color palette[256];
    unsigned char trans[256];
    png_color_16 trans_colour;
    png_fixed_point gamma;
    png_uint_32 iccp_len;
} Decoded;

typedef struct {
    const unsigned char *data;
    size_t len, at;
} Reader;

static void read_bytes(png_structp png, png_bytep out, png_size_t n) {
    Reader *r = (Reader *)png_get_io_ptr(png);
    if (r->len - r->at < n) png_error(png, "truncated");
    memcpy(out, r->data + r->at, n);
    r->at += n;
}

/* Samples as stored (no transforms), de-interlaced, plus the colour chunks. */
static int decode(const unsigned char *data, size_t len, Decoded *d) {
    memset(d, 0, sizeof(*d));
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    unsigned char **volatile rows = NULL;
    if (!info || setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        free(rows);
        free(d->pixels);
        d->pixels = NULL;
        return 0;
    }
    Reader reader = {data, len, 0};
    png_set_read_fn(png, &reader, read_bytes);
    png_read_info(png, info);
    png_get_IHDR(png, info, &d->width, &d->height, &d->depth, &d->colour, &d->interlace, NULL, NULL);
    if (d->interlace) png_set_interlace_handling(png);
    png_read_update_info(png, info);
    d->row_bytes = png_get_rowbytes(png, info);
    /* Zeroed: libpng keeps the destination's padding bits at the end of rows. */
    d->pixels = (unsigned char *)calloc(d->height, d->row_bytes);
    rows = (unsigned char **)malloc(sizeof(*rows) * d->height);
    if (!d->pixels || !rows) png_error(png, "out of memory");
    for (png_uint_32 y = 0; y < d->height; y++) rows[y] = d->pixels + y * d->row_bytes;
    png_read_image(png, rows);
    png_read_end(png, info);
    png_colorp palette;
    if (png_get_PLTE(png, info, &palette, &d->palette_count))
        memcpy(d->palette, palette, (size_t)d->palette_count * sizeof(*palette));
    png_bytep trans;
    png_color_16p trans_colour;
    if (png_get_tRNS(png, info, &trans, &d->trans_count, &trans_colour)) {
        if (trans && d->trans_count) memcpy(d->trans, trans, (size_t)d->trans_count);
        if (trans_colour && d->colour != PNG_COLOR_TYPE_PALETTE) {
            d->trans_colour = *trans_colour;
            d->has_trans_colour = 1;
        }
    }
    d->has_gamma = png_get_gAMA_fixed(png, info, &d->gamma) != 0;
    int intent;
    if (png_get_sRGB(png, info, &intent)) d->srgb = 1 + intent;
    png_charp name;
    int compression;
    png_bytep profile;
    png_get_iCCP(png, info, &name, &compression, &profile, &d->iccp_len);
    png_textp text;
    png_get_text(png, info, &text, &d->text_count);
    png_destroy_read_struct(&png, &info, NULL);
    free(rows);
    return 1;
}

static int same_image(const Decoded *a, const Decoded *b) {
    return a->width == b->width && a->height == b->height && a->depth == b->depth &&
           a->colour == b->colour && a->interlace == b->interlace && a->row_bytes == b->row_bytes &&
           memcmp(a->pixels, b->pixels, a->row_bytes * a->height) == 0 &&
           a->palette_count == b->palette_count &&
           memcmp(a->palette, b->palette, sizeof(a->palette)) == 0 &&
           a->trans_count == b->trans_count && memcmp(a->trans, b->trans, sizeof(a->trans)) == 0 &&
           a->has_trans_colour == b->has_trans_colour &&
           memcmp(&a->trans_colour, &b->trans_colour, sizeof(a->trans_colour)) == 0 &&
           a->has_gamma == b->has_gamma && a->gamma == b->gamma && a->srgb == b->srgb &&
           a->iccp_len == b->iccp_len;
}

/* --- Building test PNGs -------------------------------------------------------- */

typedef struct {
    unsigned char *data;
    size_t len, cap;
} Buf;

static void put(Buf *b, const void *data, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2;
        b->data = (unsigned char *)realloc(b->data, b->cap);
        if (!b->data) abort();
    }
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

static void put32(Buf *b, unsigned long v) {
    unsigned char be[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16),
                           (unsigned char)(v >> 8), (unsigned char)v};
    put(b, be, 4);
}

static void chunk(Buf *b, const char *type, const void *data, size_t n) {
    put32(b, (unsigned long)n);
    size_t start = b->len;
    put(b, type, 4);
    if (n) put(b, data, n);
    put32(b, crc32(0L, b->data + start, (uInt)(n + 4)));
}

static unsigned long rng = 12345;
static unsigned next_byte(void) {
    rng = rng * 1103515245u + 12345u;
    return (unsigned)(rng >> 16) & 0xFF;
}

typedef struct {
    unsigned width, height;
    int colour, depth, interlace;
    int text, gamma, trans, animated, critical; /* extra chunks */
} Spec;

/* Unfiltered rows (filter byte 0) stored without compression, so any
 * reasonable trim is smaller. Samples are smooth with some noise; RGBA has
 * fully transparent pixels hiding non-zero colour. */
static Buf build(const Spec *s) {
    static const unsigned x0[7] = {0, 4, 0, 2, 0, 1, 0}, dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const unsigned y0[7] = {0, 0, 4, 0, 2, 0, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    static const int channels[7] = {1, 0, 3, 1, 2, 0, 4};
    Buf out = {0}, rows = {0};
    put(&out, "\x89PNG\r\n\x1a\n", 8);
    unsigned char ihdr[13] = {0};
    ihdr[0] = (unsigned char)(s->width >> 24); ihdr[1] = (unsigned char)(s->width >> 16);
    ihdr[2] = (unsigned char)(s->width >> 8); ihdr[3] = (unsigned char)s->width;
    ihdr[4] = (unsigned char)(s->height >> 24); ihdr[5] = (unsigned char)(s->height >> 16);
    ihdr[6] = (unsigned char)(s->height >> 8); ihdr[7] = (unsigned char)s->height;
    ihdr[8] = (unsigned char)s->depth; ihdr[9] = (unsigned char)s->colour;
    ihdr[12] = (unsigned char)s->interlace;
    chunk(&out, "IHDR", ihdr, 13);
    if (s->gamma) {
        unsigned char g[4] = {0, 0, 0xB1, 0x8F}; /* 1/2.2 */
        chunk(&out, "gAMA", g, 4);
    }
    if (s->animated) {
        unsigned char actl[8] = {0, 0, 0, 1, 0, 0, 0, 0};
        chunk(&out, "acTL", actl, 8);
    }
    if (s->critical) chunk(&out, "XYZW", "x", 1);
    if (s->colour == 3) {
        unsigned char plte[768];
        for (int i = 0; i < 768; i++) plte[i] = (unsigned char)(i * 7);
        chunk(&out, "PLTE", plte, (size_t)3 << s->depth);
        if (s->trans) {
            unsigned char t[3] = {0, 128, 255};
            chunk(&out, "tRNS", t, s->depth == 1 ? 2 : 3);
        }
    } else if (s->trans && (s->colour == 0 || s->colour == 2)) {
        unsigned char t[6] = {0, 1, 0, 2, 0, 3};
        chunk(&out, "tRNS", t, s->colour == 0 ? 2 : 6);
    }
    if (s->text) chunk(&out, "tEXt", "Software\0Adobe Photoshop", 24);
    int passes = s->interlace ? 7 : 1;
    int bits = channels[s->colour] * s->depth;
    for (int p = 0; p < passes; p++) {
        unsigned w = s->width, h = s->height;
        if (s->interlace) {
            w = s->width > x0[p] ? (s->width - x0[p] + dx[p] - 1) / dx[p] : 0;
            h = s->height > y0[p] ? (s->height - y0[p] + dy[p] - 1) / dy[p] : 0;
        }
        if (!w || !h) continue;
        size_t row_bytes = ((size_t)w * (size_t)bits + 7) / 8;
        for (unsigned y = 0; y < h; y++) {
            unsigned char zero = 0;
            put(&rows, &zero, 1);
            for (size_t i = 0; i < row_bytes; i++) {
                unsigned char v = (unsigned char)((i + y * 3) / 4 + (next_byte() & 3));
                if (s->colour == 6 && s->depth == 8 && i % 4 == 3) v = (i / 4 + y) % 5 == 0 ? 0 : 255;
                put(&rows, &v, 1);
            }
        }
    }
    uLongf stored = compressBound((uLong)rows.len);
    unsigned char *z = (unsigned char *)malloc(stored);
    if (!z || compress2(z, &stored, rows.data, (uLong)rows.len, 0) != Z_OK) abort();
    size_t half = stored / 2; /* two IDAT chunks */
    chunk(&out, "IDAT", z, half);
    chunk(&out, "IDAT", z + half, stored - half);
    if (s->text) chunk(&out, "tIME", "\x07\xEA\x0B\x06\x0C\x00\x00", 7);
    chunk(&out, "IEND", NULL, 0);
    free(z);
    free(rows.data);
    return out;
}

/* --- Tests ----------------------------------------------------------------------- */

static void test_every_format(void) {
    static const struct { int colour, depth; } formats[] = {
        {0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1}, {3, 2},
        {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};
    for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); f++) {
        for (int interlace = 0; interlace < 2; interlace++) {
            Spec s = {37, 23, formats[f].colour, formats[f].depth, interlace, 1, 1, 1, 0, 0};
            Buf png = build(&s);
            unsigned char *out = NULL;
            size_t out_len = 0;
            char why[128] = "";
            PngTrimResult r = pngtrim(png.data, png.len, &out, &out_len, why, sizeof(why));
            Decoded a, b;
            int decoded = decode(png.data, png.len, &a);
            CHECK(decoded);
            CHECK(r == PNGTRIM_SMALLER);
            if (r == PNGTRIM_SMALLER && decoded) {
                CHECK(out_len < png.len);
                CHECK(decode(out, out_len, &b));
                if (b.pixels) {
                    if (!same_image(&a, &b))
                        fprintf(stderr, "  colour %d depth %d interlace %d differs\n",
                                s.colour, s.depth, interlace);
                    CHECK(same_image(&a, &b));
                    CHECK(a.text_count == 1 && b.text_count == 0);
                    CHECK(b.has_gamma);
                }
                free(b.pixels);
                /* Trimming the result again finds nothing more to gain. */
                unsigned char *again = NULL;
                size_t again_len = 0;
                CHECK(pngtrim(out, out_len, &again, &again_len, NULL, 0) == PNGTRIM_NOT_SMALLER);
                free(again);
            }
            free(a.pixels);
            free(out);
            free(png.data);
        }
    }
}

static void test_refusals(void) {
    unsigned char *out = NULL;
    size_t out_len = 0;
    char why[128];
    Spec animated = {16, 16, 6, 8, 0, 0, 0, 0, 1, 0};
    Buf png = build(&animated);
    CHECK(pngtrim(png.data, png.len, &out, &out_len, why, sizeof(why)) == PNGTRIM_SKIPPED);
    CHECK(out == NULL);
    free(png.data);

    Spec critical = {16, 16, 2, 8, 0, 0, 0, 0, 0, 1};
    png = build(&critical);
    CHECK(pngtrim(png.data, png.len, &out, &out_len, why, sizeof(why)) == PNGTRIM_FAILED);
    free(png.data);

    Spec plain = {16, 16, 2, 8, 0, 0, 0, 0, 0, 0};
    png = build(&plain);
    png.data[40] ^= 1; /* inside the IDAT data: CRC mismatch */
    CHECK(pngtrim(png.data, png.len, &out, &out_len, why, sizeof(why)) == PNGTRIM_FAILED);
    png.data[40] ^= 1;
    CHECK(pngtrim(png.data, png.len - 20, &out, &out_len, why, sizeof(why)) == PNGTRIM_FAILED);
    CHECK(pngtrim(png.data, 7, &out, &out_len, why, sizeof(why)) == PNGTRIM_FAILED);
    CHECK(out == NULL);
    free(png.data);
}

static unsigned char *load(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = size > 0 ? (unsigned char *)malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *len = data ? (size_t)size : 0;
    return data;
}

static int compare_files(const char *original, const char *trimmed) {
    size_t a_len = 0, b_len = 0;
    unsigned char *a_data = load(original, &a_len), *b_data = load(trimmed, &b_len);
    Decoded a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    int ok = a_data && b_data && decode(a_data, a_len, &a) && decode(b_data, b_len, &b) &&
             same_image(&a, &b);
    printf("%s %s (%lu -> %lu bytes)\n", ok ? "same" : "DIFFERENT", trimmed,
           (unsigned long)a_len, (unsigned long)b_len);
    free(a.pixels);
    free(b.pixels);
    free(a_data);
    free(b_data);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--compare") == 0) return compare_files(argv[2], argv[3]);
    test_every_format();
    test_refusals();
    if (failures) {
        fprintf(stderr, "%d check(s) failed.\n", failures);
        return 1;
    }
    printf("pngtrim: all checks passed.\n");
    return 0;
}
