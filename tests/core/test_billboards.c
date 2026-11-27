/* Boards laid out by choice: a board's faces are replaced by new ones over
 * the same rectangle (the picture at its own proportions, tiles, bars), and
 * a sprite takes a new width. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_billboards.h"
#include "core/wo_png.h"
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
        CHECK_OK(wo_billboards_apply(entries, NULL, &made, &changed, &error), &error);
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
    CHECK_OK(wo_billboards_apply(entries, NULL, &made, NULL, &error), &error);
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
    CHECK_OK(wo_billboards_apply(entries, NULL, &made, NULL, &error), &error);
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
    CHECK_OK(wo_billboards_apply(entries, NULL, &made, &changed, &error), &error);
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
    CHECK_OK(wo_billboards_apply(NULL, NULL, &same, NULL, &error), &error);
    CHECK(same.len == original.len && memcmp(same.data, original.data, same.len) == 0);
    wo_buf_free(&same);
    wo_buf_free(&original);
    wo_buf_free(&made);
    wo_json_free(entries);
}

/* A shown picture of 128x128, white but for a black square over the
 * texture's texels 4 to 59 across and down (unless `dark`: black all over). */
static const char *shown_png(const char *dir, bool dark) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, dark ? "dark" : "framed");
    WoImage image = {128, 128, NULL};
    image.pixels = calloc(128u * 128u, 4);
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            bool black = dark || (x >= 8 && x < 120 && y >= 8 && y < 120);
            uint8_t *px = image.pixels + ((size_t)y * 128 + (size_t)x) * 4;
            px[0] = px[1] = px[2] = black ? 0 : 255;
            px[3] = 255;
        }
    }
    WoError error = {0};
    CHECK_OK(wo_png_write(path, &image, WO_PIXELS_BGRA, &error), &error);
    free(image.pixels);
    return path;
}

/* The bars' texel is found in the picture the game shows in the texture's
 * place: an archive picture that is white everywhere but a shown one that
 * is black gives plain bars; a shown one white only round its edge gives a
 * texel there, though the archive's picture has no white at all. */
static void test_shown_picture(void) {
    const char *dir = wo_test_dir("billboards-shown");
    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[0],\"mode\":\"fit\",\"source\":[0,0,64,64],"
                            "\"stretch\":100,\"scale\":100,\"tiles\":0,\"bars\":\"#ffffff\"}]");
    for (int framed = 0; framed < 2; framed++) {
        WoJson *shown = wo_json_new_object();
        CHECK(wo_json_put_str(shown, "0", shown_png(dir, !framed)));
        WoBuf made = {0};
        archive(&made, !framed); /* flat: white everywhere */
        CHECK_OK(wo_billboards_apply(entries, shown, &made, NULL, &error), &error);
        WoWad wad = {0};
        size_t size = 0;
        const uint8_t *prm = models(&made, &wad, &size);
        if (prm) {
            const uint8_t *bar = prm + 144 + 8 * 8 + 16 + 44;
            if (framed) {
                /* A texel round the edge, where the shown picture is white. */
                CHECK(be16(bar) == 8);
                int u = bar[18], v = bar[19];
                CHECK(u < 4 || u > 59 || v < 4 || v > 59);
            } else {
                CHECK(be16(bar) == 7);
            }
        }
        wo_wad_free(&wad);
        wo_buf_free(&made);
        wo_json_free(shown);
    }
    wo_json_free(entries);
}

/* The new faces are shaded as the board was where they lie: a board dark
 * at its left edge and bright at its right gives the picture in its middle
 * (150 to 250 of 400 across) the shades a quarter and more of the way
 * across, and the bars theirs, so neighbours meet without a step. */
static void test_shading_follows_the_board(void) {
    WoBuf made = {0};
    archive(&made, true);
    WoWad wad = {0};
    size_t size = 0;
    const uint8_t *found = models(&made, &wad, &size);
    if (found) {
        uint8_t *colours = made.data + (found - made.data) + 144 + 4 * 8 + 4 + 8 + 6 + 8 + 2;
        for (int c = 0; c < 4; c++) {
            colours[c * 4] = (c == 1 || c == 3) ? 200 : 0; /* the right-hand corners */
        }
    }
    wo_wad_free(&wad);
    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[0],\"mode\":\"fit\",\"source\":[0,0,64,64],"
                            "\"stretch\":100,\"scale\":100,\"tiles\":0,\"bars\":\"#ffffff\"}]");
    CHECK_OK(wo_billboards_apply(entries, NULL, &made, NULL, &error), &error);
    const uint8_t *out = models(&made, &wad, &size);
    if (out) {
        const uint8_t *picture = out + 144 + 8 * 8 + 16;
        const uint8_t *shade = picture + 4 + 8 + 6 + 8 + 2;
        CHECK(shade[0] == 75 && shade[4] == 125 && shade[8] == 75 && shade[12] == 125);
        const uint8_t *bar = picture + 44; /* the left one: from the edge to the picture */
        const uint8_t *bar_shade = bar + 4 + 8 + 6 + 8 + 2;
        CHECK(bar_shade[0] == 0 && bar_shade[4] == 75);
    }
    wo_wad_free(&wad);
    wo_json_free(entries);
    wo_buf_free(&made);
}

/* A tiled board made of shaded textured triangles (like Valparaiso's
 * three-panel overhead boards, whose UVs crop the picture to 3..59, 3..57):
 * the triangles are replaced by quads over the full board rectangle showing
 * the whole picture, with shading kept. */
static void test_tiled_triangles(void) {
    WoBuf contents[2] = {{0}, {0}};
    WoBuf *prm = &contents[0];
    uint8_t header[144] = {0};
    memcpy(header, "cube23", 6);
    header[17] = 8; /* four columns, top and bottom */
    header[33] = 6; /* six triangles: three panels */
    wo_buf_append(prm, header, sizeof(header), NULL);
    /* Width 360 (three 120-wide panels), height 100 -> aspect 3.6. */
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 2; row++) {
            put_be16(prm, (unsigned)(col * 120));
            put_be16(prm, (unsigned)(row * 100));
            put_be16(prm, 0);
            put_be16(prm, 0);
        }
    }
    for (unsigned panel = 0; panel < 3; panel++) {
        unsigned tl = panel * 2, bl = tl + 1, tr = tl + 2, br = tl + 3;
        static const uint8_t light[4] = {124, 121, 121, 0x3c};
        /* Upper-right triangle (tl, tr, br) and lower-left (br, bl, tl). */
        unsigned tris[2][3] = {{tl, tr, br}, {br, bl, tl}};
        uint8_t uvs[2][6] = {{3, 3, 59, 3, 59, 57}, {59, 57, 3, 57, 3, 3}};
        for (int t = 0; t < 2; t++) {
            put_be16(prm, 6); /* shaded textured triangle */
            put_be16(prm, 0x0001);
            for (int c = 0; c < 3; c++) {
                put_be16(prm, tris[t][c]);
            }
            put_be16(prm, 0);
            put_be16(prm, 0x0000);
            put_be16(prm, 0x1008);
            wo_buf_append(prm, uvs[t], 6, NULL);
            put_be16(prm, 0);
            for (int c = 0; c < 3; c++) {
                wo_buf_append(prm, light, 4, NULL);
            }
        }
    }
    WoBuf tim = {0};
    picture(&tim, true);
    tb_cmp(&contents[1], &tim, 1);
    static const char *const names[] = {"scene.prm", "scene.cmp"};
    WoBuf made = {0};
    tb_wad(&made, names, contents, 2);
    wo_buf_free(&tim);
    wo_buf_free(&contents[0]);
    wo_buf_free(&contents[1]);

    WoError error = {0};
    WoJson *entries = parse("[{\"object\":0,\"faces\":[0,1,2,3,4,5],\"mode\":\"tile\","
                            "\"source\":[0,0,64,64],\"stretch\":100,\"scale\":100,"
                            "\"tiles\":3,\"bars\":\"#ffffff\"}]");
    size_t changed = 0;
    CHECK_OK(wo_billboards_apply(entries, NULL, &made, &changed, &error), &error);
    CHECK(changed == 1);
    WoWad wad = {0};
    size_t size = 0;
    const uint8_t *out = models(&made, &wad, &size);
    if (out) {
        CHECK(be16(out + 32) == 5); /* three 100x100 tiles and two 30-wide bars */
        const uint8_t *p = out + 144 + be16(out + 16) * 8u;
        for (int i = 0; i < 3; i++, p += 44) {
            CHECK(be16(p) == 8 && be16(p + 2) == 0x0001);
            CHECK(corner_x(out, be16(p + 4)) == 30 + i * 100);
            CHECK(corner_x(out, be16(p + 6)) == 130 + i * 100);
            CHECK(p[18] == 1 && p[19] == 0 && p[20] == 62 && p[23] == 63);
            CHECK(p[28] == 124 && p[29] == 121 && p[30] == 121);
        }
        /* Two side bars, 0..30 and 330..360. */
        CHECK(be16(p) == 8 && corner_x(out, be16(p + 4)) == 0 && corner_x(out, be16(p + 6)) == 30);
        p += 44;
        CHECK(be16(p) == 8 && corner_x(out, be16(p + 4)) == 330 &&
              corner_x(out, be16(p + 6)) == 360);
    }
    wo_wad_free(&wad);
    wo_buf_free(&made);
    wo_json_free(entries);
}

int main(void) {
    RUN(test_fit);
    RUN(test_no_corner_on_an_edge);
    RUN(test_tile);
    RUN(test_sprite_and_others);
    RUN(test_shown_picture);
    RUN(test_shading_follows_the_board);
    RUN(test_tiled_triangles);
    return wo_test_finish();
}
