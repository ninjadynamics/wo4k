/* pngtrim - lossless PNG recompression. See pngtrim.h. */
#include "pngtrim.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_RAW_BYTES ((uint64_t)1 << 30)
#define MAX_KEPT 24
#define IDAT_PIECE ((size_t)1 << 20)
#define DEFLATE_SLICE ((size_t)1 << 20)
#define ADAPTIVE 5
#define SCREEN_LEVEL 5
#define FINALISTS 2

static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const unsigned char iend_chunk[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};

typedef struct {
    uint32_t width, height;
    int depth, colour, interlace, channels;
    size_t pixel_bytes; /* filter distance */
    const unsigned char *ihdr; /* whole chunks, verbatim */
    const unsigned char *kept[MAX_KEPT];
    size_t kept_size[MAX_KEPT];
    int kept_count;
    unsigned char *idat; /* concatenated IDAT data */
    size_t idat_len;
    int animated;
} Png;

typedef struct {
    uint32_t width, height;
    size_t row_bytes;
} Pass;

static uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int fail(char *why, size_t size, const char *text) {
    if (why && size) snprintf(why, size, "%s", text);
    return 0;
}

static int is_type(const unsigned char *type, const char *name) {
    return memcmp(type, name, 4) == 0;
}

static int is_kept(const unsigned char *type) {
    static const char names[][5] = {"PLTE", "tRNS", "cHRM", "gAMA", "iCCP",
                                    "sBIT", "sRGB", "cICP", "mDCv", "cLLi"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (is_type(type, names[i])) return 1;
    return 0;
}

static int depth_ok(int colour, int depth) {
    switch (colour) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2: case 4: case 6: return depth == 8 || depth == 16;
    default: return 0;
    }
}

/* Chunk structure, CRCs and IHDR; IDAT data is copied into png->idat. */
static int parse(const unsigned char *in, size_t len, Png *png, char *why, size_t why_size) {
    static const int channels[7] = {1, 0, 3, 1, 2, 0, 4};
    memset(png, 0, sizeof(*png));
    if (len < 8 || memcmp(in, signature, 8) != 0) return fail(why, why_size, "not a PNG file");
    png->idat = (unsigned char *)malloc(len);
    if (!png->idat) return fail(why, why_size, "out of memory");
    int seen_idat = 0, idat_done = 0, seen_iend = 0, seen_plte = 0;
    size_t at = 8;
    while (at < len && !seen_iend) {
        if (len - at < 12) return fail(why, why_size, "truncated chunk");
        uint32_t n = be32(in + at);
        if (n > 0x7FFFFFFFu || n > len - at - 12) return fail(why, why_size, "invalid chunk length");
        const unsigned char *type = in + at + 4, *data = type + 4;
        for (int i = 0; i < 4; i++) {
            int c = type[i] | 0x20;
            if (c < 'a' || c > 'z') return fail(why, why_size, "invalid chunk type");
        }
        if (crc32(crc32(0L, Z_NULL, 0), type, n + 4) != be32(data + n))
            return fail(why, why_size, "chunk CRC mismatch");
        size_t size = (size_t)n + 12;
        if ((at == 8) != is_type(type, "IHDR")) return fail(why, why_size, "IHDR is not the first chunk");
        if (is_type(type, "IHDR")) {
            if (n != 13) return fail(why, why_size, "invalid IHDR");
            png->width = be32(data);
            png->height = be32(data + 4);
            png->depth = data[8];
            png->colour = data[9];
            png->interlace = data[12];
            if (!png->width || !png->height || png->width > 0x7FFFFFFFu || png->height > 0x7FFFFFFFu ||
                !depth_ok(png->colour, png->depth) || data[10] != 0 || data[11] != 0 || data[12] > 1)
                return fail(why, why_size, "unsupported IHDR");
            png->channels = channels[png->colour];
            png->pixel_bytes = (size_t)png->channels * (size_t)png->depth / 8;
            if (png->pixel_bytes < 1) png->pixel_bytes = 1;
            png->ihdr = in + at;
        } else if (is_type(type, "IDAT")) {
            if (idat_done) return fail(why, why_size, "IDAT chunks are not consecutive");
            memcpy(png->idat + png->idat_len, data, n);
            png->idat_len += n;
            seen_idat = 1;
        } else {
            idat_done = seen_idat;
            if (is_type(type, "IEND")) {
                seen_iend = 1;
            } else if (is_type(type, "acTL")) {
                png->animated = 1;
            } else if (is_kept(type)) {
                if (seen_idat) return fail(why, why_size, "colour chunk after the image data");
                if (png->kept_count == MAX_KEPT) return fail(why, why_size, "too many colour chunks");
                seen_plte |= is_type(type, "PLTE");
                png->kept[png->kept_count] = in + at;
                png->kept_size[png->kept_count++] = size;
            } else if (!(type[0] & 0x20)) {
                return fail(why, why_size, "unsupported critical chunk");
            } /* other ancillary chunks are dropped */
        }
        at += size;
    }
    if (!png->ihdr || !seen_idat || !seen_iend) return fail(why, why_size, "incomplete PNG");
    if (png->colour == 3 && !seen_plte) return fail(why, why_size, "palette image without PLTE");
    return 1;
}

/* Sub-images (one, or seven Adam7 passes) and the byte sizes of the image
 * data with and without filter bytes. */
static int layout(const Png *png, Pass passes[7], int *count, size_t *filtered, size_t *raw) {
    static const uint32_t x0[7] = {0, 4, 0, 2, 0, 1, 0}, dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const uint32_t y0[7] = {0, 0, 4, 0, 2, 0, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    uint64_t with = 0, without = 0;
    *count = png->interlace ? 7 : 1;
    for (int p = 0; p < *count; p++) {
        uint32_t w = png->width, h = png->height;
        if (png->interlace) {
            w = png->width > x0[p] ? (png->width - x0[p] + dx[p] - 1) / dx[p] : 0;
            h = png->height > y0[p] ? (png->height - y0[p] + dy[p] - 1) / dy[p] : 0;
        }
        uint64_t row = ((uint64_t)w * (uint64_t)png->channels * (uint64_t)png->depth + 7) / 8;
        if (row > MAX_RAW_BYTES) return 0;
        passes[p].width = w;
        passes[p].height = h;
        passes[p].row_bytes = (size_t)row;
        if (w && h) {
            with += (uint64_t)h * (row + 1);
            without += (uint64_t)h * row;
        }
        if (with > MAX_RAW_BYTES) return 0;
    }
    *filtered = (size_t)with;
    *raw = (size_t)without;
    return 1;
}

static int inflate_exact(const unsigned char *src, size_t n, unsigned char *dst, size_t expected) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit(&z) != Z_OK) return 0;
    z.next_in = (unsigned char *)src;
    z.avail_in = (uInt)n;
    z.next_out = dst;
    z.avail_out = (uInt)expected;
    int ret = inflate(&z, Z_FINISH);
    int ok = ret == Z_STREAM_END && z.avail_out == 0;
    inflateEnd(&z);
    return ok;
}

static unsigned paeth(unsigned a, unsigned b, unsigned c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a), pb = abs(p - (int)b), pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

static unsigned predict(int filter, unsigned a, unsigned b, unsigned c) {
    switch (filter) {
    case 1: return a;
    case 2: return b;
    case 3: return (a + b) >> 1;
    case 4: return paeth(a, b, c);
    default: return 0;
    }
}

static int unfilter(const Png *png, const Pass *passes, int count, const unsigned char *in,
                    unsigned char *out) {
    size_t bpp = png->pixel_bytes;
    for (int p = 0; p < count; p++) {
        const Pass *pass = &passes[p];
        if (!pass->width || !pass->height) continue;
        const unsigned char *prior = NULL;
        for (uint32_t y = 0; y < pass->height; y++) {
            int filter = *in++;
            if (filter > 4) return 0;
            for (size_t i = 0; i < pass->row_bytes; i++) {
                unsigned a = i >= bpp ? out[i - bpp] : 0;
                unsigned b = prior ? prior[i] : 0;
                unsigned c = prior && i >= bpp ? prior[i - bpp] : 0;
                out[i] = (unsigned char)(in[i] + predict(filter, a, b, c));
            }
            in += pass->row_bytes;
            prior = out;
            out += pass->row_bytes;
        }
    }
    return 1;
}

static void filter_row(int filter, const unsigned char *row, const unsigned char *prior, size_t n,
                       size_t bpp, unsigned char *out) {
    for (size_t i = 0; i < n; i++) {
        unsigned a = i >= bpp ? row[i - bpp] : 0;
        unsigned b = prior ? prior[i] : 0;
        unsigned c = prior && i >= bpp ? prior[i - bpp] : 0;
        out[i] = (unsigned char)(row[i] - predict(filter, a, b, c));
    }
}

static unsigned long row_cost(const unsigned char *row, size_t n) {
    unsigned long sum = 0;
    for (size_t i = 0; i < n; i++) sum += row[i] < 128 ? row[i] : 256u - row[i];
    return sum;
}

/* One fixed filter for every row, or ADAPTIVE: per row, the filter with the
 * smallest sum of absolute (signed) residuals. scratch holds 5 rows. */
static void filter_image(const Png *png, const Pass *passes, int count, int strategy,
                         const unsigned char *raw, unsigned char *out, unsigned char *scratch) {
    size_t bpp = png->pixel_bytes;
    for (int p = 0; p < count; p++) {
        const Pass *pass = &passes[p];
        if (!pass->width || !pass->height) continue;
        size_t n = pass->row_bytes;
        const unsigned char *prior = NULL;
        for (uint32_t y = 0; y < pass->height; y++) {
            int best = strategy;
            if (strategy == ADAPTIVE) {
                unsigned long best_cost = 0;
                for (int f = 0; f < 5; f++) {
                    filter_row(f, raw, prior, n, bpp, scratch + (size_t)f * n);
                    unsigned long cost = row_cost(scratch + (size_t)f * n, n);
                    if (f == 0 || cost < best_cost) {
                        best = f;
                        best_cost = cost;
                    }
                }
                *out++ = (unsigned char)best;
                memcpy(out, scratch + (size_t)best * n, n);
            } else {
                *out++ = (unsigned char)best;
                filter_row(best, raw, prior, n, bpp, out);
            }
            out += n;
            prior = raw;
            raw += n;
        }
    }
}

/* Deflate src into dst (capacity cap). Returns the compressed size, or 0 when
 * the result would not fit in cap, so a losing strategy stops early. */
static size_t deflate_capped(const unsigned char *src, size_t n, int level, int z_strategy,
                             unsigned char *dst, size_t cap) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (deflateInit2(&z, level, Z_DEFLATED, 15, 9, z_strategy) != Z_OK) return 0;
    z.next_out = dst;
    z.avail_out = (uInt)cap;
    size_t done = 0, result = 0;
    for (;;) {
        size_t slice = n - done < DEFLATE_SLICE ? n - done : DEFLATE_SLICE;
        z.next_in = (unsigned char *)src + done;
        z.avail_in = (uInt)slice;
        int last = done + slice == n;
        int ret = deflate(&z, last ? Z_FINISH : Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            result = cap - z.avail_out;
            break;
        }
        if (ret != Z_OK || z.avail_out == 0 || z.avail_in != 0) break; /* full: too big */
        done += slice;
    }
    deflateEnd(&z);
    return result;
}

static size_t output_size(const Png *png, size_t stream) {
    size_t size = 8 + 25 + sizeof(iend_chunk);
    for (int i = 0; i < png->kept_count; i++) size += png->kept_size[i];
    return size + stream + (stream + IDAT_PIECE - 1) / IDAT_PIECE * 12;
}

static void assemble(const Png *png, const unsigned char *stream, size_t n, unsigned char *out) {
    memcpy(out, signature, 8);
    out += 8;
    memcpy(out, png->ihdr, 25);
    out += 25;
    for (int i = 0; i < png->kept_count; i++) {
        memcpy(out, png->kept[i], png->kept_size[i]);
        out += png->kept_size[i];
    }
    for (size_t at = 0; at < n; at += IDAT_PIECE) {
        size_t piece = n - at < IDAT_PIECE ? n - at : IDAT_PIECE;
        put32(out, (uint32_t)piece);
        memcpy(out + 4, "IDAT", 4);
        memcpy(out + 8, stream + at, piece);
        put32(out + 8 + piece, (uint32_t)crc32(crc32(0L, Z_NULL, 0), out + 4, (uInt)piece + 4));
        out += piece + 12;
    }
    memcpy(out, iend_chunk, sizeof(iend_chunk));
}

PngTrimResult pngtrim(const unsigned char *in, size_t in_len, unsigned char **out,
                      size_t *out_len, char *why, size_t why_size) {
    static const struct { int filter, z_strategy; } trials[] = {
        {ADAPTIVE, Z_DEFAULT_STRATEGY}, {ADAPTIVE, Z_FILTERED}, {4, Z_DEFAULT_STRATEGY},
        {1, Z_DEFAULT_STRATEGY}, {2, Z_DEFAULT_STRATEGY}, {3, Z_DEFAULT_STRATEGY},
        {0, Z_DEFAULT_STRATEGY}};
    PngTrimResult result = PNGTRIM_FAILED;
    Png png, check;
    Pass passes[7];
    int count = 0;
    size_t filtered_len = 0, raw_len = 0, best_len = 0;
    unsigned char *filtered = NULL, *raw = NULL, *scratch = NULL, *best = NULL, *trial = NULL;
    unsigned char *file = NULL, *decoded = NULL;
    *out = NULL;
    *out_len = 0;
    memset(&check, 0, sizeof(check));
    if (in_len > 0xFFFFFFFFu / 2) {
        fail(why, why_size, "file too large");
        memset(&png, 0, sizeof(png));
        goto done;
    }
    if (!parse(in, in_len, &png, why, why_size)) goto done;
    if (png.animated) {
        fail(why, why_size, "animated PNG left unchanged to preserve every frame");
        result = PNGTRIM_SKIPPED;
        goto done;
    }
    if (!layout(&png, passes, &count, &filtered_len, &raw_len)) {
        fail(why, why_size, "image too large");
        goto done;
    }
    size_t widest = 0;
    for (int p = 0; p < count; p++)
        if (passes[p].row_bytes > widest) widest = passes[p].row_bytes;
    filtered = (unsigned char *)malloc(filtered_len);
    raw = (unsigned char *)malloc(raw_len ? raw_len : 1);
    scratch = (unsigned char *)malloc(widest * 5 + 1);
    best = (unsigned char *)malloc(in_len * 2);
    trial = (unsigned char *)malloc(in_len * 2);
    if (!filtered || !raw || !scratch || !best || !trial) {
        fail(why, why_size, "out of memory");
        goto done;
    }
    if (!inflate_exact(png.idat, png.idat_len, filtered, filtered_len) ||
        !unfilter(&png, passes, count, filtered, raw)) {
        fail(why, why_size, "corrupt image data");
        goto done;
    }
    /* Level 9 is up to 20 times slower than level 5 on large photographic
     * images, and a few percent smaller. Rank every strategy at level 5 and
     * compress only the most promising ones at level 9. */
    enum { TRIALS = sizeof(trials) / sizeof(trials[0]) };
    size_t screened[TRIALS];
    for (size_t t = 0; t < TRIALS; t++) {
        filter_image(&png, passes, count, trials[t].filter, raw, filtered, scratch);
        screened[t] = deflate_capped(filtered, filtered_len, SCREEN_LEVEL, trials[t].z_strategy,
                                     trial, in_len * 2);
        if (!screened[t]) screened[t] = SIZE_MAX;
    }
    for (int round = 0; round < FINALISTS; round++) {
        size_t t = 0;
        for (size_t i = 1; i < TRIALS; i++)
            if (screened[i] < screened[t]) t = i;
        if (screened[t] == SIZE_MAX) break;
        screened[t] = SIZE_MAX; /* taken */
        size_t cap = best_len ? best_len - 1 : in_len;
        filter_image(&png, passes, count, trials[t].filter, raw, filtered, scratch);
        size_t n = cap ? deflate_capped(filtered, filtered_len, 9, trials[t].z_strategy, trial, cap)
                       : 0;
        if (n) {
            unsigned char *swap = best;
            best = trial;
            trial = swap;
            best_len = n;
        }
    }
    if (!best_len || output_size(&png, best_len) >= in_len) {
        result = PNGTRIM_NOT_SMALLER;
        goto done;
    }
    size_t file_len = output_size(&png, best_len);
    file = (unsigned char *)malloc(file_len);
    decoded = (unsigned char *)malloc(raw_len ? raw_len : 1);
    if (!file || !decoded) {
        fail(why, why_size, "out of memory");
        goto done;
    }
    assemble(&png, best, best_len, file);
    /* Decode the result again: same header, chunks and samples. */
    int same = parse(file, file_len, &check, NULL, 0) && !check.animated &&
               memcmp(check.ihdr, png.ihdr, 25) == 0 && check.kept_count == png.kept_count &&
               inflate_exact(check.idat, check.idat_len, filtered, filtered_len) &&
               unfilter(&check, passes, count, filtered, decoded);
    for (int i = 0; same && i < png.kept_count; i++)
        same = check.kept_size[i] == png.kept_size[i] &&
               memcmp(check.kept[i], png.kept[i], png.kept_size[i]) == 0;
    if (!same || memcmp(decoded, raw, raw_len) != 0) {
        fail(why, why_size, "verification failed; original kept");
        goto done;
    }
    *out = file;
    *out_len = file_len;
    file = NULL;
    result = PNGTRIM_SMALLER;
done:
    free(decoded);
    free(file);
    free(trial);
    free(best);
    free(scratch);
    free(raw);
    free(filtered);
    free(check.idat);
    free(png.idat);
    return result;
}
