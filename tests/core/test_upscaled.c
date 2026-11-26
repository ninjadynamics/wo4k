/* The upscaled textures made on the player's computer: four times the size,
 * made once per source, left out where there is authored art, and taken
 * away again without touching PNGs made otherwise. */
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_json.h"
#include "core/wo_png.h"
#include "core/wo_upscaled.h"
#include "wo_test.h"

static void write_png(const char *root, const char *name, uint8_t shade) {
    WoError error = {0};
    WoImage image = {8, 6, wo_alloc(8 * 6 * 4, NULL)};
    for (size_t i = 0; i < 8 * 6; i++) {
        image.pixels[i * 4] = (uint8_t)(i * 5);
        image.pixels[i * 4 + 1] = shade;
        image.pixels[i * 4 + 2] = 90;
        image.pixels[i * 4 + 3] = 255;
    }
    char *path = wo_path_join(root, name, &error);
    CHECK_OK(path && wo_png_write(path, &image, WO_PIXELS_RGBA, &error), &error);
    wo_free(path);
    wo_image_free(&image);
}

static bool exists(const char *root, const char *name) {
    char *path = wo_path_join(root, name, NULL);
    bool there = path && wo_is_file(path);
    wo_free(path);
    return there;
}

static void test_made_kept_and_removed(void) {
    const char *root = wo_test_dir("upscaled");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *sd = wo_path_join(root, "sd", NULL);
    char *hd = wo_path_join(root, "hd", NULL);
    char *auth = wo_path_join(root, "auth", NULL);
    CHECK_OK(wo_mkdirs(sd, &error) && wo_mkdirs(auth, &error) && wo_mkdirs(hd, &error), &error);
    write_png(sd, "wall.png", 10);
    write_png(sd, "sign.png", 20);
    write_png(auth, "sign.png", 30);     /* authored: never shown upscaled */
    write_png(hd, "someone-else.png", 1); /* not ours */

    WoUpscaledResult r;
    CHECK_OK(wo_upscaled_make(sd, hd, 4, auth, NULL, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.made == 1 && r.kept == 0 && r.left_out == 1 && r.removed == 0);
    WoImage made = {0};
    char *wall = wo_path_join(hd, "wall.png", NULL);
    CHECK_OK(wo_png_read(wall, WO_PIXELS_RGBA, &made, &error), &error);
    CHECK(made.width == 32 && made.height == 24);
    wo_image_free(&made);
    CHECK(!exists(hd, "sign.png"));

    /* Again: nothing to do. With the source changed: made again. */
    CHECK_OK(wo_upscaled_make(sd, hd, 4, auth, NULL, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.made == 0 && r.kept == 1);
    write_png(sd, "wall.png", 11);
    CHECK_OK(wo_upscaled_make(sd, hd, 4, auth, NULL, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.made == 1 && r.kept == 0);
    /* At another scale: made again. */
    CHECK_OK(wo_upscaled_make(sd, hd, 2, auth, NULL, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.made == 1);
    CHECK_OK(wo_upscaled_make(sd, hd, 4, auth, NULL, NULL, NULL, 0, 0, &r, &error), &error);

    /* A source gone: what was made from it goes too. */
    CHECK_OK(wo_delete_file(wall, &error), &error);
    char *wall_sd = wo_path_join(sd, "wall.png", NULL);
    write_png(sd, "floor.png", 40);
    CHECK_OK(wo_delete_file(wall_sd, &error), &error);
    write_png(hd, "wall.png", 1); /* stands in for the one made before */
    CHECK_OK(wo_upscaled_make(sd, hd, 4, auth, NULL, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.made == 1 && r.removed == 1 && !exists(hd, "wall.png") && exists(hd, "floor.png"));

    /* Switched off: ours go, the record too; the other PNG stays. */
    size_t removed = 0;
    CHECK_OK(wo_upscaled_remove(hd, &removed, &error), &error);
    CHECK(removed == 1 && !exists(hd, "floor.png") && !exists(hd, "upscaled.json"));
    CHECK(exists(hd, "someone-else.png"));
    CHECK_OK(wo_upscaled_remove(hd, &removed, &error), &error);
    CHECK(removed == 0);
    wo_free(wall_sd);
    wo_free(wall);
    wo_free(auth);
    wo_free(hd);
    wo_free(sd);
}

/* With the HD catalogue: a picture it does not name is not made (the HD
 * artwork step would refuse it), and one made before is taken away. */
static void test_only_catalogued(void) {
    const char *root = wo_test_dir("upscaled-catalogue");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *sd = wo_path_join(root, "sd", NULL);
    char *hd = wo_path_join(root, "hd", NULL);
    CHECK_OK(wo_mkdirs(sd, &error), &error);
    write_png(sd, "known.png", 10);
    write_png(sd, "stray.png", 20);
    WoUpscaledResult r;
    CHECK_OK(wo_upscaled_make(sd, hd, 2, NULL, NULL, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.made == 2 && exists(hd, "stray.png"));
    WoJson *catalog = NULL;
    const char *text = "[{\"file\":\"known.png\"}]";
    CHECK_OK(wo_json_parse(text, strlen(text), &catalog, &error), &error);
    CHECK_OK(wo_upscaled_make(sd, hd, 2, NULL, catalog, NULL, NULL, 0, 0, &r, &error), &error);
    CHECK(r.kept == 1 && r.left_out == 1 && r.removed == 1);
    CHECK(exists(hd, "known.png") && !exists(hd, "stray.png"));
    wo_json_free(catalog);
    wo_free(hd);
    wo_free(sd);
}

int main(void) {
    RUN(test_made_kept_and_removed);
    RUN(test_only_catalogued);
    return wo_test_finish();
}
