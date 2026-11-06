#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_patch.h"
#include "core/wo_patch_file.h"
#include "wo_test.h"

static const char *ZERO_HASH = "0000000000000000000000000000000000000000000000000000000000000000";

static char *recipe_text(const char *ranges, size_t source_length, size_t target_length) {
    return wo_format(NULL,
                     "{\"format\":1,\"source_length\":%zu,\"target_length\":%zu,"
                     "\"source_sha256\":\"%s\",\"target_sha256\":\"%s\",\"ranges\":[%s]}",
                     source_length, target_length, ZERO_HASH, ZERO_HASH, ranges);
}

static void test_parse_and_apply(void) {
    char *text = recipe_text("{\"offset\":1,\"xor_hex\":[\"0102\",\"03\"]},"
                             "{\"offset\":5,\"xor_hex\":\"FF\"}",
                             5, 7);
    WoError error = {0};
    WoPatchRecipe recipe;
    CHECK_OK(wo_patch_parse(text, strlen(text), &recipe, &error), &error);
    CHECK(recipe.count == 2 && recipe.ranges[0].len == 3 && recipe.ranges[1].offset == 5);
    const uint8_t source[] = {10, 20, 30, 40, 50};
    uint8_t *out = NULL;
    CHECK_OK(wo_patch_apply_bytes(&recipe, source, sizeof(source), &out, &error), &error);
    const uint8_t expected[] = {10, 20 ^ 1, 30 ^ 2, 40 ^ 3, 50, 0xFF, 0};
    CHECK(out && memcmp(out, expected, sizeof(expected)) == 0);
    wo_free(out);
    CHECK_FAILS(wo_patch_apply_bytes(&recipe, source, 4, &out, &error), &error, WO_ERR_CHECKSUM);
    wo_patch_free(&recipe);
    wo_free(text);

    /* Truncation: a shorter target keeps the source's first bytes. */
    text = recipe_text("", 5, 2);
    CHECK_OK(wo_patch_parse(text, strlen(text), &recipe, &error), &error);
    CHECK_OK(wo_patch_apply_bytes(&recipe, source, sizeof(source), &out, &error), &error);
    CHECK(out && out[0] == 10 && out[1] == 20);
    wo_free(out);
    wo_patch_free(&recipe);
    wo_free(text);
}

static void expect_reject(const char *text, WoErrorKind kind) {
    WoError error = {0};
    WoPatchRecipe recipe;
    if (wo_patch_parse(text, strlen(text), &recipe, &error)) {
        wo_test_fail(__FILE__, __LINE__, text, "was accepted");
        wo_patch_free(&recipe);
    } else if (error.kind != kind) {
        wo_test_fail(__FILE__, __LINE__, text, error.message);
    }
}

static void expect_range_reject(const char *ranges) {
    char *text = recipe_text(ranges, 10, 10);
    expect_reject(text, WO_ERR_FORMAT);
    wo_free(text);
}

static void test_rejects(void) {
    expect_reject("[]", WO_ERR_FORMAT);
    expect_reject("{\"format\":2}", WO_ERR_UNSUPPORTED);
    expect_reject("{\"format\":\"1\"}", WO_ERR_UNSUPPORTED);
    expect_reject("not json", WO_ERR_FORMAT);
    char *text = recipe_text("", 10, 10);
    char *bad = wo_format(NULL, "%.*s,\"extra\":1}", (int)strlen(text) - 1, text);
    expect_reject(bad, WO_ERR_FORMAT);
    wo_free(bad);
    wo_free(text);
    expect_reject("{\"format\":1,\"source_length\":1,\"target_length\":1,\"source_sha256\":\"ab\","
                  "\"target_sha256\":\"ab\",\"ranges\":[]}",
                  WO_ERR_FORMAT);
    char *huge = wo_format(NULL,
                           "{\"format\":1,\"source_length\":1,\"target_length\":%u,"
                           "\"source_sha256\":\"%s\",\"target_sha256\":\"%s\",\"ranges\":[]}",
                           WO_PATCH_MAX_BYTES + 1, ZERO_HASH, ZERO_HASH);
    expect_reject(huge, WO_ERR_FORMAT);
    wo_free(huge);
    char *missing = wo_format(NULL,
                              "{\"format\":1,\"source_length\":1,\"target_length\":1,"
                              "\"source_sha256\":\"%s\",\"target_sha256\":\"%s\"}",
                              ZERO_HASH, ZERO_HASH);
    expect_reject(missing, WO_ERR_FORMAT);
    wo_free(missing);
    expect_range_reject("{\"offset\":0,\"xor_hex\":\"\"}");
    expect_range_reject("{\"offset\":0,\"xor_hex\":[]}");
    expect_range_reject("{\"offset\":0,\"xor_hex\":\"abc\"}");
    expect_range_reject("{\"offset\":0,\"xor_hex\":\"zz\"}");
    expect_range_reject("{\"offset\":0,\"xor_hex\":[\"a\",1]}");
    expect_range_reject("{\"offset\":0,\"xor_hex\":7}");
    expect_range_reject("{\"offset\":-1,\"xor_hex\":\"00\"}");
    expect_range_reject("{\"offset\":1.5,\"xor_hex\":\"00\"}");
    expect_range_reject("{\"offset\":9,\"xor_hex\":\"0000\"}");
    expect_range_reject("{\"offset\":11,\"xor_hex\":\"00\"}");
    expect_range_reject("{\"offset\":4,\"xor_hex\":\"0000\"},{\"offset\":5,\"xor_hex\":\"00\"}");
    expect_range_reject("{\"offset\":4,\"xor_hex\":\"00\"},{\"offset\":2,\"xor_hex\":\"00\"}");
    expect_range_reject("{\"offset\":0,\"xor_hex\":\"00\",\"note\":\"x\"}");
    expect_range_reject("{\"xor_hex\":\"00\"}");
    expect_range_reject("3");
}

static void test_diff_round_trip(void) {
    uint8_t a[300];
    uint8_t b[340];
    for (size_t i = 0; i < sizeof(a); i++) {
        a[i] = (uint8_t)(i * 13);
    }
    memcpy(b, a, sizeof(a));
    memset(b + sizeof(a), 0, sizeof(b) - sizeof(a));
    b[3] ^= 1;
    b[10] ^= 2;  /* within 16 bytes: joined with offset 3 */
    b[40] ^= 4;  /* a new range */
    b[299] ^= 8;
    b[320] = 9;  /* past the source's end */
    WoError error = {0};
    WoPatchRecipe recipe;
    CHECK_OK(wo_patch_diff(a, sizeof(a), b, sizeof(b), &recipe, &error), &error);
    CHECK(recipe.count == 4);
    if (recipe.count == 4) {
        CHECK(recipe.ranges[0].offset == 3 && recipe.ranges[0].len == 8);
        CHECK(recipe.ranges[1].offset == 40 && recipe.ranges[1].len == 1);
        CHECK(recipe.ranges[2].offset == 299 && recipe.ranges[3].offset == 320);
    }
    memcpy(recipe.source_sha256, ZERO_HASH, 65);
    memcpy(recipe.target_sha256, ZERO_HASH, 65);
    WoJson *json = NULL;
    CHECK_OK(wo_patch_to_json(&recipe, &json, &error), &error);
    WoPatchRecipe again;
    CHECK_OK(wo_patch_from_json(json, &again, &error), &error);
    uint8_t *out = NULL;
    CHECK_OK(wo_patch_apply_bytes(&again, a, sizeof(a), &out, &error), &error);
    CHECK(out && memcmp(out, b, sizeof(b)) == 0);
    wo_free(out);
    wo_patch_free(&again);
    wo_json_free(json);
    wo_patch_free(&recipe);

    /* Long ranges are written as 256-digit lines. */
    uint8_t c[400];
    for (size_t i = 0; i < sizeof(c); i++) {
        c[i] = (uint8_t)(a[i % 300] ^ 0x55);
    }
    CHECK_OK(wo_patch_diff(a, sizeof(a), c, sizeof(c), &recipe, &error), &error);
    CHECK_OK(wo_patch_to_json(&recipe, &json, &error), &error);
    const WoJson *lines = wo_json_get(wo_json_at(wo_json_get(json, "ranges"), 0), "xor_hex");
    CHECK(wo_json_length(lines) == 4);
    CHECK(strlen(wo_json_as_str(wo_json_at(lines, 0), "")) == 256);
    wo_json_free(json);
    wo_patch_free(&recipe);
}

static void test_files(void) {
    const char *dir = wo_test_dir("patch-files");
    if (!dir) {
        return;
    }
    WoError error = {0};
    char *source = wo_path_join(dir, "game/Wipeout2.exe", &error);
    char *wanted = wo_path_join(dir, "wanted.exe", &error);
    char *recipe = wo_path_join(dir, "mod/patches/exe.json", &error);
    char *out_dir = wo_path_join(dir, "out", &error);
    char *target = wo_path_join(dir, "out/Wipeout2097.exe", &error);
    CHECK_OK(wo_write_file(source, "MZ original bytes", 17, &error), &error);
    CHECK_OK(wo_write_file(wanted, "MZ patched! bytes and more", 26, &error), &error);
    CHECK_OK(wo_patch_create(source, wanted, recipe, &error), &error);
    CHECK_OK(wo_patch_apply_file(source, recipe, target, &error), &error);
    char a[WO_SHA256_HEX_SIZE];
    char b[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_file(wanted, a, &error), &error);
    CHECK_OK(wo_sha256_file(target, b, &error), &error);
    CHECK(wo_digest_eq(a, b));
    CHECK_OK(wo_delete_file(target, &error), &error);

    /* A different source is refused and nothing is written. */
    CHECK_FAILS(wo_patch_apply_file(wanted, recipe, target, &error), &error, WO_ERR_CHECKSUM);
    CHECK(!wo_exists(target));
    char *other = wo_path_join(dir, "other.exe", &error);
    CHECK_OK(wo_write_file(other, "MZ originaL bytes", 17, &error), &error);
    CHECK_FAILS(wo_patch_apply_file(other, recipe, target, &error), &error, WO_ERR_CHECKSUM);
    CHECK(!wo_exists(target));

    /* A recipe whose target hash is wrong fails after patching. */
    WoPatchRecipe loaded;
    CHECK_OK(wo_patch_load(recipe, &loaded, &error), &error);
    memcpy(loaded.target_sha256, ZERO_HASH, 65);
    WoJson *json = NULL;
    CHECK_OK(wo_patch_to_json(&loaded, &json, &error), &error);
    char *broken = wo_path_join(dir, "broken.json", &error);
    CHECK_OK(wo_write_json(broken, json, &error), &error);
    CHECK_FAILS(wo_patch_apply_file(source, broken, target, &error), &error, WO_ERR_CHECKSUM);
    CHECK(!wo_exists(target));
    wo_json_free(json);
    wo_patch_free(&loaded);

    /* Choosing the executable from a game folder via the manifest. */
    char *mod = wo_path_join(dir, "mod", &error);
    char *game = wo_path_join(dir, "game", &error);
    char *manifest_text = wo_format(NULL,
                                    "{\"output_sha256\":\"%s\",\"executables\":[{\"source_sha256\":"
                                    "\"%s\",\"patch\":\"patches/exe.json\"}]}",
                                    a, ZERO_HASH);
    WoJson *manifest = NULL;
    CHECK_OK(wo_json_parse(manifest_text, strlen(manifest_text), &manifest, &error), &error);
    CHECK_FAILS(wo_patch_install_executable(game, out_dir, manifest, mod, &error), &error,
                WO_ERR_UNSUPPORTED);
    char source_hash[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_file(source, source_hash, &error), &error);
    WoJson *entry = wo_json_at(wo_json_get(manifest, "executables"), 0);
    wo_json_put_str(entry, "source_sha256", source_hash);
    CHECK_OK(wo_patch_install_executable(game, out_dir, manifest, mod, &error), &error);
    CHECK_OK(wo_sha256_file(target, b, &error), &error);
    CHECK(wo_digest_eq(a, b));
    /* An already patched executable is copied as it is. */
    CHECK_OK(wo_delete_file(source, &error), &error);
    char *patched_copy = wo_path_join(game, "D3D.exe", &error);
    CHECK_OK(wo_copy_file(wanted, patched_copy, &error), &error);
    CHECK_OK(wo_delete_file(target, &error), &error);
    CHECK_OK(wo_patch_install_executable(game, out_dir, manifest, mod, &error), &error);
    CHECK_OK(wo_sha256_file(target, b, &error), &error);
    CHECK(wo_digest_eq(a, b));
    /* A recipe path that leaves the mod folder is refused. */
    wo_json_put_str(entry, "patch", "../outside.json");
    wo_json_put_str(manifest, "output_sha256", ZERO_HASH);
    CHECK_OK(wo_copy_file(wanted, source, &error), &error);
    wo_json_put_str(entry, "source_sha256", a);
    CHECK_FAILS(wo_patch_install_executable(game, out_dir, manifest, mod, &error), &error,
                WO_ERR_UNSAFE);
    wo_json_free(manifest);
    wo_free(manifest_text);
    wo_free(patched_copy);
    wo_free(game);
    wo_free(mod);
    wo_free(broken);
    wo_free(other);
    wo_free(target);
    wo_free(out_dir);
    wo_free(recipe);
    wo_free(wanted);
    wo_free(source);
}

static void test_shipped_recipes(void) {
    /* Every recipe the manifest names must parse under the strict rules. */
    WoError error = {0};
    WoJson *manifest = NULL;
    CHECK_OK(wo_read_json("manifests/patches.json", 1 << 20, &manifest, &error), &error);
    const WoJson *executables = wo_json_get(manifest, "executables");
    CHECK(wo_json_length(executables) >= 1);
    for (size_t i = 0; i < wo_json_length(executables); i++) {
        const WoJson *entry = wo_json_at(executables, i);
        const char *patch = wo_json_str(entry, "patch", "");
        CHECK(wo_str_starts_with(patch, "patches/") && wo_str_iends_with(patch, ".json"));
        WoPatchRecipe recipe;
        CHECK_OK(wo_patch_load(patch, &recipe, &error), &error);
        CHECK(wo_digest_eq(recipe.source_sha256, wo_json_str(entry, "source_sha256", "")));
        CHECK(recipe.source_length == (size_t)wo_json_int(entry, "source_size", -1));
        CHECK(wo_digest_eq(recipe.target_sha256, wo_json_str(manifest, "output_sha256", "")));
        wo_patch_free(&recipe);
    }
    wo_json_free(manifest);
}

int main(void) {
    RUN(test_parse_and_apply);
    RUN(test_rejects);
    RUN(test_diff_round_trip);
    RUN(test_files);
    RUN(test_shipped_recipes);
    return wo_test_finish();
}
