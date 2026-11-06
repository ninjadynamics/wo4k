#include "core/wo_patch_file.h"

#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"

bool wo_patch_load(const char *recipe_path, WoPatchRecipe *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    WoBuf text = {0};
    bool ok = wo_read_file(recipe_path, WO_PATCH_RECIPE_MAX_BYTES, &text, error) &&
              wo_patch_parse((const char *)text.data, text.len, out, error);
    if (!ok) {
        wo_error_context(error, "%s: ", wo_path_basename(recipe_path));
    }
    wo_buf_free(&text);
    return ok;
}

bool wo_patch_apply_file(const char *source, const char *recipe_path, const char *target,
                         WoError *error) {
    WoPatchRecipe recipe;
    if (!wo_patch_load(recipe_path, &recipe, error)) {
        return false;
    }
    WoBuf input = {0};
    uint8_t *output = NULL;
    char hash[WO_SHA256_HEX_SIZE];
    bool ok = wo_read_file(source, WO_PATCH_MAX_BYTES, &input, error) &&
              wo_sha256_mem(input.data, input.len, hash, error);
    if (ok && (input.len != recipe.source_length || !wo_digest_eq(hash, recipe.source_sha256))) {
        ok = wo_fail(error, WO_ERR_CHECKSUM, "Unsupported source checksum: %s",
                     wo_path_basename(source));
    }
    ok = ok && wo_patch_apply_bytes(&recipe, input.data, input.len, &output, error) &&
         wo_sha256_mem(output, recipe.target_length, hash, error);
    if (ok && !wo_digest_eq(hash, recipe.target_sha256)) {
        ok = wo_fail(error, WO_ERR_CHECKSUM, "The patched executable failed its checksum.");
    }
    ok = ok && wo_write_file_atomic(target, output, recipe.target_length, error);
    wo_free(output);
    wo_buf_free(&input);
    wo_patch_free(&recipe);
    return ok;
}

static bool is_game_executable(const char *name) {
    static const char *const names[] = {"Wipeout2.exe", "Wipeout2_HighTest.exe",
                                        "Wipeout2097.exe", "D3D.exe"};
    for (size_t i = 0; i < WO_COUNT(names); i++) {
        if (wo_str_ieq(name, names[i])) {
            return true;
        }
    }
    return false;
}

/* The recipe path for a source hash, or NULL. */
static const char *patch_for(const WoJson *manifest, const char *hash) {
    const WoJson *executables = wo_json_get(manifest, "executables");
    for (size_t i = 0; i < wo_json_length(executables); i++) {
        const WoJson *entry = wo_json_at(executables, i);
        const char *source = wo_json_str(entry, "source_sha256", NULL);
        if (source && wo_digest_eq(source, hash)) {
            return wo_json_str(entry, "patch", NULL);
        }
    }
    return NULL;
}

/* Try one candidate; *done is set once the executable has been installed. */
static bool try_executable(const char *path, const char *target, const WoJson *manifest,
                           const char *mod_root, bool *done, WoError *error) {
    char hash[WO_SHA256_HEX_SIZE];
    if (!wo_sha256_file(path, hash, error)) {
        return false;
    }
    const char *output = wo_json_str(manifest, "output_sha256", NULL);
    if (output && wo_digest_eq(output, hash)) {
        *done = true;
        return wo_copy_file(path, target, error);
    }
    const char *patch = patch_for(manifest, hash);
    if (!patch) {
        return true;
    }
    char *recipe = wo_path_child(mod_root, patch, error);
    bool ok = recipe && wo_patch_apply_file(path, recipe, target, error);
    wo_free(recipe);
    *done = ok;
    return ok;
}

bool wo_patch_install_executable(const char *source_dir, const char *destination_dir,
                                 const WoJson *manifest, const char *mod_root, WoError *error) {
    char *target = wo_path_child(destination_dir, "Wipeout2097.exe", error);
    if (!target) {
        return false;
    }
    WoDirList list = {0};
    bool ok = wo_list_dir(source_dir, &list, error);
    bool done = false;
    for (size_t i = 0; ok && !done && i < list.count; i++) {
        const WoDirEntry *entry = &list.items[i];
        if (entry->is_dir || !is_game_executable(entry->name)) {
            continue;
        }
        char *path = wo_path_join(source_dir, entry->name, error);
        ok = path && try_executable(path, target, manifest, mod_root, &done, error);
        wo_free(path);
    }
    if (ok && !done) {
        ok = wo_fail(error, WO_ERR_UNSUPPORTED,
                     "Unsupported executable checksum; source files were left unchanged.");
    }
    wo_dirlist_free(&list);
    wo_free(target);
    return ok;
}

bool wo_patch_create(const char *source, const char *target, const char *recipe_path,
                     WoError *error) {
    WoBuf a = {0};
    WoBuf b = {0};
    WoPatchRecipe recipe = {0};
    WoJson *json = NULL;
    bool ok = wo_read_file(source, WO_PATCH_MAX_BYTES, &a, error) &&
              wo_read_file(target, WO_PATCH_MAX_BYTES, &b, error) &&
              wo_patch_diff(a.data, a.len, b.data, b.len, &recipe, error) &&
              wo_sha256_mem(a.data, a.len, recipe.source_sha256, error) &&
              wo_sha256_mem(b.data, b.len, recipe.target_sha256, error) &&
              wo_patch_to_json(&recipe, &json, error) && wo_write_json(recipe_path, json, error);
    wo_json_free(json);
    wo_patch_free(&recipe);
    wo_buf_free(&b);
    wo_buf_free(&a);
    return ok;
}
