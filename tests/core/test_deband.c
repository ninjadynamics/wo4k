#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../tools/deband/filter.h"
#include "core/wo_deband.h"
#include "core/wo_fs.h"
#include "core/wo_natives.h"
#include "core/wo_png.h"
#include "tex_builder.h"
#include "wo_test.h"

static WoJson *parse(const char *text) {
    WoJson *json = NULL;
    WoError error = {0};
    if (!wo_json_parse(text, strlen(text), &json, &error)) {
        wo_test_fail(__FILE__, __LINE__, "parse", error.message);
    }
    return json;
}

static void write_file(const char *root, const char *relative, const void *data, size_t len) {
    WoError error = {0};
    char *path = wo_path_join(root, relative, &error);
    CHECK_OK(path && wo_write_file(path, data, len, &error), &error);
    wo_free(path);
}

/* --- The filter -------------------------------------------------------------------------- */

/* Two flat bands side by side become a ramp across their step; a step too
 * large to be a band, and whatever is see-through, stay as they are. */
static void test_filter(void) {
    enum { W = 32, H = 4 };
    uint8_t picture[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = picture + (y * W + x) * 4;
            p[0] = p[1] = p[2] = x < W / 2 ? 100 : 110;
            p[3] = 255;
        }
    }
    uint8_t *ramped = deband_band_ramps(picture, W, H, 0.16f, 12, 1.0f);
    CHECK(ramped != NULL);
    if (ramped) {
        /* A band with a neighbour on one side only is a detail, kept at
         * full strength; at none it ramps, half the step at the border. */
        CHECK(!memcmp(ramped, picture, sizeof(picture)));
        free(ramped);
    }
    ramped = deband_band_ramps(picture, W, H, 0.16f, 12, 0.0f);
    CHECK(ramped != NULL);
    if (ramped) {
        CHECK(ramped[(W / 2 - 1) * 4] > 100 && ramped[(W / 2 - 1) * 4] <= 105);
        CHECK(ramped[(W / 2) * 4] < 110 && ramped[(W / 2) * 4] >= 105);
        CHECK(ramped[0] == 100 && ramped[(W - 1) * 4] == 110); /* beyond the reach */
        CHECK(ramped[(W / 2 - 1) * 4] > ramped[(W / 2 - 4) * 4]);
        CHECK(ramped[3] == 255);
        free(ramped);
    }
    for (int y = 0; y < H; y++) {
        for (int x = W / 2; x < W; x++) {
            picture[(y * W + x) * 4] = 200; /* a step of 100 in red: an edge */
        }
    }
    picture[5 * 4 + 3] = 0; /* a hole, with a colour of its own under it */
    picture[5 * 4] = 7;
    ramped = deband_band_ramps(picture, W, H, 0.16f, 12, 0.0f);
    CHECK(ramped != NULL);
    if (ramped) {
        CHECK(ramped[(W / 2 - 1) * 4] == 100 && ramped[(W / 2) * 4] == 200);
        CHECK(ramped[5 * 4] == 7 && ramped[5 * 4 + 3] == 0);
        free(ramped);
    }
    /* The whole filter: the same picture and name give the same result, the
     * hole comes through untouched and nothing visible turns see-through. */
    float *noise = malloc(DEBAND_NOISE_COUNT * sizeof(float));
    CHECK(noise && deband_noise(noise));
    if (noise) {
        DebandOptions options;
        deband_defaults(&options);
        uint8_t *one = deband_picture(picture, W, H, &options, noise, deband_seed("a.png"));
        uint8_t *two = deband_picture(picture, W, H, &options, noise, deband_seed("a.png"));
        CHECK(one && two && !memcmp(one, two, sizeof(picture)));
        if (one) {
            CHECK(one[5 * 4] == 7 && one[5 * 4 + 3] == 0);
            bool solid = true;
            for (int i = 0; i < W * H; i++) {
                solid = solid && (i == 5 || one[i * 4 + 3] == 255);
            }
            CHECK(solid);
        }
        free(one);
        free(two);
        free(noise);
    }
}

/* --- Which frame borders meet ------------------------------------------------------------ */

static void put_be16(WoBuf *b, int value) {
    uint8_t bytes[2] = {(uint8_t)((unsigned)value >> 8), (uint8_t)value};
    wo_buf_append(b, bytes, 2, NULL);
}

/* A textured quad (kind 8) with its four corners' places on the picture. */
static void quad(WoBuf *b, const int *corners, int texture, const uint8_t *uvs) {
    put_be16(b, 8);
    put_be16(b, 0);
    for (int i = 0; i < 4; i++) {
        put_be16(b, corners[i]);
    }
    put_be16(b, texture);
    static const uint8_t pad[18] = {0};
    wo_buf_append(b, pad, 4, NULL);
    wo_buf_append(b, uvs, 8, NULL);
    wo_buf_append(b, pad, 18, NULL);
}

/* A wall of two pictures side by side, two faces high: the right border of
 * picture 0 meets the left border of picture 1 along two edges. A third
 * picture lies over one face of the first. */
static void test_joins(void) {
    static const int16_t points[9][3] = {{0, 0, 0},   {100, 0, 0},   {200, 0, 0},
                                         {0, 100, 0}, {100, 100, 0}, {200, 100, 0},
                                         {0, 200, 0}, {100, 200, 0}, {200, 200, 0}};
    WoBuf models = {0};
    uint8_t head[144] = {0};
    memcpy(head, "wall", 4);
    head[17] = 9; /* corners */
    head[33] = 5; /* faces */
    wo_buf_append(&models, head, sizeof(head), NULL);
    for (int i = 0; i < 9; i++) {
        for (int k = 0; k < 3; k++) {
            put_be16(&models, points[i][k]);
        }
        put_be16(&models, 0);
    }
    /* Corners in the order top left, top right, bottom left, bottom right. */
    static const uint8_t whole[8] = {0, 0, 63, 0, 0, 63, 63, 63};
    static const int upper_left[4] = {0, 1, 3, 4}, upper_right[4] = {1, 2, 4, 5};
    static const int lower_left[4] = {3, 4, 6, 7}, lower_right[4] = {4, 5, 7, 8};
    quad(&models, upper_left, 0, whole);
    quad(&models, upper_right, 1, whole);
    quad(&models, lower_left, 0, whole);
    quad(&models, lower_right, 1, whole);
    quad(&models, upper_left, 2, whole); /* over the first: each of its edges shared, but once */
    uint32_t widths[4] = {64, 64, 64, 0};
    uint32_t heights[4] = {64, 64, 64, 0};
    WoDebandJoin joins[4 * WO_DEBAND_SIDES];
    WoError error = {0};
    CHECK_OK(wo_deband_joins(models.data, models.len, widths, heights, 4, joins, &error), &error);
    /* Picture 0's right border meets picture 1's left one, and the other
     * way about, both running the same way: two edges say so. */
    const WoDebandJoin *right = &joins[0 * WO_DEBAND_SIDES + 1];
    const WoDebandJoin *left = &joins[1 * WO_DEBAND_SIDES + 0];
    CHECK(right->set && right->frame == 1 && right->side == 0 && !right->reversed);
    CHECK(left->set && left->frame == 0 && left->side == 1 && !left->reversed);
    /* Nothing else: one shared edge is not enough to join two borders. */
    size_t set = 0;
    for (size_t i = 0; i < WO_COUNT(joins); i++) {
        set += joins[i].set;
    }
    CHECK(set == 2);
    /* A file cut short gives the faces read so far, never a failure. */
    CHECK_OK(wo_deband_joins(models.data, models.len - 30, widths, heights, 4, joins, &error),
             &error);
    CHECK_OK(wo_deband_joins(models.data, 10, widths, heights, 4, joins, &error), &error);
    wo_buf_free(&models);
}

/* --- The layer --------------------------------------------------------------------------- */

static const char *PLAN =
    "{\"format\":1,\"revision\":3,"
    "\"archives\":[{\"file\":\"COMMON/MINE.CMP\",\"selection\":\"pc_byte_identical\"},"
    "{\"file\":\"TRACK01/LIBRARY.CMP\",\"selection\":\"pc_byte_identical\"}],"
    "\"operations\":["
    "{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":0,\"y\":0,\"width\":32,\"height\":32,"
    "\"source\":{\"wad\":\"TRACK01/TRACK.WAD\",\"entry\":\"library.cmp\",\"table\":\"library.ttf\","
    "\"material\":1,\"level\":32},\"label\":\"road\"},"
    "{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":32,\"y\":0,\"width\":8,\"height\":4,"
    "\"source\":{\"path\":\"COMMON/MINE.CMP\",\"frame\":0},\"label\":\"mine\"},"
    "{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":36,\"y\":2,\"width\":8,\"height\":4,"
    "\"source\":{\"path\":\"COMMON/MINE.CMP\",\"frame\":0},\"label\":\"mine again, over the first\"},"
    "{\"atlas\":\"Pics/Lo-res/SHEET.TGA\",\"x\":32,\"y\":0,\"width\":8,\"height\":4,"
    "\"source\":{\"path\":\"COMMON/MINE.CMP\",\"frame\":0},\"label\":\"a sheet the game does not load\"}]}";

#define CHOICES                                                                                   "{\"format\":\"wo4k.deband-selection\",\"version\":1,\"choices\":["                              "{\"id\":\"TRACK01/LIBRARY.CMP#0000\",\"file\":\"track01-library-cmp-0.png\","                    "\"version\":\"original\"},"                                                                   "{\"id\":\"COMMON/MINE.CMP#0000\",\"file\":\"common-mine-cmp-0.png\",\"version\":\"%s\"},"      "{\"id\":\"TRACK01/LIBRARY.CMP#0001\",\"file\":\"track01-library-cmp-1.png\","                    "\"version\":\"v2\"}]}"

/* Game folder with a 64x32 sheet in both sizes, a track archive with two
 * road tiles (material 1 at 32 pixels is tile 1) and a shared texture. */
static void build_game(const char *game) {
    WoBuf sheet = {0};
    tb_tga(&sheet, 64, 32, 0x1234);
    write_file(game, "textures/sd/Pics/Hi-res/SHEET.TGA", sheet.data, sheet.len);
    write_file(game, "textures/sd/Pics/Lo-res/SHEET.TGA", sheet.data, sheet.len);
    wo_buf_free(&sheet);
    WoBuf tiles[2] = {{0}, {0}};
    tb_tim16(&tiles[0], 32, 32, 100);
    tb_tim16(&tiles[1], 32, 32, 5000);
    WoBuf contents[2] = {{0}, {0}};
    tb_cmp(&contents[0], tiles, 2);
    wo_buf_reserve(&contents[1], 84, NULL);
    memset(contents[1].data, 0, 84);
    contents[1].len = 84;
    contents[1].data[42 + 41] = 1;
    static const char *const names[] = {"library.cmp", "library.ttf"};
    WoBuf wad = {0};
    tb_wad(&wad, names, contents, 2);
    write_file(game, "TRACK01/TRACK.WAD", wad.data, wad.len);
    wo_buf_free(&wad);
    for (int i = 0; i < 2; i++) {
        wo_buf_free(&tiles[i]);
        wo_buf_free(&contents[i]);
    }
    WoBuf mine_tim = {0};
    WoBuf mine = {0};
    tb_tim16(&mine_tim, 8, 4, 7);
    tb_cmp(&mine, &mine_tim, 1);
    write_file(game, "textures/sd/COMMON/MINE.CMP", mine.data, mine.len);
    wo_buf_free(&mine);
    wo_buf_free(&mine_tim);
}

static void write_choices(const char *path, const char *mine) {
    char text[1024];
    int length = snprintf(text, sizeof(text), CHOICES, mine);
    WoError error = {0};
    CHECK_OK(wo_write_file(path, text, (size_t)length, &error), &error);
}

/* The index is hd-regions.bin's: 8 bytes, a count, records of 312 bytes. */
static bool index_has(const WoBuf *index, const char *png, uint32_t page, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height) {
    for (size_t i = 0; 12 + (i + 1) * 312 <= index->len; i++) {
        const uint8_t *record = index->data + 12 + i * 312;
        size_t n = 0;
        while (png[n] && record[n * 2] == (uint8_t)png[n] && !record[n * 2 + 1]) {
            n++;
        }
        if (!png[n] && !record[n * 2] && !strcmp((const char *)record + 256, "sheet.tga") &&
            wo_le32(record + 288) == page && wo_le32(record + 292) == x &&
            wo_le32(record + 296) == y && wo_le32(record + 300) == width &&
            wo_le32(record + 304) == height) {
            return true;
        }
    }
    return false;
}

static void test_layer(void) {
    const char *root = wo_test_dir("deband-layer");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *game = wo_path_join(root, "game", &error);
    char *output = wo_path_join(root, "assets/sd", &error);
    char *selection = wo_path_join(root, "choices.json", &error);
    char *road = wo_path_join(output, "track01-road-1-32.png", &error);
    char *mine = wo_path_join(output, "common-mine-cmp-0.png", &error);
    char *index_path = wo_path_join(game, "textures/sd-regions.bin", &error);
    build_game(game);
    WoJson *plan = parse(PLAN);
    WoNativeOptions natives = {game, plan, NULL, NULL, NULL, false, false, NULL, false};
    WoJson *receipt = NULL;
    CHECK_OK(wo_natives_install(&natives, &receipt, &error), &error);
    WoDebandOptions options = {game, output, selection, plan, receipt, false, NULL};
    WoDebandResult result;

    /* Without the choices nothing is made. */
    CHECK_OK(wo_deband_install(&options, &result, &error), &error);
    CHECK(!result.built && !wo_exists(output) && !wo_exists(index_path));

    /* The road is one picture of its tiles; the shared texture is debanded
     * once and placed where the sheet shows all of it: its first place has
     * the second pasted over a corner. The Lo-res sheet is not the game's. */
    write_choices(selection, "v1");
    CHECK_OK(wo_deband_install(&options, &result, &error), &error);
    CHECK(result.built && result.pictures == 2 && result.regions == 2);
    uint32_t width = 0;
    uint32_t height = 0;
    CHECK_OK(wo_png_read_size(road, &width, &height, &error), &error);
    CHECK(width == 32 && height == 32);
    CHECK_OK(wo_png_read_size(mine, &width, &height, &error), &error);
    CHECK(width == 8 && height == 4);
    WoBuf index = {0};
    CHECK_OK(wo_read_file(index_path, 1 << 20, &index, &error), &error);
    CHECK(index.len == 12 + 2 * 312 && !memcmp(index.data, "WOHDREG1", 8));
    CHECK(index_has(&index, "track01-road-1-32.png", 0, 0, 0, 32, 32));
    CHECK(index_has(&index, "common-mine-cmp-0.png", 0, 36, 2, 8, 4));
    CHECK(!index_has(&index, "common-mine-cmp-0.png", 0, 32, 0, 8, 4));
    wo_buf_free(&index);
    /* Every texel of the fixture is solid, and stays so. */
    WoImage image;
    CHECK_OK(wo_png_read(mine, WO_PIXELS_RGBA, &image, &error), &error);
    bool solid = image.pixels != NULL;
    for (uint32_t i = 0; solid && i < image.width * image.height; i++) {
        solid = image.pixels[i * 4 + 3] == 255;
    }
    CHECK(solid);
    wo_image_free(&image);

    /* Made from the same things, it is left alone: a picture deleted since
     * stays deleted. */
    CHECK_OK(wo_delete_file(mine, &error), &error);
    CHECK_OK(wo_deband_install(&options, &result, &error), &error);
    CHECK(!result.built && !wo_exists(mine) && wo_is_file(road));
    options.force = true;
    CHECK_OK(wo_deband_install(&options, &result, &error), &error);
    CHECK(result.built && wo_is_file(mine));
    options.force = false;

    /* Other choices: built again, and a picture no longer chosen goes. */
    write_choices(selection, "original");
    CHECK_OK(wo_deband_install(&options, &result, &error), &error);
    CHECK(result.built && result.pictures == 1 && result.regions == 1);
    CHECK(!wo_exists(mine) && wo_is_file(road));

    /* Choices of a form this does not know are refused, and the layer in
     * place stays as it was. */
    CHECK_OK(wo_write_file(selection, "{\"format\":\"other\"}", 18, &error), &error);
    CHECK_FAILS(wo_deband_install(&options, &result, &error), &error, WO_ERR_FORMAT);
    CHECK(wo_is_file(road) && wo_is_file(index_path));

    wo_json_free(receipt);
    wo_json_free(plan);
    wo_free(index_path);
    wo_free(mine);
    wo_free(road);
    wo_free(selection);
    wo_free(output);
    wo_free(game);
}

int main(void) {
    RUN(test_filter);
    RUN(test_joins);
    RUN(test_layer);
    return wo_test_finish();
}
