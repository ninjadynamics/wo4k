/* Neural texture upscaling: the network against its author's library, the
 * filter that makes a texture what the network expects, and a texture's
 * see-through texels. */
#include <string.h>

#include "core/wo_upscale.h"
#include "wo_test.h"

static uint64_t digest(const uint8_t *bytes, size_t count) {
    uint64_t hash = 1469598103934665603ull;
    while (count--) {
        hash ^= *bytes++;
        hash *= 1099511628211ull;
    }
    return hash;
}

/* A made-up picture, and the digest of what neural_upsampler's own
 * jpegc_up2x_rgba (BEST, strength 50; commit c33a4c6) makes of it. */
static void reference(uint32_t width, uint32_t height, uint32_t seed, uint64_t expected,
                      const uint8_t first[4]) {
    size_t bytes = (size_t)width * height * 4;
    uint8_t *in = wo_alloc(bytes, NULL);
    uint8_t *out = wo_alloc(bytes * 4, NULL);
    CHECK(in && out);
    if (in && out) {
        uint32_t state = seed;
        for (size_t i = 0; i < bytes; i++) {
            state = state * 1664525u + 1013904223u;
            in[i] = (uint8_t)(state >> 24);
        }
        wo_upscale_double(in, width, height, out);
        CHECK(memcmp(out, first, 4) == 0);
        CHECK(digest(out, bytes * 4) == expected);
    }
    wo_free(out);
    wo_free(in);
}

/* The network in C gives the library's bytes: with one thread's worth of
 * rows, with rows shared among threads, and where the window hangs over
 * every edge. */
static void test_network_matches_library(void) {
    static const uint8_t a[4] = {42, 98, 124, 180};
    static const uint8_t b[4] = {0, 106, 14, 189};
    static const uint8_t c[4] = {25, 120, 219, 199};
    reference(11, 9, 1, 0xd854c0c2b1c731e2ull, a);
    reference(5, 3, 2, 0x24e3d8fa830567e2ull, b);
    reference(40, 33, 3, 0x8a97ba65ed96c2f6ull, c);
}

static void fill(uint8_t *rgba, size_t texels, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (size_t i = 0; i < texels; i++) {
        rgba[i * 4] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }
}

/* The filter leaves a flat colour as it is and spreads one bright texel
 * evenly to its four sides: about an eighth of its light to each, which
 * shows brighter than an eighth because the filter works in linear light.
 * Alpha is not touched. */
static void test_prefilter(void) {
    WoError error = {0};
    uint8_t flat[7 * 5 * 4];
    fill(flat, 7 * 5, 200, 90, 15, 77);
    CHECK_OK(wo_upscale_prefilter(flat, 7, 5, &error), &error);
    for (size_t i = 0; i < 7 * 5; i++) {
        CHECK(flat[i * 4] == 200 && flat[i * 4 + 1] == 90 && flat[i * 4 + 2] == 15 &&
              flat[i * 4 + 3] == 77);
    }
    uint8_t spot[7 * 7 * 4];
    fill(spot, 7 * 7, 0, 0, 0, 255);
    spot[(3 * 7 + 3) * 4 + 1] = 255; /* green, in the middle */
    CHECK_OK(wo_upscale_prefilter(spot, 7, 7, &error), &error);
    uint8_t centre = spot[(3 * 7 + 3) * 4 + 1];
    uint8_t left = spot[(3 * 7 + 2) * 4 + 1];
    uint8_t right = spot[(3 * 7 + 4) * 4 + 1];
    uint8_t above = spot[(2 * 7 + 3) * 4 + 1];
    uint8_t below = spot[(4 * 7 + 3) * 4 + 1];
    uint8_t corner = spot[(2 * 7 + 2) * 4 + 1];
    CHECK(centre > 200 && centre < 255);
    CHECK(left == right && left == above && left == below);
    CHECK(left > 255 / 8 && left < centre / 2);
    CHECK(corner > 0 && corner < left);
    CHECK(spot[(3 * 7 + 3) * 4] == 0 && spot[(3 * 7 + 3) * 4 + 2] == 0); /* the other colours stay dark */
    CHECK(spot[0] == 0 && spot[1] == 0 && spot[3] == 255);                /* far away nothing changes */
}

/* A solid texture comes out solid, twice or four times the size, and a flat
 * colour stays that colour. Other scales and empty pictures are refused. */
static void test_sizes(void) {
    WoError error = {0};
    uint8_t rgba[12 * 10 * 4];
    fill(rgba, 12 * 10, 30, 160, 220, 255);
    for (unsigned scale = 2; scale <= 4; scale += 2) {
        WoImage out;
        CHECK_OK(wo_upscale_texture(rgba, 12, 10, scale, &out, &error), &error);
        CHECK(out.width == 12 * scale && out.height == 10 * scale);
        bool same = out.pixels != NULL;
        for (size_t i = 0; same && i < (size_t)out.width * out.height; i++) {
            same = out.pixels[i * 4] == 30 && out.pixels[i * 4 + 1] == 160 &&
                   out.pixels[i * 4 + 2] == 220 && out.pixels[i * 4 + 3] == 255;
        }
        CHECK(same);
        wo_image_free(&out);
    }
    WoImage none;
    CHECK_FAILS(wo_upscale_texture(rgba, 12, 10, 3, &none, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_upscale_texture(rgba, 0, 10, 2, &none, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_upscale_texture(NULL, 12, 10, 2, &none, &error), &error, WO_ERR_FORMAT);
    CHECK(none.pixels == NULL);
}

/* A texture red on its left and see-through on its right, where its texels
 * are black underneath: upscaled, no black is drawn into the edge. The
 * colour is red all the way across, and the cut-out goes from solid to
 * clear across the middle and nowhere else. */
static void test_cut_out(void) {
    WoError error = {0};
    enum { SIDE = 16 };
    uint8_t rgba[SIDE * SIDE * 4];
    for (size_t y = 0; y < SIDE; y++) {
        for (size_t x = 0; x < SIDE; x++) {
            uint8_t *texel = rgba + (y * SIDE + x) * 4;
            bool shown = x < SIDE / 2;
            texel[0] = shown ? 220 : 0;
            texel[1] = shown ? 40 : 0;
            texel[2] = shown ? 30 : 0;
            texel[3] = shown ? 255 : 0;
        }
    }
    WoImage out;
    CHECK_OK(wo_upscale_texture(rgba, SIDE, SIDE, 4, &out, &error), &error);
    CHECK(out.width == SIDE * 4 && out.height == SIDE * 4);
    if (out.pixels) {
        const uint8_t *row = out.pixels + (size_t)(SIDE * 2) * out.width * 4; /* a row in the middle */
        bool red = true;
        /* Eight rounds of filling reach eight texels: the whole clear half here. */
        for (size_t x = 0; x < out.width; x++) {
            red = red && row[x * 4] > 200 && row[x * 4 + 1] < 60 && row[x * 4 + 2] < 50;
        }
        CHECK(red);
        CHECK(row[3] == 255 && row[(SIDE * 2 - 12) * 4 + 3] == 255);
        CHECK(row[(SIDE * 2 + 12) * 4 + 3] == 0 && row[(out.width - 1) * 4 + 3] == 0);
        bool falling = true;
        for (size_t x = SIDE * 2 - 12; x < SIDE * 2 + 12; x++) {
            falling = falling && row[(x + 1) * 4 + 3] <= row[x * 4 + 3];
        }
        CHECK(falling);
        /* Half way between solid and clear lies at the texture's own edge. */
        CHECK(row[(SIDE * 2 - 2) * 4 + 3] > 128 && row[(SIDE * 2 + 1) * 4 + 3] < 128);
    }
    wo_image_free(&out);
}

int main(void) {
    RUN(test_network_matches_library);
    RUN(test_prefilter);
    RUN(test_sizes);
    RUN(test_cut_out);
    return wo_test_finish();
}
