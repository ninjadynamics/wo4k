/* Skies laid out by choice: the picture goes a chosen number of times round
 * the sky from a chosen seam and covers it once from top to bottom; seams
 * only fall between faces. */
#include <math.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_install.h"
#include "core/wo_mod.h"
#include "core/wo_sky.h"
#include "core/wo_texfmt.h"
#include "tex_builder.h"
#include "wo_test.h"

#define COLUMNS 8

static void put_be16(WoBuf *b, int v) {
    uint8_t bytes[2] = {(uint8_t)((unsigned)v >> 8), (uint8_t)v};
    wo_buf_append(b, bytes, 2, NULL);
}

/* A track archive whose sky is a ring of eight columns 45 degrees wide, a
 * little above and below the horizon, capped by a triangle to the top in
 * each column; the 128x64 picture is repeated in every column, as the
 * game's skies have it. */
static void archive(WoBuf *out) {
    WoBuf contents[2] = {{0}, {0}};
    WoBuf *prm = &contents[0];
    uint8_t header[144] = {0};
    memcpy(header, "grid2", 5);
    int vertices = COLUMNS * 2 + 1;
    header[17] = (uint8_t)vertices;
    header[33] = COLUMNS * 2;
    wo_buf_append(prm, header, sizeof(header), NULL);
    for (int ring = 0; ring < 2; ring++) {
        for (int k = 0; k < COLUMNS; k++) {
            double a = k * 6.283185307179586 / COLUMNS;
            put_be16(prm, (int)lround(1000 * cos(a)));
            put_be16(prm, ring ? -100 : 100); /* y is down: ring 1 is the upper */
            put_be16(prm, (int)lround(1000 * sin(a)));
            put_be16(prm, 0);
        }
    }
    put_be16(prm, 0); /* the top */
    put_be16(prm, -1000);
    put_be16(prm, 0);
    put_be16(prm, 0);
    for (int k = 0; k < COLUMNS; k++) {
        int next = (k + 1) % COLUMNS;
        put_be16(prm, 8); /* a shaded textured quad: 0 1 2, 1 3 2 */
        put_be16(prm, 0);
        int corners[4] = {k, COLUMNS + k, next, COLUMNS + next};
        for (int c = 0; c < 4; c++) {
            put_be16(prm, corners[c]);
        }
        put_be16(prm, 0);
        put_be16(prm, 0);
        put_be16(prm, 0);
        static const uint8_t uv[8] = {0, 63, 0, 0, 127, 63, 127, 0};
        wo_buf_append(prm, uv, sizeof(uv), NULL);
        for (int i = 0; i < 9; i++) {
            put_be16(prm, 0);
        }
        put_be16(prm, 6); /* a shaded textured triangle to the top */
        put_be16(prm, 0);
        put_be16(prm, COLUMNS + k);
        put_be16(prm, COLUMNS + next);
        put_be16(prm, vertices - 1);
        put_be16(prm, 0);
        put_be16(prm, 0);
        put_be16(prm, 0);
        static const uint8_t cap[6] = {0, 0, 127, 0, 64, 0};
        wo_buf_append(prm, cap, sizeof(cap), NULL);
        for (int i = 0; i < 7; i++) {
            put_be16(prm, 0);
        }
    }
    WoBuf tim = {0};
    tb_tim16(&tim, 128, 64, 1);
    tb_cmp(&contents[1], &tim, 1);
    static const char *const names[] = {"sky.prm", "sky.cmp"};
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

/* The corner of `sky` in direction `az` (degrees) on ring `upper` of the
 * face whose middle is nearest `middle`. */
static const WoSkyCorner *corner_at(const WoSky *sky, double middle, double az, bool upper) {
    const WoSkyCorner *best = NULL;
    double best_d = 1e9;
    for (size_t f = 0; f < sky->count; f++) {
        const WoSkyFace *face = &sky->faces[f];
        if (face->corners != 4) {
            continue;
        }
        double sum = 0;
        for (int c = 0; c < 4; c++) {
            double d = fmod(face->c[c].az - face->c[0].az + 540.0, 360.0) - 180.0;
            sum += face->c[0].az + d;
        }
        double d = fabs(fmod(sum / 4 - middle + 540.0, 360.0) - 180.0);
        for (int c = 0; c < 4; c++) {
            double da = fabs(fmod(face->c[c].az - az + 540.0, 360.0) - 180.0);
            if (da < 1 && (face->c[c].el > 0) == upper && d < best_d) {
                best_d = d;
                best = &face->c[c];
            }
        }
    }
    return best;
}

static void test_read_and_seams(void) {
    WoBuf made = {0};
    archive(&made);
    WoSky sky;
    WoError error = {0};
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    CHECK(sky.count == COLUMNS * 2);
    CHECK(sky.width == 128 && sky.height == 64);
    CHECK(sky.high > 89 && fabs(sky.low + 5.71) < 0.05);
    double seams[64];
    CHECK(wo_sky_seams(&sky, 1, seams, 64) == COLUMNS);
    CHECK(wo_sky_seams(&sky, 2, seams, 64) == COLUMNS / 2);
    CHECK(seams[0] == 0 && seams[1] == 45 && seams[3] == 135);
    CHECK(wo_sky_seams(&sky, 3, seams, 64) == 0); /* 120 degrees cuts a column */
    CHECK(wo_sky_fits(&sky, 8, 0) && !wo_sky_fits(&sky, 16, 0)); /* not within a face */
    CHECK(!wo_sky_fits(&sky, 1, 20));
    wo_sky_free(&sky);
    wo_buf_free(&made);
}

static void test_once_round(void) {
    WoBuf made = {0};
    archive(&made);
    size_t len = made.len;
    WoJson *layout = parse("{\"around\": 1, \"turn\": 0}");
    WoError error = {0};
    bool changed = false;
    CHECK_OK(wo_sky_apply(layout, &made, &changed, &error), &error);
    CHECK(changed && made.len == len);
    WoSky sky;
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    /* The first column starts the picture and the last ends it, at the same
     * place round the sky: the seam. */
    const WoSkyCorner *start = corner_at(&sky, 22.5, 0, true);
    const WoSkyCorner *end = corner_at(&sky, 337.5, 0, true);
    const WoSkyCorner *quarter = corner_at(&sky, 67.5, 90, false);
    CHECK(start && end && quarter);
    CHECK(start->u == 0 && end->u == 127);
    CHECK(quarter->u == 32); /* a quarter of the way: 127 / 4, rounded */
    /* A corner shared by two faces has one place on the picture. */
    CHECK(corner_at(&sky, 112.5, 90, false)->u == 32);
    /* Top to bottom once: the dome's top is the picture's top row, the lower
     * ring its bottom row. */
    CHECK(quarter->v == 63);
    CHECK(start->v == (uint8_t)lround((90 - 5.71) / (90 + 5.71) * 63));
    for (size_t f = 0; f < sky.count; f++) {
        if (sky.faces[f].corners == 3) {
            CHECK(sky.faces[f].c[2].apex && sky.faces[f].c[2].v == 0);
            /* the top takes the middle of its column */
            CHECK(abs((int)sky.faces[f].c[2].u -
                      ((int)sky.faces[f].c[0].u + (int)sky.faces[f].c[1].u + 1) / 2) <= 1 ||
                  sky.faces[f].c[1].u == 127);
        }
    }
    wo_sky_free(&sky);
    wo_json_free(layout);
    wo_buf_free(&made);
}

static void test_twice_round_turned(void) {
    WoBuf made = {0};
    archive(&made);
    WoJson *layout = parse("{\"around\": 2, \"turn\": 45}");
    WoError error = {0};
    bool changed = false;
    CHECK_OK(wo_sky_apply(layout, &made, &changed, &error), &error);
    CHECK(changed);
    WoSky sky;
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    /* The picture starts at 45 and at 225 degrees, and is half way at 135. */
    CHECK(corner_at(&sky, 67.5, 45, false)->u == 0);
    CHECK(corner_at(&sky, 247.5, 225, false)->u == 0);
    CHECK(corner_at(&sky, 202.5, 225, false)->u == 127);
    CHECK(corner_at(&sky, 112.5, 135, false)->u == 64);
    wo_sky_free(&sky);
    wo_json_free(layout);
    wo_buf_free(&made);
}

/* The picture's top and bottom edges where chosen: here from the top of the
 * dome down to the horizon, the sky below it showing the picture's bottom
 * row. */
static void test_edges(void) {
    WoBuf made = {0};
    archive(&made);
    WoJson *layout = parse("{\"around\": 1, \"turn\": 0, \"top\": 90, \"bottom\": 0}");
    WoError error = {0};
    bool changed = false;
    CHECK_OK(wo_sky_apply(layout, &made, &changed, &error), &error);
    CHECK(changed);
    WoSky sky;
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    CHECK(corner_at(&sky, 22.5, 0, true)->v == (uint8_t)lround((90 - 5.71) / 90 * 63));
    CHECK(corner_at(&sky, 22.5, 0, false)->v == 63); /* below the bottom edge */
    double rows[16];
    CHECK(wo_sky_rows(&sky, rows, 16) == 3);
    CHECK(fabs(rows[0] + 5.7) < 0.05 && fabs(rows[1] - 5.7) < 0.05 && rows[2] > 89);
    wo_sky_free(&sky);
    wo_json_free(layout);
    /* A picture taller than the sky: only its middle is seen. */
    archive(&made);
    made.len = 0;
    archive(&made);
    layout = parse("{\"around\": 1, \"turn\": 0, \"top\": 180, \"bottom\": -180}");
    CHECK_OK(wo_sky_apply(layout, &made, &changed, &error), &error);
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    CHECK(corner_at(&sky, 22.5, 0, false)->v == (uint8_t)lround((180 + 5.71) / 360 * 63));
    wo_sky_free(&sky);
    wo_json_free(layout);
    /* Edges less than a degree apart are not a layout. */
    layout = parse("{\"around\": 1, \"turn\": 0, \"top\": 10, \"bottom\": 9.5}");
    CHECK(!wo_sky_apply(layout, &made, NULL, &error));
    wo_json_free(layout);
    wo_buf_free(&made);
}

/* Laid flat (a photo): the octagon's sides are equally long, so its corners
 * stay where they were round it, and heights become degrees of a circle as
 * long as the outline (8 sides of 2000 sin 22.5 = 6122.9 round: radius
 * 974.5, so the rings at 100 are 5.88 up and down). The picture eight times
 * round fills each side from edge to edge and from ring to ring. */
static void test_flat(void) {
    WoBuf made = {0};
    archive(&made);
    WoSky sky;
    WoError error = {0};
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    wo_sky_flatten(&sky);
    CHECK(fabs(sky.low + 5.88) < 0.02 && sky.high == 89);
    CHECK(corner_at(&sky, 67.5, 90, false) != NULL);
    double seams[64];
    CHECK(wo_sky_seams(&sky, 8, seams, 64) == 1 && fabs(seams[0]) < 0.01);
    wo_sky_free(&sky);
    WoJson *layout = parse("{\"around\": 8, \"turn\": 0, \"top\": 5.88, \"bottom\": -5.88, \"flat\": true}");
    bool changed = false;
    CHECK_OK(wo_sky_apply(layout, &made, &changed, &error), &error);
    CHECK(changed);
    CHECK_OK(wo_sky_read(made.data, made.len, &sky, &error), &error);
    const WoSkyCorner *left = corner_at(&sky, 67.5, 45, true);
    const WoSkyCorner *right = corner_at(&sky, 67.5, 90, false);
    CHECK(left && right);
    CHECK(left->u == 0 && left->v == 0 && right->u == 127 && right->v == 63);
    wo_sky_free(&sky);
    wo_json_free(layout);
    wo_buf_free(&made);
}

static void test_left_alone(void) {
    WoBuf made = {0};
    archive(&made);
    WoBuf before = {0};
    CHECK(wo_buf_append(&before, made.data, made.len, NULL));
    WoError error = {0};
    bool changed = true;
    /* Three times round would cut columns: the sky stays as it is. */
    WoJson *layout = parse("{\"around\": 3, \"turn\": 0}");
    CHECK_OK(wo_sky_apply(layout, &made, &changed, &error), &error);
    CHECK(!changed && made.len == before.len && !memcmp(made.data, before.data, made.len));
    CHECK_OK(wo_sky_apply(NULL, &made, &changed, &error), &error);
    CHECK(!changed);
    wo_json_free(layout);
    /* A layout that is not one fails. */
    layout = parse("{\"around\": 0, \"turn\": 0}");
    CHECK(!wo_sky_apply(layout, &made, NULL, &error));
    wo_json_free(layout);
    layout = parse("{\"around\": 1, \"turn\": 400}");
    CHECK(!wo_sky_apply(layout, &made, NULL, &error));
    wo_json_free(layout);
    /* An archive without a sky is left as it is. */
    WoBuf other = {0};
    WoBuf contents[1] = {{0}};
    wo_buf_append_str(&contents[0], "nothing", NULL);
    static const char *const names[] = {"scene.prm"};
    tb_wad(&other, names, contents, 1);
    layout = parse("{\"around\": 1, \"turn\": 0}");
    CHECK_OK(wo_sky_apply(layout, &other, &changed, &error), &error);
    CHECK(!changed);
    wo_json_free(layout);
    wo_buf_free(&contents[0]);
    wo_buf_free(&other);
    wo_buf_free(&before);
    wo_buf_free(&made);
}

static void write_text(const char *dir, const char *relative, const char *text) {
    char *path = wo_path_join(dir, relative, NULL);
    char *folder = path ? wo_path_parent(path, NULL) : NULL;
    WoError error = {0};
    CHECK_OK(folder && wo_mkdirs(folder, &error) &&
                 wo_write_file_atomic(path, text, strlen(text), &error),
             &error);
    wo_free(folder);
    wo_free(path);
}

/* The plan carries a track's sky layout only while its picture is
 * authored, and loses them all for HD artwork turned off. */
static void test_plan(void) {
    const char *dir = wo_test_dir("sky-plan");
    CHECK(dir != NULL);
    write_text(dir, "manifests/texture-selection.json",
               "{\"scenery\": [{\"track\": \"TRACK01\"}, {\"track\": \"TRACK02\"}]}");
    write_text(dir, "manifests/sky-layout.json",
               "{\"tracks\": {\"TRACK01\": {\"picture\": \"a.png\", \"around\": 1, \"turn\": 0},"
               " \"TRACK02\": {\"picture\": \"b.png\", \"around\": 2, \"turn\": 0}}}");
    write_text(dir, "assets/auth/a.png", "not read");
    WoMod mod;
    WoJson *plan = NULL;
    WoError error = {0};
    CHECK_OK(wo_mod_init(&mod, dir, &error) && wo_load_plan(&mod, &plan, &error), &error);
    const WoJson *scenery = wo_json_get(plan, "scenery");
    const WoJson *sky = wo_json_get(wo_json_at(scenery, 0), "sky");
    CHECK(sky && wo_json_int(sky, "around", 0) == 1);
    CHECK(!wo_json_get(wo_json_at(scenery, 1), "sky")); /* b.png is not authored */
    wo_plan_drop_authored(plan);
    CHECK(!wo_json_get(wo_json_at(scenery, 0), "sky"));
    wo_json_free(plan);
    wo_mod_free(&mod);
}

int main(void) {
    RUN(test_read_and_seams);
    RUN(test_once_round);
    RUN(test_twice_round_turned);
    RUN(test_edges);
    RUN(test_flat);
    RUN(test_left_alone);
    RUN(test_plan);
    return wo_test_finish();
}
