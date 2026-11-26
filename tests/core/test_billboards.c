/* Boards laid out by choice: a board's faces are replaced by new ones over
 * the same rectangle (the picture at its own proportions, tiles, bars), and
 * a sprite takes a new width. */
#include <string.h>

#include "core/wo_billboards.h"
#include "core/wo_texfmt.h"
#include "tex_builder.h"
#include "wo_test.h"

static void put_be16(WoBuf *b, unsigned v) {
    uint8_t bytes[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    wo_buf_append(b, bytes, 2, NULL);
}

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

/* A 64x64 picture of one colour, or of a different colour at every texel. */
static void picture(WoBuf *tim, bool flat) {
    tb_tim16(tim, 64, 64, 1);
    if (flat) {
        for (size_t i = 0; i < 64 * 64; i++) {
            wo_put_le16(tim->data + 20 + i * 2, 0x7fff);
        }
    }
}

/* A track archive: one object, a board 400 wide and 100 high showing the
 * whole picture on one face, and a sprite 300 wide and 100 high. */
static void archive(WoBuf *out, bool flat) {
    WoBuf contents[2] = {{0}, {0}};
    WoBuf *prm = &contents[0];
    uint8_t header[144] = {0};
    memcpy(header, "sign", 4);
    header[17] = 4; /* corners */
    header[33] = 2; /* primitives */
    wo_buf_append(prm, header, sizeof(header), NULL);
    static const int corners[4][3] = {{0, 0, 0}, {400, 0, 0}, {0, 100, 0}, {400, 100, 0}};
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) {
            put_be16(prm, (unsigned)corners[i][k]);
        }
        put_be16(prm, 0);
    }
    put_be16(prm, 8); /* a shaded textured quad */
    put_be16(prm, 0x0016);
    for (unsigned c = 0; c < 4; c++) {
        put_be16(prm, c);
    }
    put_be16(prm, 0); /* texture */
    put_be16(prm, 0x1234); /* palette */
    put_be16(prm, 0x0056); /* page */
    static const uint8_t uv[8] = {0, 0, 63, 0, 0, 63, 63, 63};
    wo_buf_append(prm, uv, sizeof(uv), NULL);
    put_be16(prm, 0);
    for (int c = 0; c < 4; c++) {
        static const uint8_t light[4] = {64, 128, 192, 0x3c};
        wo_buf_append(prm, light, 4, NULL);
    }
    put_be16(prm, 11); /* a sprite */
    put_be16(prm, 0);
    put_be16(prm, 0);   /* its corner */
    put_be16(prm, 300); /* width */
    put_be16(prm, 100); /* height */
    put_be16(prm, 0);   /* texture */
    put_be16(prm, 0x8080);
    put_be16(prm, 0x8000);
    WoBuf tim = {0};
    picture(&tim, flat);
    tb_cmp(&contents[1], &tim, 1);
    static const char *const names[] = {"scene.prm", "scene.cmp"};
    tb_wad(out, names, contents, 2);
    wo_buf_free(&tim);
    wo_buf_free(&contents[0]);
    wo_buf_free(&contents[1]);
}

static WoJson *parse(const char *text) {
    WoJson *json = NULL;
    WoError error = {0};
    CHECK_OK(wo_json_parse(text, strlen(text), &json, &error), &error);
    return json;
}

/* The model file of an archive. */
static const uint8_t *models(const WoBuf *archive, WoWad *wad, size_t *size) {
    WoError error = {0};
    CHECK_OK(wo_wad_parse(archive->data, archive->len, wad, &error), &error);
    const WoWadEntry *entry = wo_wad_find(wad, "scene.prm");
    CHECK(entry != NULL);
    *size = entry ? entry->size : 0;
    return entry ? archive->data + entry->offset : NULL;
}

static int corner_x(const uint8_t *prm, unsigned index) {
    return (int16_t)be16(prm + 144 + index * 8u);
}

/* Fit: the picture square in the middle of the 4:1 board, bars beside it.
 * With a picture of the bars colour, the bars are faces like the board's
 * showing one texel of it; with none, plain faces lit as the board was. */
static void test_fit(void) {
    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[0],\"mode\":\"fit\",\"source\":[0,0,64,64],"
                            "\"stretch\":100,\"scale\":100,\"tiles\":0,\"bars\":\"#ffffff\"}]");
    for (int flat = 0; flat < 2; flat++) {
        WoBuf made = {0};
        archive(&made, flat);
        size_t changed = 0;
        CHECK_OK(wo_billboards_apply(entries, &made, &changed, &error), &error);
        CHECK(changed == 1);
        WoWad wad = {0};
        size_t size = 0;
        const uint8_t *prm = models(&made, &wad, &size);
        if (prm) {
            /* A picture and two bars, sharing corners: the bars' outer ones
             * are the board's, their inner ones the picture's. */
            CHECK(be16(prm + 16) == 4 + 4);
            CHECK(be16(prm + 32) == 1 + 3);
            const uint8_t *p = prm + 144 + 8 * 8;
            CHECK(be16(p) == 11); /* the sprite is kept, first */
            p += 16;
            /* The picture: from 150 to 250 across, the whole picture but a
             * texel at the sides that meet the bars, drawn as the board was. */
            CHECK(be16(p) == 8 && be16(p + 2) == 0x0016);
            CHECK(corner_x(prm, be16(p + 4)) == 150 && corner_x(prm, be16(p + 6)) == 250);
            CHECK(be16(p + 12) == 0 && be16(p + 14) == 0x1234 && be16(p + 16) == 0x0056);
            CHECK(p[18] == 1 && p[20] == 62 && p[19] == 0 && p[23] == 63);
            CHECK(p[28] == 64 && p[29] == 128 && p[30] == 192);
            p += 44;
            for (int bar = 0; bar < 2; bar++) {
                if (flat) {
                    /* One texel of the picture, inside it, with neighbours alike. */
                    CHECK(be16(p) == 8 && p[18] == p[20] && p[19] == p[21] && p[18] == p[22] &&
                          p[18] >= 8 && p[18] <= 55 && p[19] >= 8 && p[19] <= 55);
                    p += 44;
                } else {
                    /* White, lit as the board was: 255 at the board's 128. */
                    CHECK(be16(p) == 7);
                    CHECK(p[12] == 127 && p[13] == 255 && p[14] == 255 && p[15] == 0x3c);
                    p += 28;
                }
            }
        }
        wo_wad_free(&wad);
        wo_buf_free(&made);
    }
    wo_json_free(entries);
}

/* Scaled: the bars above and below are cut where the picture's corners
 * meet them, so no corner lies partway along another face's edge. */
static void test_no_corner_on_an_edge(void) {
    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[0],\"mode\":\"stretch\",\"source\":[0,0,64,64],"
                            "\"stretch\":100,\"scale\":50,\"tiles\":0,\"bars\":\"#123456\"}]");
    WoBuf made = {0};
    archive(&made, false);
    CHECK_OK(wo_billboards_apply(entries, &made, NULL, &error), &error);
    WoWad wad = {0};
    size_t size = 0;
    const uint8_t *prm = models(&made, &wad, &size);
    if (prm) {
        unsigned nv = be16(prm + 16), np = be16(prm + 32);
        CHECK(np == 1 + 1 + 8); /* the picture and a ring of eight bars */
        const uint8_t *p = prm + 144 + nv * 8u + 16;
        for (unsigned f = 0; f + 1 < np; f++) {
            unsigned kind = be16(p);
            int x0 = 1 << 20, x1 = -(1 << 20), y0 = 1 << 20, y1 = -(1 << 20);
            for (int c = 0; c < 4; c++) {
                const uint8_t *v = prm + 144 + be16(p + 4 + c * 2) * 8u;
                int x = (int16_t)be16(v), y = (int16_t)be16(v + 2);
                x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
            }
            for (unsigned v = 0; v < nv; v++) {
                int x = (int16_t)be16(prm + 144 + v * 8u), y = (int16_t)be16(prm + 146 + v * 8u);
                bool on_edge = ((y == y0 || y == y1) && x > x0 && x < x1) ||
                               ((x == x0 || x == x1) && y > y0 && y < y1);
                CHECK(!on_edge);
            }
            p += kind == 8 ? 44 : 28;
        }
    }
    wo_wad_free(&wad);
    wo_buf_free(&made);
    wo_json_free(entries);
}

/* Tile, filling: pictures side by side from the middle, those at the edges
 * cut, showing the part of the picture that falls inside. No bars. */
static void test_tile(void) {
    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[0],\"mode\":\"tile\",\"source\":[0,0,64,64],"
                            "\"stretch\":100,\"scale\":100,\"tiles\":0,\"bars\":\"#000000\"}]");
    WoBuf made = {0};
    archive(&made, false);
    CHECK_OK(wo_billboards_apply(entries, &made, NULL, &error), &error);
    WoWad wad = {0};
    size_t size = 0;
    const uint8_t *prm = models(&made, &wad, &size);
    if (prm) {
        CHECK(be16(prm + 32) == 1 + 5);
        const uint8_t *p = prm + 144 + be16(prm + 16) * 8u + 16;
        int full = 0, left_cut = 0, right_cut = 0;
        for (int i = 0; i < 5; i++, p += 44) {
            int x0 = corner_x(prm, be16(p + 4)), x1 = corner_x(prm, be16(p + 6));
            /* A texel in where tiles meet, not at the board's edges. */
            if (x1 - x0 == 100 && p[18] == 1 && p[20] == 62) {
                full++;
            } else if (x0 == 0 && x1 == 50 && p[18] == 32 && p[20] == 62) {
                left_cut++;
            } else if (x0 == 350 && x1 == 400 && p[18] == 1 && p[20] == 31) {
                right_cut++;
            }
        }
        CHECK(full == 3 && left_cut == 1 && right_cut == 1);
    }
    wo_wad_free(&wad);
    wo_buf_free(&made);
    wo_json_free(entries);
}

/* A sprite takes the picture's proportions at the stretch and scale chosen;
 * a board that is not the archive's is left alone, and so is an archive
 * with nothing to lay out. */
static void test_sprite_and_others(void) {
    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[],\"sprites\":[0],\"mode\":\"stretch\","
                            "\"source\":[0,0,64,64],\"stretch\":150,\"scale\":80,\"tiles\":0},"
                            "{\"object\":0,\"faces\":[5],\"mode\":\"fit\",\"source\":[0,0,64,64],"
                            "\"stretch\":100,\"scale\":100,\"tiles\":0}]");
    WoBuf made = {0};
    archive(&made, false);
    size_t changed = 0;
    CHECK_OK(wo_billboards_apply(entries, &made, &changed, &error), &error);
    CHECK(changed == 1);
    WoWad wad = {0};
    size_t size = 0;
    const uint8_t *prm = models(&made, &wad, &size);
    if (prm) {
        const uint8_t *sprite = prm + 144 + 4 * 8 + 44;
        CHECK(be16(prm + 32) == 2 && be16(sprite) == 11);
        CHECK(be16(sprite + 6) == 120 && be16(sprite + 8) == 80); /* 80 high, 1.5 times as wide */
    }
    wo_wad_free(&wad);
    WoBuf original = {0};
    archive(&original, false);
    WoBuf same = {0};
    archive(&same, false);
    CHECK_OK(wo_billboards_apply(NULL, &same, NULL, &error), &error);
    CHECK(same.len == original.len && memcmp(same.data, original.data, same.len) == 0);
    wo_buf_free(&same);
    wo_buf_free(&original);
    wo_buf_free(&made);
    wo_json_free(entries);
}

int main(void) {
    RUN(test_fit);
    RUN(test_no_corner_on_an_edge);
    RUN(test_tile);
    RUN(test_sprite_and_others);
    return wo_test_finish();
}
