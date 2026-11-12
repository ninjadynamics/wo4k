#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hdtex.h"
#include "core/wo_png.h"
#include "tex_builder.h"
#include "wo_test.h"

#define FONT_AND_SCENE_A                                                                         \
    "{\"file\":\"font.png\",\"kind\":\"atlas\",\"native\":\"hud2.tga\",\"required\":true},"     \
    "{\"file\":\"scene-a.png\",\"kind\":\"region\",\"source_width\":64,\"source_height\":32,"   \
    "\"regions\":[{\"native\":\"nrace01.tga\",\"page\":7,\"x\":0,\"y\":128,\"width\":64,"         \
    "\"height\":32},{\"native\":\"nrace01.tga\",\"page\":2,\"x\":192,\"y\":224,\"width\":64,"     \
    "\"height\":32}]}"

static const char *CATALOG =
    "[" FONT_AND_SCENE_A ","
    "{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":16,\"source_height\":16,"
    "\"alpha_mode\":\"black-key\",\"regions\":[{\"native\":\"sky01.tga\",\"page\":0,\"x\":0,"
    "\"y\":0,\"width\":16,\"height\":16}]},"
    "{\"file\":\"shot.png\",\"kind\":\"reference\",\"reason\":\"Just a screenshot.\"}]";

static WoJson *parse(const char *text) {
    WoJson *json = NULL;
    WoError error = {0};
    if (!wo_json_parse(text, strlen(text), &json, &error)) {
        wo_test_fail(__FILE__, __LINE__, "parse", error.message);
    }
    return json;
}

/* A width x height image; alpha 255 unless transparent. */
static void write_png(const char *root, const char *relative, uint32_t width, uint32_t height,
                      bool transparent) {
    WoError error = {0};
    WoImage image = {width, height, wo_alloc((size_t)width * height * 4, NULL)};
    for (size_t i = 0; i < (size_t)width * height; i++) {
        image.pixels[i * 4] = (uint8_t)i;
        image.pixels[i * 4 + 1] = (uint8_t)(i >> 3);
        image.pixels[i * 4 + 2] = 200;
        image.pixels[i * 4 + 3] = (transparent && i % 3 == 0) ? 40 : 255;
    }
    char *path = wo_path_join(root, relative, &error);
    CHECK_OK(path && wo_png_write(path, &image, WO_PIXELS_RGBA, &error), &error);
    wo_free(path);
    wo_image_free(&image);
}

static void write_tga_header(const char *pics, const char *relative, uint16_t width,
                             uint16_t height) {
    uint8_t header[18] = {0};
    header[2] = 2;
    wo_put_le16(header + 12, width);
    wo_put_le16(header + 14, height);
    header[16] = 16;
    WoError error = {0};
    char *path = wo_path_join(pics, relative, &error);
    CHECK_OK(path && wo_write_file(path, header, sizeof(header), &error), &error);
    wo_free(path);
}

static void test_png_round_trip(void) {
    const char *dir = wo_test_dir("hd-png");
    if (!dir) {
        return;
    }
    write_png(dir, "a.png", 7, 5, true);
    WoError error = {0};
    char *path = wo_path_join(dir, "a.png", &error);
    WoImage rgba = {0};
    WoImage bgra = {0};
    CHECK_OK(wo_png_read(path, WO_PIXELS_RGBA, &rgba, &error), &error);
    CHECK_OK(wo_png_read(path, WO_PIXELS_BGRA, &bgra, &error), &error);
    CHECK(rgba.width == 7 && rgba.height == 5);
    CHECK(rgba.pixels[2] == 200 && rgba.pixels[3] == 40 && rgba.pixels[4 * 4 + 3] == 255);
    CHECK(bgra.pixels[0] == 200 && bgra.pixels[2] == rgba.pixels[0]);
    CHECK(rgba.pixels[(7 * 5 - 1) * 4] == (uint8_t)(7 * 5 - 1));
    uint32_t w = 0;
    uint32_t h = 0;
    CHECK_OK(wo_png_read_size(path, &w, &h, &error), &error);
    CHECK(w == 7 && h == 5);
    bool transparent = false;
    CHECK_OK(wo_png_has_transparency(path, &transparent, &error), &error);
    CHECK(transparent);
    wo_image_free(&rgba);
    wo_image_free(&bgra);
    write_png(dir, "b.png", 3, 3, false);
    char *opaque = wo_path_join(dir, "b.png", &error);
    CHECK_OK(wo_png_has_transparency(opaque, &transparent, &error), &error);
    CHECK(!transparent);
    /* Not an image, missing, and impossible sizes. */
    char *bad = wo_path_join(dir, "bad.png", &error);
    CHECK_OK(wo_write_file(bad, "not a png at all", 16, &error), &error);
    CHECK_FAILS(wo_png_read(bad, WO_PIXELS_BGRA, &rgba, &error), &error, WO_ERR_FORMAT);
    char *missing = wo_path_join(dir, "missing.png", &error);
    CHECK_FAILS(wo_png_read(missing, WO_PIXELS_BGRA, &rgba, &error), &error, WO_ERR_NOT_FOUND);
    uint8_t pixel[4] = {0};
    WoImage empty = {0, 1, pixel};
    CHECK_FAILS(wo_png_write(missing, &empty, WO_PIXELS_RGBA, &error), &error, WO_ERR_LIMIT);
    WoImage huge = {WO_PNG_MAX_SIDE + 1, 1, pixel};
    CHECK_FAILS(wo_png_write(missing, &huge, WO_PIXELS_RGBA, &error), &error, WO_ERR_LIMIT);
    CHECK(!wo_exists(missing));
    wo_free(missing);
    wo_free(bad);
    wo_free(opaque);
    wo_free(path);
}

typedef struct HdFixture {
    const char *root;
    char *assets;
    char *extra;
    char *game;
    char *index;
} HdFixture;

static bool setup_fixture(HdFixture *f, const char *name) {
    memset(f, 0, sizeof(*f));
    f->root = wo_test_dir(name);
    if (!f->root) {
        return false;
    }
    f->assets = wo_path_join(f->root, "assets/hd", NULL);
    f->extra = wo_path_join(f->root, "extra", NULL);
    f->game = wo_path_join(f->root, "game", NULL);
    f->index = wo_path_join(f->game, "textures/hd-regions.bin", NULL);
    char *pics = wo_path_join(f->game, "textures/sd/Pics", NULL);
    write_tga_header(pics, "Hi-res/HUD2.TGA", 768, 256);
    write_tga_header(pics, "Lo-res/HUD2.TGA", 256, 256);
    write_tga_header(pics, "Hi-res/SKY01.TGA", 256, 256);
    write_tga_header(pics, "Hi-res/NRACE01.TGA", 2048, 256);
    write_tga_header(pics, "Hi-res/STRIP.TGA", 100, 256); /* not page-shaped */
    wo_free(pics);
    write_png(f->assets, "font.png", 8, 8, false);
    write_png(f->assets, "scene-a.png", 128, 64, true);
    write_png(f->assets, "scene-b.png", 32, 32, true);
    write_png(f->assets, "Pics/Hi-res/SKY01.png", 12, 9, false);
    write_png(f->extra, "HUD2.page1.png", 20, 10, false);
    return true;
}

static void free_fixture(HdFixture *f) {
    wo_free(f->index);
    wo_free(f->game);
    wo_free(f->extra);
    wo_free(f->assets);
}

static void test_install(void) {
    HdFixture f;
    if (!setup_fixture(&f, "hd-install")) {
        return;
    }
    WoJson *catalog = parse(CATALOG);
    WoHdOptions options = {catalog, f.assets, f.extra, f.game, NULL, NULL};
    WoHdResult result;
    WoError error = {0};
    CHECK_OK(wo_hd_install(&options, &result, &error), &error);
    CHECK(result.checked == 5 && result.region_placements == 3);
    CHECK(result.updated == 2 && result.region_index_updated);
    const char *kinds[][2] = {{"font.png", "atlas"},
                              {"HUD2.page1.png", "atlas"},
                              {"Pics/Hi-res/SKY01.png", "strip"},
                              {"scene-a.png", "region"},
                              {"scene-b.png", "region"}};
    CHECK(wo_json_length(result.files) == 5);
    for (size_t i = 0; i < WO_COUNT(kinds) && i < wo_json_length(result.files); i++) {
        const WoJson *record = wo_json_at(result.files, i);
        CHECK_STR(wo_json_str(record, "file", ""), kinds[i][0]);
        CHECK_STR(wo_json_str(record, "kind", ""), kinds[i][1]);
    }
    CHECK(wo_json_int(wo_json_at(result.files, 3), "width", 0) == 128);
    wo_hd_result_free(&result);
    /* The supplied PNG now lives with the shipped artwork. */
    char *kept = wo_path_join(f.assets, "HUD2.page1.png", &error);
    CHECK(wo_is_file(kept));
    wo_free(kept);

    WoBuf bytes = {0};
    CHECK_OK(wo_read_file(f.index, 1 << 20, &bytes, &error), &error);
    CHECK(bytes.len == 12 + 3 * WO_HD_REGION_RECORD_BYTES);
    CHECK(memcmp(bytes.data, "WOHDREG1", 8) == 0 && wo_le32(bytes.data + 8) == 3);
    const uint8_t *r0 = bytes.data + 12;
    CHECK(r0[0] == 's' && r0[1] == 0 && r0[2] == 'c' && r0[20] == 'g' && r0[22] == 0);
    CHECK(memcmp(r0 + 256, "nrace01.tga", 12) == 0);
    CHECK(wo_le32(r0 + 288) == 7 && wo_le32(r0 + 292) == 0 && wo_le32(r0 + 296) == 128);
    CHECK(wo_le32(r0 + 300) == 64 && wo_le32(r0 + 304) == 32 && wo_le32(r0 + 308) == 1);
    const uint8_t *r2 = bytes.data + 12 + 2 * WO_HD_REGION_RECORD_BYTES;
    CHECK(memcmp(r2 + 256, "sky01.tga", 10) == 0 && wo_le32(r2 + 308) == 0); /* black-key */
    wo_buf_free(&bytes);

    /* Nothing changed: nothing is rewritten. */
    CHECK_OK(wo_hd_install(&options, &result, &error), &error);
    CHECK(result.updated == 0 && !result.region_index_updated);
    wo_hd_result_free(&result);
    wo_json_free(catalog);
    free_fixture(&f);
}

static void expect_failure(HdFixture *f, const char *catalog_text, WoErrorKind kind,
                           const char *needle) {
    char before[WO_SHA256_HEX_SIZE];
    char after[WO_SHA256_HEX_SIZE];
    WoError error = {0};
    CHECK_OK(wo_sha256_file(f->index, before, &error), &error);
    WoJson *catalog = parse(catalog_text);
    WoHdOptions options = {catalog, f->assets, f->extra, f->game, NULL, NULL};
    WoHdResult result;
    CHECK_FAILS(wo_hd_install(&options, &result, &error), &error, kind);
    if (!strstr(error.message, needle)) {
        wo_test_fail(__FILE__, __LINE__, needle, error.message);
    }
    CHECK(result.files == NULL);
    CHECK_OK(wo_sha256_file(f->index, after, &error), &error);
    CHECK(wo_digest_eq(before, after)); /* the last valid index stays */
    wo_json_free(catalog);
}

static void remove_png(HdFixture *f, const char *relative) {
    WoError error = {0};
    char *path = wo_path_join(f->assets, relative, &error);
    CHECK_OK(path && wo_delete_file(path, &error), &error);
    wo_free(path);
}

static void test_rejects(void) {
    HdFixture f;
    if (!setup_fixture(&f, "hd-rejects")) {
        return;
    }
    WoJson *catalog = parse(CATALOG);
    WoHdOptions options = {catalog, f.assets, f.extra, f.game, NULL, NULL};
    WoHdResult result;
    WoError error = {0};
    CHECK_OK(wo_hd_install(&options, &result, &error), &error);
    wo_hd_result_free(&result);
    wo_json_free(catalog);

    write_png(f.assets, "shot.png", 4, 4, false);
    expect_failure(&f, CATALOG, WO_ERR_FORMAT, "reference screenshot, not a game texture. Just");
    remove_png(&f, "shot.png");
    write_png(f.assets, "HUD2.page3.png", 4, 4, false);
    expect_failure(&f, CATALOG, WO_ERR_FORMAT, "page number exceeds");
    remove_png(&f, "HUD2.page3.png");
    write_png(f.assets, "unmapped.png", 4, 4, false);
    expect_failure(&f, CATALOG, WO_ERR_FORMAT, "Cannot map HD PNG 'unmapped.png'");
    remove_png(&f, "unmapped.png");
    write_png(f.assets, "Other/SKY01.png", 4, 4, false);
    expect_failure(&f, CATALOG, WO_ERR_FORMAT, "Cannot map");
    remove_png(&f, "Other/SKY01.png");
    write_png(f.assets, "STRIP.png", 4, 4, false);
    expect_failure(&f, CATALOG, WO_ERR_FORMAT, "Cannot map"); /* not page-shaped */
    remove_png(&f, "STRIP.png");
    char *bad = wo_path_join(f.assets, "SKY01.png", &error);
    CHECK_OK(wo_write_file(bad, "broken", 6, &error), &error);
    expect_failure(&f, CATALOG, WO_ERR_FORMAT, "SKY01.png");
    CHECK_OK(wo_delete_file(bad, &error), &error);
    wo_free(bad);

    const char *mode = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":16,"
                       "\"source_height\":16,\"alpha_mode\":\"magic\",\"regions\":[{\"native\":"
                       "\"sky01.tga\",\"page\":0,\"x\":0,\"y\":0,\"width\":16,\"height\":16}]}]";
    expect_failure(&f, mode, WO_ERR_FORMAT, "Unknown HD transparency mode");
    const char *place = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":64,"
                        "\"source_height\":16,\"regions\":[{\"native\":\"sky01.tga\",\"page\":0,"
                        "\"x\":200,\"y\":0,\"width\":64,\"height\":16}]}]";
    expect_failure(&f, place, WO_ERR_FORMAT, "placement is invalid");
    const char *page = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":16,"
                       "\"source_height\":16,\"regions\":[{\"native\":\"sky01.tga\",\"page\":1,"
                       "\"x\":0,\"y\":0,\"width\":16,\"height\":16}]}]";
    expect_failure(&f, page, WO_ERR_FORMAT, "placement is invalid");
    const char *size = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":16,"
                       "\"source_height\":16,\"regions\":[{\"native\":\"sky01.tga\",\"page\":0,"
                       "\"x\":0,\"y\":0,\"width\":8,\"height\":16}]}]";
    expect_failure(&f, size, WO_ERR_FORMAT, "placement is invalid");
    const char *native = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":16,"
                         "\"source_height\":16,\"regions\":[{\"native\":\"zzz.tga\",\"page\":0,"
                         "\"x\":0,\"y\":0,\"width\":16,\"height\":16}]}]";
    expect_failure(&f, native, WO_ERR_FORMAT, "placement is invalid");
    const char *none = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"source_width\":16,"
                       "\"source_height\":16,\"regions\":[]}]";
    expect_failure(&f, none, WO_ERR_FORMAT, "no valid region mapping");
    const char *rect = "[" FONT_AND_SCENE_A ",{\"file\":\"scene-b.png\",\"kind\":\"region\",\"regions\":[{\"native\":"
                       "\"sky01.tga\"}]}]";
    expect_failure(&f, rect, WO_ERR_FORMAT, "Invalid source rectangle");
    const char *required = "[{\"file\":\"missing.png\",\"kind\":\"atlas\",\"required\":true}]";
    expect_failure(&f, required, WO_ERR_NOT_FOUND, "Missing HD artwork: missing.png");
    write_png(f.assets, "scene_c.png", 4, 4, false);
    char *odd = wo_format(NULL,
                          "%.*s,{\"file\":\"scene_c.png\",\"kind\":\"region\",\"source_width\":4,"
                          "\"source_height\":4,\"regions\":[{\"native\":\"sky01.tga\",\"page\":0,"
                          "\"x\":0,\"y\":0,\"width\":4,\"height\":4}]}]",
                          (int)strlen(CATALOG) - 1, CATALOG);
    expect_failure(&f, odd, WO_ERR_FORMAT, "no valid region mapping");
    wo_free(odd);
    remove_png(&f, "scene_c.png");
    expect_failure(&f, "{}", WO_ERR_FORMAT, "must be a list");

    /* Removing a region PNG drops its placements from the index. */
    remove_png(&f, "scene-a.png");
    catalog = parse(CATALOG);
    options.catalog = catalog;
    CHECK_OK(wo_hd_install(&options, &result, &error), &error);
    CHECK(result.region_placements == 1 && result.region_index_updated);
    wo_hd_result_free(&result);
    wo_json_free(catalog);
    free_fixture(&f);
}

static void test_region_index(void) {
    WoHdRegionIndex index = {{0}, 0};
    WoError error = {0};
    CHECK_OK(wo_hd_regions_add(&index, "a-1.png", "x1.tga", 255, 0, 0, 256, 256, true, &error),
             &error);
    CHECK_FAILS(wo_hd_regions_add(&index, "a_1.png", "x1.tga", 0, 0, 0, 1, 1, false, &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_hd_regions_add(&index, "a.png", "x-1.tga", 0, 0, 0, 1, 1, false, &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_hd_regions_add(&index, ".png", "x.tga", 0, 0, 0, 1, 1, false, &error), &error,
                WO_ERR_FORMAT);
    CHECK_FAILS(wo_hd_regions_add(&index, "a.png", "x.tga", 256, 0, 0, 1, 1, false, &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_hd_regions_add(&index, "a.png", "x.tga", 0, 1, 0, 256, 1, false, &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_hd_regions_add(&index, "a.png", "x.tga", 0, 0, 0, 0, 1, false, &error), &error,
                WO_ERR_FORMAT);
    char long_name[140];
    memset(long_name, 'a', 124);
    memcpy(long_name + 124, ".png", 5);
    CHECK_FAILS(wo_hd_regions_add(&index, long_name, "x.tga", 0, 0, 0, 1, 1, false, &error),
                &error, WO_ERR_FORMAT);
    long_name[123] = 0;
    memcpy(long_name + 119, ".png", 5);
    CHECK_OK(wo_hd_regions_add(&index, long_name, "x.tga", 0, 0, 0, 1, 1, false, &error), &error);
    for (uint32_t i = index.count; i < WO_HD_REGION_MAX_RECORDS; i++) {
        index.count++; /* pretend the index is full */
    }
    CHECK_FAILS(wo_hd_regions_add(&index, "a.png", "x.tga", 0, 0, 0, 1, 1, false, &error), &error,
                WO_ERR_FORMAT);
    wo_hd_regions_free(&index);
}

int main(void) {
    RUN(test_png_round_trip);
    RUN(test_install);
    RUN(test_rejects);
    RUN(test_region_index);
    return wo_test_finish();
}
