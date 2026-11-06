#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_json.h"
#include "core/wo_sdtex.h"
#include "tex_builder.h"
#include "wo_test.h"

static void put(const char *root, const char *relative, const WoBuf *data) {
    WoError error = {0};
    char *path = wo_path_join(root, relative, &error);
    CHECK_OK(path && wo_write_file(path, data->data, data->len, &error), &error);
    wo_free(path);
}

static void put_text(const char *root, const char *relative, const char *text) {
    WoBuf data = {(uint8_t *)(uintptr_t)text, strlen(text), 0};
    put(root, relative, &data);
}

static bool has(const char *root, const char *relative) {
    char *path = wo_path_join(root, relative, NULL);
    bool exists = path && wo_is_file(path);
    wo_free(path);
    return exists;
}

static char *read_text(const char *root, const char *relative) {
    char *path = wo_path_join(root, relative, NULL);
    char *text = NULL;
    WoError error = {0};
    CHECK_OK(path && wo_read_text(path, 1 << 20, &text, &error), &error);
    wo_free(path);
    return text;
}

static void build_game(const char *game) {
    WoBuf tga = {0};
    WoBuf tim = {0};
    WoBuf cmp = {0};
    tb_tga(&tga, 4, 2, 0x7C00);
    tb_tim16(&tim, 2, 2, 1);
    tb_cmp(&cmp, &tim, 1);
    put(game, "Pics/Hi-res/A.TGA", &tga);
    put(game, "Pics/Lo-res/2097LOGO.TGA", &tga);
    put(game, "Textures/X.TIM", &tim);
    put(game, "Track01/LIB.CMP", &cmp);
    put(game, "Common/MINE.CMP", &cmp);
    put(game, "Track1/NOT.CMP", &cmp);
    put_text(game, "Blitsurf/Hi-res/RSTART97.PPM", "P6 97");
    put_text(game, "Blitsurf/Hi-res/RSTARTXL.PPM", "P6 XL");
    put_text(game, "Blitsurf/Lo-res/2097COPY.PPM", "P6 copy");
    put_text(game, "Track01/TRACK.TEX", "tex");
    put_text(game, "Track01/TRACK.WAD", "wad");
    put_text(game, "Track01/sub/DEEP.CMP", "deep");
    wo_buf_free(&cmp);
    wo_buf_free(&tim);
    wo_buf_free(&tga);
}

static void test_extract_and_aliases(void) {
    const char *root = wo_test_dir("sdtex");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *game = wo_path_join(root, "game", &error);
    char *sd = wo_path_join(root, "out/textures/sd", &error);
    build_game(game);
    CHECK_OK(wo_sd_extract(game, sd, &error), &error);
    CHECK(has(sd, "Pics/Hi-res/A.TGA") && has(sd, "Textures/X.TIM"));
    CHECK(has(sd, "Track01/LIB.CMP") && has(sd, "Track01/TRACK.TEX") && has(sd, "Common/MINE.CMP"));
    CHECK(has(sd, "Blitsurf/Hi-res/RSTART97.PPM"));
    CHECK(!has(sd, "Track01/TRACK.WAD") && !has(sd, "Track1/NOT.CMP"));
    CHECK(!has(sd, "Track01/sub/DEEP.CMP"));
    char *inventory = wo_path_join(sd, "sources.json", &error);
    WoJson *records = NULL;
    CHECK_OK(wo_read_json(inventory, 1 << 20, &records, &error), &error);
    /* Every copied file except the inventory itself, backslash-separated. */
    CHECK(wo_json_length(records) == 9);
    bool found = false;
    for (size_t i = 0; i < wo_json_length(records); i++) {
        const char *file = wo_json_str(wo_json_at(records, i), "file", "");
        CHECK(!strstr(file, "exports") && !strchr(file, '/'));
        CHECK(wo_is_hex_digest(wo_json_str(wo_json_at(records, i), "sha256", "")));
        found = found || wo_str_eq(file, "Track01\\LIB.CMP");
    }
    CHECK(found);
    wo_json_free(records);

    CHECK_OK(wo_sd_add_regional_aliases(sd, &error), &error);
    char *xl = read_text(sd, "Blitsurf/Hi-res/RSTARTXL.PPM");
    CHECK_STR(xl, "P6 XL"); /* an existing XL file is kept */
    char *copy = read_text(sd, "Blitsurf/Lo-res/WOXLCOPY.PPM");
    CHECK_STR(copy, "P6 copy");
    CHECK(has(sd, "Pics/Lo-res/XLLOGO.TGA") && !has(sd, "Pics/Hi-res/XLLOGO.TGA"));
    wo_free(copy);
    wo_free(xl);

    /* Maintainer export: every TGA, TIM and CMP frame as PNG. */
    CHECK_OK(wo_sd_export_png(sd, &error), &error);
    CHECK(has(sd, "exports/Pics/Hi-res/A.TGA.png") && has(sd, "exports/Textures/X.TIM.png"));
    CHECK(has(sd, "exports/Track01/LIB.CMP.png.parts/0000.png"));
    put_text(sd, "Textures/BAD.TIM", "nope");
    CHECK_FAILS(wo_sd_export_png(sd, &error), &error, WO_ERR_FORMAT);

    char *missing = wo_path_join(root, "missing", &error);
    CHECK_FAILS(wo_sd_extract(missing, sd, &error), &error, WO_ERR_NOT_FOUND);
    wo_free(missing);
    wo_free(inventory);
    wo_free(sd);
    wo_free(game);
}

/* A previous WO4K install as the source: its textures/sd tree is taken as is,
 * and WO4K's own textures folder is not mistaken for the game's Textures
 * (Windows treats the two names as one). */
static void test_extract_from_install(void) {
    const char *root = wo_test_dir("sdtex-install");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *game = wo_path_join(root, "wobble", &error);
    char *sd = wo_path_join(root, "out/textures/sd", &error);
    WoBuf tim = {0};
    WoBuf cmp = {0};
    tb_tim16(&tim, 2, 2, 1);
    tb_cmp(&cmp, &tim, 1);
    put(game, "textures/sd/Textures/X.TIM", &tim);
    put_text(game, "textures/sd/Pics/HD/z.rgba", "rgba");
    put_text(game, "textures/sd/exports/old.png", "old");
    put_text(game, "textures/selection.json", "{}");
    put_text(game, "textures/ps1-source/TRACK01/A.TIM", "ps1");
    put(game, "Track01/LIB.CMP", &cmp);
    CHECK_OK(wo_sd_extract(game, sd, &error), &error);
    CHECK(has(sd, "Textures/X.TIM") && has(sd, "Pics/HD/z.rgba") && has(sd, "Track01/LIB.CMP"));
    CHECK(!has(sd, "Textures/selection.json") && !has(sd, "Textures/sd/Pics/HD/z.rgba"));
    CHECK(!has(sd, "Textures/ps1-source/TRACK01/A.TIM"));
    wo_buf_free(&cmp);
    wo_buf_free(&tim);
    wo_free(sd);
    wo_free(game);
}

int main(void) {
    RUN(test_extract_and_aliases);
    RUN(test_extract_from_install);
    return wo_test_finish();
}
